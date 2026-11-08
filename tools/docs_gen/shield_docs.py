#!/usr/bin/env python3
"""
Сборка и подключение через шилд (BACKLOG-033) → docs/assembly/*.svg

  python3 tools/docs_gen/shield_docs.py

Источник — hardware/shield/design.py (разъёмы, подписи выводов на плате) и els_shield.kicad_pcb (где на плате
стоит каждый разъём и его вывод 1). Схемы: «разъём шилда → провод → вывод модуля», по одной на группу.
"""
import math, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'hardware', 'shield'))
import design                                    # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'assembly')
PCB = os.path.join(ROOT, 'hardware', 'shield', 'els_shield.kicad_pcb')
TERM = {ref: (title, nets, labels) for ref, title, nets, labels in design.TERMINALS}

FONT = 'Arial, Helvetica, sans-serif'
C_GND, C_5V, C_3V = '#222222', '#d62828', '#f08c00'
PALETTE = ['#1d70b8', '#2a9d8f', '#7b2cbf', '#c9a227', '#6a994e', '#e76f51', '#3a86ff', '#9c6644']


def esc(t):
    return str(t).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class Svg:
    def __init__(self, w, h, auto=True):
        self.w, self.h, self.e, self.auto, self.maxy = w, h, [], auto, 0

    def add(self, s):
        self.e.append(s)
        for v in re.findall(r' (?:y|y1|y2|cy)="([\d.\-]+)"', s):        # нижний край рисунка — для высоты
            self.maxy = max(self.maxy, float(v))
        m = re.search(r' y="([\d.\-]+)"[^>]* height="([\d.\-]+)"', s)
        if m:
            self.maxy = max(self.maxy, float(m.group(1)) + float(m.group(2)))

    def text(self, x, y, t, size=14, bold=False, anchor='start', color='#111', italic=False):
        b = ' font-weight="bold"' if bold else ''
        it = ' font-style="italic"' if italic else ''
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{FONT}" font-size="{size}"{b}{it} '
                 f'text-anchor="{anchor}" fill="{color}">{esc(t)}</text>')

    def line(self, x1, y1, x2, y2, color='#222', w=2, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ''
        self.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{color}" stroke-width="{w}"'
                 f' stroke-linecap="round"{d}/>')

    def path(self, d, color='#222', w=2, fill='none'):
        self.add(f'<path d="{d}" stroke="{color}" stroke-width="{w}" fill="{fill}" stroke-linecap="round" '
                 f'stroke-linejoin="round"/>')

    def rect(self, x, y, w, h, fill='#fff', stroke='#335', sw=2, rx=6, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ''
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" '
                 f'stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def circle(self, x, y, r, fill='#fff', stroke='#222', sw=2):
        self.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def save(self, name):
        if self.auto:
            self.h = int(self.maxy + 20)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, name), 'w', encoding='utf-8') as f:
            f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                    f'viewBox="0 0 {self.w} {self.h}"><rect width="100%" height="100%" fill="#fff"/>'
                    + ''.join(self.e) + '</svg>')
        print('схема', name)


def net_color(net, i):
    if net == 'GND':
        return C_GND
    if net.startswith('+5V'):
        return C_5V
    if net == '+3V':
        return C_3V
    return PALETTE[i % len(PALETTE)]


# ── Разъём шилда: корпус с выводами сверху вниз ───────────────────────────────────────────────────────────
PITCH = 38
HX = 150                    # левый край корпуса


def shield_conn(s, ref, y0, pins=None):
    """Рисует разъём ref, возвращает {номер вывода: (x правого края, y)}. pins — свой список (для IDC)."""
    title, nets, labels = TERM[ref]
    rows = pins or [(i + 1, labels[i], nets[i]) for i in range(len(nets))]
    h = len(rows) * PITCH + 10
    s.rect(HX, y0, 46, h, fill='#f4f4f4', stroke='#555', rx=3)
    s.text(HX + 23, y0 - 10, ref, size=15, bold=True, anchor='middle')
    out = {}
    for k, (n, lab, net) in enumerate(rows):
        y = y0 + 5 + PITCH * k + PITCH / 2
        s.rect(HX + 13, y - 9, 20, 18, fill='#c9a227' if n == 1 else '#e6c35c', stroke='#7a5d00', sw=1.5,
               rx=1 if n == 1 else 9)
        s.text(HX - 8, y + 5, lab, size=14, bold=True, anchor='end', color=net_color(net, k) if net.startswith(('+', 'G')) else '#111')
        s.text(HX + 54, y - 6, str(n), size=11, color='#777')
        out[n] = (HX + 46, y, net)
    return out


