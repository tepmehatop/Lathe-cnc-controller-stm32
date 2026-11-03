#!/usr/bin/env python3
"""
Сверка старого (LCD2004) и нового (ESP32) дисплея на живой связке.

Старый экран читается из памяти STM32 через ST-Link (теневой буфер E2Lcd, 4×20),
новый — командой <UIDUMP> по USB дисплея. Действия: касания <BTN:...> через дисплей
(те же Switch_*/Key_* ядра, что и у кнопок пульта) и нажатия физических входов —
вывод STM32 временно переводится в выход с низким уровнем через SWD.

Проверка: каждая подпись старого экрана (таблица RULES) — в ячейке нового с тем же
значением; строки без правила и одинаковые подписи в двух ячейках — расхождение.
Все пары экранов — в отчёт.

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
# LCD2004 пишет кириллицу латинскими двойниками (A, B, C, ... из знакогенератора) — приводим
# обе стороны к кириллице верхнего регистра. Оси X/Y: X → Х (кириллица) с обеих сторон.
_LAT2CYR = str.maketrans('ABCEHKMOPTXaceopxymb', 'АВСЕНКМОРТХасеорхумь')

def norm(t):
    t = re.sub(r'#[0-9a-fA-F]{6} ([^#]*)#', r'\1', t or '')
    return t.translate(_LAT2CYR).upper().replace('\\N', ' ').strip()

NUM_RE = re.compile(r'[+-]?\d+(?:\.\d+)?')

# Ячейки нового дисплея: (ключ видимости, заголовок, значение)
CELLS = (('', 'primary_unit', 'primary_val'), ('', 'secondary_unit', 'secondary_val'),
         ('row1_box', 'row1_title', 'row1_val'), ('row2_box', 'row2_title', 'row2_val'),
         ('row3_box', 'row3_title', 'row3_val'), ('row4_box', 'row4_title', 'row4_val'),
         ('thr_type_box', 'thr_type_title', 'thr_type_val'), ('diam_box', 'diam_title', 'diam_val'))

def ui_cells(ui):
    out = []
    for box, t, v in CELLS:
        if t not in ui or (box and ui.get(box) == '0'):
            continue
        out.append((norm(ui[t]), norm(ui.get(v, ''))))
    return out

# Правила: подпись LCD2004 → где и как она должна быть на новом дисплее.
#   где: 'cell' (ячейка с заголовком по регулярке), 'mode', 'sub', 'sm', 'alert', 'any' (любой текст), 'skip'
#   как: 'num' — число, 'snum' — число со знаком (группа 1 — минус LCD), 'first' — первое целое ячейки,
#        'text' — текст, None — только наличие
R = lambda name, lcd, where, title=None, kind=None: (name, re.compile(lcd), where, title and re.compile(title), kind)
RULES = [
    R('Ø (диаметр)',      r'^Ø(-?)\s*([\d.]+)\s',            'cell', r'^⌀ ДИАМЕТР$', 'snum'),
    R('Ось X',            r'^Х(-?)\s*([\d.]+)\s',            'cell', r'^(ПОЗИЦИЯ|ОСЬ) Х$', 'snum'),
    R('Ось Y',            r'^Y(-?)\s*([\d.]+)\s',            'cell', r'^(ПОЗИЦИЯ|ОСЬ) Y$', 'snum'),
    R('Подача',           r'ПОДАЧА:\s*([\d.]+)',             'cell', r'^ПОДАЧА$', 'num'),
    R('Циклов',           r'ЦИКЛОВ:\s*(\d+)',                'cell', r'^(ПРОХОДЫ|ЦИКЛОВ)$', 'first'),
    R('Съём Ø',           r'СЪЁМ Ø:\s*([\d.]+)',             'cell', r'^СЪЁМ ⌀$', 'num'),
    R('Съём R',           r'СЪЁМ R:\s*([\d.]+)',             'cell', r'^СЪЁМ R$', 'num'),
    R('Шаг',              r'ШАГ:\s*([\d.]+)',                'cell', r'^(ШАГ ММ|ДЮЙМ|G-ТРУБ|K-ТРУБ|К-ТРУБ)$', 'num'),
    R('Об/мин',           r'ОБ/МИН:\s*(\d+)',                'cell', r'^ОБ/МИН$', 'num'),
    R('Ножка',            r'НОЖКА:Ø([\d.]+)',                'cell', r'^НОЖКА ⌀$', 'num'),
    R('Заходов',          r'ЗАХОДОВ:\s*(\d+)',               'cell', r'^ЗАХОДОВ$', 'first'),
    R('Шар Ø',            r'ШАР Ø([\d.]+)ММ',                'cell', r'^ШАР ⌀$', 'num'),
    R('Шар: состояние',   r'ШАР Ø[\d.]+ММ\s+(\S+)',          'sub',  None, 'text'),
    R('Подрежим',         r'^(РУЧНО\S*|РУЧНА\S*|ВНУТР\S*|НАРУЖ\S*)\s', 'sub', None, 'prefix4'),
    R('Режим Синхрон',    r'(?<!А)(СИНХРОН)\s*$',            'mode', None, 'text'),
    R('Режим Асинхрон',   r'(АСИНХРОН)\s*$',                 'mode', None, 'text'),
    R('Режим Резьба',     r'→\s+(РЕЗЬБА)\s*$',               'mode', None, 'text'),
    R('Режим Конус',      r'([<>])\s*\S+\s+(КОНУС)\s*$',     'mode', None, 'cone'),
    R('Угол конуса',      r'[<>]\s*(\S+?)°?\s+КОНУС\s*$',    'cell', r'^КОНУС$', 'text'),
    R('Ввод Ø/Сброс',     r'ВВОД Ø\s+СБРОС_ОСЕЙ',            'sm',   r'ВВОД ⌀', None),
    R('Диаметр (ввод)',   r'ДИАМЕТР ←→ (-?)\s*([\d.]+)ММ',   'cell', r'^⌀ ДИАМЕТР$', 'snum'),
    R('Ось X (ввод)',     r'ОСЬ Х:.*?(-?)\s*([\d.]+)ММ',     'cell', r'^(ПОЗИЦИЯ|ОСЬ) Х$', 'snum'),
    R('Ось Y (ввод)',     r'ОСЬ Y:.*?(-?)\s*([\d.]+)ММ',     'cell', r'^(ПОЗИЦИЯ|ОСЬ) Y$', 'snum'),
    R('Чист проходов',    r'ЧИСТ ПРОХОДОВ:.*?(\d+)\s*$',     'cell', r'ЧИСТ', 'num'),
    R('Колич заходов',    r'КОЛИЧ ЗАХОДОВ:.*?(\d+)\s*$',     'cell', r'ЗАХОД', 'num'),
    R('Ход резьбы',       r'ХОД РЕЗЬБЫ:\s*([\d.]+)',         'cell', r'^ХОД', 'num'),
    R('Шаг резьбы',       r'ШАГ РЕЗЬБЫ:\s*([\d.]+)',         'cell', r'^(ШАГ ММ|ДЮЙМ|G-ТРУБ|K-ТРУБ|К-ТРУБ)$', 'num'),
    R('Текущий угол',     r'ТЕКУЩИЙ\s+УГОЛ:\s*([\d.]+)°',    'cell', r'^УГОЛ', 'num'),
    R('Делим круг на',    r'ДЕЛИМ КРУГ НА:\s*(\d+)',         'cell', r'^ДЕЛЕНИЙ$', 'num'),
    R('Выбор отметки',    r'ВЫБОР ОТМЕТКИ:\s*(\d+)',         'cell', r'^МЕТКА$', 'num'),
    R('Угол сектора',     r'УГОЛ\s+СЕКТОРА:\s*([\d.]+)°',    'cell', r'СЕКТОР', 'num'),
    R('Конус (экран 2)',  r'^КОНУС\s+[<>]\s+(\S+?)°?\s',     'cell', r'.', 'text'),
    R('Кон. резьба',      r'РЕЗЬБА:\s+(ВКЛ|ВЫКЛ)',           'cell', r'.', 'text'),
    R('Коническая',       r'^КОНИЧЕСКАЯ',                    'skip'),
    R('В режиме резьба',  r'В РЕЖИМЕ РЕЗЬБА',                'cell', r'^В РЕЖИМЕ$', None),
    R('Ширина резца',     r'ШИРИНА РЕЗЦА:\s*([\d.]+)',       'cell', r'ШИРИН', 'num'),
    R('Шаг по оси Y',     r'ШАГ ПО ОСИ Y:\s*([\d.]+)',       'cell', r'ШАГ.*Y', 'num'),
    R('Делилка: заголовок', r'^ТЕКУЩИЙ УГОЛ$',               'cell', r'^УГОЛ', None),
    R('Делилка: угол',    r'^([\d.]+)°$',                    'cell', r'^УГОЛ', 'num'),
    R('Сброс кнопкой Селект', r'СБРОС КНОПКОЙ СЕЛЕКТ',       'any',  r'СБРОС', None),
    R('Отскок Y',         r'ОТСКОК Y:.*?([\d.]+)ММ',        'cell', r'^ОТСКОК Y$', 'num'),
    R('Ослабление',       r'^ОСЛАБЛЕНИЕ',                    'skip'),
    R('…натяга Y',        r'НАТЯГА Y:.*?([\d.]+)ММ',        'cell', r'НАТЯГА Y$', 'num'),
    R('Режим невозможен', r'РЕЖИМ НЕВОЗМОЖЕН',               'alert', r'НЕВОЗМОЖЕН', None),
    R('ВНИМАНИЕ',         r'^ВНИМАНИЕ:?$',                   'alert'),
    R('Установите упоры', r'УСТАНОВИТЕ УПОРЫ',               'alert', r'УПОРЫ', None),
    R('Установите джойстик', r'УСТАНОВИТЕ ДЖОЙСТИК',         'alert', r'ДЖОЙСТИК', None),
    R('…в нейтральное',   r'^В НЕЙТРАЛЬНОЕ$|^ПОЛОЖЕНИЕ$',    'alert'),
    R('Суппорт',          r'СУППОРТ|ИСХОДН',                 'alert', r'СУППОРТ', None),
    R('Операция завершена', r'ОПЕРАЦИЯ [3З]АВЕРШЕНА',        'alert', r'ЗАВЕРШЕНА', None),
]

def _num(text):
    m = NUM_RE.search(text.replace(',', '.'))
    return float(m.group()) if m else None

def _value_ok(kind, m, val):
    if kind is None:
        return True, ''
    if kind == 'snum':
        want = float(m.group(1) + m.group(2))
        got = _num(val)
        return got is not None and abs(got - want) < 0.006, f'{want:g}'
    if kind in ('num', 'first'):
        want = float(m.group(1))
        got = _num(val)
        return got is not None and abs(got - want) < 0.006, f'{want:g}'
    want = m.group(1).replace('°', '')
    v = val.replace('°', '')
    if kind == 'prefix4':
        return v[:4] == want[:4], want
    return want in v, want

def compare(lcd_rows, ui):
    """Список проблем: (подпись LCD, что не так)."""
    issues = []
    cells = ui_cells(ui)
    alert_on = ui.get('alert') == '1'
    alert_txt = norm(ui.get('alert_msg', ''))
    all_txt = ' '.join([t + ' ' + v for t, v in cells] +
                       [norm(ui.get(k, '')) for k in ('mode_lbl', 'submode_lbl', 'sm_row6')] +
                       ([alert_txt] if alert_on else []))
    for raw in lcd_rows:
        row = norm(raw)
        if not row:
            continue
        matched = False
        for name, rx, where, title, kind in RULES:
            m = rx.search(row)
            if not m:
                continue
            matched = True
            if where == 'skip':
                continue
            if where == 'cell':
                cand = [(t, v) for t, v in cells if title.search(t)]
                if not cand:
                    issues.append((name, f'нет ячейки «{title.pattern}» на новом'))
                    continue
                res = [_value_ok(kind, m, v) for t, v in cand]
                if not any(ok for ok, _ in res):
                    issues.append((name, f'LCD {res[0][1]}, новый: ' + '; '.join(f'{t}={v}' for t, v in cand)))
            elif where in ('mode', 'sub', 'sm'):
                key = {'mode': 'mode_lbl', 'sub': 'submode_lbl', 'sm': 'sm_row6'}[where]
                val = norm(ui.get(key, ''))
                if kind == 'cone':
                    ok = ('КОНУС ' + m.group(1)) in val
                elif title is not None and kind is None:
                    ok = bool(title.search(val))
                else:
                    ok = _value_ok(kind, m, val)[0]
                if not ok:
                    issues.append((name, f'LCD «{m.group(0).strip()}», новый {key}={val}'))
            elif where == 'alert':
                if not alert_on:
                    issues.append((name, 'на новом нет сообщения'))
                elif title is not None and not title.search(alert_txt):
                    issues.append((name, f'на новом другое сообщение: {alert_txt}'))
            elif where == 'any':
                if not title.search(all_txt):
                    issues.append((name, 'текста нет на новом'))
        if not matched:
            issues.append(('строка LCD', f'нет правила сверки: «{raw.strip()}»'))
    # одна и та же подпись в двух ячейках
    titles = [t for t, v in cells if t and v not in ('', '--', '---')]
    for t in sorted(set(titles)):
        if titles.count(t) > 1:
            issues.append(('дубль', f'«{t}» в {titles.count(t)} ячейках'))
    return issues


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
    steps.append(('переключатель подрежима «Ручной» (физ.), M2', [('press', 'SUB_MAN'), ('btn', 'M2'), ('btn', 'S2')]))
    # Левый упор ставится правее правого и наоборот (Menu.ino) — между нажатиями отгоняем
    # каретку джойстиком (в аПодаче мотор идёт без вращения шпинделя)
    for pin, flag in (('LIM_LEFT', 'limit_Left_flag'), ('LIM_RIGHT', 'limit_Right_flag'),
                      ('LIM_FRONT', 'limit_Front_flag'), ('LIM_REAR', 'limit_Rear_flag')):
        steps.append((f'упор {pin} снят перед установкой', [('lim', (pin, flag, 0))]))
    steps.append(('упор LIM_LEFT установлен (физ.)', [('lim', ('LIM_LEFT', 'limit_Left_flag', 1))]))
    steps.append(('джойстик Вправо 3 с',   [('press', 'JOY_RIGHT'), ('wait', 3), ('release', 'JOY_RIGHT')]))
    steps.append(('упор LIM_RIGHT установлен (физ.)', [('lim', ('LIM_RIGHT', 'limit_Right_flag', 1))]))
    steps.append(('упор LIM_FRONT установлен (физ.)', [('lim', ('LIM_FRONT', 'limit_Front_flag', 1))]))
    # Передний упор встаёт в текущей точке и запрещает движение к нему (Вверх) — отгон Вниз
    steps.append(('джойстик Вниз 3 с',     [('press', 'JOY_DOWN'), ('wait', 3), ('release', 'JOY_DOWN')]))
    steps.append(('упор LIM_REAR установлен (физ.)', [('lim', ('LIM_REAR', 'limit_Rear_flag', 1))]))
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
    for pin, flag in (('LIM_LEFT', 'limit_Left_flag'), ('LIM_RIGHT', 'limit_Right_flag'),
                      ('LIM_FRONT', 'limit_Front_flag'), ('LIM_REAR', 'limit_Rear_flag')):
        steps.append((f'упор {pin} снят (физ.)', [('lim', (pin, flag, 0))]))
    steps.append(('переключатель подрежима отпущен', [('release', 'SUB_MAN')]))
    return steps


def run_actions(stm, esp, actions):
    """Выполнить действия; вернуть список проблем сценария (например, упор не встал)."""
    problems = []
    for act, arg in actions:
        if act == 'btn':
            esp.btn(arg); time.sleep(0.25)
        elif act == 'press':
            stm.press(arg)
        elif act == 'release':
            stm.release(arg)
        elif act == 'wait':
            time.sleep(arg)
        elif act == 'lim':                      # кнопка упора переключает — жмём, только если нужно
            pin, flag, want = arg
            for _ in range(3):
                if stm.var(flag) == want:
                    break
                stm.press(pin); time.sleep(0.3); stm.release(pin); time.sleep(0.4)
            if stm.var(flag) != want:
                problems.append(f'{flag} не стал {want} после 3 нажатий {pin}')
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--out', required=True)
    ap.add_argument('--settle', type=float, default=0.8, help='пауза после действия, с')
    ap.add_argument('--only', default=None, help='только шаги, содержащие одну из подстрок (через |)')
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
            if a.only and not any(x in name for x in a.only.split('|')):
                continue
            scen = run_actions(stm, esp, actions)
            time.sleep(a.settle)
            rows = [dec(r) for r in stm.lcd_codes()]
            ui = esp.uidump()
            if compare(rows, ui):
                # снимок мог попасть на перерисовку одного из экранов — пересъёмка через 0.5 с
                time.sleep(0.5)
                rows = [dec(r) for r in stm.lcd_codes()]
                ui = esp.uidump()
            core = {v: stm.var(v) for v in ('Mode', 'SelectMenu', 'limit_Left_flag', 'limit_Right_flag',
                                            'limit_Front_flag', 'limit_Rear_flag') if v in stm.sym}
            issues = [('сценарий', p) for p in scen] + compare(rows, ui)
            total += 1
            status = 'OK' if not issues else f'{len(issues)} расхожд.'
            if issues:
                fails.append((name, issues))
            print(f'[{total:3}] {name:45} {status}')
            for n, why in issues:
                print(f'        - {n}: {why}')
            report += [f'## {total}. {name} — {status}', '',
                       'Ядро: ' + ' '.join(f'{k}={v}' for k, v in core.items()), '']
            report += [f'- ❗ {n}: {why}' for n, why in issues] + ([''] if issues else [])
            report += ['| LCD2004 | новый дисплей (видимые ячейки: заголовок = значение) |', '|---|---|']
            ui_lines = [f'{k}: {norm(ui.get(k, ""))}' for k in ('mode_lbl', 'submode_lbl', 'sm_row6')
                        if norm(ui.get(k, '')) not in ('', '--')]
            ui_lines += [f'{t} = {v}' for t, v in ui_cells(ui) if (t or v) and v not in ('---',)]
            if ui.get('alert') == '1':
                ui_lines.append(f'СООБЩЕНИЕ: {norm(ui.get("alert_msg", ""))}')
            flags = ' '.join(f'{k}={ui[k]}' for k in ('mode_menu', 'joystick', 'edit', 'sub_edit') if ui.get(k) not in (None, '0'))
            if flags:
                ui_lines.append(flags)
            for i in range(max(4, len(ui_lines))):
                l = rows[i].replace('|', '/') if i < 4 else ''
                r = ui_lines[i].replace('|', '/') if i < len(ui_lines) else ''
                report.append(f'| `{l}` | {r} |')
            report.append('')
    finally:
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'SUB_MAN', 'LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT', 'LIM_REAR'):
            try:
                stm.release(p)
            except Exception:
                pass
        stm.close()
        mega.write(b'w1\n'); mega.close()

    report[3:3] = [f'**Итог: {total - len(fails)} из {total} состояний — каждая подпись и значение LCD2004 есть на новом, без дублей.**', '']
    if fails:
        report[5:5] = ['Расхождения:', ''] + [f'- {n}: ' + '; '.join(f'{a} — {b}' for a, b in m) for n, m in fails] + ['']
    path = os.path.join(a.out, 'compare_report.md')
    open(path, 'w', encoding='utf-8').write('\n'.join(report))
    print(f'\nИТОГ: {total - len(fails)}/{total} без расхождений; отчёт: {path}')


if __name__ == '__main__':
    main()
