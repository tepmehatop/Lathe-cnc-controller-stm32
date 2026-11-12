#!/usr/bin/env python3
"""
Наглядная схема подключения → docs/wiring.html (одна страница, открывается без интернета).

  python3 tools/docs_gen/wiring_html.py

Вкладки:
  * «Полная схема» — плата STM32F4DISCOVERY в центре со всеми 100 выводами разъёмов P1/P2, модули по бокам
    (как выглядят, их клеммы), провод от каждой ноги к клемме модуля, детали шилда на линии (подтяжки,
    конденсаторы, ключи). Наведение на вывод, провод или клемму — подсветка всей цепи и подсказка.
  * «Шилд» — плата шилда с разъёмами: наведение на вывод разъёма — куда он идёт на Discovery и к модулю.
  * «Таблица» — все сигналы с поиском.

Данные — из Core/e2/e2_pins.h и Core/els_config.h (выводы STM32), hardware/shield/design.py (разъёмы и детали
шилда) и els_shield.kicad_pcb (где стоят разъёмы) — страница всегда совпадает с прошивкой и платой.
"""
import html, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wiring_svg as W                          # noqa: E402  выводы STM32, P1/P2, названия
import shield_docs as SD                        # noqa: E402  разъёмы шилда и их места на плате
import design as D                              # noqa: E402  (путь добавил shield_docs)

ROOT = W.ROOT
OUT = os.path.join(ROOT, 'docs', 'wiring.html')

GCOL = {'enc': '#ff6b6b', 'drv': '#ffa94d', 'hand': '#c084fc', 'mode': '#2dd4bf', 'btn': '#60a5fa',
        'joy': '#38bdf8', 'lim': '#a3e635', 'out': '#e0b07a', 'link': '#e5e7eb', 'scale': '#fb923c'}
GNAME = {g: n for g, _, n in W.GROUPS}
HALO = ' stroke="#0b0f17" stroke-width="3.5" paint-order="stroke"'

# ── Модули: сигнал → клемма модуля ─────────────────────────────────────────────────────────────────────
# (id, название, модель/подпись, значок, [(сигнал, клемма)], [строки про питание и общий провод])
MODULES = [
    ('enc', 'Энкодер шпинделя', 'Omron E6B2-CWZ6C, 1800 линий', 'enc',
     [('ENC_A', 'A — чёрный'), ('ENC_B', 'B — белый')], ['+V коричн. → +5V (J4-1)', '0V синий → GND (J4-2)']),
    ('drvz', 'Драйвер мотора Z', 'каретка; DM542 / TB6600', 'drv',
     [('Z_STEP', 'PUL−'), ('Z_DIR', 'DIR−'), ('Z_ENA', 'ENA−')], ['PUL+ DIR+ ENA+ → +5V шилда (J5)']),
    ('drvx', 'Драйвер мотора X', 'суппорт; DM542 / TB6600', 'drv',
     [('X_STEP', 'PUL−'), ('X_DIR', 'DIR−'), ('X_ENA', 'ENA−')], ['PUL+ DIR+ ENA+ → +5V шилда (J6)']),
    ('mpg', 'Маховичок РГИ', 'MPG 100 имп/об, 5 В', 'mpg',
     [('HAND_A', 'A'), ('HAND_B', 'B')], ['+5V, 0V → J13-1, J13-2']),
    ('mpgsw', 'Переключатели РГИ', 'ось Z — выкл — X, масштаб ×1/×10', 'tog',
     [('HAND_AXIS_Z', 'ось Z'), ('HAND_AXIS_X', 'ось X'), ('HAND_X1', '×1'), ('HAND_X10', '×10')],
     ['общий → GND (J14-5)']),
    ('mode', 'Галетник режимов', '1 направление × 8 положений', 'rot',
     [('MODE_FEED', 'M1 синхр. подача'), ('MODE_AFEED', 'M2 асинхр. подача'), ('MODE_THREAD', 'M3 резьба'),
      ('MODE_CONE_L', 'M4 конус <'), ('MODE_CONE_R', 'M5 конус >'), ('MODE_SPHERE', 'M6 шар'),
      ('MODE_DIVIDER', 'M7 делилка'), ('MODE_RESERVE', 'M8 G-код')], ['общий → GND (J7-9, J7-10)']),
    ('sub', 'Переключатель подрежимов', '3 положения', 'rot',
     [('SUB_INT', 'Внутренний'), ('SUB_MAN', 'Ручной'), ('SUB_EXT', 'Наружный')], ['общий → GND (J8-4)']),
    ('btn', 'Кнопки меню', 'без фиксации, NO', 'btn',
     [('BTN_LEFT', 'Влево'), ('BTN_RIGHT', 'Вправо'), ('BTN_UP', 'Вверх'), ('BTN_DOWN', 'Вниз'),
      ('BTN_SELECT', 'Выбор')], ['вторые выводы → GND (J9-6)']),
    ('joy', 'Джойстик', '4 положения с самовозвратом', 'joy',
     [('JOY_LEFT', 'Влево (к патрону)'), ('JOY_RIGHT', 'Вправо'), ('JOY_UP', 'Вверх — на себя'),
      ('JOY_DOWN', 'Вниз — от себя'), ('JOY_RAPID', 'Ускоренный ход')], ['общий → GND (J10-6)']),
    ('lim', 'Кнопки упоров', 'без фиксации', 'btn',
     [('LIM_LEFT', 'Левый'), ('LIM_RIGHT', 'Правый'), ('LIM_FRONT', 'Передний'), ('LIM_REAR', 'Задний')],
     ['вторые выводы → GND (J11-5)']),
    ('led', 'Светодиоды упоров', 'красные / жёлтые / зелёные', 'led',
     [('LED_LEFT', 'Левый — катод'), ('LED_RIGHT', 'Правый — катод'), ('LED_FRONT', 'Передний — катод'),
      ('LED_REAR', 'Задний — катод')], ['аноды → +3V (J12-1)']),
    ('beep', 'Зуммер', 'активный 5 В', 'buz', [('BEEPER', '«−»')], ['«+» → +5V (J15-1)']),
    ('tacho', 'Тахометр', 'по желанию', 'tach', [('TACHO', 'вход импульса')], ['+5V, GND → J16-1, J16-2']),
    ('pot', 'Переменник подачи', '10 кОм, по желанию', 'pot', [('FEED_POT', 'средний вывод')],
     ['крайние → +3V и GND (J17)']),
    ('disp', 'Сенсорный дисплей', '10.1" JC8012P4A1C, разъём CN2', 'disp',
     [('DISP_TX', 'RXD (CN2-3)'), ('DISP_RX', 'TXD (CN2-2)')], ['VIN CN2-1 ← +5V (J19-2)', 'GND CN2-4 (J19-1)']),
    ('lcd', 'LCD2004 + I2C', 'запасной экран, по желанию', 'lcd',
     [('LCD_SCL', 'SCL'), ('LCD_SDA', 'SDA')], ['VCC → +5V, GND (J18)']),
    ('dro', 'Блок DRO HS800-2', 'выход TTL', 'dro', [('DRO_RX', 'TX блока')], ['GND (J20-1)']),
    ('dbg', 'USB-UART', 'отладка, по желанию', 'uart', [('DBG_TX', 'RXD адаптера'), ('DBG_RX', 'TXD адаптера')],
     ['GND (J21-1)']),
    ('scz', 'Линейка Z', 'напрямую, без блока DRO', 'scale',
     [('SCALE_Z_A', 'A / CLK'), ('SCALE_Z_B', 'B / DATA'), ('SCALE_Z_REQ', 'REQ (Digimatic)')],
     ['+5V, GND → J22-1, J22-2']),
    ('scx', 'Линейка X', 'напрямую, без блока DRO', 'scale',
     [('SCALE_X_A', 'A / CLK'), ('SCALE_X_B', 'B / DATA'), ('SCALE_X_REQ', 'REQ (Digimatic)')],
     ['+5V, GND → J23-1, J23-2']),
]
SIG_MOD = {s: (m[0], t) for m in MODULES for s, t in m[4]}
MOD = {m[0]: m for m in MODULES}