def block_title(s, y, ref, what):
    title = TERM[ref][0]
    s.text(20, y, f'{ref} «{title}» — {what}', size=17, bold=True)


def module(s, x, y, w, h, title, sub=''):
    s.rect(x, y, w, h, fill='#fdf6e8', stroke='#8a6d3b')
    s.text(x + w / 2, y + 24, title, size=15, bold=True, anchor='middle')
    for i, t in enumerate(sub.split('\n') if sub else []):
        s.text(x + w / 2, y + 44 + i * 17, t, size=12, anchor='middle', color='#555')


def term(s, x, y, label, side='left'):
    """Клемма модуля на его левом краю."""
    s.circle(x, y, 6, fill='#fff', stroke='#8a6d3b')
    s.text(x + 14, y + 5, label, size=14, bold=True)


def wire(s, x1, y1, x2, y2, color, label=''):
    mx = (x1 + x2) / 2
    if abs(y1 - y2) < 1:
        s.line(x1, y1, x2, y2, color, 4)
    else:
        s.path(f'M{x1:.1f} {y1:.1f} H{mx - 30:.1f} C{mx:.1f} {y1:.1f} {mx:.1f} {y2:.1f} {mx + 30:.1f} {y2:.1f} '
               f'H{x2:.1f}', color, 4)
    if label:
        s.text(x1 + 24, y1 - 7, label, size=12, color='#444', italic=True)


def notes(s, x, y, lines, size=13):
    for i, t in enumerate(lines):
        s.text(x, y + i * (size + 6), t, size=size, color='#333')
    return y + len(lines) * (size + 6)


# ── Виды блоков ─────────────────────────────────────────────────────────────────────────────────────────
MX = 640                    # левый край модуля


def direct(s, y0, ref, what, mtitle, msub, conns, extra=(), mw=330):
    """Провод на провод: conns — [(вывод шилда, клемма модуля, подпись провода)] в порядке клемм модуля."""
    block_title(s, y0, ref, what)
    pins = shield_conn(s, ref, y0 + 34)
    rows = len(conns) + len(extra)
    top = y0 + 34
    mh = max(rows * PITCH + 70, len(pins) * PITCH + 10)
    module(s, MX, top, mw, mh, mtitle, msub)
    ty0 = top + 70
    for k, (pin, tlabel, wl) in enumerate(conns):
        x1, y1, net = pins[pin]
        ty = ty0 + k * PITCH
        wire(s, x1, y1, MX, ty, net_color(net, pin), wl)
        term(s, MX, ty, tlabel)
    for k, t in enumerate(extra):
        ty = ty0 + (len(conns) + k) * PITCH
        s.circle(MX, ty, 6, fill='#eee', stroke='#aaa')
        s.text(MX + 14, ty + 5, t, size=13, color='#888')
    return top + max(mh, len(pins) * PITCH + 10) + 26


def switch_sym(s, x, y, kind='btn'):
    """Контакт: два кружка и рычаг (кнопка — с толкателем)."""
    s.circle(x, y, 4, fill='#fff'); s.circle(x + 50, y, 4, fill='#fff')
    s.line(x + 4, y, x + 46, y - 14, '#222', 2.5)
    if kind == 'btn':
        s.line(x + 25, y - 8, x + 25, y - 22, '#222', 2); s.line(x + 17, y - 22, x + 33, y - 22, '#222', 2.5)


