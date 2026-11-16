#!/usr/bin/env python3
"""
Рисунки к руководству «Механика станка» и «Калибровка» (docs/manual/mechanics.md, README раздел 7).

SVG рисуются кодом — их легко поправить и перерисовать:
  python3 tools/docs_gen/mech_figures.py           → docs/manual/img/mech_*.svg, cal_*.svg
Правила допустимых «шагов на мм» — как в ядре (e2_mech_check в Core/e2/e2_core.cpp), см. steps_band().
"""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'docs', 'manual', 'img')

# цвета: поверхность светлая (картинка видна и в тёмной теме GitHub — как карточка)
BG, INK, INK2, MUTED, GRID = '#fcfcfb', '#1b1b1a', '#52514e', '#8a8984', '#e4e3df'
METAL, METAL_D, BLUE, ORANGE, GREEN, VIOLET, RED = '#c9ccd1', '#8f949b', '#2a78d6', '#eb6834', '#1baf7a', '#6250d6', '#d03b3b'
FONT = "font-family='DejaVu Sans, Verdana, Arial, sans-serif'"


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h, self.items = w, h, []
        self.title = title

    def add(self, s):
        self.items.append(s)

    def rect(self, x, y, w, h, fill=METAL, stroke=METAL_D, rx=4, sw=1.5, extra=''):
        self.add(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}' fill='{fill}' stroke='{stroke}' "
                 f"stroke-width='{sw}' {extra}/>")

    def line(self, x1, y1, x2, y2, color=INK, sw=2, dash=None, arrow=None):
        d = f" stroke-dasharray='{dash}'" if dash else ''
        m = ''
        if arrow in ('end', 'both'):
            m += f" marker-end='url(#a{color[1:]})'"
        if arrow in ('start', 'both'):
            m += f" marker-start='url(#s{color[1:]})'"
        self.add(f"<line x1='{x1}' y1='{y1}' x2='{x2}' y2='{y2}' stroke='{color}' stroke-width='{sw}'{d}{m}/>")

    def text(self, x, y, t, size=15, color=INK, anchor='start', weight='normal'):
        lines = t.split('\n')
        spans = ''.join(f"<tspan x='{x}' dy='{0 if i == 0 else size * 1.25:.1f}'>{l}</tspan>"
                        for i, l in enumerate(lines))
        self.add(f"<text x='{x}' y='{y}' {FONT} font-size='{size}' fill='{color}' text-anchor='{anchor}' "
                 f"font-weight='{weight}'>{spans}</text>")

    def badge(self, x, y, n, color=ORANGE):
        self.add(f"<circle cx='{x}' cy='{y}' r='13' fill='{color}'/>")
        self.text(x, y + 5, str(n), 15, '#ffffff', 'middle', 'bold')

    def path(self, d, fill='none', stroke=INK, sw=2):
        self.add(f"<path d='{d}' fill='{fill}' stroke='{stroke}' stroke-width='{sw}'/>")

    def save(self, name):
        markers = ''
        for c in (INK, BLUE, ORANGE, GREEN, VIOLET, RED, INK2):
            k = c[1:]
            markers += (f"<marker id='a{k}' viewBox='0 0 10 10' refX='9' refY='5' markerWidth='7' markerHeight='7' "
                        f"orient='auto'><path d='M0,0 L10,5 L0,10 z' fill='{c}'/></marker>"
                        f"<marker id='s{k}' viewBox='0 0 10 10' refX='1' refY='5' markerWidth='7' markerHeight='7' "
                        f"orient='auto'><path d='M10,0 L0,5 L10,10 z' fill='{c}'/></marker>")
        body = '\n'.join(self.items)
        svg = (f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {self.w} {self.h}' width='{self.w}' "
               f"height='{self.h}' role='img' aria-label='{self.title}'>\n<title>{self.title}</title>\n"
               f"<defs>{markers}</defs>\n<rect width='100%' height='100%' fill='{BG}' rx='10'/>\n{body}\n</svg>\n")
        with open(os.path.join(OUT, name), 'w', encoding='utf-8') as f:
            f.write(svg)
        print('  ', name)


def screw(s, x1, x2, y, h=16, pitch=12, color=METAL_D):
    """Ходовой винт сбоку: витки наклонными штрихами."""
    s.rect(x1, y - h / 2, x2 - x1, h, '#dfe2e6', color, 3, 1.2)
    x = x1 + 4
    while x < x2 - 4:
        s.line(x, y + h / 2 - 1, x + pitch * 0.6, y - h / 2 + 1, color, 1.4)
        x += pitch


