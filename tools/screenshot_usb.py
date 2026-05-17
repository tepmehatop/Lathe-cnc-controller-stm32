#!/usr/bin/env python3
"""
ELS Display — USB CDC Screenshot Tool
Бинарный протокол: шлём 'S', получаем magic+size+JPEG

Использование:
    python3 screenshot_usb.py                  # авто-определение порта
    python3 screenshot_usb.py /dev/cu.usbmodem1401

ВАЖНО: закрой Serial Monitor в VSCode/PlatformIO перед запуском!
"""

import serial
import serial.tools.list_ports
import sys
import os
import struct
import subprocess
import time
from datetime import datetime

try:
    from PIL import Image
    import io
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

MAGIC = b'\xDE\xAD\xBE\xEF'
TIMEOUT = 15  # секунд


def find_esp32_port():
    """Ищет порт ESP32 среди доступных USB-serial устройств."""
    ports = list(serial.tools.list_ports.comports())
    # Приоритет: usbmodem (USB CDC на macOS)
    for p in ports:
        if 'usbmodem' in p.device:
            return p.device
    # Запасной вариант: usbserial
    for p in ports:
        if 'usbserial' in p.device or 'SLAB' in p.device or 'CP210' in (p.description or ''):
            return p.device
    # Первый доступный
    if ports:
        return ports[0].device
    return None


def recv_exact(ser, n, timeout=TIMEOUT):
    """Читает ровно n байт с таймаутом."""
    buf = b''
    t0 = time.time()
    while len(buf) < n:
        if time.time() - t0 > timeout:
            return None
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return buf


def find_magic(ser, timeout=TIMEOUT):
    """Читает байты пока не найдёт magic header 0xDE 0xAD 0xBE 0xEF."""
    buf = b''
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if not b:
            continue
        buf += b
        # Для отладки: показываем ASCII байты (debug-вывод ESP32)
        if b[0] >= 0x20 and b[0] < 0x7F:
            pass  # часть debug-текста, игнорируем
        if len(buf) >= 4 and buf[-4:] == MAGIC:
            return True
        # Ограничиваем буфер
        if len(buf) > 2048:
            buf = buf[-4:]
    return False


def take_screenshot(port):
    """Захватывает скриншот и возвращает путь к файлу."""
    print(f"Подключаюсь к {port} ...")
    try:
        ser = serial.Serial(port, 115200, timeout=2)
    except serial.SerialException as e:
        print(f"Ошибка открытия порта: {e}")
        print("Убедись что Serial Monitor закрыт!")
        return None

    time.sleep(0.3)  # даём USB CDC стабилизироваться

    # Очищаем входной буфер (могут быть остатки debug-вывода)
    ser.reset_input_buffer()

    # Отправляем запрос
    print("Отправляю запрос скриншота ('S')...")
    ser.write(b'S')
    ser.flush()

    # Ждём magic header
    print("Жду ответ...", end='', flush=True)
    if not find_magic(ser, timeout=10):
        print("\nОшибка: magic header не получен за 10 секунд")
        print("Проверь что:\n  1. Прошивка загружена\n  2. Serial Monitor закрыт")
        ser.close()
        return None
    print(" OK")

    # Читаем 4 байта размера (little-endian)
    size_bytes = recv_exact(ser, 4, timeout=3)
    if size_bytes is None:
        print("Ошибка: не получил размер JPEG")
        ser.close()
        return None
    jpeg_size = struct.unpack('<I', size_bytes)[0]
    print(f"Размер JPEG: {jpeg_size} байт ({jpeg_size/1024:.1f} KB)")

    if jpeg_size == 0 or jpeg_size > 5000000:
        print(f"Ошибка: неправильный размер JPEG ({jpeg_size})")
        ser.close()
        return None

    # Читаем JPEG данные
    print(f"Принимаю данные...", end='', flush=True)
    jpeg_data = b''
    t0 = time.time()
    while len(jpeg_data) < jpeg_size:
        if time.time() - t0 > TIMEOUT:
            print(f"\nТаймаут! Получено {len(jpeg_data)}/{jpeg_size} байт")
            ser.close()
            return None
        remaining = jpeg_size - len(jpeg_data)
        chunk = ser.read(min(512, remaining))
        if chunk:
            jpeg_data += chunk
            pct = len(jpeg_data) * 100 // jpeg_size
            print(f"\r  {pct}% ({len(jpeg_data)}/{jpeg_size} байт)   ", end='', flush=True)

    print(f"\r  100% ({jpeg_size} байт) — готово!          ")
    ser.close()

    # Если JPEG портретный (800×1280) — автоповорот на 90° CCW → ландшафт 1280×800
    if HAS_PIL:
        try:
            img = Image.open(io.BytesIO(jpeg_data))
            w, h = img.size
            if h > w:  # портретный кадр (физические coords P4)
                img = img.rotate(90, expand=True)
                print(f"  Авто-поворот 90° (физический {w}×{h} → логический {img.width}×{img.height})")
            buf = io.BytesIO()
            img.save(buf, format='JPEG', quality=90)
            jpeg_data = buf.getvalue()
        except Exception as e:
            print(f"  PIL rotate failed: {e} — сохраняем как есть")

    # Сохраняем файл
    out_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(out_dir, 'screenshot_latest.jpg')
    with open(out_path, 'wb') as f:
        f.write(jpeg_data)

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    archive_path = os.path.join(out_dir, f'screenshot_{ts}.jpg')
    with open(archive_path, 'wb') as f:
        f.write(jpeg_data)

    print(f"Сохранено: {out_path}")
    print(f"Архив:     {archive_path}")
    return out_path


def main():
    if len(sys.argv) >= 2:
        port = sys.argv[1]
    else:
        port = find_esp32_port()
        if not port:
            print("ESP32 не найден. Доступные порты:")
            for p in serial.tools.list_ports.comports():
                print(f"  {p.device}: {p.description}")
            print("\nУкажи порт явно: python3 screenshot_usb.py /dev/cu.usbmodemXXXX")
            sys.exit(1)
        print(f"Авто-определён порт: {port}")

    out = take_screenshot(port)
    if out:
        # Открываем в системном просмотрщике
        if sys.platform == 'darwin':
            subprocess.Popen(['open', out])
        elif sys.platform == 'win32':
            os.startfile(out)
        else:
            subprocess.Popen(['xdg-open', out])
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