def switches(s, y0, ref, what, items, kind='btn', note_lines=(), pins_override=None, gnd_pins=None, rail_label='общий'):
    """Входы на землю: items — [(вывод, подпись контакта)]; общий провод контактов — на GND разъёма."""
    block_title(s, y0, ref, what)
    pins = shield_conn(s, ref, y0 + 34, pins_override)
    SX, RX = 470, 560
    ys = []
    for k, (pin, lab) in enumerate(items):
        x1, y1, net = pins[pin]
        s.line(x1, y1, SX, y1, PALETTE[k % len(PALETTE)], 4)
        switch_sym(s, SX, y1, kind)
        s.line(SX + 54, y1, RX, y1, C_GND, 3)
        s.circle(RX, y1, 3.5, fill=C_GND, stroke=C_GND)
        s.text(RX + 14, y1 + 5, lab, size=14)
        ys.append(y1)
    gp = gnd_pins or [n for n, (_, _, net) in pins.items() if net == 'GND']
    gys = [pins[n][1] for n in gp]
    s.line(RX, min(ys), RX, max(gys), C_GND, 3)
    for gy in gys:
        s.line(pins[gp[0]][0], gy, RX, gy, C_GND, 4)
        s.circle(RX, gy, 3.5, fill=C_GND, stroke=C_GND)
    s.text(RX + 14, max(gys) + 5, rail_label, size=13, color='#555', italic=True)
    bottom = y0 + 34 + len(pins) * PITCH + 10
    if note_lines:
        bottom = max(bottom, notes(s, 700, y0 + 60, note_lines))
    return bottom + 26


# ── Схемы по группам ───────────────────────────────────────────────────────────────────────────────────
def p_power():
    s = Svg(1000, 640)
    y = direct(s, 34, 'J3', 'блок питания 5 В', 'Блок питания 5 В', 'от 2 А; с дисплеем 10.1" — 3 А\n(Mean Well RS-15-5, LRS-35-5…)',
               [(1, '+V', 'красный 0.75 мм²'), (2, '−V (COM)', 'чёрный 0.75 мм²')], extra=('L, N, ⏚ — сеть 220 В',), mw=320)
    notes(s, 20, y, [
        '• Блок питает всё: Discovery (через диод D1 на шилде), дисплей, LCD, энкодеры, линейки, зуммер.',
        '• Драйверы моторов питаются от своего блока 24–48 В. К шилду он не подключается: «минус» этого блока',
        '  с GND шилда не соединяйте — входы драйверов PUL/DIR/ENA развязаны оптронами.',
        '• Подключение 220 В — только при выключенной вилке; клемма ⏚ блока — на корпус станка.',
        '• Без блока, только по USB ST-LINK, питается одна Discovery: дисплей и пульт работать не будут.',
        '• Перед первым включением: мультиметром +5V–GND на J3 — нет короткого; после — 4.9…5.2 В.',
    ])
    s.save('power.svg')


def p_encoder():
    s = Svg(1000, 560)
    y = direct(s, 34, 'J4', 'энкодер шпинделя', 'Энкодер шпинделя', 'Omron E6B2-CWZ6C, Autonics E40S…\nNPN «открытый коллектор», 5–24 В',
               [(1, 'коричневый — +V', ''), (2, 'синий — 0 V', ''), (3, 'чёрный — A', ''), (4, 'белый — B', '')],
               extra=('оранжевый — Z: не подключать, заизолировать', 'экран кабеля — к GND шилда'))
    notes(s, 20, y, [
        '• Цвета — у Omron E6B2-CWZ6C; у других энкодеров смотрите паспорт: «+», «0 V», A, B.',
        '• Нужен выход NPN (открытый коллектор) или «push-pull» 5 В. Выход «line driver» (A, A−, B, B−) — через',
        '  приёмник RS-422 (AM26LS32).',
        '• Линий на оборот — в «НАСТРОЙКИ» → «МЕХАНИКА» → «Энкодер» (таблицы автора — для 1800). Энкодер вращается',
        '  вместе со шпинделем 1:1, без проскальзывания (муфта или зубчатый ремень).',
        '• Экранированный кабель, отдельно от проводов моторов. Подтяжки 1 кОм и конденсаторы 1 нФ — на шилде.',
        '• Не крутится в ту сторону (обороты «с минусом»)? Поменяйте местами провода A и B.',
    ])
    s.save('encoder.svg')