def motor(s, x, y, w=70, h=56, label='', color=INK2):
    s.rect(x, y, w, h, '#3b3f45', '#24272b', 6, 1.5)
    s.rect(x + 6, y + 6, w - 12, h - 12, '#4a4f56', '#24272b', 4, 1)
    if label:
        s.text(x + w / 2, y + h / 2 + 5, label, 13, '#ffffff', 'middle', 'bold')


# ─────────────────────────────────────────────────────────────────────────────
def overview():
    s = Svg(960, 600, 'Что такое настройки механики: энкодер шпинделя, винты и моторы осей Z и X')
    s.text(24, 34, 'Станок сверху: что означает каждая строка экрана «МЕХАНИКА СТАНКА»', 19, INK, weight='bold')
    # энкодер и подпись — сверху слева
    s.badge(40, 72, 5)
    s.text(62, 68, 'ЭНКОДЕР — линий на 1 оборот шпинделя', 15, INK, weight='bold')
    s.text(62, 88, 'на энкодере написано «1000P/R» — это 1000 линий', 13, INK2)
    # передняя бабка, шпиндель, патрон, энкодер на ремне
    s.rect(40, 170, 170, 130, '#b9bec6', METAL_D, 8)
    s.text(125, 290, 'передняя бабка', 12, INK2, 'middle')
    s.add(f"<circle cx='125' cy='122' r='20' fill='#2f6fb8' stroke='#1d4f8a' stroke-width='2'/>")
    s.text(125, 127, 'E', 15, '#ffffff', 'middle', 'bold')
    s.add(f"<circle cx='125' cy='205' r='13' fill='{METAL}' stroke='{METAL_D}' stroke-width='2'/>")
    s.line(105, 122, 112, 205, INK2, 2); s.line(145, 122, 138, 205, INK2, 2)
    s.text(152, 128, 'ремень 1:1', 12, INK2)
    s.rect(210, 205, 40, 60, METAL, METAL_D, 6)                       # патрон
    s.rect(250, 227, 70, 16, '#e9e3d6', '#a59a83', 3)                # заготовка
    s.text(285, 262, 'заготовка', 12, INK2, 'middle')
    s.line(250, 235, 930, 235, INK2, 1, dash='6 6')                   # ось шпинделя
    s.text(440, 228, 'ось шпинделя', 12, MUTED, 'end')
    # станина
    s.rect(40, 300, 890, 36, '#d8dbe0', METAL_D, 4)
    s.text(920, 323, 'станина', 12, INK2, 'end')
    # каретка, поперечные салазки, винт X, мотор X спереди
    s.rect(450, 270, 170, 80, '#aab0b8', METAL_D, 6)
    s.text(456, 344, 'каретка', 12, INK, 'start')
    s.rect(500, 200, 70, 180, '#c3c8cf', METAL_D, 5)                  # поперечные салазки
    s.rect(518, 190, 34, 26, '#7d838b', '#55595f', 3)                 # резцедержатель
    s.add("<polygon points='530,190 522,178 540,190' fill='#55595f'/>")
    s.add(f"<rect x='527' y='222' width='16' height='150' fill='#dfe2e6' stroke='{METAL_D}' rx='3'/>")
    for yy in range(226, 366, 10):
        s.line(527, yy + 6, 543, yy, METAL_D, 1.2)
    motor(s, 500, 382, 70, 44, 'X')
    # винт Z вдоль станины спереди, мотор Z справа
    screw(s, 230, 836, 452)
    motor(s, 840, 424, 80, 56, 'Z')
    # подписи
    s.badge(650, 160, 1)
    s.text(672, 156, 'ШАГ ВИНТА X — винт поперечной', 14, INK)
    s.text(672, 175, 'подачи (мотор X спереди суппорта)', 14, INK)
    s.badge(240, 486, 1)
    s.text(262, 491, 'ШАГ ВИНТА Z — на сколько мм уходит каретка за 1 оборот винта', 14, INK)
    s.badge(650, 386, 2, BLUE); s.badge(680, 386, 3, BLUE)
    s.text(700, 382, 'мотор и драйвер каждой оси:', 13, INK)
    s.text(700, 400, 'ШАГОВ НА ОБОРОТ, МИКРОШАГ', 13, INK, weight='bold')
    s.badge(240, 524, 4, GREEN)
    s.text(262, 520, 'ЛЮФТ — на сколько ось «не доезжает», когда меняет направление', 14, INK)
    s.text(262, 539, '(зазор в гайке винта). Меряет КАЛИБРОВКА.', 14, INK)
    # оси
    s.line(40, 578, 200, 578, BLUE, 3, arrow='both')
    s.text(210, 583, 'Z — вдоль станины (продольная)', 13, BLUE, weight='bold')
    s.line(560, 562, 560, 594, VIOLET, 3, arrow='both')
    s.text(572, 583, 'X — поперёк (поперечная)', 13, VIOLET, weight='bold')
    s.save('mech_overview.svg')