# Выводы Discovery с назначением на самой плате (UM1472) — в подсказке свободных выводов
ONBOARD = {'PA0': 'кнопка USER', 'PD12': 'светодиод LD4', 'PD13': 'светодиод LD3', 'PD14': 'светодиод LD5',
           'PD15': 'светодиод LD6', 'PA13': 'SWD (ST-LINK)', 'PA14': 'SWD (ST-LINK)', 'PA5': 'акселерометр SPI',
           'PA6': 'акселерометр SPI', 'PA7': 'акселерометр SPI', 'PE0': 'акселерометр INT', 'PE1': 'акселерометр INT',
           'PA11': 'USB OTG', 'PA12': 'USB OTG', 'PC0': 'питание USB OTG', 'PD5': 'перегрузка USB OTG',
           'PA9': 'USB OTG VBUS (C49 4.7 мкФ)', 'PH0': 'кварц 8 МГц', 'PH1': 'кварц 8 МГц', 'PC14': 'кварц 32 кГц',
           'PC15': 'кварц 32 кГц', 'PC3': 'микрофон PDM', 'PB10': 'микрофон CLK (не мешает)', 'PC4': 'микрофон (R49 не установлен)'}
POWER = {'GND': 'земля', 'VDD': '3 В питания чипа', '5V': '5 В: вход питания платы (на шилде — через диод D1)',
         '3V': '3 В: подтяжки и светодиоды шилда (до 100 мА)', 'NRST': 'сброс', 'BOOT0': 'BOOT0', 'NC': 'не подключён'}


def comps(sig):
    """Детали шилда на линии сигнала: подтяжка, конденсатор, ключ, последовательный резистор."""
    out = []
    for p in D.PARTS:
        pins = p['pins']
        if p['sym'] == 'Device:R' and sig in (pins.get(1), pins.get(2)):
            other = pins[2] if pins[1] == sig else pins[1]
            if other == '+3V':
                out.append(f"{p['value']}↑3V")
            elif other.startswith('B_'):
                out.append('ключ NPN')
            else:
                out.append(f"{p['value']} Ом")
        elif p['sym'] == 'Device:C' and sig in pins.values():
            out.append(p['value'].replace('nF', ' нФ'))
    return out


def signals():
    sigs = {}
    for p in W.parse_pins() + W.scale_pins():
        name = p['name'].replace('_PIN', '')
        hdr = W.HEADER.get(p['pin'], '')
        mod = SIG_MOD.get(name)
        sigs[name] = {
            'n': name, 't': W.TITLES.get(p['name'], p.get('title', name)), 'pin': p['pin'], 'hdr': hdr,
            'sh': W.shield_pin(p['name']).replace('—', ''), 'g': p['group'], 'mega': p['mega'],
            'mod': MOD[mod[0]][1] if mod else '', 'term': mod[1] if mod else '', 'mid': mod[0] if mod else '',
            'c': ' · '.join(comps(name)),
            'note': {'DRO_TX': 'на шилде наружу не выводится', 'AUDIO_RESET': 'держит аудиочип Discovery в сбросе, наружу не выводится'}.get(name, ''),
        }
    return sigs


# ── SVG-кирпичики ──────────────────────────────────────────────────────────────────────────────────────
def e(t):
    return html.escape(str(t), quote=True)


def tx(x, y, t, size=13, color='#cbd5e1', anchor='start', bold=False, cls='', extra=''):
    b = ' font-weight="700"' if bold else ''
    c = f' class="{cls}"' if cls else ''
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{color}" text-anchor="{anchor}"{b}{c}{extra}>'
            f'{e(t)}</text>')