def p_drivers():
    s = Svg(1000, 1010)
    conns = [(1, 'PUL+', ''), (2, 'PUL−', ''), (3, 'DIR+', ''), (4, 'DIR−', ''), (5, 'ENA+', ''), (6, 'ENA−', '')]
    extra = ('A+ A− B+ B− — обмотки мотора', 'VCC, GND — блок 24–48 В')
    y = direct(s, 34, 'J5', 'драйвер мотора Z (каретка, продольная)', 'Драйвер мотора Z',
               'DM542, DM556, TB6600…', conns, extra)
    y = direct(s, y + 10, 'J6', 'драйвер мотора X (суппорт, поперечная)', 'Драйвер мотора X',
               'DM542, DM556, TB6600…', conns, extra)
    notes(s, 20, y, [
        '• Провода — один в один: PUL+ к PUL+, PUL− к PUL− и т. д. Витая пара или плоский кабель, до 2 м.',
        '• На «+» шилд даёт +5 В, «−» замыкает транзистор на шилде (как у автора 7e2). В прошивке DRIVER_SIGNALS_DIRECT 0.',
        '• Переключатели драйвера: микрошаг — как в «МЕХАНИКА» → «Микрошаг» (по умолчанию 800 шагов/об = ×4),',
        '  ток — по мотору (номинал из паспорта мотора), «половинный ток в покое» — включить.',
        '• Обмотки мотора: A+ A− — одна обмотка (у 4-проводного: прозвонить, между проводами одной обмотки — 1–3 Ом).',
        '  Ось едет в обратную сторону — поменяйте местами A+ и A−.',
        '• Блок 24–48 В — только для драйверов: «минус» блока с GND шилда не соединяйте.',
    ])
    s.save('drivers.svg')


def p_modes():
    s = Svg(1000, 1080)
    title, nets, labels = TERM['J7']
    idc = [(i + 1, labels[i], nets[i]) for i in range(9)] + [(10, 'GND', 'GND')]
    names = ['M1 синхронная подача', 'M2 асинхронная подача', 'M3 резьба', 'M4 конус <', 'M5 конус >', 'M6 шар',
             'M7 делилка', 'M8 G-код']
    y = switches(s, 34, 'J7', 'галетный переключатель режимов (8 положений)', [(i + 1, names[i]) for i in range(8)],
                 kind='rot', pins_override=idc, gnd_pins=[9, 10], rail_label='общий вывод галетника',
                 note_lines=())
    # вид на вилку IDC сверху — как на плате (J7 у нижнего края): нечётные выводы — нижний ряд, к краю платы
    bx, by = 730, 175
    s.text(bx + 125, by - 14, 'J7 на плате, вид сверху', size=13, bold=True, anchor='middle')
    s.rect(bx, by, 250, 104, fill='#333', stroke='#111', rx=4)
    s.rect(bx + 100, by + 98, 50, 8, fill='#fff', stroke='#111', sw=1.5, rx=1)          # ключ
    for n in range(1, 11):
        col, row = (n - 1) // 2, 1 - (n - 1) % 2
        px, py = bx + 33 + col * 46, by + 30 + row * 44
        s.rect(px - 9, py - 9, 18, 18, fill='#c9a227' if n == 1 else '#e6c35c', stroke='#7a5d00', sw=1.5,
               rx=1 if n == 1 else 9)
        s.text(px + 13, py + (-9 if row == 0 else 21), str(n), size=12, bold=True, color='#fff')
    s.text(bx + 125, by + 128, 'край платы ↓   ключ — к краю платы', size=12, anchor='middle', color='#555')
    s.text(bx + 125, by + 146, 'красная жила шлейфа — вывод 1', size=12, anchor='middle', color='#555')
    y = notes(s, 20, y, [
        '• Галетный переключатель 1 направление × 8 (или 12) положений. Общий вывод — на GND (выводы 9 и 10 J7).',
        '• Положение N галетника — на вывод N разъёма: тогда цифра на ручке совпадает с режимом на экране.',
        '• Удобно: 10-жильный плоский шлейф с розеткой IDC-10 обжимом; на галетнике — каждую жилу на свой лепесток.',
        '• Галетника нет — режим выбирается на экране (тап по названию режима), J7 можно не подключать.',
    ])
    y = switches(s, y + 30, 'J8', 'переключатель подрежимов (3 положения)',
                 [(1, 'Внутренний'), (2, 'Ручной'), (3, 'Наружный')], kind='rot', rail_label='общий вывод')
    notes(s, 20, y, [
        '• Переключатель на 3 положения, в каждом замкнут один вывод (галетный 1×3 или тумблер ON-ON-ON).',
        '  Тумблер ON-OFF-ON не подойдёт: в среднем положении ничего не замкнуто.',
        '• Упоры кнопками ставятся только в «Ручном»; автоцикл — в «Наружном» и «Внутреннем».',
    ])
    s.save('modes.svg')


