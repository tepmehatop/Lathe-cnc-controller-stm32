#!/usr/bin/env python3
"""
Автоциклы на железе: STM32 + LCD2004 + новый дисплей + Mega-эмулятор DRO.

Шпиндель без станка: TIM5 CH2 выводится на PA1 (канал A энкодера) — меандр с частотой
обороты × ENC_LINE_PER_REV / 60; прерывание PA1 ядра видит фронты как от энкодера.
Управление — как в tools/compare_displays.py: касания через дисплей, кнопки пульта —
перевод вывода в выход с низким уровнем по SWD.

Для каждого цикла: упоры с отгоном, параметры, подрежим, джойстик в сторону реза до
«ОПЕРАЦИЯ ЗАВЕРШЕНА». Каждые 0.5 с: счётчик «осталось» на LCD2004 и на новом дисплее
(должны совпадать и не расти), положение кареток относительно упоров. Скриншоты нового
дисплея по ходу цикла — в --out (для README).

Запуск из корня проекта:
  python3 tools/hil_cycles.py --out <папка> [--only <подстрока>] [--shots]
"""
import argparse, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd

RCC_APB1ENR = 0x40023840
TIM5 = 0x40000C00
GPIOA = 0x40020000
ENC_LINES = 1800          # ENC_LINE_PER_REV (els_config.h)
TIM_CLK = 84_000_000      # APB1 timers


class Spindle:
    def __init__(self, stm):
        self.stm = stm

    def on(self, rpm):
        s = self.stm
        f = rpm * ENC_LINES / 60.0                       # период меандра = одна линия (два фронта)
        arr = int(TIM_CLK / f) - 1
        en = s.read(RCC_APB1ENR, 1, 32)[0]
        s.cmd(f'mww 0x{RCC_APB1ENR:08x} 0x{en | (1 << 3):08x}')     # TIM5EN
        s.cmd(f'mww 0x{TIM5 + 0x00:08x} 0')                            # CR1: стоп
        s.cmd(f'mww 0x{TIM5 + 0x28:08x} 0')                            # PSC
        s.cmd(f'mww 0x{TIM5 + 0x2C:08x} 0x{arr:08x}')                  # ARR
        s.cmd(f'mww 0x{TIM5 + 0x38:08x} 0x{arr // 2:08x}')             # CCR2
        s.cmd(f'mww 0x{TIM5 + 0x18:08x} 0x{(6 << 12) | (1 << 11):08x}')  # CCMR1: OC2 PWM1, preload
        s.cmd(f'mww 0x{TIM5 + 0x20:08x} 0x{1 << 4:08x}')               # CCER: CC2E
        s.cmd(f'mww 0x{TIM5 + 0x14:08x} 1')                            # EGR: UG
        afrl = s.read(GPIOA + 0x20, 1, 32)[0]
        s.cmd(f'mww 0x{GPIOA + 0x20:08x} 0x{(afrl & ~(0xF << 4)) | (2 << 4):08x}')   # PA1 AF2 = TIM5_CH2
        moder = s.read(GPIOA, 1, 32)[0]
        s.cmd(f'mww 0x{GPIOA:08x} 0x{(moder & ~(3 << 2)) | (2 << 2):08x}')          # PA1 — альт. функция
        s.cmd(f'mww 0x{TIM5 + 0x00:08x} 1')                            # CR1: CEN

    def off(self):
        s = self.stm
        moder = s.read(GPIOA, 1, 32)[0]
        s.cmd(f'mww 0x{GPIOA:08x} 0x{moder & ~(3 << 2):08x}')          # PA1 — снова вход
        s.cmd(f'mww 0x{TIM5 + 0x00:08x} 0')


LIMS = (('LIM_LEFT', 'limit_Left_flag'), ('LIM_RIGHT', 'limit_Right_flag'),
        ('LIM_FRONT', 'limit_Front_flag'), ('LIM_REAR', 'limit_Rear_flag'))


def set_limits(stm, esp, problems):
    """Упоры как на станке: «Ручной» на переключателе, левый — отгон вправо — правый, передний — отгон вниз — задний."""
    stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.4); esp.btn('S2'); time.sleep(0.6)
    for pin, flag in LIMS:
        problems += cd.run_actions(stm, esp, [('lim', (pin, flag, 0))])
    problems += cd.run_actions(stm, esp, [
        ('lim', ('LIM_LEFT', 'limit_Left_flag', 1)),
        ('press', 'JOY_RIGHT'), ('wait', 2.5), ('release', 'JOY_RIGHT'), ('wait', 0.5),
        ('lim', ('LIM_RIGHT', 'limit_Right_flag', 1)),
        ('lim', ('LIM_FRONT', 'limit_Front_flag', 1)),
        ('press', 'JOY_DOWN'), ('wait', 2.5), ('release', 'JOY_DOWN'), ('wait', 0.5),
        ('lim', ('LIM_REAR', 'limit_Rear_flag', 1)),
    ])