def icon(kind, x, y):
    """Значок модуля «как выглядит», 46×46, левый верхний угол (x, y)."""
    g = f'<g transform="translate({x:.1f},{y:.1f})">'
    k = {
        'enc': '<rect x="6" y="8" width="30" height="30" rx="15" fill="#9aa4b1"/><rect x="34" y="20" width="12" height="6" fill="#d1d5db"/>'
               '<circle cx="21" cy="23" r="6" fill="#4b5563"/><path d="M21 38 v8" stroke="#9aa4b1" stroke-width="4"/>',
        'drv': '<rect x="2" y="6" width="42" height="34" rx="3" fill="#1f2937" stroke="#9ca3af"/>' +
               ''.join(f'<rect x="{6 + i * 6}" y="2" width="3" height="8" fill="#9ca3af"/>' for i in range(6)) +
               '<rect x="8" y="30" width="30" height="6" fill="#16a34a"/><rect x="10" y="14" width="10" height="8" fill="#dc2626"/>',
        'mpg': '<circle cx="23" cy="23" r="20" fill="#d1d5db" stroke="#6b7280" stroke-width="2"/>' +
               ''.join(f'<line x1="{23 + 17 * __import__("math").cos(a / 10 * 6.283):.1f}" y1="{23 + 17 * __import__("math").sin(a / 10 * 6.283):.1f}" '
                       f'x2="{23 + 20 * __import__("math").cos(a / 10 * 6.283):.1f}" y2="{23 + 20 * __import__("math").sin(a / 10 * 6.283):.1f}" stroke="#374151" stroke-width="2"/>' for a in range(10)) +
               '<circle cx="23" cy="23" r="9" fill="#9ca3af"/><circle cx="31" cy="15" r="3" fill="#374151"/>',
        'tog': '<rect x="6" y="20" width="34" height="18" rx="3" fill="#374151"/><path d="M23 22 L33 4" stroke="#e5e7eb" stroke-width="5" stroke-linecap="round"/>',
        'rot': '<circle cx="23" cy="25" r="17" fill="#111827" stroke="#9ca3af" stroke-width="2"/>' +
               ''.join(f'<circle cx="{23 + 21 * __import__("math").cos((a / 8 - .25) * 6.283):.1f}" cy="{25 + 21 * __import__("math").sin((a / 8 - .25) * 6.283):.1f}" r="1.8" fill="#e5e7eb"/>' for a in range(8)) +
               '<path d="M23 25 L23 11" stroke="#fbbf24" stroke-width="4" stroke-linecap="round"/>',
        'btn': '<rect x="8" y="22" width="30" height="16" rx="3" fill="#374151"/><circle cx="23" cy="20" r="11" fill="#ef4444" stroke="#7f1d1d" stroke-width="2"/>',
        'joy': '<rect x="6" y="30" width="34" height="12" rx="3" fill="#374151"/><path d="M23 32 L23 10" stroke="#9ca3af" stroke-width="5"/><circle cx="23" cy="9" r="8" fill="#ef4444"/>',
        'led': '<path d="M13 40 V18 a10 10 0 0 1 20 0 V40 z" fill="#ef4444" opacity=".85"/><rect x="10" y="38" width="26" height="4" fill="#9ca3af"/>'
               '<path d="M18 42 v4 M28 42 v4" stroke="#9ca3af" stroke-width="2"/>',
        'buz': '<circle cx="23" cy="23" r="18" fill="#111827" stroke="#6b7280" stroke-width="2"/><circle cx="23" cy="23" r="4" fill="#374151"/>',
        'tach': '<rect x="2" y="10" width="42" height="26" rx="3" fill="#111827" stroke="#9ca3af"/>' + tx(23, 29, '1250', 13, '#ef4444', 'middle', True),
        'pot': '<circle cx="23" cy="26" r="15" fill="#374151"/><rect x="20" y="4" width="6" height="16" fill="#9ca3af"/><path d="M23 26 L32 17" stroke="#e5e7eb" stroke-width="3"/>',
        'disp': '<rect x="1" y="6" width="44" height="32" rx="3" fill="#0b1220" stroke="#9ca3af"/><rect x="5" y="10" width="36" height="24" fill="#0e7490"/>'
                '<rect x="8" y="13" width="14" height="7" fill="#22d3ee"/><rect x="24" y="13" width="14" height="7" fill="#22d3ee"/><rect x="8" y="23" width="30" height="7" fill="#155e75"/>',
        'lcd': '<rect x="1" y="10" width="44" height="26" rx="2" fill="#1e3a8a"/><rect x="4" y="13" width="38" height="20" fill="#60a5fa"/>' +
               tx(23, 22, 'Z 12.34', 8, '#1e3a8a', 'middle', True) + tx(23, 31, 'X  5.00', 8, '#1e3a8a', 'middle', True),
        'dro': '<rect x="1" y="6" width="44" height="34" rx="3" fill="#111827" stroke="#9ca3af"/>' + tx(23, 20, '012.34', 10, '#ef4444', 'middle', True) +
               tx(23, 33, '005.00', 10, '#ef4444', 'middle', True),
        'uart': '<rect x="2" y="14" width="34" height="18" rx="2" fill="#1d4ed8"/><rect x="34" y="17" width="10" height="12" fill="#d1d5db"/>'
                '<rect x="6" y="18" width="8" height="8" fill="#111827"/>',
        'scale': '<rect x="0" y="16" width="46" height="14" fill="#9ca3af"/>' +
                 ''.join(f'<line x1="{2 + i * 4}" y1="16" x2="{2 + i * 4}" y2="{21 if i % 5 else 24}" stroke="#111827"/>' for i in range(12)) +
                 '<rect x="14" y="8" width="18" height="26" rx="2" fill="#374151"/>',
    }[kind]
    return g + k + '</g>'