def p_buttons():
    s = Svg(1000, 820)
    y = switches(s, 34, 'J9', 'кнопки меню', [(1, 'Влево'), (2, 'Вправо'), (3, 'Вверх'), (4, 'Вниз'), (5, 'Выбор')],
                 note_lines=['Кнопки без фиксации, нормально', 'разомкнутые. Дублируют кнопки', 'на экране.'])
    y = switches(s, y + 10, 'J10', 'джойстик и кнопка ускоренного хода',
                 [(1, 'Влево (к патрону)'), (2, 'Вправо'), (3, 'Вверх — на себя'), (4, 'Вниз — от себя'),
                  (5, 'Ускоренный ход')],
                 note_lines=['Джойстик на 4 положения с', 'самовозвратом (4 микровыключателя),',
                             'кнопка ускоренного хода — на', 'ручке или рядом.'])
    notes(s, 20, y, [
        '• Один вывод каждого контакта — на свой вывод разъёма, вторые выводы всех контактов — вместе на GND.',
        '• Подтяжки 1 кОм к 3.3 В — на шилде; внешних резисторов не нужно. Провода до 1.5 м, лучше витые с GND.',
        '• Направление джойстика неверное? Поменяйте местами провода «Влево» и «Вправо» (или «Вверх» и «Вниз»).',
    ])
    s.save('buttons.svg')


def p_limits():
    s = Svg(1000, 860)
    y = switches(s, 34, 'J11', 'кнопки упоров', [(1, 'Левый'), (2, 'Правый'), (3, 'Передний'), (4, 'Задний')],
                 note_lines=['Кнопки без фиксации — ставят и', 'снимают упор в текущей точке', '(в подрежиме «Ручной»).'])
    # светодиоды: +3V → анод, катод → вывод разъёма (220 Ом на шилде)
    ref = 'J12'
    block_title(s, y + 10, ref, 'светодиоды упоров')
    pins = shield_conn(s, ref, y + 44)
    RX, LX = 560, 470
    x3, y3, _ = pins[1]
    s.line(x3, y3, RX, y3, C_3V, 4)
    names = ['Левый', 'Правый', 'Передний', 'Задний']
    for k, n in enumerate((2, 3, 4, 5)):
        x1, y1, _ = pins[n]
        s.line(x1, y1, LX, y1, PALETTE[k], 4)
        s.path(f'M{LX} {y1} L{LX + 30} {y1 - 13} L{LX + 30} {y1 + 13} Z', '#222', 2, '#ffd34d')   # треугольник
        s.line(LX, y1 - 13, LX, y1 + 13, '#222', 2.5)                                               # катод
        s.line(LX + 30, y1, RX, y1, C_3V, 3)
        s.circle(RX, y1, 3.5, fill=C_3V, stroke=C_3V)
        s.text(RX + 14, y1 + 5, f'{names[k]}: катод (−, короткая ножка) — к разъёму', size=13)
    s.line(RX, y3, RX, pins[5][1], C_3V, 3)
    s.text(RX + 14, y3 + 5, 'аноды (+, длинная ножка) — вместе на +3V', size=13, color='#555', italic=True)
    y = y + 44 + 5 * PITCH + 36
    notes(s, 20, y, [
        '• Светодиод горит, когда упор поставлен. Резисторы 220 Ом — на шилде, свои не нужны.',
        '• Только красные, жёлтые или зелёные (от 3.3 В). Синие и белые светить не будут.',
        '• Кнопки «со светодиодом» (16 мм) подходят, если светодиод в них без резистора на 12/24 В — иначе не загорится.',
    ])
    s.save('limits.svg')


