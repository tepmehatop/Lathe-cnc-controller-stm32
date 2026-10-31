#!/usr/bin/env python3
"""
UI-сценарии через имитацию касаний — проверка экранов дисплея по скриншотам.

Дисплей (прошивка ELS_Display) принимает по USB CDC:
  <TAP:x,y>   — касание в координатах базового экрана 480×272 (на 10.1" пересчитывается)
  <CMD:val>   — команда как будто от STM32
  'S'         — скриншот (JPEG)
Поэтому один и тот же сценарий работает и на 4.3", и на 10.1".

Каждый шаг сохраняет скриншот; [STATE]-строки пишутся в лог. Результат —
папка со скриншотами + UI_TAP_REPORT.md, скриншоты проверяются глазами по дизайну.

Запуск:
  python3 tools/ui_tap_tests.py --port /dev/cu.usbmodem14801 --out <папка> [--only U-03]
"""

import argparse, io, os, re, struct, time
import serial

MAGIC = b'\xde\xad\xbe\xef'

# Координаты элементов (база 480×272, компоновка K) — см. create_k_content / create_dark_pro_ui
MODE_TAP   = (100, 18)            # зона режима в статусбаре → меню режимов
WARN_ICON  = (464, 18)            # ⚠: тап — WiFi-инфо, двойной тап — настройки
FEAT       = (100, 80)            # основное значение: тап — редактирование, двойной — тип резьбы (M3)
BTN_UP, BTN_OK, BTN_DN = (446, 74), (446, 154), (446, 234)
CELL = {'r1': (72, 154), 'r2': (209, 154), 'r3': (346, 154),
        'r4': (72, 230), 'r5': (209, 230), 'r6': (346, 230)}