# ── Вкладка 1: полная схема ────────────────────────────────────────────────────────────────────────────
def schematic(sigs):
    PIN_TO_SIG = {s['pin']: k for k, s in sigs.items()}
    Wd = 2440
    BX0, BX1, BY0 = 900, 1540, 120                  # плата Discovery
    ROW0, PITCH = 300, 26
    col = {('P1', 1): 970, ('P1', 0): 996, ('P2', 1): 1444, ('P2', 0): 1470}   # (разъём, нечётный?) → x
    parts = []
    pins_el, wires, cards = [], [], []

    def pin_xy(hdr, n):
        return col[(hdr, n % 2)], ROW0 + (n - 1) // 2 * PITCH

    # провода: старт у вывода, сторона — по разъёму
    starts = []
    for k, s in sigs.items():
        if not s['mid'] or not s['hdr']:
            continue
        hdr, n = s['hdr'].split('-'); n = int(n)
        x, y = pin_xy(hdr, n)
        inner = (hdr == 'P1' and n % 2 == 0) or (hdr == 'P2' and n % 2 == 1)
        starts.append({'s': k, 'side': 'L' if hdr == 'P1' else 'R', 'x': x, 'y': y, 'yy': y + (13 if inner else 0)})

    BOARD_BOTTOM = ROW0 + 24 * PITCH + 90
    maxy = BOARD_BOTTOM
    for side in ('L', 'R'):
        ws = [w for w in starts if w['side'] == side]
        # части модулей на этой стороне, по среднему положению выводов
        groups = {}
        for w in ws:
            groups.setdefault(sigs[w['s']]['mid'], []).append(w)
        order = sorted(groups, key=lambda m: sum(w['yy'] for w in groups[m]) / len(groups[m]))
        CW = 330
        cx = 150 if side == 'L' else Wd - 150 - CW
        edge = cx + CW if side == 'L' else cx                                   # край карточки к плате
        y = 70
        for mid in order:
            m = MOD[mid]
            mine = groups[mid]
            terms = [(s_, t) for s_, t in m[4] if any(w['s'] == s_ for w in mine)]
            split = len(terms) < len(m[4])
            h = 62 + len(terms) * 26 + len(m[5]) * 16 + 18
            cards.append(f'<g class="card" data-m="{mid}">'
                         f'<rect x="{cx}" y="{y}" width="{CW}" height="{h}" rx="8" fill="#141c2b" stroke="#3b4a63" stroke-width="1.5"/>')
            ix = cx + CW - 58 if side == 'L' else cx + 10
            tx0 = cx + 14 if side == 'L' else cx + 66
            cards.append(icon(m[3], ix, y + 8))
            cards.append(tx(tx0, y + 24, m[1] + (' (часть)' if split else ''), 15, '#f1f5f9', bold=True))
            cards.append(tx(tx0, y + 42, m[2], 11.5, '#94a3b8'))
            for i, (s_, t) in enumerate(terms):
                ty = y + 72 + i * 26
                w = next(w for w in mine if w['s'] == s_)
                w['ty'] = ty
                c = GCOL[sigs[s_]['g']]
                tx_ = edge - 16 if side == 'L' else edge + 16
                cards.append(f'<g class="hot" data-s="{s_}"><circle cx="{edge}" cy="{ty}" r="5.5" fill="#0b1220" stroke="{c}" stroke-width="2.5"/>'
                             + tx(tx_, ty + 4.5, t, 13, '#e2e8f0', 'end' if side == 'L' else 'start')
                             + tx(tx_ - (0 if side == 'R' else 0), ty + 17, sigs[s_]['sh'], 9.5, '#64748b', 'end' if side == 'L' else 'start') + '</g>')
            for i, line in enumerate(m[5]):
                cards.append(tx(cx + 14, y + 72 + (len(terms) - 1) * 26 + 34 + i * 16, line, 11, '#94a3b8'))
            cards.append('</g>')
            y += h + 16
        maxy = max(maxy, y)
        # дорожки (вертикальные участки) — по правилу без лишних пересечений
        down = sorted([w for w in ws if w['ty'] >= w['yy']], key=lambda w: -w['yy'])
        up = sorted([w for w in ws if w['ty'] < w['yy']], key=lambda w: w['yy'])
        lanes = down + up
        span = (BX0 - 30) - (edge + 110) if side == 'L' else (Wd - 150 - CW - 110) - (BX1 + 30)
        step = min(9, span / max(1, len(lanes)))
        for i, w in enumerate(lanes):
            lx = BX0 - 30 - i * step if side == 'L' else BX1 + 30 + i * step
            s = sigs[w['s']]
            c = GCOL[s['g']]
            d = (f'M{w["x"]:.1f} {w["y"]:.1f} V{w["yy"]:.1f} H{lx:.1f} V{w["ty"]:.1f} H{edge:.1f}')
            wires.append(f'<path class="w" data-s="{w["s"]}" d="{d}" stroke="{c}"/>')
            if s['c']:                                                           # детали шилда на линии
                bw = 7 * len(s['c']) + 10
                bx = edge + 8 if side == 'L' else edge - 8 - bw
                wires.append(f'<g class="hot" data-s="{w["s"]}"><rect x="{bx:.1f}" y="{w["ty"] - 8:.1f}" width="{bw}" height="16" rx="4" '
                             f'fill="#0b1220" stroke="{c}" stroke-width="1"/>' + tx(bx + bw / 2, w['ty'] + 4, s['c'], 10.5, c, 'middle') + '</g>')

    H = max(maxy, BOARD_BOTTOM) + 40
    # плата Discovery
    pb = [f'<rect x="{BX0}" y="{BY0}" width="{BX1 - BX0}" height="{BOARD_BOTTOM - BY0}" rx="14" fill="#0f3a2c" stroke="#16a34a" stroke-width="2"/>']
    mx = (BX0 + BX1) / 2
    pb.append(f'<rect x="{mx - 26}" y="{BY0 - 10}" width="52" height="26" rx="3" fill="#9ca3af"/>')
    pb.append(tx(mx, BY0 + 34, 'mini-USB ST-LINK — прошивка и питание', 11, '#a7f3d0', 'middle'))
    pb.append(f'<rect x="{mx - 70}" y="{BY0 + 46}" width="140" height="56" rx="4" fill="#111827" stroke="#334155"/>')
    pb.append(tx(mx, BY0 + 80, 'ST-LINK/V2', 12, '#94a3b8', 'middle'))
    pb.append(tx(mx, BY0 + 140, 'STM32F4DISCOVERY', 22, '#f1f5f9', 'middle', True))
    pb.append(tx(mx, BY0 + 160, 'вид сверху, как стоит на шилде', 12, '#a7f3d0', 'middle'))
    cy = (ROW0 + BOARD_BOTTOM) / 2 - 10
    pb.append(f'<rect x="{mx - 85}" y="{cy - 85}" width="170" height="170" rx="6" fill="#111827" stroke="#475569" stroke-width="2"/>')
    pb.append(tx(mx, cy - 6, 'STM32F407VGT6', 14, '#e2e8f0', 'middle', True))
    pb.append(tx(mx, cy + 14, 'LQFP100 · 168 МГц', 11, '#94a3b8', 'middle'))
    for i, (cl, nm) in enumerate((('#f97316', 'LD3'), ('#22c55e', 'LD4'), ('#ef4444', 'LD5'), ('#3b82f6', 'LD6'))):
        lx_, ly_ = mx - 45 + i * 30, cy + 120
        pb.append(f'<rect x="{lx_ - 6}" y="{ly_ - 4}" width="12" height="8" fill="{cl}"/>' + tx(lx_, ly_ + 18, nm, 9, '#a7f3d0', 'middle'))
    pb.append(f'<circle cx="{mx - 60}" cy="{BOARD_BOTTOM - 70}" r="11" fill="#2563eb"/>' + tx(mx - 60, BOARD_BOTTOM - 46, 'USER (PA0)', 10, '#a7f3d0', 'middle'))
    pb.append(f'<circle cx="{mx + 60}" cy="{BOARD_BOTTOM - 70}" r="11" fill="#111"/>' + tx(mx + 60, BOARD_BOTTOM - 46, 'RESET', 10, '#a7f3d0', 'middle'))
    pb.append(f'<rect x="{mx - 18}" y="{BOARD_BOTTOM - 16}" width="36" height="20" rx="3" fill="#9ca3af"/>')
    pb.append(tx(mx, BOARD_BOTTOM - 24, 'micro-USB OTG (не нужен)', 10, '#a7f3d0', 'middle'))
    for hdr, table in (('P1', W.P1), ('P2', W.P2)):
        x_odd = col[(hdr, 1)]
        pb.append(f'<rect x="{x_odd - 14}" y="{ROW0 - 14}" width="54" height="{24 * PITCH + 28}" rx="3" fill="#0b0f17"/>')
        pb.append(tx(x_odd + 13, ROW0 - 22, hdr, 16, '#f1f5f9', 'middle', True))
        for n, name in table.items():
            x, y = pin_xy(hdr, n)
            outer_left = (hdr == 'P1') == (n % 2 == 1)                          # подпись снаружи ряда
            lx_ = x - 13 if n % 2 == 1 else x + 13
            anc = 'end' if n % 2 == 1 else 'start'
            sig = PIN_TO_SIG.get(name)
            if sig:
                s = sigs[sig]
                c = GCOL[s['g']] if s['mid'] else '#94a3b8'
                pins_el.append(f'<g class="hot pin" data-s="{sig}"><rect x="{x - 7}" y="{y - 7}" width="14" height="14" rx="{2 if n == 1 else 7}" fill="{c}" stroke="#0b0f17"/>'
                               + tx(lx_, y + 4, name, 11, c, anc, True, extra=HALO) + '</g>')
            else:
                tip = POWER.get(name) or ('свободен' + (f' (на плате: {ONBOARD[name]})' if name in ONBOARD else ''))
                pins_el.append(f'<g class="hot pin" data-p="{e(name)}" data-tip="{e(f"{hdr}-{n} · {name}: {tip}")}">'
                               f'<rect x="{x - 6}" y="{y - 6}" width="12" height="12" rx="{2 if n == 1 else 6}" fill="#475569"/>'
                               + tx(lx_, y + 4, name, 10, '#64748b', anc, extra=HALO) + '</g>')
            del outer_left
    parts = pb + wires + pins_el + cards
    legend = ''.join(f'<g transform="translate({20 + i * 236},{H - 18})"><rect width="22" height="5" y="-6" fill="{GCOL[g]}"/>'
                     + tx(30, 0, n, 13, '#cbd5e1') + '</g>' for i, (g, _, n) in enumerate(W.GROUPS))
    return Wd, H + 20, ''.join(parts) + legend