def screw_pitch():
    s = Svg(960, 470, 'Как узнать шаг винта: посчитать витки по линейке или провернуть винт на один оборот')
    s.text(24, 34, 'Шаг винта: два способа узнать на своём станке', 19, INK, weight='bold')
    # способ 1: витки по линейке
    s.text(24, 70, 'Способ 1. Посчитать витки на 10 мм', 16, BLUE, weight='bold')
    x0, ppm = 60, 36                                  # 36 px на мм
    s.rect(40, 92, 420, 44, '#dfe2e6', METAL_D, 4)
    for k in range(0, 11):
        cx = x0 + k * 2 * ppm * 0.5 * 2              # гребень каждые 2 мм
        if cx > 450: break
        s.path(f'M{cx - 18},132 L{cx},96 L{cx + 18},132', '#c3c8cf', METAL_D, 1.5)
    # линейка
    s.rect(40, 150, 420, 40, '#f4e7b5', '#b49a3b', 2)
    for mm in range(0, 12):
        x = x0 + mm * ppm
        if x > 455: break
        s.line(x, 150, x, 150 + (16 if mm % 5 == 0 else 9), '#6b5a1c', 1.5)
        if mm % 5 == 0:
            s.text(x, 184, f'{mm}', 13, '#6b5a1c', 'middle')
    s.line(x0, 210, x0 + 10 * ppm, 210, ORANGE, 2.5, arrow='both')
    s.text(x0 + 5 * ppm, 232, '10 мм — 5 витков (от гребня до гребня)', 14, ORANGE, 'middle', 'bold')
    s.text(40, 266, 'Шаг = 10 мм / число витков = 10 / 5 = 2.00 мм', 15, INK, weight='bold')
    s.text(40, 292, 'Двухзаходный винт (две «дорожки» рядом) — шаг\nвдвое больше: считайте по одной дорожке.', 13, INK2)
    s.text(40, 340, 'Дюймовый винт: посчитайте витки на 25.4 мм (1 дюйм).\n'
                    'Шаг = 25.4 / витков: 8 ниток → 3.18 мм, 10 → 2.54,\n'
                    '12 → 2.12. На экран — 2 знака после точки.', 13, INK2)
    # способ 2: один оборот
    s.text(520, 70, 'Способ 2. Провернуть винт на 1 оборот', 16, BLUE, weight='bold')
    s.add(f"<circle cx='590' cy='170' r='52' fill='{METAL}' stroke='{METAL_D}' stroke-width='3'/>")
    s.add(f"<circle cx='590' cy='170' r='10' fill='{METAL_D}'/>")
    s.line(590, 170, 590, 122, RED, 4)
    s.text(530, 120, 'метка', 13, RED, 'end')
    s.path('M640,140 A60,60 0 1,1 600,112', 'none', ORANGE, 3)
    s.add(f"<polygon points='600,104 612,114 598,122' fill='{ORANGE}'/>")
    s.text(590, 252, 'маховик или муфта винта', 13, INK2, 'middle')
    screw(s, 650, 900, 170, 16, 12)
    s.rect(760, 140, 80, 50, '#aab0b8', METAL_D, 5)
    s.text(800, 212, 'каретка', 12, INK2, 'middle')
    s.line(760, 230, 840, 230, ORANGE, 2.5, arrow='end')
    s.text(800, 252, 'проехала = шаг', 14, ORANGE, 'middle', 'bold')
    s.text(520, 296, '1. Выберите люфт: поверните винт немного вперёд.\n'
                     '2. Обнулите индикатор, штангенциркуль или DRO.\n'
                     '3. Поверните винт ровно на 1 оборот по метке.\n'
                     '4. Сколько проехала каретка — это шаг винта.\n'
                     '   Точнее: 10 оборотов и разделить на 10.', 14, INK)
    s.text(520, 430, 'Мотор уже стоит? Поверните его вал рукой с выключенным\nдрайвером (или кнопкой «Энергосбережение»).', 13, MUTED)
    s.save('mech_screw_pitch.svg')


