#!/usr/bin/env python3
"""
Сверка старого (LCD2004) и нового (ESP32) дисплея на живой связке.

Старый экран читается из памяти STM32 через ST-Link (теневой буфер E2Lcd, 4×20),
новый — командой <UIDUMP> по USB дисплея. Действия: касания <BTN:...> через дисплей
(те же Switch_*/Key_* ядра, что и у кнопок пульта) и нажатия физических входов —
вывод STM32 временно переводится в выход с низким уровнем через SWD.

Проверка: каждое число со старого экрана должно быть на новом (с точностью до
формата: «0.13» = «+0.13», «45°» = «45»). Все пары экранов — в отчёт.

Нужно: STM32 по ST-Link, дисплей по USB, Mega-эмулятор (позиции фиксируются командами
w0/z/x). Запуск из корня проекта:
  python3 tools/compare_displays.py --esp /dev/cu.usbmodem14801 --mega /dev/cu.usbserial-1410 --out <папка>
"""
import argparse, os, re, socket, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ELF = os.path.join(ROOT, '.pio', 'build', 'stm32f407_discovery', 'firmware.elf')
PIO = os.path.expanduser('~/.platformio')
NM = os.path.join(PIO, 'packages', 'toolchain-gccarmnoneeabi', 'bin', 'arm-none-eabi-nm')
OCD = os.path.join(PIO, 'packages', 'tool-openocd')

GPIO_BASE = {c: 0x40020000 + 0x400 * i for i, c in enumerate('ABCDE')}
MODE_NAMES = {1: 'Подача', 2: 'аПодача', 3: 'Резьба', 4: 'Конус<', 5: 'Конус>', 6: 'Шар', 7: 'Делилка', 8: 'Резерв'}