# ── Вкладка 2: шилд ────────────────────────────────────────────────────────────────────────────────────
def net_sig(net, sigs):
    rev = {v: k for k, v in D.KEYS}
    for cand in (net, rev.get(net), net[:-2] if net.endswith(('_K', '_T')) else None, 'TACHO' if net == 'TACHO_OUT' else None):
        if cand and cand in sigs:
            return cand
    return None


def shield(sigs):
    fp = SD.footprints()
    K, OX, OY = 7.0, 330, 60
    Wd, H = 1340, int(OY + 126 * K + 170)
    X = lambda v: OX + v * K
    Y = lambda v: OY + v * K
    p = [f'<rect x="{X(0)}" y="{Y(0)}" width="{96 * K}" height="{126 * K}" rx="12" fill="#14532d" stroke="#052e16" stroke-width="3"/>']
    for hx, hy in ((3.5, 3.5), (92.5, 3.5), (3.5, 122.5), (92.5, 122.5)):
        p.append(f'<circle cx="{X(hx)}" cy="{Y(hy)}" r="11" fill="#0b1220" stroke="#ca8a04" stroke-width="3"/>')
    j1, j2 = fp['J1'], fp['J2']
    xs = [q[0] for q in list(j1.values()) + list(j2.values())]
    ys = [q[1] for q in j1.values()]
    p.append(f'<rect x="{X(min(xs) - 7)}" y="{Y(min(ys) - 30)}" width="{(max(xs) - min(xs) + 14) * K}" height="{(max(ys) - min(ys) + 37) * K}" '
             f'rx="10" fill="none" stroke="#86efac" stroke-dasharray="8 6" stroke-width="2"/>')
    p.append(tx(X((min(xs) + max(xs)) / 2), Y(min(ys) - 22), 'здесь стоит Discovery (USB ST-LINK — к этому краю)', 13, '#bbf7d0', 'middle'))
    for ref, table in (('J1', W.P1), ('J2', W.P2)):
        for n, (px, py) in fp[ref].items():
            name = table[n]
            sig = next((k for k, s in sigs.items() if s['pin'] == name), None)
            if sig:
                p.append(f'<circle class="hot" data-s="{sig}" cx="{X(px)}" cy="{Y(py)}" r="5" fill="{GCOL[sigs[sig]["g"]]}"/>')
            else:
                p.append(f'<circle class="hot" data-tip="{e(f"{ref} = P{ref[1]}-{n} · {name}")}" cx="{X(px)}" cy="{Y(py)}" r="4" fill="#4b5563"/>')
        p.append(tx(X(sum(q[0] for q in fp[ref].values()) / 50), Y(max(q[1] for q in fp[ref].values())) + 34,
                    f'{ref} — гнездо P{ref[1]}', 13, '#f1f5f9', 'middle', True))
    bottom = []
    for ref, (title, nets, labels) in SD.TERM.items():
        ps = fp[ref]
        xs = [q[0] for q in ps.values()]; ys = [q[1] for q in ps.values()]
        p.append(f'<rect x="{X(min(xs)) - 11}" y="{Y(min(ys)) - 11}" width="{(max(xs) - min(xs)) * K + 22}" height="{(max(ys) - min(ys)) * K + 22}" '
                 f'rx="3" fill="#e5e7eb"/>')
        full = labels + (['GND'] if len(nets) == 9 else [])
        nets_ = nets + (['GND'] if len(nets) == 9 else [])
        for n, (px, py) in ps.items():
            net, lab = nets_[n - 1], full[n - 1]
            sig = net_sig(net, sigs)
            c = GCOL[sigs[sig]['g']] if sig else {'GND': '#111827', '+5V': '#dc2626', '+3V': '#f59e0b'}.get(net, '#6b7280')
            attr = f'data-s="{sig}"' if sig else f'data-tip="{e(f"{ref}-{n} «{lab}» — {net}")}"'
            p.append(f'<rect class="hot" {attr} x="{X(px) - 7}" y="{Y(py) - 7}" width="14" height="14" rx="{2 if n == 1 else 7}" '
                     f'fill="{c}" stroke="#111" stroke-width="1"/>')
        mx_ = (min(xs) + max(xs)) / 2
        if min(xs) < 10 or min(xs) > 85:                 # боковые: название над разъёмом, подпись у каждого вывода
            left = min(xs) < 10
            xx = X(0) - 18 if left else X(96) + 18
            anc = 'end' if left else 'start'
            p.append(tx(xx, Y(min(ys)) - 13, ref + ' ' + title, 13, '#f1f5f9', anc, True))
            for n, (px, py) in ps.items():
                p.append(tx(xx, Y(py) + 4, full[n - 1], 10.5, '#cbd5e1', anc))
        else:
            bottom.append((X(mx_), Y(max(ys)) + 11, ref, title, full))
    bottom.sort()
    n = len(bottom)
    for i, (bx, by, ref, title, full) in enumerate(bottom):
        sx = bx
        ty = Y(126) + 34 + (i % 3) * 40
        p.append(f'<path d="M{bx} {by} V{ty - 16}" stroke="#64748b" fill="none"/>')
        p.append(tx(sx, ty, ref + ' ' + title, 13, '#f1f5f9', 'middle', True))
        p.append(tx(sx, ty + 15, ' · '.join(full), 10.5, '#94a3b8', 'middle'))
    return Wd, H, ''.join(p)