def motor_belt():
    s = Svg(960, 400, 'Шагов на оборот: по шильдику мотора и с учётом ремня или шестерён')
    s.text(24, 34, 'ШАГОВ НА ОБОРОТ — шаги мотора на один оборот ВИНТА (без микрошага)', 19, INK, weight='bold')
    # шильдик
    s.rect(40, 70, 320, 150, '#eef0f2', METAL_D, 8)
    s.text(60, 100, 'Шильдик мотора', 15, INK2, weight='bold')
    s.text(60, 130, '57HS76-3004  NEMA23', 14, INK)
    s.text(60, 155, 'STEP ANGLE: 1.8°', 16, RED, weight='bold')
    s.text(60, 180, 'RATED CURRENT: 3.0 A', 14, INK)
    s.text(60, 205, 'HOLDING TORQUE: 1.9 N·m', 14, INK)
    s.text(40, 252, 'Шагов = 360° / угол шага', 15, INK, weight='bold')
    s.text(40, 278, '1.8° → 360 / 1.8 = 200 (почти все моторы)\n0.9° → 360 / 0.9 = 400', 14, INK)
    s.text(40, 330, 'Мотор прямо на винте (муфта) — пишите 200.', 14, GREEN, weight='bold')
    # ремень
    s.text(500, 80, 'Мотор крутит винт через ремень или шестерни', 15, BLUE, weight='bold')
    s.add(f"<circle cx='560' cy='190' r='34' fill='{METAL}' stroke='{METAL_D}' stroke-width='3'/>")
    s.add(f"<circle cx='800' cy='190' r='68' fill='{METAL}' stroke='{METAL_D}' stroke-width='3'/>")
    s.path('M560,156 L800,122 M560,224 L800,258', 'none', '#3b3f45', 5)
    s.text(560, 196, '20', 18, INK, 'middle', 'bold')
    s.text(800, 196, '40', 22, INK, 'middle', 'bold')
    s.text(560, 250, 'на моторе\n20 зубьев', 13, INK2, 'middle')
    s.text(800, 284, 'на винте — 40 зубьев', 13, INK2, 'middle')
    s.text(500, 330, 'Шагов на оборот = 200 × 40 / 20 = 400', 16, INK, weight='bold')
    s.text(500, 356, 'зубья на винте / зубья на моторе; шестерни — так же.\nЕсли получилось дробное — выберите шкивы, где делится нацело.', 13, INK2)
    s.save('mech_motor_belt.svg')


def driver_dip():
    s = Svg(960, 420, 'Микрошаг: по переключателям и таблице на драйвере')
    s.text(24, 34, 'МИКРОШАГ — как выставлено переключателями на драйвере', 19, INK, weight='bold')
    # драйвер
    s.rect(40, 60, 300, 330, '#2b2e33', '#151719', 10)
    s.text(190, 90, 'DM542 / TB6600', 15, '#ffffff', 'middle', 'bold')
    s.rect(70, 110, 240, 60, '#c0392b', '#7a2219', 4)
    for i in range(8):
        x = 82 + i * 28
        s.rect(x, 118, 20, 44, '#f5f5f5', '#888', 2, 1)
        up = i in (5,)           # SW5..SW8 — микрошаг; пример: 1600
        s.rect(x + 3, 122 if up else 140, 14, 18, '#333', '#333', 2, 1)
        s.text(x + 10, 184, f'{i + 1}', 12, '#ffffff', 'middle')
    s.text(190, 206, 'SW1–3 — ток, SW4 — полток,\nSW5–8 — микрошаг', 12, '#cfd3d8', 'middle')
    s.text(190, 260, 'PUL+ PUL− DIR+ DIR− ENA+ ENA−', 11, '#cfd3d8', 'middle')
    s.text(190, 290, 'A+ A− B+ B−   GND +V', 11, '#cfd3d8', 'middle')
    # таблица
    tx, ty = 400, 70
    s.text(tx, ty, 'Таблица на корпусе драйвера (пример)', 15, INK2, weight='bold')
    rows = [('Pulse/rev', 'Микрошаг'), ('200', '1'), ('400', '2'), ('800', '4'), ('1600', '8'), ('3200', '16'),
            ('6400', '32'), ('12800', '64'), ('25600', '128')]
    for i, (a, b) in enumerate(rows):
        y = ty + 22 + i * 30
        hl = a == '1600'
        if hl:
            s.rect(tx - 6, y - 4, 330, 30, '#fff1c9', ORANGE, 4, 2)
        s.text(tx + 10, y + 17, a, 14, INK, weight='bold' if i == 0 or hl else 'normal')
        s.text(tx + 200, y + 17, b, 14, INK, weight='bold' if i == 0 or hl else 'normal')
    s.text(tx + 350, 200, 'Микрошаг =\nPulse/rev / 200', 17, ORANGE, weight='bold')
    s.text(tx + 350, 260, '1600 / 200 = 8', 16, INK, weight='bold')
    s.text(tx + 350, 300, 'Поменяли переключатели —\nпоменяйте и на экране,\nиначе размеры уйдут\nв разы.', 13, RED)
    s.save('mech_driver_dip.svg')