# ─── STM32 через OpenOCD (TCL-порт) ──────────────────────────────────────────
class Stm32:
    def __init__(self, port=6666):
        self.proc = subprocess.Popen(
            [os.path.join(OCD, 'bin', 'openocd'), '-s', os.path.join(OCD, 'openocd', 'scripts'),
             '-f', 'interface/stlink.cfg', '-f', 'target/stm32f4x.cfg',
             '-c', f'tcl_port {port}', '-c', 'gdb_port disabled', '-c', 'telnet_port disabled', '-c', 'init'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.sock = socket.create_connection(('127.0.0.1', port), timeout=5)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError('OpenOCD не запустился (ST-Link подключён?)')
        self.sym = {}
        out = subprocess.run([NM, '-S', '-C', ELF], capture_output=True, text=True).stdout
        for l in out.splitlines():
            p = l.split()
            if len(p) == 4:
                self.sym[p[3]] = (int(p[0], 16), int(p[1], 16))
        self.pins = {}
        for m in re.finditer(r'#define\s+E2_PIN_(\w+)\s+([A-E]),\s*(\d+)',
                             open(os.path.join(ROOT, 'Core', 'e2', 'e2_pins.h'), encoding='utf-8').read()):
            self.pins[m.group(1)] = (m.group(2), int(m.group(3)))

    def cmd(self, c):
        self.sock.sendall(c.encode() + b'\x1a')
        buf = b''
        while not buf.endswith(b'\x1a'):
            buf += self.sock.recv(65536)
        return buf[:-1].decode(errors='replace')

    def read(self, addr, n, width=8):
        return [int(x, 16) for x in self.cmd(f'read_memory 0x{addr:08x} {width} {n}').split()]

    def var(self, name, signed=True):
        addr, size = self.sym[name]
        w = {1: 8, 2: 16, 4: 32}[size]
        v = self.read(addr, 1, w)[0]
        if signed and v >= 1 << (w - 1):
            v -= 1 << w
        return v

    def lcd_codes(self):
        addr, _ = self.sym['lcd']
        b = self.read(addr + 7, 80)
        return [b[i * 20:(i + 1) * 20] for i in range(4)]

    def _gpio(self, pin):
        port, n = self.pins[pin]
        return GPIO_BASE[port], n

    def press(self, pin):
        """Замкнуть вход на GND: ODR=0, MODER=выход."""
        base, n = self._gpio(pin)
        self.cmd(f'mww 0x{base + 0x18:08x} 0x{1 << (n + 16):08x}')            # BSRR: сброс
        moder = self.read(base, 1, 32)[0]
        self.cmd(f'mww 0x{base:08x} 0x{(moder & ~(3 << 2 * n)) | (1 << 2 * n):08x}')

    def release(self, pin):
        base, n = self._gpio(pin)
        moder = self.read(base, 1, 32)[0]
        self.cmd(f'mww 0x{base:08x} 0x{moder & ~(3 << 2 * n):08x}')

    def close(self):
        try:
            self.cmd('shutdown')
        except Exception:
            pass
        self.proc.wait(timeout=5)


# ─── Декодер LCD2004 (обратный к e2_lcd.cpp) ─────────────────────────────────
def lcd_decoder():
    src = open(os.path.join(ROOT, 'Core', 'e2', 'e2_lcd.cpp'), encoding='utf-8').read()
    tabs = re.findall(r'(?:upper|lower)\[\d*\]\s*=\s*\{([^}]*)\}', src)
    rev = {}
    for tab, base in zip(tabs, (0x410, 0x430)):
        for i, code in enumerate(int(x, 16) for x in re.findall(r'0x[0-9A-F]{2}', tab)):
            if code >= 0x80:
                rev[code] = chr(base + i)
    rev[0xA2], rev[0xB5] = 'Ё', 'ё'
    custom = {1: '←', 2: '→', 3: '↑', 4: '↓', 5: '°', 6: 'Ø', 0: '·', 7: '·'}

    def dec(codes):
        return ''.join(custom.get(c) or rev.get(c) or (chr(c) if 32 <= c < 127 else '¿') for c in codes)
    return dec


# ─── Дисплей ESP32 ───────────────────────────────────────────────────────────
class Esp:
    def __init__(self, port):
        import serial
        self.s = serial.Serial(port, 115200, timeout=0.1)

    def send(self, txt):
        self.s.write(txt.encode())
        self.s.flush()

    def btn(self, name):
        self.send(f'<BTN:{name}>')

    def uidump(self):
        self.s.reset_input_buffer()
        self.send('<UIDUMP>')
        buf, t0 = b'', time.time()
        while time.time() - t0 < 3:
            buf += self.s.read(8192)
            for line in buf.split(b'\n')[:-1]:          # только полные строки
                if line.startswith(b'[UI]'):
                    return parse_ui(line.decode('utf-8', 'replace').strip())
        raise RuntimeError('нет ответа на <UIDUMP>')


def parse_ui(line):
    d = {k: v.replace('\\"', '"') for k, v in re.findall(r'(\w+)="((?:[^"\\]|\\.)*)"', line)}
    d.update({k: v for k, v in re.findall(r' (\w+)=([0-9A-F,]+)(?= |$)', line)})
    # убрать разметку цвета LVGL (#rrggbb текст#)
    for k, v in list(d.items()):
        d[k] = re.sub(r'#[0-9a-fA-F]{6} ([^#]*)#', r'\1', v)
    return d


# ─── Сравнение ───────────────────────────────────────────────────────────────
NUM = re.compile(r'\d+(?:[.,]\d+)?')

def numbers(text):
    out = []
    for m in NUM.finditer(text):
        try:
            out.append(float(m.group().replace(',', '.')))
        except ValueError:
            pass
    return out

VISIBLE_KEYS = ('mode_lbl', 'submode_lbl', 'primary_unit', 'primary_val', 'thread_tag', 'secondary_unit',
                'secondary_val', 'row1_title', 'row1_val', 'row2_title', 'row2_val', 'row3_title', 'row3_val',
                'row4_title', 'row4_val', 'thr_type_title', 'thr_type_val', 'diam_title', 'diam_val',
                'sm_row6', 's2_lbl', 'alert_msg')

def ui_visible_text(ui):
    keys = [k for k in VISIBLE_KEYS if k in ui]
    if ui.get('thr_type_box') == '0':
        keys = [k for k in keys if not k.startswith('thr_type')]
    if ui.get('diam_box') == '0':
        keys = [k for k in keys if not k.startswith('diam')]
    if ui.get('alert') == '0':
        keys = [k for k in keys if k != 'alert_msg']
    return {k: ui[k] for k in keys}

def compare(lcd_rows, ui):
    vis = ui_visible_text(ui)
    ui_nums = numbers(' '.join(vis.values()))
    missing = []
    for row in lcd_rows:
        for n in numbers(row):
            if not any(abs(n - u) < 0.006 for u in ui_nums):
                missing.append(n)
    return missing


# ─── Сценарий ────────────────────────────────────────────────────────────────
def build_steps():
    """(название, действия). Действие: ('btn', X) | ('press', PIN) | ('release', PIN) | ('wait', с)."""
    steps = []
    for m in range(1, 8):
        steps.append((f'M{m} {MODE_NAMES[m]}: выбор режима', [('btn', f'M{m}')]))
        for s, sname in ((2, 'Ручной'), (1, 'Внутр.'), (3, 'Наруж.')):
            steps.append((f'M{m} подрежим S{s} {sname}', [('btn', f'S{s}')]))
        steps.append((f'M{m} подрежим S2 (возврат)', [('btn', 'S2')]))
        for sm in (2, 3, 1):
            steps.append((f'M{m} OK → экран меню {sm}', [('btn', 'PARAM_OK')]))
            steps.append((f'M{m} SM{sm}: кнопка Вверх', [('btn', 'KEY:UP')]))
            steps.append((f'M{m} SM{sm}: кнопка Вниз', [('btn', 'KEY:DN')]))
            steps.append((f'M{m} SM{sm}: кнопка Вправо', [('btn', 'KEY:RIGHT')]))
            steps.append((f'M{m} SM{sm}: кнопка Влево', [('btn', 'KEY:LEFT')]))
        # физический джойстик вне нейтрали — сообщение на обоих дисплеях
        steps.append((f'M{m} джойстик Влево удержан (физ.)', [('press', 'JOY_LEFT')]))
        steps.append((f'M{m} джойстик отпущен', [('release', 'JOY_LEFT')]))
    # Джойстик вне нейтрали при смене режима — сообщение «джойстик в нейтраль»
    steps.append(('джойстик Вправо удержан + смена режима на M3', [('press', 'JOY_RIGHT'), ('btn', 'M3')]))
    steps.append(('джойстик отпущен', [('release', 'JOY_RIGHT')]))
    # Упоры работают только при физическом переключателе подрежима в «Ручной» (BACKLOG-022):
    # держим SUB_MAN на GND, ставим 4 упора и проходим Внутр./Наруж. во всех режимах
    steps.append(('переключатель подрежима «Ручной» (физ.), M1', [('press', 'SUB_MAN'), ('btn', 'M1'), ('btn', 'S2')]))
    for lim in ('LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT', 'LIM_REAR'):
        steps.append((f'упор {lim} установлен (физ.)', [('press', lim), ('wait', 0.3), ('release', lim)]))
    for m in range(1, 7):
        steps.append((f'[упоры] M{m} {MODE_NAMES[m]}', [('btn', f'M{m}')]))
        for s, sname in ((1, 'Внутр.'), (3, 'Наруж.')):
            steps.append((f'[упоры] M{m} S{s} {sname}', [('btn', f'S{s}')]))
            for sm in (2, 3, 1):
                steps.append((f'[упоры] M{m} S{s} OK → экран {sm}', [('btn', 'PARAM_OK')]))
                steps.append((f'[упоры] M{m} S{s} SM{sm} Вверх', [('btn', 'KEY:UP')]))
                steps.append((f'[упоры] M{m} S{s} SM{sm} Вниз', [('btn', 'KEY:DN')]))
        steps.append((f'[упоры] M{m} S2 возврат', [('btn', 'S2')]))
    steps.append(('[упоры] M1 S2', [('btn', 'M1'), ('btn', 'S2')]))
    for lim in ('LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT', 'LIM_REAR'):
        steps.append((f'упор {lim} снят (физ.)', [('press', lim), ('wait', 0.3), ('release', lim)]))
    steps.append(('переключатель подрежима отпущен', [('release', 'SUB_MAN')]))
    return steps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--out', required=True)
    ap.add_argument('--settle', type=float, default=0.8, help='пауза после действия, с')
    ap.add_argument('--only', default=None, help='только шаги, содержащие подстроку')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    import serial
    mega = serial.Serial(a.mega, 115200, timeout=0.2)
    time.sleep(2.5)                                   # Mega перезагружается при открытии порта
    for c in ('w0', 'z12345', 'x-2345'):              # Y = 12.345 мм, X = −2.345 мм
        mega.write((c + '\n').encode()); time.sleep(0.2)

    stm, esp, dec = Stm32(), Esp(a.esp), lcd_decoder()
    report = ['# Сверка LCD2004 ↔ новый дисплей', '',
              f'Позиции эмулятора: Y = 12.345 мм, X = −2.345 мм (Ø = −4.69 мм). Пауза после действия {a.settle} с.', '']
    fails, total = [], 0
    try:
        for name, actions in build_steps():
            if a.only and a.only not in name:
                continue
            for act, arg in actions:
                if act == 'btn':
                    esp.btn(arg); time.sleep(0.25)
                elif act == 'press':
                    stm.press(arg)
                elif act == 'release':
                    stm.release(arg)
                elif act == 'wait':
                    time.sleep(arg)
            time.sleep(a.settle)
            rows = [dec(r) for r in stm.lcd_codes()]
            ui = esp.uidump()
            core = {v: stm.var(v) for v in ('Mode', 'SelectMenu') if v in stm.sym}
            missing = compare(rows, ui)
            total += 1
            status = 'OK' if not missing else 'НЕТ НА НОВОМ: ' + ', '.join(f'{n:g}' for n in missing)
            if missing:
                fails.append((name, missing))
            print(f'[{total:3}] {name:45} {status}')
            vis = ui_visible_text(ui)
            report += [f'## {total}. {name} — {status}', '',
                       f'Ядро: Mode={core.get("Mode")} SelectMenu={core.get("SelectMenu")}', '',
                       '| LCD2004 | новый дисплей |', '|---|---|']
            ui_lines = [f'{k}: {v}' for k, v in vis.items() if v not in ('', '--', '---')]
            flags = ' '.join(f'{k}={ui[k]}' for k in ('alert', 'mode_menu', 'joystick', 'edit', 'sub_edit') if ui.get(k) not in (None, '0'))
            if flags:
                ui_lines.append(flags)
            for i in range(max(4, len(ui_lines))):
                l = rows[i].replace('|', '/') if i < 4 else ''
                r = ui_lines[i].replace('|', '/') if i < len(ui_lines) else ''
                report.append(f'| `{l}` | {r} |')
            report.append('')
    finally:
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'SUB_MAN', 'LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT', 'LIM_REAR'):
            try:
                stm.release(p)
            except Exception:
                pass
        stm.close()
        mega.write(b'w1\n'); mega.close()

    report[3:3] = [f'**Итог: {total - len(fails)} из {total} состояний — все числа старого экрана есть на новом.**', '']
    if fails:
        report[5:5] = ['Расхождения:', ''] + [f'- {n}: {", ".join(f"{x:g}" for x in m)}' for n, m in fails] + ['']
    path = os.path.join(a.out, 'compare_report.md')
    open(path, 'w', encoding='utf-8').write('\n'.join(report))
    print(f'\nИТОГ: {total - len(fails)}/{total} без расхождений; отчёт: {path}')


if __name__ == '__main__':
    main()