# ── Вкладка 3: таблица ─────────────────────────────────────────────────────────────────────────────────
def table(sigs):
    rows = []
    order = {g: i for i, (g, _, _) in enumerate(W.GROUPS)}
    def where(s):                                # по разъёму и выводу шилда: J4-3 → ('J4', 3)
        ref, _, n = s['sh'].split(' ')[0].partition('-')
        return (ref, int(n or 0))
    for k, s in sorted(sigs.items(), key=lambda kv: (order[kv[1]['g']],) + where(kv[1])):
        rows.append(f'<tr data-s="{k}"><td><i style="background:{GCOL[s["g"]]}"></i>{e(s["t"])}</td><td><b>{s["pin"]}</b></td>'
                    f'<td>{s["hdr"]}</td><td>{e(s["sh"] or "—")}</td><td>{e(s["mod"])}{": " + e(s["term"]) if s["term"] else ""}</td>'
                    f'<td>{e(s["c"])}</td><td>{e(s["mega"])}</td></tr>')
    return ('<table id="tbl"><thead><tr><th>Сигнал</th><th>STM32</th><th>Discovery</th><th>Шилд</th><th>Модуль: клемма</th>'
            '<th>Детали на шилде</th><th>Mega (7e2)</th></tr></thead><tbody>' + ''.join(rows) + '</tbody></table>')