def encoder():
    s = Svg(960, 330, 'Энкодер: линии на оборот шпинделя, с учётом передачи')
    s.text(24, 34, 'ЭНКОДЕР — линий на один оборот ШПИНДЕЛЯ', 19, INK, weight='bold')
    s.add("<circle cx='130' cy='150' r='60' fill='#2f6fb8' stroke='#1d4f8a' stroke-width='3'/>")
    s.add("<circle cx='130' cy='150' r='12' fill='#cfd3d8'/>")
    s.text(130, 135, 'E6B2-CWZ6C', 11, '#ffffff', 'middle')
    s.text(130, 182, '1000P/R', 15, '#ffd34d', 'middle', 'bold')
    s.text(40, 250, 'На корпусе: «1000P/R», «1000 PPR»,\n«1000 lines» — это 1000 линий.\nНа экран — 1000.', 14, INK)
    s.text(400, 80, 'Как энкодер связан со шпинделем', 15, BLUE, weight='bold')
    s.text(400, 112, '• Напрямую или шестерни 1:1 → линий = как на энкодере (1000).', 14, INK)
    s.text(400, 140, '• Энкодер крутится БЫСТРЕЕ шпинделя: шестерня шпинделя 60 зуб.,', 14, INK)
    s.text(420, 160, 'энкодера 30 → 1000 × 60 / 30 = 2000 линий.', 14, INK)
    s.text(400, 188, '• Медленнее — так же по формуле: 1000 × 30 / 60 = 500.', 14, INK)
    s.text(400, 230, 'Не путайте линии с «импульсами ×4»: продавцы иногда пишут\n'
                     '4000 для энкодера на 1000 линий. На экран — именно линии.', 13, RED)
    s.text(400, 286, 'По умолчанию 1800 — из конфигурации автора 7e2.', 13, MUTED)
    s.save('mech_encoder.svg')


def band(e):
    """Допустимые шагов на мм оси при энкодере e линий — как в e2_mech_check."""
    lo = max(50 * e / 256, 2 * e / (0.2 * 231))
    hi = min(e, 2 * e / 4.5, 6250)
    return lo, hi