def remaining_lcd(rows, sphere=False):
    # «Циклов:» — 3-я строка LCD2004; у шара «Заходов:» — 4-я
    m = re.search(r'(\d+)\s*$', rows[3 if sphere else 2])
    return int(m.group(1)) if m else None


def remaining_new(ui):
    m = re.match(r'\s*(\d+)\s*/\s*(\d+)', cd.norm(ui.get('row4_val', '')))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


CASES = [
    # имя, режим (M1..M6), подрежим (S1/S3), обороты, действия подготовки (касания/кнопки в «Ручном»)
    ('подача наружн., 3 прохода × 0.20', 'M1', 'S3', 600, ['FEED:20', 'AP:20', 'PASSES:3']),
    ('подача внутр., 2 прохода × 0.10', 'M1', 'S1', 600, ['FEED:15', 'AP:10', 'PASSES:2']),
    ('аподача наружн., 3 прохода × 0.10', 'M2', 'S3', 0, ['AFEED:300', 'AP:10', 'PASSES:3']),
    ('аподача внутр., 2 прохода × 0.10', 'M2', 'S1', 0, ['AFEED:300', 'AP:10', 'PASSES:2']),
    ('резьба 1.50 наружн.', 'M3', 'S3', 400, ['THREAD_STEP:13', 'PH:1']),
    ('резьба 1.00 внутр.', 'M3', 'S1', 500, ['THREAD_STEP:11', 'PH:1']),
    ('резьба 1.50 наружн., 2 захода', 'M3', 'S3', 300, ['THREAD_STEP:13', 'PH:2']),
    ('конус < 30° наружн., 2 прохода', 'M4', 'S3', 600, ['CONE:1', 'FEED:20', 'AP:20', 'PASSES:2']),
    ('конус > 45° внутр., 2 прохода', 'M5', 'S1', 600, ['CONE:0', 'FEED:20', 'AP:20', 'PASSES:2']),
    ('шар Ø20 наружн.', 'M6', 'S3', 600, []),
]


def prepare(stm, esp, mode, prep):
    esp.btn(mode); time.sleep(0.8)
    for p in prep:
        if p.startswith('PASSES:'):
            n = int(p.split(':')[1])
            for _ in range(20):
                if stm.var('Pass_Total') <= 1:
                    break
                esp.btn('KEY:LEFT'); time.sleep(0.15)
            for _ in range(n - 1):
                esp.btn('KEY:RIGHT'); time.sleep(0.15)
        else:
            esp.btn(p); time.sleep(0.2)
    time.sleep(0.5)