def p_mpg():
    s = Svg(1000, 860)
    y = direct(s, 34, 'J13', 'маховичок РГИ (ручной генератор импульсов)', 'Маховичок (MPG)',
               '100 имп/об, питание 5 В', [(1, '+5V (VCC)', ''), (2, '0V (GND)', ''), (3, 'A', ''), (4, 'B', '')],
               extra=('A−, B− — не подключать',))
    y = switches(s, y + 10, 'J14', 'переключатели РГИ: ось и масштаб',
                 [(1, 'ось Z'), (2, 'ось X'), (3, '×1'), (4, '×10')], kind='rot',
                 note_lines=['Ось: переключатель Z — выкл — X.', 'Масштаб: ×1 — 0.01 мм на щелчок,',
                             '×10 — 0.1 мм.', 'У готового пульта с маховичком', 'провода подписаны так же,',
                             'общий — COM.'])
    notes(s, 20, y, [
        '• Маховичок двигает ось, выбранную переключателем; в среднем положении («выкл») — не двигает ничего.',
        '• Конденсаторы 10 нФ на A и B — на шилде.',
        '• Крутится «наоборот» — поменяйте местами A и B.',
        '• Без РГИ J13 и J14 можно не подключать.',
    ])
    s.save('mpg.svg')


def p_outputs():
    s = Svg(1000, 900)
    y = direct(s, 34, 'J15', 'зуммер', 'Активный зуммер 5 В', 'со встроенным генератором',
               [(1, '«+» (длинная ножка)', ''), (2, '«−»', '')])
    y = direct(s, y, 'J16', 'тахометр (необязательно)', 'Тахометр / счётчик', 'вход импульсов',
               [(1, 'питание +5 В', ''), (2, 'GND', ''), (3, 'вход импульса', '')])
    y = direct(s, y, 'J17', 'переменный резистор подачи (необязательно)', 'Переменный резистор', '10 кОм, линейный (B10K)',
               [(1, 'крайний вывод', ''), (2, 'средний (движок)', ''), (3, 'другой крайний', '')])
    notes(s, 20, y, [
        '• Зуммер — активный (пищит от постоянного напряжения). Пассивный пищать не будет.',
        '• Тахометр: импульс 3.3 В на каждый оборот шпинделя (через 100 Ом на шилде).',
        '• Переменник: только к +3V шилда, не к 5 В. Подача растёт не в ту сторону — поменяйте крайние выводы.',
        '  Без переменника в прошивке USE_FEED_POT 0 — подача задаётся с экрана и кнопками.',
    ])
    s.save('outputs.svg')


def p_links():
    s = Svg(1000, 1380)
    y = direct(s, 34, 'J19', 'сенсорный дисплей 10.1" (разъём CN2 «INPUT»)', 'Дисплей JC8012P4A1C',
               'разъём CN2: MX 1.25 мм, 4 конт.', [(2, '1 — VIN (5 В)', ''), (1, '4 — GND', ''),
                                                   (3, '3 — RXD (приём дисплея)', 'TX → RX'),
                                                   (4, '2 — TXD (передача дисплея)', 'RX ← TX')])
    y = notes(s, 20, y, [
        '• TX и RX перекрещиваются: передача шилда — на приём дисплея и наоборот. Номера CN2 — по схеме дисплея.',
        '• Дисплей 10.1" берёт до 1.5 А и питается по VIN от +5V шилда (или от своего USB-C — тогда VIN не подключать).',
        '• Дисплей 4.3" JC4827W543 — разъём P1 по подписям на его плате: TX шилда → RX (GPIO44), RX шилда ← TX (GPIO43),',
        '  GND; питание — от его USB-C или «5V» разъёма P1, если он есть на вашей ревизии.',
    ]) + 20
    y = direct(s, y, 'J18', 'LCD2004 с I2C-модулем (запасной экран, необязательно)', 'LCD2004 + I2C (PCF8574)',
               'адрес 0x27 или 0x3F', [(1, 'GND', ''), (2, 'VCC', ''), (3, 'SDA', ''), (4, 'SCL', '')])
    y = direct(s, y, 'J20', 'блок DRO HS800-2 (если координаты от него)', 'Блок DRO HS800-2', 'выход TTL',
               [(1, 'GND', ''), (2, 'TX блока', 'RX ← TX')])
    y = direct(s, y, 'J21', 'USB-UART для отладки (необязательно)', 'USB-UART (CP2102, CH340)', '3.3 В',
               [(1, 'GND', ''), (2, 'RXD адаптера', 'TX → RX'), (3, 'TXD адаптера', 'RX ← TX')])
    notes(s, 20, y, [
        '• LCD: контраст — винтом на I2C-модуле. Подтяжки к 5 В на модуле лучше снять (на Discovery есть свои к 3 В).',
        '• Блок DRO: выход должен быть TTL. RS-232 (±12 В) — только через преобразователь MAX3232.',
        '• Отладка: на Discovery вывод TX (PA9) занят USB — передача от STM32 не работает, приём работает. Для станка не нужен.',
    ])
    s.save('links.svg')


