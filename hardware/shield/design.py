"""
Шилд ELS под STM32F4DISCOVERY — описание схемы (единый источник).

Из этого файла генерируются схема KiCad (gen_sch.py) и плата (gen_pcb.py).
Цепи выводов STM32 берутся из Core/e2/e2_pins.h и Core/els_config.h, поэтому
шилд всегда совпадает с прошивкой.

Схемотехника — как у шилда автора 7e2 (Diagram ELS_V7), с поправками на 3.3 В:
  * подтяжки входов 1 кОм — к 3V платы, а не к +5 В;
  * STEP/DIR/ENA — NPN-ключи (MMBT3904), вход драйвера «+» на +5 В;
  * зуммер — через NPN-ключ (BEEPER_ON_LEVEL = 1);
  * светодиоды упоров — от 3V через 220 Ом (горят при «0», как у автора);
  * конденсаторы на линиях энкодера (1 нФ) и РГИ (10 нФ).
"""
import os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# ── Разъёмы платы Discovery ────────────────────────────────────────────────────
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


def mcu_pins():
    """Имя сигнала → вывод STM32 (PA1, ...) по e2_pins.h и els_config.h."""
    txt = open(os.path.join(ROOT, 'Core', 'e2', 'e2_pins.h'), encoding='utf-8').read()
    m = {name: f'P{port}{n}' for name, port, n in re.findall(r'#define E2_PIN_(\w+)\s+([A-E]),\s*(\d+)', txt)}
    cfg = open(os.path.join(ROOT, 'Core', 'els_config.h'), encoding='utf-8').read()
    for k in ('SCALE_Z_PIN_A', 'SCALE_Z_PIN_B', 'SCALE_Z_PIN_REQ', 'SCALE_X_PIN_A', 'SCALE_X_PIN_B', 'SCALE_X_PIN_REQ'):
        m[k.replace('_PIN', '')] = re.search(r'#define\s+' + k + r'\s+(P[A-E]\d+)', cfg).group(1)
    return m


PINS = mcu_pins()
PIN_TO_NET = {v: k for k, v in PINS.items()}      # PA1 → ENC_A

# ── Библиотеки ────────────────────────────────────────────────────────────────
# Rev 2 (компактный, как Shield3.0 на Mega): мелочь — SMD 0805 и SOT-23 под платой Discovery (она стоит сверху
# в гнёздах, зазор ≈ 8 мм), разъёмы — штыревые 2.54 с защёлкой (Molex KK-254 / KF2510) по краям, режимы — IDC.
FP_R = 'Resistor_SMD:R_0805_2012Metric'
FP_C = 'Capacitor_SMD:C_0805_2012Metric'
FP_Q = 'Package_TO_SOT_SMD:SOT-23'              # MMBT3904: 1 = Б, 2 = Э, 3 = К
# Посадочное место штыревого разъёма: у PinSocket из библиотеки KiCad ряды зеркальны (вывод 2
# слева от 1), а Discovery вставляется лицом вверх — вывод 2 справа. Паяется гнездо 2×25.
FP_SOCK = 'Connector_PinHeader_2.54mm:PinHeader_2x25_P2.54mm_Vertical'
FP_HOLE = 'MountingHole:MountingHole_3.2mm_M3'


def fp_term(n):
    if n == 9:                                   # режимы — 8 положений + общий: коробка IDC 2×5 (10-й — GND)
        return 'Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical'
    return f'Connector_Molex:Molex_KK-254_AE-6410-{n:02d}A_1x{n:02d}_P2.54mm_Vertical'


# ── Детали ─────────────────────────────────────────────────────────────────────
# Каждая деталь: ref, символ, посадочное место, номинал, {номер вывода: цепь}, подпись
PARTS = []


def part(ref, sym, fp, value, pins, note=''):
    PARTS.append({'ref': ref, 'sym': sym, 'fp': fp, 'value': value, 'pins': pins, 'note': note})