def steps_band():
    W, H = 960, 470
    s = Svg(W, H, 'Допустимые шаги на мм оси в зависимости от линий энкодера')
    s.text(24, 34, 'Сколько «шагов на мм» можно у оси при вашем энкодере', 19, INK, weight='bold')
    s.text(24, 58, 'Шагов на мм = шагов на оборот × микрошаг / шаг винта. Должно попасть в закрашенную полосу — у Z и у X.',
           13, INK2)
    L, R, T, B = 90, W - 40, 90, H - 70
    emin, emax, smax = 500, 4000, 1800
    X = lambda e: L + (e - emin) / (emax - emin) * (R - L)
    Y = lambda v: B - v / smax * (B - T)
    for v in range(0, smax + 1, 300):
        s.line(L, Y(v), R, Y(v), GRID, 1)
        s.text(L - 10, Y(v) + 5, str(v), 12, MUTED, 'end')
    for e in range(500, 4001, 500):
        s.line(X(e), B, X(e), B + 6, MUTED, 1)
        s.text(X(e), B + 24, str(e), 12, MUTED, 'middle')
    s.text((L + R) / 2, H - 18, 'ЭНКОДЕР, линий на оборот шпинделя', 13, INK2, 'middle')
    s.add(f"<text x='24' y='{(T + B) / 2}' {FONT} font-size='13' fill='{INK2}' text-anchor='middle' "
          f"transform='rotate(-90 24 {(T + B) / 2})'>шагов на мм</text>")
    pts_lo, pts_hi = [], []
    for e in range(emin, emax + 1, 50):
        lo, hi = band(e)
        pts_lo.append((X(e), Y(lo))); pts_hi.append((X(e), Y(min(hi, smax))))
    poly = ' '.join(f'{x:.1f},{y:.1f}' for x, y in pts_lo + pts_hi[::-1])
    s.add(f"<polygon points='{poly}' fill='{BLUE}' fill-opacity='0.16' stroke='none'/>")
    s.add("<polyline points='" + ' '.join(f'{x:.1f},{y:.1f}' for x, y in pts_lo) +
          f"' fill='none' stroke='{BLUE}' stroke-width='2'/>")
    s.add("<polyline points='" + ' '.join(f'{x:.1f},{y:.1f}' for x, y in pts_hi) +
          f"' fill='none' stroke='{BLUE}' stroke-width='2'/>")
    s.text(X(1750), Y(1150), 'выше полосы — не успеет\nкрупная резьба 4.5 мм', 12, INK2, 'start')
    s.text(X(2600), Y(170), 'ниже полосы — не выйдет подача 0.01 мм/об', 12, INK2, 'start')
    # точки примеров: (энкодер, шагов/мм, подпись, ок?)
    ex = [(1800, 400, 'Z по умолчанию 400', True, 1), (1800, 800, 'X по умолчанию 800', True, 1),
          (3600, 400, 'энкодер 3600 и Z 400 — мало', False, -1), (3600, 800, 'энкодер 3600, микрошаг 8: Z 800', True, -1),
          (3600, 1600, 'X 1600', True, -1), (1000, 200, '1000 линий, Z 200', True, 1)]
    for e, v, t, ok, side in ex:
        c = GREEN if ok else RED
        s.add(f"<circle cx='{X(e):.1f}' cy='{Y(v):.1f}' r='6' fill='{c}' stroke='{BG}' stroke-width='2'/>")
        s.text(X(e) + 10 * side, Y(v) + 4, t, 12, INK, 'start' if side > 0 else 'end')
    # таблица чисел
    s.rect(L + 10, T - 4, 250, 118, '#ffffff', GRID, 6, 1)
    s.text(L + 22, T + 16, 'энкодер → можно шагов на мм', 12, INK2, weight='bold')
    for i, e in enumerate((1000, 1800, 2500, 3600)):
        lo, hi = band(e)
        s.text(L + 22, T + 40 + i * 21, f'{e:>5}  →  от {int(lo) + 1} до {int(hi)}', 13, INK)
    s.save('mech_steps_band.svg')


def cone_ratio():
    s = Svg(960, 250, 'Конусы: соотношение шагов на мм осей Z и X')
    s.text(24, 34, 'Для конусов важно соотношение осей: Z / X от 0.50 до 1.27', 19, INK, weight='bold')
    L, R, y = 80, 880, 120
    X = lambda r: L + (r - 0.2) / (1.6 - 0.2) * (R - L)
    s.rect(X(0.2), y - 14, X(0.5) - X(0.2), 28, '#f6d4d4', RED, 0, 0)
    s.rect(X(0.5), y - 14, X(1.27) - X(0.5), 28, '#d3ecdf', GREEN, 0, 0)
    s.rect(X(1.27), y - 14, X(1.6) - X(1.27), 28, '#f6d4d4', RED, 0, 0)
    for r in (0.2, 0.5, 0.8, 1.0, 1.27, 1.6):
        s.line(X(r), y + 14, X(r), y + 22, MUTED, 1)
        s.text(X(r), y + 40, f'{r:.2f}', 12, MUTED, 'middle')
    s.text(X(0.35), y + 5, 'конус 45° — нет', 12, RED, 'middle', 'bold')
    s.text(X(0.88), y + 5, 'все конусы получатся', 13, '#0d6b49', 'middle', 'bold')
    s.text(X(1.43), y + 5, 'пологие (1:50) — нет', 12, RED, 'middle', 'bold')
    s.add(f"<circle cx='{X(0.5):.1f}' cy='{y}' r='6' fill='{INK}'/>")
    s.text(X(0.5), y - 22, 'по умолчанию: 400 / 800 = 0.50', 12, INK, 'middle')
    s.text(40, 210, 'Z/X = шагов на мм Z / шагов на мм X. Меньше 0.50 — поднимите микрошаг Z (или опустите X); '
                    'больше 1.27 — наоборот.', 13, INK2)
    s.save('mech_cone_ratio.svg')