def p_scales():
    s = Svg(1000, 840)
    for i, (ref, ax) in enumerate((('J22', 'Z (продольная)'), ('J23', 'X (поперечная)'))):
        y0 = 34 if not i else y
        y = direct(s, y0, ref, f'линейка оси {ax} — напрямую, без блока DRO', 'Линейка (через сопряжение)',
                   'тип — в «НАСТРОЙКИ» → «ЛИНЕЙКИ»',
                   [(1, 'питание +5 В', ''), (2, 'GND', ''), (3, 'A / такт (CLK)', ''), (4, 'B / данные (DATA)', ''),
                    (5, 'REQ (только Digimatic)', '')])
    notes(s, 20, y, [
        '• Резисторы 470 Ом последовательно — на шилде (защита выводов STM32).',
        '• Какая у вас линейка и нужна ли схема сопряжения (ёмкостные 1.5 В, RS-422, Digimatic) — docs/scales.',
        '• Линейки к блоку DRO — тогда J22/J23 не нужны, координаты идут через J20.',
    ])
    s.save('scales.svg')


# ── Карта шилда: где какой разъём ──────────────────────────────────────────────────────────────────────
def footprints():
    t = open(PCB, encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'\(footprint "([^"]+)"(.*?)\n\t\)', t, re.S):
        body = m.group(2)
        ref = re.search(r'\(property "Reference" "([^"]+)"', body).group(1)
        x, y, *r = re.search(r'\(at ([\d.\-]+) ([\d.\-]+)(?: ([\d.\-]+))?\)', body).groups()
        a = math.radians(float(r[0]) if r and r[0] else 0)
        pads = {}
        for n, px, py in re.findall(r'\(pad "(\d+)" \w+ \w+\s*\(at ([\d.\-]+) ([\d.\-]+)', body):
            px, py = float(px), float(py)
            pads[int(n)] = (float(x) + px * math.cos(a) + py * math.sin(a), float(y) - px * math.sin(a) + py * math.cos(a))
        out[ref] = pads
    return out


GROUP_COL = {'J3': C_5V, 'J4': '#d62828', 'J5': '#f77f00', 'J6': '#f77f00', 'J7': '#2a9d8f', 'J8': '#2a9d8f',
             'J9': '#1d70b8', 'J10': '#3a86ff', 'J11': '#6a994e', 'J12': '#6a994e', 'J13': '#7b2cbf', 'J14': '#7b2cbf',
             'J15': '#9c6644', 'J16': '#9c6644', 'J17': '#9c6644', 'J18': '#444', 'J19': '#444', 'J20': '#444',
             'J21': '#444', 'J22': '#e76f51', 'J23': '#e76f51'}