def run_case(stm, esp, dec, sp, case, a, shots_dir):
    name, mode, sub, rpm, prep = case
    sph = mode == 'M6'
    problems = []
    set_limits(stm, esp, problems)
    L, R = stm.var('Limit_Pos_Left'), stm.var('Limit_Pos_Right')
    prepare(stm, esp, mode, prep)
    esp.btn(sub); time.sleep(1.0)
    if stm.var('err_1_flag') or stm.var('err_2_flag'):
        problems.append(f'подрежим {sub} не включился (err1={stm.var("err_1_flag")} err2={stm.var("err_2_flag")})')
    z0, x0 = stm.var('Motor_Z_Pos'), stm.var('Motor_X_Pos')
    if rpm:
        sp.on(rpm); time.sleep(0.8)
    stm.press('JOY_LEFT')
    t0 = time.time(); seq = []; new_seq = []; mism = 0; zmin = zmax = z0; shot_n = 0
    while time.time() - t0 < a.timeout:
        time.sleep(0.5)
        if stm.var('Complete_flag'):
            break
        z = stm.var('Motor_Z_Pos'); zmin, zmax = min(zmin, z), max(zmax, z)
        rows = [dec(r) for r in stm.lcd_codes()]
        ui = esp.uidump()
        lcd_left = remaining_lcd(rows, sph)
        new_left, total = remaining_new(ui)
        if lcd_left is not None and (not seq or seq[-1] != lcd_left):
            seq.append(lcd_left)
        if new_left is not None and (not new_seq or new_seq[-1] != new_left):
            new_seq.append(new_left)
        if lcd_left is not None and new_left is not None and lcd_left != new_left:
            # счётчики обновляются в разные моменты — расхождением считаем только устойчивое
            time.sleep(0.4)
            rows = [dec(r) for r in stm.lcd_codes()]; ui = esp.uidump()
            if remaining_lcd(rows, sph) != remaining_new(ui)[0]:
                mism += 1
                if mism <= 3:
                    problems.append(f'осталось: LCD {remaining_lcd(rows, sph)}, новый {remaining_new(ui)}')
        if shots_dir and shot_n < 1 and len(seq) >= 2:
            fn = os.path.join(shots_dir, re.sub(r'[^\w.-]+', '_', name) + '.jpg')
            esp_shot(esp, fn); shot_n += 1
    done = bool(stm.var('Complete_flag'))
    # BeepComplete() (Beeper.ino) после флага 1.7 с играет сигнал через _delay_ms — главный цикл стоит,
    # на физический LCD и на новый дисплей надпись уходит после сигнала: ждём до 3 с
    t1 = time.time()
    while True:
        time.sleep(0.3)
        rows_end = [dec(r) for r in stm.lcd_codes()]
        ui_end = esp.uidump()
        if ui_end.get('alert') == '1' or time.time() - t1 > 3:
            break
    stm.release('JOY_LEFT')
    if rpm:
        sp.off()
    if not done:
        problems.append(f'не завершился за {a.timeout} с')
    if not any('ЗАВЕРШЕНА' in cd.norm(r).replace('3', 'З') for r in rows_end):
        problems.append('на LCD нет «ОПЕРАЦИЯ ЗАВЕРШЕНА»')
    if ui_end.get('alert') != '1' or 'ЗАВЕРШЕНА' not in cd.norm(ui_end.get('alert_msg', '')):
        problems.append(f'на новом дисплее нет «ОПЕРАЦИЯ ЗАВЕРШЕНА» (alert={ui_end.get("alert")}, '
                        f'msg={cd.norm(ui_end.get("alert_msg", ""))!r})')
    if any(b > a_ for a_, b in zip(seq, seq[1:])):
        problems.append(f'счётчик «осталось» рос: {seq}')
    # шар режет от правого упора на всю сферу — левый упор в нём не участвует (Sphere.ino)
    if (not sph and zmax > L + 4) or zmin < R - 4 - (abs(L - R) if 'захода' in name else 0):
        problems.append(f'Z вышел за упоры: {zmin - R:+d}..{zmax - L:+d} шагов от правого/левого')
    if not sph and stm.var('Motor_Z_Pos') != z0:
        problems.append(f'Z не вернулся к старту: {stm.var("Motor_Z_Pos") - z0:+d}')
    # выход: «Ручной», снять сообщение
    esp.btn('S2'); time.sleep(0.6)
    print(f'    длина {(L - R) / 400:.1f} мм, «осталось» по LCD: {seq}, новый в конце цикла: {new_seq[-3:]}, {time.time() - t0:.0f} с')
    return problems


def esp_shot(esp, path):
    import struct
    s = esp.s
    s.reset_input_buffer(); s.write(b'S'); s.flush()
    buf, t = b'', time.time()
    while time.time() - t < 10:
        buf += s.read(65536)
        i = buf.find(b'\xde\xad\xbe\xef')
        if i >= 0 and len(buf) >= i + 8:
            n = struct.unpack('<I', buf[i + 4:i + 8])[0]
            if len(buf) >= i + 8 + n:
                open(path, 'wb').write(buf[i + 8:i + 8 + n])
                return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default=None)
    ap.add_argument('--timeout', type=float, default=240)
    ap.add_argument('--shots', action='store_true', help='скриншоты нового дисплея по ходу цикла')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    stm, esp, dec = cd.Stm32(), cd.Esp(a.esp), cd.lcd_decoder()
    sp = Spindle(stm)
    results = []
    try:
        for case in CASES:
            if a.only and not any(x in case[0] for x in a.only.split('|')):
                continue
            print(f'[цикл] {case[0]}')
            pr = run_case(stm, esp, dec, sp, case, a, a.out if a.shots else None)
            results.append((case[0], pr))
            for p in pr:
                print(f'    ❗ {p}')
    finally:
        try:
            sp.off()
        except Exception:
            pass
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'SUB_MAN'):
            try:
                stm.release(p)
            except Exception:
                pass
        stm.close()
    ok = sum(1 for _, p in results if not p)
    print(f'\nИТОГ: {ok}/{len(results)} циклов без замечаний')


if __name__ == '__main__':
    main()