# Разъёмы под Discovery
def sock_pins(table):
    pins = {}
    for n, name in table.items():
        if name == 'GND':
            pins[n] = 'GND'
        elif name == '5V':
            pins[n] = '+5V_DISCO'        # 5V Discovery — через диод D1 от +5V шилда (UM1472 п. 4.3: не больше 100 мА наружу)
        elif name == '3V':
            pins[n] = '+3V'
        elif name in PIN_TO_NET and PIN_TO_NET[name] not in ('AUDIO_RESET', 'DRO_TX'):
            pins[n] = PIN_TO_NET[name]   # PD4 (сброс аудиочипа) и PA2 (TX к блоку DRO) наружу не выводятся
    return pins


part('J1', 'Connector_Generic:Conn_02x25_Odd_Even', FP_SOCK, 'Discovery P1', sock_pins(P1), 'гнездо 2×25 под P1')
part('J2', 'Connector_Generic:Conn_02x25_Odd_Even', FP_SOCK, 'Discovery P2', sock_pins(P2), 'гнездо 2×25 под P2')

# Подтяжки 1k к 3V — по резистору 0805 на вход
PULLUPS = [
    ['MODE_FEED', 'MODE_AFEED', 'MODE_CONE_L', 'MODE_CONE_R', 'MODE_THREAD', 'MODE_SPHERE', 'MODE_DIVIDER', 'MODE_RESERVE'],
    ['SUB_INT', 'SUB_MAN', 'SUB_EXT', 'BTN_LEFT', 'BTN_RIGHT', 'BTN_UP', 'BTN_DOWN', 'BTN_SELECT'],
    ['JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'JOY_RAPID', 'LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT'],
    ['LIM_REAR', 'HAND_AXIS_Z', 'HAND_AXIS_X', 'HAND_X1', 'HAND_X10', 'ENC_A', 'ENC_B', 'HAND_A'],
]
PULLUP_NETS = [n for grp in PULLUPS for n in grp] + ['HAND_B']
for i, n in enumerate(PULLUP_NETS):
    part(f'R{101 + i}', 'Device:R', FP_R, '1k', {1: '+3V', 2: n}, f'подтяжка {n}')

# Конденсаторы от помех и питания
part('C1', 'Device:C', FP_C, '1nF', {1: 'ENC_A', 2: 'GND'}, 'энкодер A')
part('C2', 'Device:C', FP_C, '1nF', {1: 'ENC_B', 2: 'GND'}, 'энкодер B')
part('C3', 'Device:C', FP_C, '10nF', {1: 'HAND_A', 2: 'GND'}, 'РГИ A')
part('C4', 'Device:C', FP_C, '10nF', {1: 'HAND_B', 2: 'GND'}, 'РГИ B')
part('C5', 'Device:C', FP_C, '100nF', {1: 'FEED_POT', 2: 'GND'}, 'переменник')
part('C6', 'Device:C', FP_C, '100nF', {1: '+5V', 2: 'GND'}, '')
part('C7', 'Device:C', FP_C, '100nF', {1: '+3V', 2: 'GND'}, '')
# Питание Discovery от блока 5 В шилда — через диод Шоттки: блок питает Discovery, а USB ST-LINK (без блока)
# не питает дисплей, LCD и энкодеры через диод D1 самой Discovery (UM1472 п. 4.3)
part('D1', 'Device:D_Schottky', 'Diode_SMD:D_SOD-123', 'B5819W', {1: '+5V_DISCO', 2: '+5V'}, 'Шоттки 1 А, SOD-123 (B5819W, 1N5819W)')
part('C8', 'Device:C', 'Capacitor_SMD:C_1206_3216Metric', '22uF 10V', {1: '+5V', 2: 'GND'}, 'керамика X5R, низкая — под Discovery')

# NPN-ключи: драйверы (6) и зуммер. MMBT3904 (SOT-23): 1 = B, 2 = E, 3 = C
r = 2
q = 1
KEYS = [('Z_STEP', 'Z_PUL_N'), ('Z_DIR', 'Z_DIR_N'), ('Z_ENA', 'Z_ENA_N'),
        ('X_STEP', 'X_PUL_N'), ('X_DIR', 'X_DIR_N'), ('X_ENA', 'X_ENA_N'), ('BEEPER', 'BEEP_N')]
for sig, out in KEYS:
    base = 'B_' + sig
    part(f'R{r}', 'Device:R', FP_R, '1k', {1: sig, 2: base}, f'база {sig}'); r += 1
    part(f'R{r}', 'Device:R', FP_R, '10k', {1: base, 2: 'GND'}, 'закрывает ключ при старте'); r += 1
    part(f'Q{q}', 'Transistor_BJT:Q_NPN_BEC', FP_Q, 'MMBT3904', {1: base, 2: 'GND', 3: out}, f'ключ {sig}'); q += 1

# Светодиоды упоров: 220 Ом между выводом и клеммой катода
for led in ('LED_LEFT', 'LED_RIGHT', 'LED_FRONT', 'LED_REAR'):
    part(f'R{r}', 'Device:R', FP_R, '220', {1: led, 2: led + '_K'}, 'светодиод упора'); r += 1
# Тахометр — 100 Ом последовательно
part(f'R{r}', 'Device:R', FP_R, '100', {1: 'TACHO', 2: 'TACHO_OUT'}, 'тахометр'); r += 1
# Линейки: 470 Ом последовательно (защита выводов)
for sig in ('SCALE_Z_A', 'SCALE_Z_B', 'SCALE_Z_REQ', 'SCALE_X_A', 'SCALE_X_B', 'SCALE_X_REQ'):
    part(f'R{r}', 'Device:R', FP_R, '470', {1: sig, 2: sig + '_T'}, 'линейка'); r += 1

# ── Клеммники (подписи — на шелкографии) ─────────────────────────────────────
TERMINALS = [
    ('J3', 'ПИТАНИЕ 5В', ['+5V', 'GND'], ['+5V', 'GND']),
    ('J4', 'ЭНКОДЕР', ['+5V', 'GND', 'ENC_A', 'ENC_B'], ['+5V', 'GND', 'A', 'B']),
    ('J5', 'ДРАЙВЕР Z', ['+5V', 'Z_PUL_N', '+5V', 'Z_DIR_N', '+5V', 'Z_ENA_N'], ['PUL+', 'PUL-', 'DIR+', 'DIR-', 'ENA+', 'ENA-']),
    ('J6', 'ДРАЙВЕР X', ['+5V', 'X_PUL_N', '+5V', 'X_DIR_N', '+5V', 'X_ENA_N'], ['PUL+', 'PUL-', 'DIR+', 'DIR-', 'ENA+', 'ENA-']),
    # вывод N коробки — режим M N (как на экране): положение галетника N паять на вывод N
    ('J7', 'РЕЖИМ', ['MODE_FEED', 'MODE_AFEED', 'MODE_THREAD', 'MODE_CONE_L', 'MODE_CONE_R', 'MODE_SPHERE',
                     'MODE_DIVIDER', 'MODE_RESERVE', 'GND'], ['M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8', 'GND']),
    ('J8', 'ПОДРЕЖИМ', ['SUB_INT', 'SUB_MAN', 'SUB_EXT', 'GND'], ['ВНУТ', 'РУЧ', 'НАР', 'GND']),
    ('J9', 'КНОПКИ', ['BTN_LEFT', 'BTN_RIGHT', 'BTN_UP', 'BTN_DOWN', 'BTN_SELECT', 'GND'], ['ЛЕВ', 'ПРАВ', 'ВВЕРХ', 'ВНИЗ', 'ВЫБ', 'GND']),
    ('J10', 'ДЖОЙСТИК', ['JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'JOY_RAPID', 'GND'], ['ЛЕВ', 'ПРАВ', 'ВВЕРХ', 'ВНИЗ', 'БЫСТР', 'GND']),
    ('J11', 'УПОРЫ', ['LIM_LEFT', 'LIM_RIGHT', 'LIM_FRONT', 'LIM_REAR', 'GND'], ['ЛЕВ', 'ПРАВ', 'ПЕРЕД', 'ЗАД', 'GND']),
    ('J12', 'СВЕТОДИОДЫ', ['+3V', 'LED_LEFT_K', 'LED_RIGHT_K', 'LED_FRONT_K', 'LED_REAR_K'], ['+3V', 'ЛЕВ', 'ПРАВ', 'ПЕРЕД', 'ЗАД']),
    ('J13', 'РГИ', ['+5V', 'GND', 'HAND_A', 'HAND_B'], ['+5V', 'GND', 'A', 'B']),
    ('J14', 'РГИ ПЕРЕКЛ.', ['HAND_AXIS_Z', 'HAND_AXIS_X', 'HAND_X1', 'HAND_X10', 'GND'], ['Z', 'X', 'x1', 'x10', 'GND']),
    ('J15', 'ЗУММЕР', ['+5V', 'BEEP_N'], ['+5V', '-']),
    ('J16', 'ТАХОМЕТР', ['+5V', 'GND', 'TACHO_OUT'], ['+5V', 'GND', 'ИМП']),
    ('J17', 'ПОДАЧА', ['+3V', 'FEED_POT', 'GND'], ['+3V', 'СР', 'GND']),
    ('J18', 'LCD2004', ['GND', '+5V', 'LCD_SDA', 'LCD_SCL'], ['GND', '+5V', 'SDA', 'SCL']),
    ('J19', 'ДИСПЛЕЙ', ['GND', '+5V', 'DISP_TX', 'DISP_RX'], ['GND', '+5V', 'TX', 'RX']),
    ('J20', 'DRO', ['GND', 'DRO_RX'], ['GND', 'RX']),
    ('J21', 'ОТЛАДКА', ['GND', 'DBG_TX', 'DBG_RX'], ['GND', 'TX', 'RX']),
    ('J22', 'ЛИНЕЙКА Z', ['+5V', 'GND', 'SCALE_Z_A_T', 'SCALE_Z_B_T', 'SCALE_Z_REQ_T'], ['+5V', 'GND', 'A', 'B', 'REQ']),
    ('J23', 'ЛИНЕЙКА X', ['+5V', 'GND', 'SCALE_X_A_T', 'SCALE_X_B_T', 'SCALE_X_REQ_T'], ['+5V', 'GND', 'A', 'B', 'REQ']),
]
for ref, title, nets, labels in TERMINALS:
    n = len(nets)
    if n == 9:                                   # IDC 2×5: выводы 1…9 по порядку, 10 — GND
        part(ref, 'Connector_Generic:Conn_02x05_Odd_Even', fp_term(n), title,
             {**{i + 1: net for i, net in enumerate(nets)}, 10: 'GND'}, ' / '.join(labels + ['GND']))
        continue
    part(ref, f'Connector_Generic:Conn_01x{n:02d}', fp_term(n), title, {i + 1: net for i, net in enumerate(nets)},
         ' / '.join(labels))

for i in range(4):
    part(f'H{i + 1}', 'Mechanical:MountingHole', FP_HOLE, 'M3', {}, 'крепёжное отверстие')


def nets():
    """Все цепи → список (ref, pin)."""
    out = {}
    for p in PARTS:
        for pin, net in p['pins'].items():
            out.setdefault(net, []).append((p['ref'], pin))
    return out


if __name__ == '__main__':
    n = nets()
    print(f'деталей {len(PARTS)}, цепей {len(n)}')
    for net, conns in sorted(n.items()):
        if len(conns) < 2:
            print('  цепь с одним выводом:', net, conns)