def p_map():
    fp = footprints()
    K, OX, OY = 6.0, 250, 70               # мм → px; плата 96×126 мм
    W, H = 96, 126
    s = Svg(1100, 70 + int(H * K) + 60, auto=False)
    s.text(550, 32, 'Шилд: где какой разъём (вид сверху, Discovery снята)', size=19, bold=True, anchor='middle')
    X = lambda v: OX + v * K
    Y = lambda v: OY + v * K
    s.rect(X(0), Y(0), W * K, H * K, fill='#1f5f3a', stroke='#0d2b1a', sw=3, rx=10)
    for hx, hy in ((3.5, 3.5), (92.5, 3.5), (3.5, 122.5), (92.5, 122.5)):
        s.circle(X(hx), Y(hy), 9.6, fill='#fff', stroke='#c9a227', sw=3)
    # гнёзда Discovery
    for ref, lab in (('J1', 'P1'), ('J2', 'P2')):
        ps = fp[ref]
        xs = [p[0] for p in ps.values()]; ys = [p[1] for p in ps.values()]
        s.rect(X(min(xs)) - 10, Y(min(ys)) - 10, (max(xs) - min(xs)) * K + 20, (max(ys) - min(ys)) * K + 20,
               fill='#111', stroke='#000', sw=1, rx=2)
        for n, (px, py) in ps.items():
            s.add(f'<circle cx="{X(px):.1f}" cy="{Y(py):.1f}" r="3.4" fill="{"#ffd34d" if n == 1 else "#888"}"/>')
        s.text(X((min(xs) + max(xs)) / 2), Y(max(ys)) + 30, f'{ref} — гнездо под {lab}', size=13, bold=True,
               anchor='middle', color='#fff')
    j1, j2 = fp['J1'], fp['J2']
    cx = X((j1[1][0] + j2[2][0]) / 2)
    s.text(cx, Y(36), 'Discovery — сюда, лицом вверх,', size=14, anchor='middle', color='#d8f0e0')
    s.text(cx, Y(36) + 18, 'USB ST-LINK — к этому (верхнему) краю', size=14, anchor='middle', color='#d8f0e0')
    s.text(cx, Y(36) + 40, '● жёлтый — вывод 1', size=13, anchor='middle', color='#ffd34d')
    # разъёмы
    for ref, (title, nets, labels) in TERM.items():
        ps = fp[ref]
        xs = [p[0] for p in ps.values()]; ys = [p[1] for p in ps.values()]
        col = GROUP_COL.get(ref, '#444')
        s.rect(X(min(xs)) - 9, Y(min(ys)) - 9, (max(xs) - min(xs)) * K + 18, (max(ys) - min(ys)) * K + 18,
               fill='#f4f4f4', stroke=col, sw=3, rx=3)
        for n, (px, py) in ps.items():
            s.add(f'<rect x="{X(px) - 4:.1f}" y="{Y(py) - 4:.1f}" width="8" height="8" rx="{1 if n == 1 else 4}" '
                  f'fill="{"#c9a227" if n == 1 else "#999"}"/>')
        mx, my = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        short = title
        if min(xs) < 10:                                     # левый край — подпись слева
            s.text(X(0) - 18, Y(my) - 4, ref, size=15, bold=True, anchor='end', color=col)
            s.text(X(0) - 18, Y(my) + 14, short, size=12, anchor='end', color='#333')
        elif min(xs) > 85:                                   # правый край
            s.text(X(W) + 18, Y(my) - 4, ref, size=15, bold=True, color=col)
            s.text(X(W) + 18, Y(my) + 14, short, size=12, color='#333')
        else:                                                # нижний край — подпись под платой, в два ряда
            k = sorted(r for r in TERM if 115 < min(p[1] for p in fp[r].values())).index(ref)
            ty = Y(H) + 22 + (k % 2) * 30
            s.line(X(mx), Y(max(ys)) + 9, X(mx), ty - 14, col, 1.5)
            s.text(X(mx), ty, ref, size=14, bold=True, anchor='middle', color=col)
            s.text(X(mx), ty + 14, short, size=11, anchor='middle', color='#333')
    s.h = int(Y(H) + 90)
    s.text(550, s.h - 8, 'Квадратная площадка — вывод 1 (подписи выводов — на шелкографии платы).', size=13,
           anchor='middle', color='#555')
    s.save('shield_map.svg')


def main():
    p_map(); p_power(); p_encoder(); p_drivers(); p_modes(); p_buttons(); p_limits(); p_mpg(); p_outputs()
    p_links(); p_scales()


if __name__ == '__main__':
    main()