# ── калибровка ──────────────────────────────────────────────────────────────
def indicator(s, x, y, tip_dir='left', label=''):
    """Индикатор часового типа: циферблат и ножка."""
    s.add(f"<circle cx='{x}' cy='{y}' r='30' fill='#ffffff' stroke='#333' stroke-width='3'/>")
    for k in range(12):
        import math
        a = k * math.pi / 6
        s.line(x + 24 * math.cos(a), y + 24 * math.sin(a), x + 28 * math.cos(a), y + 28 * math.sin(a), '#333', 1.2)
    s.line(x, y, x + 14, y - 16, RED, 2)
    if tip_dir == 'left':
        s.rect(x - 70, y - 4, 40, 8, '#bbb', '#777', 2, 1)
    elif tip_dir == 'up':
        s.rect(x - 4, y - 70, 8, 40, '#bbb', '#777', 2, 1)


def cal_indicator():
    s = Svg(960, 560, 'Калибровка по индикатору: куда ставить индикатор для оси Z и для оси X')
    s.text(24, 34, 'Калибровка по индикатору: куда ставить индикатор (вид сверху)', 19, INK, weight='bold')
    # Z
    s.text(40, 72, 'Ось Z (продольная)', 16, BLUE, weight='bold')
    s.rect(40, 200, 420, 34, '#d8dbe0', METAL_D, 4)
    s.text(450, 222, 'станина', 12, INK2, 'end')
    s.rect(240, 120, 110, 110, '#aab0b8', METAL_D, 6)
    s.text(295, 180, 'каретка', 13, INK, 'middle')
    s.rect(390, 130, 46, 70, '#5b6068', '#33363b', 4)                 # магнитная стойка на станине
    s.text(413, 98, 'стойка\nна станине', 12, INK2, 'middle')
    indicator(s, 413 - 0, 165 + 0, 'left')
    s.line(352, 165, 343 + 10, 165, RED, 2)
    s.line(160, 270, 340, 270, BLUE, 3, arrow='both')
    s.text(250, 292, 'каретка ходит вдоль станины', 13, BLUE, 'middle')
    s.text(40, 330, '• Стойка — на СТАНИНУ, ножка упирается в торец\n  каретки ВДОЛЬ оси Z.\n'
                    '• Натяг 1–2 мм: ножка должна ходить в обе стороны.\n'
                    '• Ход калибровки меньше хода индикатора:\n  индикатор 10 мм → ход 8 мм.', 14, INK)
    # X
    s.text(520, 72, 'Ось X (поперечная)', 16, VIOLET, weight='bold')
    s.rect(520, 300, 400, 34, '#d8dbe0', METAL_D, 4)
    s.rect(600, 150, 220, 170, '#aab0b8', METAL_D, 6)
    s.text(612, 312, 'каретка', 12, INK2, 'start')
    s.rect(660, 110, 80, 120, '#c3c8cf', METAL_D, 5)
    s.text(700, 175, 'попереч.\nсалазки', 12, INK, 'middle')
    s.rect(760, 240, 46, 60, '#5b6068', '#33363b', 4)
    s.text(783, 236 - 6, '', 12)
    indicator(s, 783, 270, 'none')
    s.rect(779, 222, 8, 18, '#bbb', '#777', 2, 1)
    s.line(783, 222, 783, 232, RED, 2)
    s.text(848, 262, 'стойка\nна каретке', 12, INK2, 'start')
    s.line(700, 98, 700, 236, INK, 1, dash='4 4')
    s.line(640, 100, 640, 240, VIOLET, 3, arrow='both')
    s.text(632, 170, 'X', 15, VIOLET, 'end', 'bold')
    s.text(520, 380, '• Стойка — на КАРЕТКУ (она стоит), ножка упирается\n  в поперечные салазки ВДОЛЬ оси X.\n'
                     '• X на экране — в радиусе или диаметре — неважно:\n  индикатор меряет ход салазок.\n'
                     '• Тот же натяг 1–2 мм.', 14, INK)
    s.text(40, 530, 'Вместо индикатора — штангенциркуль глубиномером от упора до каретки: ход 50–100 мм, точность ниже.',
           13, MUTED)
    s.save('cal_indicator.svg')


