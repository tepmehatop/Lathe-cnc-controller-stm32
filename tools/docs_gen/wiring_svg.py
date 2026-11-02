#!/usr/bin/env python3
"""
Схемы подключения пульта и периферии к STM32F4DISCOVERY → docs/wiring/*.svg
и таблица выводов docs/wiring/pins.md (из Core/e2/e2_pins.h).

Запуск: python3 tools/docs_gen/wiring_svg.py
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from svg import S

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'docs', 'wiring')

# ── Разъёмы P1/P2 платы Discovery (UM1472, табл. 5) ─────────────────────────────
P1 = {1: 'GND', 2: 'GND', 3: 'VDD', 4: 'VDD', 5: 'GND', 6: 'NRST', 7: 'PC1', 8: 'PC0', 9: 'PC3', 10: 'PC2',
      11: 'PA1', 12: 'PA0', 13: 'PA3', 14: 'PA2', 15: 'PA5', 16: 'PA4', 17: 'PA7', 18: 'PA6', 19: 'PC5', 20: 'PC4',
      21: 'PB1', 22: 'PB0', 23: 'GND', 24: 'PB2', 25: 'PE7', 26: 'PE8', 27: 'PE9', 28: 'PE10', 29: 'PE11', 30: 'PE12',
      31: 'PE13', 32: 'PE14', 33: 'PE15', 34: 'PB10', 35: 'PB11', 36: 'PB12', 37: 'PB13', 38: 'PB14', 39: 'PB15',
      40: 'PD8', 41: 'PD9', 42: 'PD10', 43: 'PD11', 44: 'PD12', 45: 'PD13', 46: 'PD14', 47: 'PD15', 48: 'NC',
      49: 'GND', 50: 'GND'}
P2 = {1: 'GND', 2: 'GND', 3: '5V', 4: '5V', 5: '3V', 6: '3V', 7: 'PH0', 8: 'PH1', 9: 'PC14', 10: 'PC15',
      11: 'PE6', 12: 'PC13', 13: 'PE4', 14: 'PE5', 15: 'PE2', 16: 'PE3', 17: 'PE0', 18: 'PE1', 19: 'PB8', 20: 'PB9',
      21: 'BOOT0', 22: 'VDD', 23: 'PB6', 24: 'PB7', 25: 'PB4', 26: 'PB5', 27: 'PD7', 28: 'PB3', 29: 'PD5', 30: 'PD6',
      31: 'PD3', 32: 'PD4', 33: 'PD1', 34: 'PD2', 35: 'PC12', 36: 'PD0', 37: 'PC10', 38: 'PC11', 39: 'PA14',
      40: 'PA15', 41: 'PA10', 42: 'PA13', 43: 'PA8', 44: 'PA9', 45: 'PC8', 46: 'PC9', 47: 'PC6', 48: 'PC7',
      49: 'GND', 50: 'GND'}
HEADER = {v: f'P1-{k}' for k, v in P1.items() if v.startswith('P')}
HEADER.update({v: f'P2-{k}' for k, v in P2.items() if v.startswith('P')})

# Группы: цвет и название
GROUPS = [
    ('enc',    '#d62828', 'Энкодер шпинделя'),
    ('drv',    '#f77f00', 'Драйверы моторов'),
    ('hand',   '#7b2cbf', 'РГИ (ручной энкодер)'),
    ('mode',   '#2a9d8f', 'Режим и подрежим'),
    ('btn',    '#1d70b8', 'Кнопки меню'),
    ('joy',    '#3a86ff', 'Джойстик'),
    ('lim',    '#6a994e', 'Упоры и их светодиоды'),
    ('out',    '#9c6644', 'Зуммер, тахометр, переменник'),
    ('link',   '#444444', 'Дисплей, LCD, DRO, отладка'),
    ('scale',  '#e76f51', 'Линейки напрямую'),
]
GCOLOR = {g: c for g, c, _ in GROUPS}


def group_of(name):
    if name.startswith('ENC'): return 'enc'
    if name.startswith(('Z_', 'X_')): return 'drv'
    if name.startswith('HAND'): return 'hand'
    if name.startswith(('MODE', 'SUB')): return 'mode'
    if name.startswith('BTN'): return 'btn'
    if name.startswith('JOY'): return 'joy'
    if name.startswith(('LIM', 'LED')): return 'lim'
    if name in ('TACHO', 'BEEPER', 'FEED_POT'): return 'out'
    return 'link'


# Человеческие названия
TITLES = {
    'ENC_A': 'Энкодер шпинделя, канал A', 'ENC_B': 'Энкодер шпинделя, канал B',
    'HAND_A': 'РГИ, канал A', 'HAND_B': 'РГИ, канал B', 'HAND_AXIS_Z': 'РГИ: переключатель оси — Z',
    'HAND_AXIS_X': 'РГИ: переключатель оси — X', 'HAND_X1': 'РГИ: масштаб ×1', 'HAND_X10': 'РГИ: масштаб ×10',
    'Z_STEP': 'Драйвер Z (каретка): STEP', 'Z_DIR': 'Драйвер Z: DIR', 'Z_ENA': 'Драйвер Z: ENA',
    'X_STEP': 'Драйвер X (суппорт): STEP', 'X_DIR': 'Драйвер X: DIR', 'X_ENA': 'Драйвер X: ENA',
    'TACHO': 'Тахометр (выход: импульс на оборот)', 'BEEPER': 'Зуммер',
    'MODE_FEED': 'Режим: Синхронная подача', 'MODE_AFEED': 'Режим: Асинхронная подача',
    'MODE_CONE_L': 'Режим: Конус <', 'MODE_CONE_R': 'Режим: Конус >', 'MODE_THREAD': 'Режим: Резьба',
    'MODE_SPHERE': 'Режим: Шар', 'MODE_DIVIDER': 'Режим: Делилка', 'MODE_RESERVE': 'Режим: Резерв',
    'SUB_INT': 'Подрежим: Внутренний', 'SUB_MAN': 'Подрежим: Ручной', 'SUB_EXT': 'Подрежим: Наружный',
    'BTN_LEFT': 'Кнопка меню Влево', 'BTN_RIGHT': 'Кнопка меню Вправо', 'BTN_UP': 'Кнопка меню Вверх',
    'BTN_DOWN': 'Кнопка меню Вниз', 'BTN_SELECT': 'Кнопка меню Выбор',
    'JOY_LEFT': 'Джойстик Влево', 'JOY_RIGHT': 'Джойстик Вправо', 'JOY_UP': 'Джойстик Вверх (на себя)',
    'JOY_DOWN': 'Джойстик Вниз (от себя)', 'JOY_RAPID': 'Кнопка ускоренного хода',
    'LIM_LEFT': 'Кнопка упора Левый', 'LIM_RIGHT': 'Кнопка упора Правый', 'LIM_FRONT': 'Кнопка упора Передний',
    'LIM_REAR': 'Кнопка упора Задний', 'LED_LEFT': 'Светодиод упора Левый', 'LED_RIGHT': 'Светодиод упора Правый',
    'LED_FRONT': 'Светодиод упора Передний', 'LED_REAR': 'Светодиод упора Задний',
    'FEED_POT': 'Переменный резистор подачи (средний вывод)',
    'LCD_SCL': 'LCD2004 (I2C): SCL', 'LCD_SDA': 'LCD2004 (I2C): SDA',
    'DISP_TX': 'Дисплей ESP32: TX STM32 → RX дисплея', 'DISP_RX': 'Дисплей ESP32: RX STM32 ← TX дисплея',
    'DRO_RX': 'Блок DRO HS800-2: его TX → сюда', 'DRO_TX': 'Блок DRO: не подключается',
    'DBG_TX': 'Отладочный порт (USB-UART): TX', 'DBG_RX': 'Отладочный порт (USB-UART): RX',
    'AUDIO_RESET': '— (служебный: держит аудиочип платы выключенным)',
}


# Короткие подписи для карты разъёмов
SHORT = {
    'ENC_A': 'Энкодер шпинделя A', 'ENC_B': 'Энкодер шпинделя B', 'HAND_A': 'РГИ A', 'HAND_B': 'РГИ B',
    'HAND_AXIS_Z': 'РГИ ось Z', 'HAND_AXIS_X': 'РГИ ось X', 'HAND_X1': 'РГИ ×1', 'HAND_X10': 'РГИ ×10',
    'Z_STEP': 'Z STEP', 'Z_DIR': 'Z DIR', 'Z_ENA': 'Z ENA', 'X_STEP': 'X STEP', 'X_DIR': 'X DIR', 'X_ENA': 'X ENA',
    'TACHO': 'Тахометр', 'BEEPER': 'Зуммер', 'MODE_FEED': 'Реж. Синхр.', 'MODE_AFEED': 'Реж. Асинхр.',
    'MODE_CONE_L': 'Реж. Конус <', 'MODE_CONE_R': 'Реж. Конус >', 'MODE_THREAD': 'Реж. Резьба', 'MODE_SPHERE': 'Реж. Шар',
    'MODE_DIVIDER': 'Реж. Делилка', 'MODE_RESERVE': 'Реж. Резерв', 'SUB_INT': 'Подреж. Внутр.', 'SUB_MAN': 'Подреж. Ручной',
    'SUB_EXT': 'Подреж. Наруж.', 'BTN_LEFT': 'Кн. Влево', 'BTN_RIGHT': 'Кн. Вправо', 'BTN_UP': 'Кн. Вверх',
    'BTN_DOWN': 'Кн. Вниз', 'BTN_SELECT': 'Кн. Выбор', 'JOY_LEFT': 'Джойст. Влево', 'JOY_RIGHT': 'Джойст. Вправо',
    'JOY_UP': 'Джойст. Вверх', 'JOY_DOWN': 'Джойст. Вниз', 'JOY_RAPID': 'Ускор. ход', 'LIM_LEFT': 'Упор Л',
    'LIM_RIGHT': 'Упор П', 'LIM_FRONT': 'Упор Перед.', 'LIM_REAR': 'Упор Задн.', 'LED_LEFT': 'Светод. Л',
    'LED_RIGHT': 'Светод. П', 'LED_FRONT': 'Светод. Перед.', 'LED_REAR': 'Светод. Задн.', 'FEED_POT': 'Переменник',
    'LCD_SCL': 'LCD SCL', 'LCD_SDA': 'LCD SDA', 'DISP_TX': 'Дисплей ← TX', 'DISP_RX': 'Дисплей → RX',
    'DRO_RX': 'DRO (вход)', 'DRO_TX': '—', 'DBG_TX': 'Отладка TX', 'DBG_RX': 'Отладка RX', 'AUDIO_RESET': 'служебный',
    'SCALE_Z_PIN_A': 'Линейка Z A', 'SCALE_Z_PIN_B': 'Линейка Z B', 'SCALE_Z_PIN_REQ': 'Линейка Z REQ',
    'SCALE_X_PIN_A': 'Линейка X A', 'SCALE_X_PIN_B': 'Линейка X B', 'SCALE_X_PIN_REQ': 'Линейка X REQ',
}

# Как подключать (короткая подсказка в таблице)
HOW = {
    'enc': 'подтяжка 1k к 3V; см. схему энкодера', 'drv': 'через NPN-ключ, см. схему драйверов',
    'hand': 'подтяжка 1k к 3V (+10 нФ на A и B)', 'mode': 'контакт переключателя на GND, подтяжка 1k к 3V',
    'btn': 'кнопка на GND, подтяжка 1k к 3V', 'joy': 'контакт на GND, подтяжка 1k к 3V',
    'lim': 'кнопка на GND / светодиод от 3V через 220 Ом', 'out': 'см. схему', 'link': 'см. схему связи',
    'scale': 'см. docs/scales',
}


def parse_pins():
    txt = open(os.path.join(ROOT, 'Core', 'e2', 'e2_pins.h'), encoding='utf-8').read()
    pins = []
    for m in re.finditer(r'#define E2_PIN_(\w+)\s+([A-E]),\s*(\d+)\s*//\s*(.*)', txt):
        name, port, n, comment = m.groups()
        mega = re.match(r'\(([^)]*)\)', comment)
        pins.append({'name': name, 'pin': f'P{port}{n}', 'mega': mega.group(1) if mega else '',
                     'group': group_of(name), 'comment': comment})
    return pins


def scale_pins():
    txt = open(os.path.join(ROOT, 'Core', 'els_config.h'), encoding='utf-8').read()
    out = []
    for key, title in (('SCALE_Z_PIN_A', 'Линейка Z: A / такт'), ('SCALE_Z_PIN_B', 'Линейка Z: B / данные'),
                       ('SCALE_Z_PIN_REQ', 'Линейка Z: REQ (Digimatic)'), ('SCALE_X_PIN_A', 'Линейка X: A / такт'),
                       ('SCALE_X_PIN_B', 'Линейка X: B / данные'), ('SCALE_X_PIN_REQ', 'Линейка X: REQ (Digimatic)')):
        m = re.search(r'#define\s+' + key + r'\s+(P[A-E]\d+)', txt)
        out.append({'name': key, 'pin': m.group(1), 'mega': '', 'group': 'scale', 'title': title})
    return out


# ── 1. Карта разъёмов Discovery ───────────────────────────────────────────────
def pinout_svg(pins):
    used = {p['pin']: p for p in pins}
    W, H = 1100, 1050
    s = S(W, H, 'STM32F4DISCOVERY: что к какому штырьку разъёмов P1 и P2', OUT)
    s.text(W / 2, 50, 'Вид сверху, USB-разъём ST-Link — вверху. Штырёк 1 отмечен квадратом на плате.',
           size=13, anchor='middle', color='#444')
    row_h, top = 34, 112

    def header(x0, table, label):
        s.e.append(f'<rect x="{x0}" y="{top - 14}" width="70" height="{25 * row_h + 8}" rx="6" fill="#222"/>')
        s.text(x0 + 35, top - 24, label, size=16, bold=True, anchor='middle')
        for r in range(25):
            for c in range(2):
                n = r * 2 + c + 1
                pin = table[n]
                cx, cy = x0 + 18 + c * 34, top + r * row_h + 4
                p = used.get(pin)
                fill = GCOLOR[p['group']] if p else ('#c00' if pin in ('5V',) else '#e0a000' if pin in ('3V', 'VDD') else '#777' if pin == 'GND' else '#bbb')
                shape = 'rect' if n == 1 else 'circle'
                if shape == 'rect':
                    s.e.append(f'<rect x="{cx - 9}" y="{cy - 9}" width="18" height="18" fill="{fill}" stroke="#fff" stroke-width="1.5"/>')
                else:
                    s.e.append(f'<circle cx="{cx}" cy="{cy}" r="9" fill="{fill}" stroke="#fff" stroke-width="1.5"/>')
                s.text(cx, cy + 4, str(n), size=8, anchor='middle', color='#fff', bold=True)
                # подпись
                lbl = pin
                if p:
                    lbl = f'{pin} {SHORT.get(p["name"], p["name"])}'
                elif pin in ('GND', '5V', '3V', 'VDD', 'NRST', 'BOOT0', 'NC'):
                    lbl = {'GND': 'GND', '5V': '5V', '3V': '3V (подтяжки)',
                           'VDD': 'VDD', 'NRST': 'NRST', 'BOOT0': 'BOOT0', 'NC': '—'}[pin]
                else:
                    lbl = f'{pin} свободен' if pin.startswith('P') else pin
                col = '#111' if p else '#888'
                if c == 0:
                    s.text(x0 - 10, cy + 4, lbl, size=11, anchor='end', color=col)
                else:
                    s.text(x0 + 80, cy + 4, lbl, size=11, color=col)

    header(230, P1, 'P1')
    header(800, P2, 'P2')
    # легенда
    y = top + 25 * row_h + 20
    x = 20
    for g, c, name in GROUPS:
        s.e.append(f'<circle cx="{x + 8}" cy="{y}" r="7" fill="{c}"/>')
        s.text(x + 20, y + 4, name, size=12)
        x += 20 + len(name) * 7 + 18
        if x > W - 200:
            x, y = 20, y + 22
    s.save('discovery_pinout.svg')


# ── 2. Кнопка / переключатель / концевик ──────────────────────────────────────
def input_svg():
    s = S(820, 400, 'Любая кнопка, переключатель, концевик → STM32', OUT)
    s.box(560, 70, 230, 150, 'STM32 (Discovery)', 'вывод из таблицы\n(например PB13 — «Влево»)', fill='#eaf6ea')
    s.text(60, 92, '3V платы', size=13, bold=True)
    s.line(110, 100, 110, 120); s.res(110, 120, '1k', vertical=True); s.line(110, 160, 110, 180)
    s.dot(110, 180); s.line(110, 180, 560, 180)
    # кнопка
    s.line(110, 180, 110, 220); s.line(110, 220, 95, 245); s.dot(110, 220)
    s.line(110, 252, 110, 270); s.gnd(110, 270)
    s.text(130, 245, 'кнопка / контакт переключателя', size=12)
    s.text(122, 286, 'GND', size=12)
    s.dot(330, 180); s.line(330, 180, 330, 200); s.cap(330, 200, '100 нФ (по желанию)')
    s.note(325, ['• Не нажато: на выводе 1 (3.3 В через 1k). Нажато: 0 (замкнуто на GND).',
                 '• Резистор 1k — как у автора 7e2, но к 3V платы, НЕ к +5 В. Внутренняя подтяжка STM32 тоже',
                 '  включена; внешняя 1k нужна для помехоустойчивости на длинных проводах пульта.',
                 '• Галетный переключатель: общий контакт — на GND, каждое положение — на свой вывод.'])
    s.save('input_button.svg')


# ── 3. Энкодер шпинделя ───────────────────────────────────────────────────────
def encoder_svg():
    s = S(900, 600, 'Энкодер шпинделя → STM32 (PA1 — канал A, PA8 — канал B)', OUT)
    # Вариант 1: промышленный NPN
    s.text(20, 66, 'Вариант 1 — промышленный энкодер с выходом NPN «открытый коллектор»', size=14, bold=True)
    s.text(20, 84, '(Omron E6B2-CWZ6C, Autonics E40S и аналоги, питание 5–24 В)', size=12, color='#444')
    s.box(20, 100, 200, 160, 'Энкодер', 'коричн. — +5…24 В\nсиний — GND\nчёрный — A\nбелый — B', fill='#fdf3e6')
    s.box(660, 100, 220, 160, 'STM32', 'A → PA1 (P1-11)\nB → PA8 (P2-43)\nGND → GND', fill='#eaf6ea')
    for i, (y, n) in enumerate(((150, 'A'), (210, 'B'))):
        rx = 380 + i * 70
        s.line(220, y, 660, y); s.text(232, y - 6, n, size=12, bold=True); s.dot(rx, y)
        s.line(rx, y, rx, y - 10); s.res(rx, y - 50, '1k', vertical=True)
        s.text(rx + 10, y - 54, '3V', size=11)
        s.dot(540, y); s.line(540, y, 540, y + 6); s.line(530, y + 6, 550, y + 6); s.line(530, y + 11, 550, y + 11)
        s.line(540, y + 11, 540, y + 20); s.gnd(540, y + 20); s.text(556, y + 14, '1 нФ', size=11)
    s.note(285, ['• Выход NPN сам тянет линию к земле; подтяжка 1k к 3V даёт «1» безопасного уровня 3.3 В.',
                 '• 1 нФ на землю у самой платы глушит иголки от двигателя шпинделя и ЧП.',
                 '• Число линий — ENC_LINE_PER_REV в els_config.h (у автора 1800).'])
    # Вариант 2: самодельный датчик + LM393 как у автора
    s.text(20, 370, 'Вариант 2 — самодельный оптический датчик (как у автора 7e2): компаратор LM393', size=14, bold=True)
    s.box(20, 390, 180, 120, 'Фотодатчики', 'аналоговый сигнал\nA и B', fill='#fdf3e6')
    s.box(300, 390, 220, 120, 'LM393 (питание 5 В)', 'порог — подстроечник 3–5k\nгистерезис — 22k\nиз выхода на вход +', fill='#eef4fb')
    s.box(660, 390, 220, 120, 'STM32', 'A → PA1\nB → PA8', fill='#eaf6ea')
    s.line(200, 450, 300, 450)
    s.line(520, 450, 660, 450); s.dot(600, 450); s.line(600, 450, 600, 440); s.res(600, 400, '1k', vertical=True)
    s.text(612, 384, '3V (не 5 В!)', size=11, bold=True)
    s.note(540, ['• Выход LM393 — открытый коллектор: подтяжку выхода сделать к 3V платы, а не к +5 В, как было на Mega.',
                 '• Остальная схема (делитель порога, 22k гистерезиса) — без изменений.'])
    s.save('spindle_encoder.svg')


# ── 4. РГИ ────────────────────────────────────────────────────────────────────
def hand_svg():
    s = S(900, 470, 'РГИ — ручной энкодер 100 линий (маховичок) и его переключатели', OUT)
    s.box(20, 70, 210, 170, 'Маховичок (MPG)', '+5V, GND\nA, B\n(A−, B− не нужны)', fill='#fdf3e6')
    s.box(660, 70, 220, 170, 'STM32', 'A → PE7 (P1-25)\nB → PC13 (P2-12)', fill='#eaf6ea')
    for i, (y, n) in enumerate(((130, 'A'), (190, 'B'))):
        rx = 380 + i * 70
        s.line(230, y, 660, y); s.text(242, y - 6, n, size=12, bold=True); s.dot(rx, y)
        s.line(rx, y, rx, y - 10); s.res(rx, y - 50, '1k', vertical=True)
        s.text(rx + 10, y - 54, '3V', size=11)
        s.dot(540, y); s.line(540, y, 540, y + 6); s.line(530, y + 6, 550, y + 6); s.line(530, y + 11, 550, y + 11)
        s.line(540, y + 11, 540, y + 20); s.gnd(540, y + 20); s.text(556, y + 14, '10 нФ', size=11)
    s.text(20, 280, 'Переключатель оси (3 положения): Z — PC10 (P2-37), выкл., X — PC12 (P2-35); общий — GND', size=13)
    s.text(20, 302, 'Переключатель масштаба: ×1 — PA4 (P1-16), ×10 — PD8 (P1-40); общий — GND', size=13)
    s.note(335, ['• Конденсаторы 10 нФ на линиях A и B — совет автора 7e2 против ложных щелчков.',
                 '• Выходы маховичка 5 В допустимы: PE7 и PC13 терпят 5 В. Если выход «открытый коллектор» —',
                 '  работает через подтяжку 1k к 3V.',
                 '• PA4 (×1) НЕ терпит 5 В — только контакт на GND и подтяжка к 3V.',
                 '• Направление вращения — HC_Z_DIR / HC_X_DIR в els_config.h.'])
    s.save('hand_encoder.svg')


# ── 5. Драйверы моторов ───────────────────────────────────────────────────────
def npn(s, x, y, label):
    """NPN-ключ: база слева на (x, y), коллектор вверх (x+40, y-30), эмиттер вниз (x+40, y+30)."""
    s.e.append(f'<circle cx="{x + 30}" cy="{y}" r="22" fill="none" stroke="#222" stroke-width="2"/>')
    s.line(x, y, x + 20, y); s.line(x + 20, y - 14, x + 20, y + 14)
    s.line(x + 20, y - 6, x + 40, y - 20); s.line(x + 40, y - 20, x + 40, y - 30)
    s.line(x + 20, y + 6, x + 40, y + 20); s.line(x + 40, y + 20, x + 40, y + 30)
    s.e.append(f'<polygon points="{x + 40},{y + 20} {x + 30},{y + 18} {x + 35},{y + 11}" fill="#222"/>')
    s.text(x + 58, y + 4, label, size=11)


def driver_svg():
    s = S(980, 690, 'Драйверы шаговых моторов: STEP, DIR, ENA (по три сигнала на ось)', OUT)
    s.text(20, 62, 'Вариант 1 (по умолчанию, как у автора 7e2) — через NPN-ключ: DRIVER_SIGNALS_DIRECT = 0', size=14, bold=True)
    s.box(20, 90, 200, 380, 'STM32', 'ось Z:\nSTEP PE9 (P1-27)\nDIR PE10 (P1-28)\nENA PE11 (P1-29)\n\nось X:\nSTEP PE13 (P1-31)\nDIR PE14 (P1-32)\nENA PE15 (P1-33)', fill='#eaf6ea')
    s.box(760, 90, 200, 380, 'Драйвер мотора', '(DM542, TB6600 и т.п.)', fill='#fdf3e6')
    s.text(640, 124, '+5 В', size=13, bold=True); s.line(660, 130, 660, 415)
    for y, plus, minus in ((200, 'PUL+', 'PUL−'), (300, 'DIR+', 'DIR−'), (400, 'ENA+', 'ENA−')):
        # входы драйвера
        s.dot(660, y - 50); s.line(660, y - 50, 760, y - 50); s.text(770, y - 46, plus, size=12, bold=True)
        s.line(440, y - 30, 440, y - 20); s.line(440, y - 30, 760, y - 30); s.text(770, y - 26, minus, size=12, bold=True)
        # ключ
        s.line(220, y, 300, y); s.res(300, y, '1k'); s.line(340, y, 400, y)
        npn(s, 400, y + 10, 'BC547 / 2N2222')
        s.line(440, y + 40, 440, y + 46); s.gnd(440, y + 46)
        s.dot(370, y); s.line(370, y, 370, y + 6); s.res(370, y + 6, '10k', vertical=True)
        s.line(370, y + 46, 370, y + 50); s.gnd(370, y + 50)
    s.text(870, 440, 'оптроны внутри', size=12, anchor='middle', color='#666')
    s.note(500, ['• «1» на выводе STM32 открывает транзистор → через оптрон драйвера идёт ток. Логика уровней та же, что у автора.',
                 '• 10k от базы на GND держат транзистор закрытым, пока STM32 стартует. Для оси X — такие же три ключа.',
                 '• Вариант 2 — напрямую, без транзисторов: PUL+/DIR+/ENA+ драйвера → 3V платы (только если драйвер',
                 '  срабатывает от 3.3 В), PUL−/DIR−/ENA− → выводы STM32. В els_config.h: DRIVER_SIGNALS_DIRECT = 1.',
                 '• Питание моторов (24–80 В) — отдельный блок, к плате STM32 не подключается. Общие только',
                 '  сигнальные провода; земли силовой части и платы не соединять.',
                 '• Минимальная длительность импульса — STEP_PULSE_MIN_US (3 мкс, подходит для DM542/TB6600).'])
    s.save('drivers.svg')


# ── 6. Светодиоды, зуммер, тахометр, переменник ──────────────────────────────
def outputs_svg():
    s = S(900, 560, 'Светодиоды упоров, зуммер, тахометр, переменник подачи', OUT)
    # LED
    s.text(20, 70, 'Светодиод упора (4 шт.)', size=14, bold=True)
    s.text(20, 98, '3V', size=12, bold=True); s.line(45, 94, 120, 94)
    s.e.append('<polygon points="120,82 120,106 142,94" fill="#fff" stroke="#222" stroke-width="2"/>')
    s.line(142, 82, 142, 106); s.line(142, 94, 180, 94); s.res(180, 94, '220 Ом'); s.line(220, 94, 300, 94)
    s.text(310, 98, '→ PE6 / PE5 / PE4 / PE2 (Левый / Правый / Передний / Задний)', size=12)
    s.text(20, 126, 'Светится при «0» на выводе — как у автора (у него 470–680 Ом к +5 В; здесь 220 Ом к 3V).', size=12, color='#444')
    # зуммер
    s.text(20, 180, 'Зуммер (активный, 5 В)', size=14, bold=True)
    s.text(20, 214, '+5V', size=12, bold=True); s.line(50, 210, 120, 210)
    s.box(120, 190, 100, 40, 'зуммер', '', fill='#fff')
    s.line(220, 210, 300, 210); s.line(300, 210, 300, 230)
    npn(s, 260, 260, 'BC547')
    s.line(300, 290, 300, 298); s.gnd(300, 298)
    s.line(140, 260, 220, 260); s.res(220, 260, '1k'); s.line(140, 260, 140, 260)
    s.text(20, 264, 'PE12 (P1-30) →', size=12)
    s.text(380, 264, 'BEEPER_ON_LEVEL = 1 (звук при «1»)', size=12, color='#444')
    # тахометр
    s.text(20, 350, 'Тахометр (выход)', size=14, bold=True)
    s.text(20, 376, 'PE8 (P1-26) → вход тахометра: импульс на каждый оборот шпинделя, уровень 3.3 В.', size=12)
    s.text(20, 394, 'Если тахометру нужно 5 В — тот же NPN-ключ, что для зуммера.', size=12, color='#444')
    # переменник
    s.text(20, 440, 'Переменник подачи 10k (если есть, USE_FEED_POT = 1)', size=14, bold=True)
    s.text(20, 466, 'Крайние выводы — 3V и GND платы (НЕ 5 В!), средний → PC1 (P1-7).', size=12)
    s.text(20, 484, 'Без переменника — USE_FEED_POT = 0: подача задаётся с дисплея и кнопками.', size=12, color='#444')
    s.save('outputs.svg')


# ── 7. Связь: дисплей, LCD, DRO, отладка ──────────────────────────────────────
def links_svg():
    s = S(900, 520, 'Связь: сенсорный дисплей, LCD2004, блок DRO, отладочный порт', OUT)
    s.box(330, 60, 230, 380, 'STM32 Discovery', 'PB10 (P1-34)\nPB11 (P1-35)\n\nPB6 (P2-23)\nPB7 (P2-24)\n\nPA3 (P1-13)\n\nPA9 (P2-44)\nPA10 (P2-41)\n\nGND', fill='#eaf6ea')
    s.box(20, 60, 230, 120, 'Дисплей ESP32', '4.3" JC4827W543 или\n10.1" JC8012P4A1C\n(разъём UART)', fill='#eef4fb')
    s.line(250, 100, 330, 100); s.text(290, 94, 'RX ← PB10', size=11, anchor='middle')
    s.line(250, 130, 330, 130); s.text(290, 124, 'TX → PB11', size=11, anchor='middle')
    s.box(20, 200, 230, 90, 'LCD2004 + I2C-модуль', 'VCC → +5 В, GND\nадрес 0x27 (или 0x3F)', fill='#eef4fb')
    s.line(250, 230, 330, 230); s.text(290, 224, 'SCL → PB6', size=11, anchor='middle')
    s.line(250, 260, 330, 260); s.text(290, 254, 'SDA → PB7', size=11, anchor='middle')
    s.box(620, 180, 260, 90, 'Блок DRO HS800-2', 'TX блока → PA3\nGND → GND', fill='#fdf3e6')
    s.line(560, 225, 620, 225)
    s.box(620, 300, 260, 110, 'USB-UART (CP2102, CH340)', 'для отладки и команд\nRX адаптера ← PA9\nTX адаптера → PA10\n115200', fill='#fff6f6')
    s.line(560, 340, 620, 340); s.line(560, 370, 620, 370)
    s.note(460, ['• Все связи — 3.3 В; общий GND у всех устройств. Дисплей питается от своего USB/5 В.',
                 '• Модуль LCD2004 подтягивает SDA/SCL к 5 В — выводы PB6/PB7 это выдерживают.'])
    s.save('links.svg')


# ── 8. Эмулятор на Arduino Mega ───────────────────────────────────────────────
def emulator_svg():
    s = S(900, 560, 'Стенд без станка: Arduino Mega изображает блок DRO и линейки', OUT)
    s.box(20, 70, 250, 340, 'Arduino Mega 2560', 'прошивка\nSimulator/dro_simulator', fill='#fdf3e6')
    s.box(630, 70, 250, 340, 'STM32 Discovery', 'штырьки: PA3 P1-13,\nPA15 P2-40, PB3 P2-28,\nPB4 P2-25, PB5 P2-26,\nPC6 P2-47, PC7 P2-48', fill='#eaf6ea')
    rows = [('TX1 → PA3', 'пакеты блока DRO'), ('D22 → PA15', 'линейка Z: A/такт'),
            ('D23 → PB3', 'линейка Z: B/данные'), ('D24 → PB4', 'линейка X: A/такт'),
            ('D25 → PB5', 'линейка X: B/данные'), ('D26 ← PC6', 'REQ Z (Digimatic)'),
            ('D27 ← PC7', 'REQ X (Digimatic)'), ('GND — GND', 'общая земля')]
    for i, (a, b) in enumerate(rows):
        y = 162 + i * 29
        s.line(270, y, 630, y)
        s.res(330, y, '1k') if i < 7 else None
        s.text(450, y - 5, a, size=12, anchor='middle', bold=True)
        s.text(560, y - 5, b, size=11, anchor='middle', color='#444')
    s.note(440, ['• Резисторы 1k защищают выводы: Mega работает от 5 В, STM32 — от 3.3 В.',
                 '• Для режима «блок DRO» достаточно двух проводов: TX1 → PA3 и GND.',
                 '• Обе платы — по USB к компьютеру: Mega (команды эмулятора, 115200) и ST-Link Discovery (прошивка).',
                 '• Режим линеек в эмуляторе: команда m<буква> (mq квадратура, ms Sylvac, mb BIN6, mi iGaging, md Digimatic).'])
    s.save('emulator.svg')


# ── 9. Общая схема системы ───────────────────────────────────────────────────
def overview_svg():
    s = S(980, 520, 'Из чего состоит электронная гитара на STM32', OUT)
    s.box(370, 190, 240, 130, 'STM32F4DISCOVERY', 'мозг: считает резьбы,\nподачи, упоры', fill='#eaf6ea')
    s.box(20, 40, 260, 110, 'Сенсорный дисплей', 'ESP32 4.3" или 10.1"\nкнопки на экране + показания', fill='#eef4fb')
    s.box(20, 190, 260, 130, 'Пульт', 'переключатели режима,\nкнопки, джойстик, упоры,\nРГИ, LCD2004 (запасной)', fill='#eef4fb')
    s.box(20, 370, 260, 110, 'Координаты', 'блок DRO HS800-2\nили линейки напрямую', fill='#fdf3e6')
    s.box(700, 40, 260, 110, 'Энкодер шпинделя', 'угол и обороты шпинделя', fill='#fdf3e6')
    s.box(700, 190, 260, 130, 'Драйверы и моторы', 'Z — каретка (продольная)\nX — суппорт (поперечная)', fill='#fff6f6')
    s.box(700, 370, 260, 110, 'Питание', '5 В — плата, пульт, LCD\n24–80 В — драйверы моторов', fill='#fff6f6')
    for (x1, y1, x2, y2) in ((280, 95, 370, 220), (280, 255, 370, 255), (280, 425, 370, 290),
                             (700, 95, 610, 220), (610, 255, 700, 255), (700, 425, 610, 290)):
        s.line(x1, y1, x2, y2)
    s.save('overview.svg')


# ── Таблица выводов ──────────────────────────────────────────────────────────
def pins_md(pins):
    lines = ['# Выводы STM32F4DISCOVERY — что куда подключать', '',
             'Сгенерировано из `Core/e2/e2_pins.h` и `Core/els_config.h` скриптом `tools/docs_gen/wiring_svg.py`.',
             'Карта разъёмов — [discovery_pinout.svg](discovery_pinout.svg). «Mega» — вывод в оригинальной схеме 7e2.', '',
             '| Что подключить | Вывод STM32 | Штырёк на плате | Mega (7e2) | Как |', '|---|---|---|---|---|']
    for g, _, gname in GROUPS:
        for p in pins:
            if p['group'] != g:
                continue
            title = TITLES.get(p['name'], p.get('title', p['name']))
            lines.append(f"| {title} | **{p['pin']}** | {HEADER.get(p['pin'], '—')} | {p['mega']} | {HOW[g]} |")
    lines += ['', 'Питание: **3V** (P2-5, P2-6) — для подтяжек и светодиодов (до 100 мА); **5V** (P2-3, P2-4) — вход 5 В',
              'для платы, когда она не питается по USB; **GND** — P1-1, P1-2, P1-5, P1-23, P1-49, P1-50, P2-1, P2-2, P2-49, P2-50.']
    with open(os.path.join(OUT, 'pins.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    pins = parse_pins() + scale_pins()
    for p in pins:
        if p['pin'] not in HEADER:
            print('нет на разъёмах:', p['pin'], p['name'])
    pinout_svg(pins); input_svg(); encoder_svg(); hand_svg(); driver_svg(); outputs_svg()
    links_svg(); emulator_svg(); overview_svg(); pins_md(pins)
    print('ok:', OUT)