def mode_btn(n):                  # кнопки M1..M8: 2 колонки × 4 строки
    i = n - 1
    return (12 + (i % 2) * 235 + 109, 44 + (i // 2) * 47 + 19)

MODE_CANCEL  = (240, 248)
ALERT_OK     = (392, 169)         # кнопка OK алерта (abox 400×110 по центру)
SETTINGS_CLOSE = (240, 238)
JOY_CLOSE    = (444, 52)
THR_MENU_BTN = lambda i: (240, 38 + i * 36 + 16 + 26)   # оверлей 260×220 по центру
THR_CANCEL   = (240, 226)


class Display:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(1.0)
        self.log = []

    def drain(self, t=0.3):
        end = time.time() + t
        while time.time() < end:
            d = self.ser.read(4096)
            if d:
                self.log.append(d.decode('utf-8', 'replace'))

    def send(self, s, wait=0.35):
        self.ser.write(s.encode())
        self.ser.flush()
        self.drain(wait)

    def tap(self, xy, wait=0.45):
        self.send(f"<TAP:{xy[0]},{xy[1]}>", wait)

    def dtap(self, xy, wait=0.5):
        self.send(f"<TAP:{xy[0]},{xy[1]}>", 0.2)
        self.send(f"<TAP:{xy[0]},{xy[1]}>", wait)

    def shot(self, path):
        self.drain(0.1)
        self.ser.reset_input_buffer()
        self.ser.write(b'S')
        self.ser.flush()
        buf, end = b'', time.time() + 8
        while MAGIC not in buf:
            buf += self.ser.read(4096)
            if time.time() > end:
                return False
        buf = buf[buf.index(MAGIC) + 4:]
        while len(buf) < 4:
            buf += self.ser.read(4)
        n = struct.unpack('<I', buf[:4])[0]
        data, end = buf[4:], time.time() + 20
        while len(data) < n and time.time() < end:
            data += self.ser.read(n - len(data))
        if len(data) < n:
            return False
        try:
            from PIL import Image
            img = Image.open(io.BytesIO(data[:n]))
            if img.height > img.width:            # 10.1": физический портретный кадр
                img = img.rotate(-90, expand=True)
            img.save(path, quality=90)
        except ImportError:
            open(path, 'wb').write(data[:n])
        return True

    def last_state(self):
        txt = ''.join(self.log)
        st = re.findall(r'\[STATE\][^\n]*', txt)
        return st[-1] if st else ''


def reset_ui(d):
    """Закрыть возможные оверлеи и вернуть M1."""
    d.send("<ALERT:0>", 0.2)
    d.tap(BTN_OK, 0.3)          # закрыть барабан/редактирование, если открыты
    d.tap(MODE_TAP); d.tap(mode_btn(1), 0.8)


# Сценарии: (id, описание, шаги). Шаг — ('tap', xy) | ('dtap', xy) | ('cmd', '<..>') |
#                                       ('shot', 'имя') | ('wait', сек)
SCENARIOS = [
    ("U-01", "Главный экран M1", [('shot', 'main_m1')]),
    ("U-02", "Меню выбора режима", [('tap', MODE_TAP), ('shot', 'mode_menu'),
                                     ('tap', MODE_CANCEL), ('shot', 'mode_menu_closed')]),
] + [
    (f"U-1{n}", f"Смена режима на M{n} через меню",
     [('tap', MODE_TAP), ('tap', mode_btn(n)), ('wait', 1.0), ('shot', f'mode_M{n}')])
    for n in range(1, 8)
] + [
    ("U-20", "M1: барабан подачи (тап на основное значение)",
     [('tap', MODE_TAP), ('tap', mode_btn(1)), ('wait', 1.0),
      ('tap', FEAT), ('wait', 0.6), ('shot', 'roller_feed'),
      ('tap', BTN_UP), ('tap', BTN_UP), ('shot', 'roller_feed_up2'),
      ('tap', BTN_OK), ('wait', 1.0), ('shot', 'roller_feed_ok')]),
    ("U-21", "M1: редактирование СЪЁМ (тап на ячейку)",
     [('tap', CELL['r3']), ('wait', 0.6), ('shot', 'subedit_ap'),
      ('tap', BTN_OK), ('wait', 1.0), ('shot', 'subedit_ap_ok')]),
    ("U-22", "M2: барабан асинхронной подачи",
     [('tap', MODE_TAP), ('tap', mode_btn(2)), ('wait', 1.0),
      ('tap', FEAT), ('wait', 0.6), ('shot', 'roller_afeed'),
      ('tap', BTN_DN), ('shot', 'roller_afeed_dn'),
      ('tap', BTN_OK), ('wait', 1.0), ('shot', 'roller_afeed_ok')]),
    ("U-30", "M3: меню типа резьбы (двойной тап)",
     [('tap', MODE_TAP), ('tap', mode_btn(3)), ('wait', 1.0), ('shot', 'thread_main'),
      ('dtap', FEAT), ('wait', 0.6), ('shot', 'thread_type_menu'),
      ('tap', THR_MENU_BTN(1)), ('wait', 1.0), ('shot', 'thread_inch')]),
    ("U-31", "M3: барабан шага резьбы",
     [('tap', FEAT), ('wait', 0.6), ('shot', 'roller_thread'),
      ('tap', BTN_OK), ('wait', 1.0), ('shot', 'roller_thread_ok')]),
    ("U-40", "M4: конус, редактирование угла",
     [('tap', MODE_TAP), ('tap', mode_btn(4)), ('wait', 1.0), ('shot', 'cone_main'),
      ('tap', CELL['r3']), ('wait', 0.6), ('shot', 'cone_roller'),
      ('tap', BTN_OK), ('wait', 1.0), ('shot', 'cone_ok')]),
    ("U-50", "M6: шар", [('tap', MODE_TAP), ('tap', mode_btn(6)), ('wait', 1.0),
                         ('shot', 'sphere_main')]),
    ("U-60", "M7: делилка", [('tap', MODE_TAP), ('tap', mode_btn(7)), ('wait', 1.0),
                             ('shot', 'divider_main'), ('tap', BTN_OK), ('wait', 0.8),
                             ('shot', 'divider_ok')]),
    ("U-70", "Джойстик (двойной тап ПОЗИЦИЯ X)",
     [('tap', MODE_TAP), ('tap', mode_btn(1)), ('wait', 1.0),
      ('dtap', CELL['r2']), ('wait', 0.6), ('shot', 'joystick'),
      ('tap', JOY_CLOSE), ('wait', 0.6), ('shot', 'joystick_closed')]),
    ("U-80", "Алерт от STM32", [('cmd', '<ALERT:1>'), ('wait', 0.6), ('shot', 'alert'),
                                ('tap', ALERT_OK), ('wait', 0.6), ('shot', 'alert_closed')]),
    ("U-81", "WiFi-инфо (тап ⚠) и настройки (двойной тап)",
     [('tap', WARN_ICON), ('wait', 0.6), ('shot', 'wifi_info'), ('tap', ALERT_OK),
      ('dtap', WARN_ICON), ('wait', 0.6), ('shot', 'settings'),
      ('tap', SETTINGS_CLOSE), ('wait', 0.6), ('shot', 'settings_closed')]),
    ("U-90", "Лимиты и обороты от STM32",
     [('cmd', '<LIMITS:1,1,1,1>'), ('cmd', '<RPM:1250>'), ('wait', 0.6), ('shot', 'limits_rpm'),
      ('cmd', '<LIMITS:0,0,0,0>'), ('cmd', '<RPM:0>'), ('wait', 0.4)]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/cu.usbmodem14801')
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default=None)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    d = Display(a.port)
    rows = []
    for sid, desc, steps in SCENARIOS:
        if a.only and sid != a.only:
            continue
        shots, ok = [], True
        for kind, arg in steps:
            if kind == 'tap':    d.tap(arg)
            elif kind == 'dtap': d.dtap(arg)
            elif kind == 'cmd':  d.send(arg)
            elif kind == 'wait': d.drain(arg)
            elif kind == 'shot':
                name = f"{sid}_{arg}.jpg"
                if d.shot(os.path.join(a.out, name)):
                    shots.append(name)
                else:
                    ok = False
                    shots.append(name + ' (FAIL)')
        rows.append((sid, desc, ok, shots, d.last_state()))
        print(f"{sid} {'OK ' if ok else 'ERR'} {desc}")
    with open(os.path.join(a.out, 'UI_TAP_REPORT.md'), 'w') as f:
        f.write("# UI tap tests\n\n| ID | Сценарий | Скриншоты | STATE |\n|---|---|---|---|\n")
        for sid, desc, ok, shots, st in rows:
            f.write(f"| {sid} | {desc} | {', '.join(shots)} | {st} |\n")
    with open(os.path.join(a.out, 'ui_tap.log'), 'w') as f:
        f.write(''.join(d.log))


if __name__ == '__main__':
    main()