def cal_sequence():
    s = Svg(960, 400, 'Калибровка по индикатору: что делает ось и что вводить')
    s.text(24, 34, 'Калибровка по индикатору — по шагам (пример: ход 8 мм)', 19, INK, weight='bold')
    steps = [
        ('ПУСК', 'ось отходит на 1 мм\nи возвращается —\nлюфт выбран', None),
        ('Обнулите\nиндикатор', 'поверните шкалу\nна 0 → ДАЛЕЕ', '0.00'),
        ('Ось едет вперёд\nна 8 мм', 'введите показание:\n8.04 → ВВОД', '8.04'),
        ('Ось едет назад\nна 8 мм', 'введите показание:\n0.06 → ВВОД\n(это люфт)', '0.06'),
        ('Итог', 'винт = 2.00×8.04/8\n= 2.01 мм,\nлюфт 60 мкм\n→ ПРИМЕНИТЬ', None),
    ]
    for i, (a, b, ind) in enumerate(steps):
        x = 30 + i * 186
        s.rect(x, 70, 170, 300, '#ffffff', GRID, 8, 1.5)
        s.badge(x + 22, 92, i + 1, BLUE)
        s.text(x + 44, 90, a, 14, INK, weight='bold')
        if ind:
            s.add(f"<circle cx='{x + 85}' cy='200' r='40' fill='#ffffff' stroke='#333' stroke-width='3'/>")
            s.text(x + 85, 207, ind, 18, RED, 'middle', 'bold')
        else:
            s.line(x + 40, 200, x + 130, 200, ORANGE if i == 0 else GREEN, 3,
                   arrow='both' if i == 0 else None)
            if i == 4:
                s.text(x + 85, 192, '✓', 34, GREEN, 'middle', 'bold')
        s.text(x + 14, 280, b, 13, INK2)
        if i < 4:
            s.line(x + 172, 220, x + 184, 220, MUTED, 2, arrow='end')
    s.save('cal_sequence.svg')


def cal_scales():
    s = Svg(960, 316, 'Калибровка по линейкам: ось сама ходит туда-обратно, станок сравнивает линейку и шаги')
    s.text(24, 34, 'Калибровка по линейкам или блоку DRO — станок меряет сам', 19, INK, weight='bold')
    s.rect(60, 150, 820, 30, '#d8dbe0', METAL_D, 4)
    s.rect(60, 190, 820, 12, '#f4e7b5', '#b49a3b', 2)
    for k in range(0, 41):
        s.line(60 + k * 20.5, 190, 60 + k * 20.5, 196 if k % 5 else 202, '#6b5a1c', 1)
    s.text(870, 222, 'линейка', 12, '#6b5a1c', 'end')
    s.rect(380, 90, 120, 60, '#aab0b8', METAL_D, 6)
    s.text(440, 126, 'каретка', 13, INK, 'middle')
    for k, yy in enumerate((76, 64, 52)):
        s.line(450, yy, 620, yy, BLUE, 2, arrow='both')
    s.text(630, 62, '3 раза вперёд и назад на ход\n(Z 20 мм, X 10 мм)', 13, BLUE)
    s.line(330, 120, 370, 120, ORANGE, 2, arrow='end')
    s.text(325, 116, 'сначала отход 1 мм — выбрать люфт', 12, ORANGE, 'end')
    s.text(60, 250, 'Шаг винта = винт × ход по линейке / ход по шагам мотора. Люфт — насколько линейка «не доехала»'
                    '\nпри развороте.', 13, INK2)
    s.text(60, 290, 'Нужно свободное место: ход + 2–3 мм в обе стороны. Шпиндель стоит, джойстик в середине.', 13, INK2)
    s.save('cal_scales.svg')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for f in (overview, screw_pitch, motor_belt, driver_dip, encoder, steps_band, cone_ratio,
              cal_indicator, cal_sequence, cal_scales):
        f()