PAGE = r'''<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Схема подключения ELS</title>
<style>
:root{--bg:#0b1220;--card:#111a2b;--line:#253148;--text:#e2e8f0;--dim:#94a3b8;--acc:#f43f5e}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 -apple-system,Segoe UI,Arial,sans-serif}
header{padding:14px 18px 6px;display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 18px}
h1{margin:0;font-size:22px;color:var(--acc)}header span{color:var(--dim)}
nav{display:flex;flex-wrap:wrap;gap:8px;padding:6px 18px}
nav button,.tools button{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 14px;font-size:14px;cursor:pointer}
nav button.on{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc) inset}
.tools{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:8px 18px;color:var(--dim)}
.tools input{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 10px;font-size:14px;min-width:240px}
.pane{display:none}.pane.on{display:block}
.view{margin:0 18px 18px;border:1px solid var(--line);border-radius:10px;background:#0a101c;height:calc(100vh - 190px);min-height:420px;overflow:hidden;touch-action:none;cursor:grab}
.view svg{width:100%;height:100%;display:block;font-family:-apple-system,Segoe UI,Arial,sans-serif}
.w{fill:none;stroke-width:2.2;stroke-linejoin:round;opacity:.85}
svg.dim .w,svg.dim .hot,svg.dim .card{opacity:.12}
svg.dim .hl{opacity:1!important}svg.dim .w.hl{stroke-width:4.5}
.hot{cursor:pointer}
#tip{position:fixed;z-index:9;pointer-events:none;display:none;max-width:380px;background:#020617;border:1px solid #475569;border-radius:8px;padding:9px 11px;font-size:13px;box-shadow:0 6px 24px #000a}
#tip b{color:#fff}#tip .k{color:var(--dim)}
.note{margin:0 18px 10px;color:var(--dim);font-size:13px}
#tbl{border-collapse:collapse;margin:0 18px 30px;width:calc(100% - 36px)}
#tbl th,#tbl td{border-bottom:1px solid var(--line);padding:7px 8px;text-align:left;vertical-align:top}
#tbl th{color:var(--dim);font-weight:600;position:sticky;top:0;background:var(--bg)}
#tbl tr:hover td{background:#14203a;cursor:pointer}#tbl i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:8px}
@media (max-width:700px){.view{height:70vh}.tools input{min-width:0;flex:1}}
</style></head><body>
<header><h1>Схема подключения</h1><span>STM32F4DISCOVERY · шилд rev 2 · данные из прошивки и платы KiCad</span></header>
<nav><button data-p="sch" class="on">Полная схема</button><button data-p="sh">Шилд</button><button data-p="tb">Таблица</button>
<button onclick="location.href='assembly/README.md'">Сборка и покупки →</button></nav>
<div class="tools"><input id="q" placeholder="Найти: PA1, энкодер, J5, кнопка…"><button onclick="zoom(1.3)">＋</button><button onclick="zoom(1/1.3)">−</button>
<button onclick="fit()">Вписать</button><span>Колесо — масштаб, перетаскивание — сдвиг. Наведите на вывод, провод или клемму — подсветится вся цепь; клик — закрепить.</span></div>
<div class="pane on" id="p-sch"><div class="view"><svg id="s-sch" viewBox="0 0 __W1__ __H1__">__SCH__</svg></div>
<p class="note">Плата — вид сверху, как стоит на шилде: P1 слева, P2 справа, вывод 1 — квадратный, вверху слева. На линии — детали, которые уже стоят на шилде («1k↑3V» — подтяжка 1 кОм к 3 В). Серые выводы — питание или свободные. Под клеммой модуля — разъём и вывод шилда.</p></div>
<div class="pane" id="p-sh"><div class="view"><svg id="s-sh" viewBox="0 0 __W2__ __H2__">__SH__</svg></div>
<p class="note">Шилд сверху, Discovery снята. Наведите на вывод разъёма — куда он идёт на Discovery и к какой клемме модуля. Квадратный вывод — первый.</p></div>
<div class="pane" id="p-tb">__TB__</div>
<div id="tip"></div>
<script>
const D=__DATA__;
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let pinned=null,cur='sch';
function tipHtml(s){const d=D[s];if(!d)return'';return `<b>${d.t}</b><br><span class=k>STM32</span> ${d.pin} · <span class=k>Discovery</span> ${d.hdr}`+
 (d.sh?` · <span class=k>шилд</span> ${d.sh}`:'')+(d.mod?`<br><span class=k>модуль</span> ${d.mod}: ${d.term}`:'')+
 (d.c?`<br><span class=k>на шилде</span> ${d.c}`:'')+(d.mega?`<br><span class=k>у автора (Mega)</span> ${d.mega}`:'')+(d.note?`<br>${d.note}`:'');}
function hl(svg,list){svg.classList.toggle('dim',!!list);svg.querySelectorAll('.hl').forEach(e=>e.classList.remove('hl'));
 if(!list)return;list.forEach(s=>svg.querySelectorAll(`[data-s="${s}"]`).forEach(e=>e.classList.add('hl')));
 const ms=new Set(list.map(s=>D[s]&&D[s].mid));svg.querySelectorAll('.card').forEach(c=>{if(ms.has(c.dataset.m))c.classList.add('hl');});}
const tip=$('#tip');
function showTip(ev,h){if(!h){tip.style.display='none';return;}tip.innerHTML=h;tip.style.display='block';
 const x=Math.min(ev.clientX+16,innerWidth-tip.offsetWidth-8),y=Math.min(ev.clientY+16,innerHeight-tip.offsetHeight-8);tip.style.left=x+'px';tip.style.top=y+'px';}
$$('svg').forEach(svg=>{
 svg.addEventListener('mousemove',ev=>{const t=ev.target.closest('[data-s],[data-tip],.card');
  if(!t){if(!pinned)hl(svg,null);showTip(ev,null);return;}
  if(t.dataset.s){if(!pinned)hl(svg,[t.dataset.s]);showTip(ev,tipHtml(t.dataset.s));}
  else if(t.dataset.tip){showTip(ev,t.dataset.tip);}
  else if(t.classList.contains('card')){const l=Object.keys(D).filter(k=>D[k].mid==t.dataset.m);if(!pinned)hl(svg,l);showTip(ev,null);}});
 svg.addEventListener('mouseleave',()=>{if(!pinned)hl(svg,null);showTip(null,null);});
 svg.addEventListener('click',ev=>{if(moved)return;const t=ev.target.closest('[data-s],.card');
  if(!t){pinned=null;hl(svg,null);return;}
  const l=t.dataset.s?[t.dataset.s]:Object.keys(D).filter(k=>D[k].mid==t.dataset.m);pinned=l;hl(svg,l);});
});
// масштаб и сдвиг
const VB={};let moved=false;
function svgOf(){return $('#s-'+cur);}
function getVB(svg){return svg.viewBox.baseVal;}
function zoomAt(svg,k,cx,cy){const v=getVB(svg);const nw=v.width/k,nh=v.height/k;v.x=cx-(cx-v.x)/k;v.y=cy-(cy-v.y)/k;v.width=nw;v.height=nh;}
function zoom(k){const s=svgOf();if(!s)return;const v=getVB(s);zoomAt(s,k,v.x+v.width/2,v.y+v.height/2);}
function fit(){const s=svgOf();if(!s)return;const o=VB[s.id];const v=getVB(s);v.x=o[0];v.y=o[1];v.width=o[2];v.height=o[3];}
$$('.view svg').forEach(svg=>{const v=getVB(svg);VB[svg.id]=[v.x,v.y,v.width,v.height];
 const pt=(ev)=>{const r=svg.getBoundingClientRect(),v=getVB(svg);const s=Math.max(v.width/r.width,v.height/r.height);
  const ox=(r.width*s-v.width)/2,oy=(r.height*s-v.height)/2;return [v.x-ox+(ev.clientX-r.left)*s,v.y-oy+(ev.clientY-r.top)*s,s];};
 svg.addEventListener('wheel',ev=>{ev.preventDefault();const [x,y]=pt(ev);zoomAt(svg,ev.deltaY<0?1.15:1/1.15,x,y);},{passive:false});
 let drag=null,pts=new Map(),pinch=null;
 svg.addEventListener('pointerdown',ev=>{pts.set(ev.pointerId,ev);moved=false;
  if(pts.size==1)drag=[ev.clientX,ev.clientY];if(pts.size==2){const [a,b]=[...pts.values()];pinch=Math.hypot(a.clientX-b.clientX,a.clientY-b.clientY);}});
 svg.addEventListener('pointermove',ev=>{if(!pts.has(ev.pointerId))return;pts.set(ev.pointerId,ev);
  if(pts.size==2&&pinch){const [a,b]=[...pts.values()];const d=Math.hypot(a.clientX-b.clientX,a.clientY-b.clientY);
   const m={clientX:(a.clientX+b.clientX)/2,clientY:(a.clientY+b.clientY)/2};const [x,y]=pt(m);zoomAt(svg,d/pinch,x,y);pinch=d;moved=true;return;}
  if(drag){const [,,s]=pt(ev);const dx=ev.clientX-drag[0],dy=ev.clientY-drag[1];if(Math.abs(dx)+Math.abs(dy)>3)moved=true;
   const v=getVB(svg);v.x-=dx*s;v.y-=dy*s;drag=[ev.clientX,ev.clientY];}});
 const up=ev=>{pts.delete(ev.pointerId);if(pts.size<2)pinch=null;if(!pts.size)drag=null;};
 svg.addEventListener('pointerup',up);svg.addEventListener('pointercancel',up);
});
// вкладки
function show(p){cur=p;$$('nav button[data-p]').forEach(b=>b.classList.toggle('on',b.dataset.p==p));$$('.pane').forEach(x=>x.classList.toggle('on',x.id=='p-'+p));
 document.querySelectorAll('.tools button').forEach(b=>b.style.display=p=='tb'?'none':'');try{history.replaceState(null,'','#'+p);}catch(e){}}
$$('nav button[data-p]').forEach(b=>b.onclick=()=>show(b.dataset.p));
if(['sch','sh','tb'].includes(location.hash.slice(1)))show(location.hash.slice(1));
// таблица → схема
$$('#tbl tbody tr').forEach(r=>r.onclick=()=>{show('sch');pinned=[r.dataset.s];hl($('#s-sch'),pinned);});
// поиск
$('#q').addEventListener('input',()=>{const q=$('#q').value.trim().toLowerCase();
 const l=q?Object.keys(D).filter(k=>{const d=D[k];return [d.t,d.pin,d.hdr,d.sh,d.mod,d.term,k].join(' ').toLowerCase().includes(q);}):null;
 pinned=l&&l.length?l:null;$$('.view svg').forEach(s=>hl(s,pinned));
 $$('#tbl tbody tr').forEach(r=>r.style.display=!l||l.includes(r.dataset.s)?'':'none');});
</script></body></html>'''


def main():
    sigs = signals()
    w1, h1, sch = schematic(sigs)
    w2, h2, sh = shield(sigs)
    page = (PAGE.replace('__W1__', str(w1)).replace('__H1__', str(int(h1))).replace('__SCH__', sch)
            .replace('__W2__', str(w2)).replace('__H2__', str(h2)).replace('__SH__', sh).replace('__TB__', table(sigs))
            .replace('__DATA__', json.dumps(sigs, ensure_ascii=False)))
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(page)
    miss = [k for k, s in sigs.items() if not s['mid'] and k not in ('DRO_TX', 'AUDIO_RESET')]
    print('docs/wiring.html готов; сигналов', len(sigs), '; без модуля:', miss or 'нет')


if __name__ == '__main__':
    main()
