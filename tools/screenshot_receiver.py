#!/usr/bin/env python3
"""
Screenshot Receiver for ELS Display
Принимает скриншот с ESP32 через Serial и сохраняет как PNG

Использование:
    python screenshot_receiver.py /dev/cu.usbmodem14201 screenshot.png

Или интерактивно:
    python screenshot_receiver.py /dev/cu.usbmodem14201
    > Нажмите Enter для захвата скриншота...
"""

import serial
import sys
import os
from datetime import datetime

def receive_screenshot(port, output_file=None):
    """Принимает скриншот с устройства и сохраняет как PPM/PNG"""

    try:
        ser = serial.Serial(port, 115200, timeout=10)
        print(f"Подключено к {port}")
        print("Отправляю команду SCREENSHOT...")

        ser.write(b'SCREENSHOT\n')

        # Ждём начала скриншота
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(f"< {line}")
            if line == "SCREENSHOT_START":
                break
            if "ERROR" in line:
                print(f"Ошибка: {line}")
                return None

        # Читаем PPM заголовок
        magic = ser.readline().decode('utf-8').strip()  # P6
        dimensions = ser.readline().decode('utf-8').strip()  # 480 272
        max_val = ser.readline().decode('utf-8').strip()  # 255

        print(f"Формат: {magic}, Размер: {dimensions}, Max: {max_val}")

        width, height = map(int, dimensions.split())
        total_bytes = width * height * 3

        print(f"Принимаю {total_bytes} байт данных...")

        # Читаем пиксели
        data = b''
        while len(data) < total_bytes:
            chunk = ser.read(min(4096, total_bytes - len(data)))
            if not chunk:
                print("Таймаут при чтении данных")
                break
            data += chunk
            progress = len(data) * 100 // total_bytes
            print(f"\rПрогресс: {progress}%", end='', flush=True)

        print()

        # Ждём конец
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if "SCREENSHOT_END" in line:
                break

        ser.close()

        # Генерируем имя файла если не указано
        if output_file is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_file = f"screenshot_{timestamp}.ppm"

        # Сохраняем PPM
        ppm_file = output_file if output_file.endswith('.ppm') else output_file.replace('.png', '.ppm')
        with open(ppm_file, 'wb') as f:
            f.write(f"P6\n{width} {height}\n255\n".encode())
            f.write(data)

        print(f"Сохранено: {ppm_file}")

        # Конвертируем в PNG если есть PIL
        try:
            from PIL import Image
            img = Image.open(ppm_file)
            png_file = ppm_file.replace('.ppm', '.png')
            img.save(png_file)
            print(f"Конвертировано в PNG: {png_file}")
            os.remove(ppm_file)
            return png_file
        except ImportError:
            print("PIL не установлен. Для PNG: pip install Pillow")
            return ppm_file

    except serial.SerialException as e:
        print(f"Ошибка Serial: {e}")
        return None

def interactive_mode(port):
    """Интерактивный режим - ждёт Enter для скриншота"""
    print(f"\nИнтерактивный режим. Порт: {port}")
    print("Нажмите Enter для скриншота, 'q' для выхода\n")

    while True:
        try:
            cmd = input("> ")
            if cmd.lower() == 'q':
                break
            receive_screenshot(port)
        except KeyboardInterrupt:
            break

    print("Выход")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Использование:")
        print(f"  {sys.argv[0]} <порт> [output.png]")
        print(f"  {sys.argv[0]} /dev/cu.usbmodem14201")
        print(f"  {sys.argv[0]} /dev/cu.usbmodem14201 screenshot.png")
        sys.exit(1)

    port = sys.argv[1]

    if len(sys.argv) >= 3:
        output = sys.argv[2]
        receive_screenshot(port, output)
    else:
        interactive_mode(port)
