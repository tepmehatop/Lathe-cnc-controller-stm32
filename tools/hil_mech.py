#!/usr/bin/env python3
"""
Механика станка с экрана на железе: разные допустимые наборы применяются и реально работают.

Стенд как у tools/follow_hil.py: STM32 (прошивка stm32f407_dro) по ST-Link, дисплей по USB (команды экрана),
Mega — блок DRO, следует за моторами (STEP/DIR → D2..D5), шпиндель — TIM5 на PA1.

Для каждого набора (энкодер, шаги, микрошаг, винт по Z и X, люфты):
  1. CFG_SET с экрана → STM32 проверяет и пишет во флеш; читаем e2_mech из памяти — применился ровно он.
  2. Mega переводит шаги в мкм по этой механике (нм на шаг), контроль пропуска шагов STM32 сверяет «линейку»
     с шагами — если STM32 считает механику иначе, цикл остановится «ПРОПУСК ШАГОВ».
  3. Движение: синхронная подача M1 по Z и по X, резьба M3, асинхронная подача M2. Шаги мотора за время замера
     сравниваются с расчётом — по тем же формулам, что в ядре (делители целые, как у автора 7e2).
Недопустимые наборы — STM32 отказывает, механика не меняется. В конце — набор переживает сброс STM32,
затем возврат к els_config.h (CFG_DEFAULT).

Запуск: ~/.platformio/penv/bin/python tools/hil_mech.py [--only 1,5] [--quick]
"""
import argparse, math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega

# (энкодер, шагов Z, микрошаг Z, винт Z 0.01 мм, шагов X, микрошаг X, винт X, люфт Z, люфт X, что за станок)
VALID = [
    (1800, 200, 4, 200, 200, 4, 100, 0, 0,    'по умолчанию (els_config.h)'),
    (3600, 200, 8, 200, 200, 8, 100, 0, 0,    'энкодер 3600, микрошаг 8 (вопрос с форума)'),
    (1800, 200, 8, 300, 200, 8, 200, 0, 0,    'винты 3 и 2 мм, микрошаг 8'),
    (1800, 200, 8, 250, 200, 8, 200, 30, 20,  'винт Z 2.5 мм, люфты 30/20'),
    (2000, 200, 4, 200, 200, 4, 100, 0, 0,    'энкодер 2000'),
    (1000, 200, 4, 200, 200, 2, 100, 0, 0,    'энкодер 1000, X микрошаг 2'),
    (1000, 200, 2, 200, 200, 4, 200, 0, 0,    'энкодер 1000, Z микрошаг 2'),
    (1440, 200, 4, 200, 200, 4, 125, 0, 0,    'энкодер 1440, винт X 1.25'),
    (1800, 200, 4, 150, 200, 4, 125, 0, 0,    'винты 1.5 и 1.25 мм'),
    (1800, 200, 16, 500, 200, 8, 250, 0, 0,   'винты 5 и 2.5 мм, микрошаг 16/8'),
    (1800, 300, 4, 250, 200, 4, 125, 0, 0,    'ремень 1:1.5 на Z (300 шагов на оборот винта)'),
    (3000, 200, 8, 250, 200, 8, 200, 0, 0,    'энкодер 3000'),
    (1200, 200, 4, 200, 200, 4, 150, 0, 0,    'энкодер 1200, винт X 1.5'),
    (2500, 200, 8, 300, 200, 8, 200, 0, 0,    'энкодер 2500'),
    (1800, 200, 8, 423, 200, 8, 254, 0, 0,    'дюймовые винты 6 и 10 ниток на дюйм'),
    (1800, 400, 4, 400, 400, 4, 200, 50, 50,  'моторы 0.9° (400 шагов), винты 4 и 2 мм'),
]
# недопустимые: STM32 должен отказать (ожидаемое начало сообщения — как его покажет экран)
INVALID = [
    (3600, 200, 4, 200, 200, 4, 100, 0, 0,    'MIN_FEED Z'),
    (1800, 188, 4, 200, 200, 4, 100, 0, 0,    'КОНУС 45°'),
    (1800, 200, 4, 1000, 200, 4, 100, 0, 0,   'MIN_FEED Z'),
    (1800, 200, 3, 200, 200, 4, 100, 0, 0,    'МИКРОШАГ'),
    (50, 200, 4, 200, 200, 4, 100, 0, 0,      'ЭНКОДЕР'),
    (1800, 200, 4, 200, 200, 16, 100, 0, 0,   'MAX_FEED X'),
    (1800, 200, 4, 200, 200, 4, 100, 3000, 0, 'ЛЮФТ'),
]


def steps_per_mm(steps, mc, screw):
    return steps * mc * 100 / screw


def check_rules(c):
    """Как e2_mech_check в ядре: None — годится, иначе первая причина (для сверки с ответом STM32)."""
    e, zs, zm, zw, xs, xm, xw, blz, blx = c[:9]
    if not 100 <= e <= 10000: return 'ЭНКОДЕР'
    if not (50 <= zs <= 1000 and 50 <= xs <= 1000): return 'ШАГОВ НА ОБОРОТ'
    if zm not in (1, 2, 4, 8, 16, 32, 64, 128) or xm not in (1, 2, 4, 8, 16, 32, 64, 128): return 'МИКРОШАГ'
    if not (25 <= zw <= 1000 and 25 <= xw <= 1000): return 'ШАГ ВИНТА'
    if not (0 <= blz <= 2000 and 0 <= blx <= 2000): return 'ЛЮФТ'
    for ax, (s, m, w) in (('Z', (zs, zm, zw)), ('X', (xs, xm, xw))):
        spr = s * m
        if int(e / (spr * 1 / w) / 2) > 255: return 'MIN_FEED ' + ax
        if int(e / (spr * 25 / w) / 2) <= 1: return 'MAX_FEED ' + ax
    sz, sx = steps_per_mm(zs, zm, zw), steps_per_mm(xs, xm, xw)
    for p in (0.2, 4.5):
        for s in (sz, sx):
            d = 2 * e / (p * s)
            if d < 1 or d + 24 > 255: return 'РЕЗЬБА'
    for slope in (1.0, 0.01):                          # 45° и 1:50 — крайние в таблице
        d = 2 * sz / (sx * slope)
        if d < 1 - 1e-9: return 'КОНУС 45°'
        if d >= 256: return 'КОНУС 1:50'
    return None


# ── ожидаемые шаги — формулы ядра ───────────────────────────────────────────
def feed_steps(edges, enc, steps, mc, screw, feed):
    """M1: Timer5 считает передние фронты A, Feed_Divisor+1 линий на прерывание, шаг — два прерывания."""
    div = int(enc / (steps * mc * feed / screw) / 2 + 0.5) - 1        # FEED_EXACT = 1
    return edges / 2 / (2 * (div + 1))


def thread_steps(edges, enc, s_mm, pitch):
    """M3: делитель Ks + Km/10000 фронтов на шаг (оба фронта A) — дробный, точный."""
    return edges / (2 * enc / (pitch * s_mm))


def afeed_rate(steps, mc, screw, afeed):
    """M2: таймер 250 кГц, aFeed_Divisor+1 тактов на прерывание, шаг — два прерывания. Шагов в секунду."""
    k = (afeed * steps * mc) // ((60 * screw) // 100) * 2
    div = 250000 // k - 1
    return 250000 / (div + 1) / 2


THREAD_IDX, THREAD_PITCH = 13, 1.5          # «1.50mm» в Thread_Info


class Quadrature:
    """Шпиндель с двумя каналами: A — TIM5 CH2 на PA1 (как hil_cycles.Spindle), B — TIM1 CH1 на PA8 со сдвигом 90°.
    TIM1 запускается аппаратно от TIM5 (TRGO → ITR0), фаза постоянная. Резьба (M3) следит за направлением
    шпинделя по A и B: с одним каналом A направление скачет на каждом фронте и суппорт стоит."""
    RCC_APB1ENR, RCC_APB2ENR = 0x40023840, 0x40023844
    TIM5, TIM1, GPIOA = 0x40000C00, 0x40010000, 0x40020000

    def __init__(self, stm):
        self.s = stm

    def _w(self, a, v):
        self.s.cmd(f'mww 0x{a:08x} 0x{v & 0xffffffff:08x}')

    def _r(self, a):
        return self.s.read(a, 1, 32)[0]

    def on(self, lines_per_s, lead=True):
        T5, T1, GA = self.TIM5, self.TIM1, self.GPIOA
        self.off()
        self._w(self.RCC_APB1ENR, self._r(self.RCC_APB1ENR) | (1 << 3))      # TIM5EN
        self._w(self.RCC_APB2ENR, self._r(self.RCC_APB2ENR) | 1)             # TIM1EN
        # TIM1 — 16 бит и такт 168 МГц, TIM5 — 84 МГц: предделители, чтобы оба считали одинаково и период
        # влез в 16 бит (иначе на малых оборотах B идёт не с той частотой и направление шпинделя скачет)
        k = int(84_000_000 / lines_per_s / 65536) + 1
        arr5 = int(84_000_000 / k / lines_per_s) - 1
        arr1 = arr5
        # TIM5: A, PWM1 50 %, TRGO при включении (MMS = 001)
        for off, v in ((0x00, 0), (0x28, k - 1), (0x2C, arr5), (0x38, (arr5 + 1) // 2),
                       (0x18, (6 << 12) | (1 << 11)), (0x20, 1 << 4), (0x04, 1 << 4)):
            self._w(T5 + off, v)
        self._w(T5 + 0x14, 1); self._w(T5 + 0x24, 0)                         # UG, CNT = 0
        # TIM1: B, PWM1 50 %, ведомый — старт по ITR0 (TIM5), CNT сдвинут на четверть периода
        for off, v in ((0x00, 0), (0x28, 2 * k - 1), (0x2C, arr1), (0x34, (arr1 + 1) // 2),
                       (0x18, (6 << 4) | (1 << 3)), (0x20, 1), (0x44, 1 << 15), (0x08, 6)):   # SMS = 110 trigger, TS = ITR0
            self._w(T1 + off, v)
        self._w(T1 + 0x14, 1)
        q = (arr1 + 1) // 4
        self._w(T1 + 0x24, q if lead else (arr1 + 1) - q)
        # выводы: PA1 AF2 (TIM5_CH2), PA8 AF1 (TIM1_CH1)
        self._w(GA + 0x20, (self._r(GA + 0x20) & ~(0xF << 4)) | (2 << 4))
        self._w(GA + 0x24, (self._r(GA + 0x24) & ~0xF) | 1)
        self._w(GA, (self._r(GA) & ~((3 << 2) | (3 << 16))) | (2 << 2) | (2 << 16))
        self._w(T5, 1)                                                       # CEN → TRGO → TIM1 стартует

    def off(self):
        GA = self.GPIOA
        self._w(GA, self._r(GA) & ~((3 << 2) | (3 << 16)))                  # PA1, PA8 — снова входы
        self._w(self.TIM5, 0); self._w(self.TIM1, 0)


class Bench:
    def __init__(self, a):
        self.stm, self.esp, self.mega = cd.Stm32(), cd.Esp(a.esp), Mega(a.mega)
        self.sp = Quadrature(self.stm)
        self.fails = []

    def check(self, ok, msg):
        print(('    OK   ' if ok else '    FAIL ') + msg)
        if not ok:
            self.fails.append(msg)
        return ok

    def mech(self):
        addr, _ = self.stm.sym['e2_mech']
        v = self.stm.read(addr, 9, 32)
        return [x - (1 << 32) if x >= 1 << 31 else x for x in v]

    def edges(self):
        return self.stm.var('s_spindle_edges', signed=False)

    def run_axis(self, pin, motor, secs=2.0, warm=1.2, steady=None):
        """Держать джойстик: после разгона замер (фронты энкодера, шаги, время), затем отпустить.
        steady — (переменная, цель): ждать, пока разгон дойдёт до рабочего делителя (подача разгоняется долго)."""
        st = self.stm
        t_press = time.time()
        st.press(pin)
        try:
            time.sleep(warm)
            t_end = time.time() + 20
            while steady and time.time() < t_end and st.var(steady[0], signed=False) != st.var(steady[1], signed=False):
                time.sleep(0.2)
            e0, m0, t0 = self.edges(), st.var(motor), time.time()
            e0 = (e0 + self.edges()) / 2
            time.sleep(secs)
            e1, m1, t1 = self.edges(), st.var(motor), time.time()
            e1 = (e1 + self.edges()) / 2
            loss = st.var('e2_loss_block')
        finally:
            st.release(pin)
            self.held = time.time() - t_press           # обратный ход — столько же, чтобы суппорт не уползал
            time.sleep(0.6)
        de = (e1 - e0) % (1 << 32)
        return de, abs(m1 - m0), t1 - t0, loss

    def back(self, pin):
        held = self.held
        self.stm.press(pin); time.sleep(held); self.stm.release(pin); time.sleep(0.6)

    def dro_vs_motor(self, c, label):
        """Mega переводит шаги в мкм по этой механике: ход «линейки» = ход по шагам STM32 (знак — как на стенде)."""
        mz, mx = self.mega.pos()
        for ax, dg, dm, s in (('Z', mz - self.z0_mega, self.stm.var('Motor_Z_Pos') - self.z0_motor, steps_per_mm(*c[1:4])),
                              ('X', mx - self.x0_mega, self.stm.var('Motor_X_Pos') - self.x0_motor, steps_per_mm(*c[4:7]))):
            um = dm * 1000 / s
            # 7e2 при каждой остановке округляет счётчик до полного шага (мотор без Enable доворачивает сам), а Mega
            # считает голые импульсы — до McSTEP/2 микрошагов на остановку; в наборе до 10 остановок
            tol = 20 + 10 * (c[2] if ax == 'Z' else c[5]) / 2 * 1000 / s
            self.check(abs(abs(dg) - abs(um)) <= tol,
                       f'{label}: DRO {ax} {dg:+d} мкм, по шагам STM32 {um:+.0f} мкм (допуск {tol:.0f})')

    def apply(self, c):
        self.esp.btn('CFG_SET:' + ','.join(str(x) for x in c[:9]))
        time.sleep(3.0)                                 # стирание сектора флеша — до 2 с
        return self.mech()

    def motion(self, c, quick=False):
        st, esp = self.stm, self.esp
        enc, zs, zm, zw, xs, xm, xw = c[:7]
        sz, sx = steps_per_mm(zs, zm, zw), steps_per_mm(xs, xm, xw)
        nm_z, nm_x = round(1e6 / sz), round(1e6 / sx)
        for m in ('w0', 'lz0', 'lx0', f'kz{nm_z}', f'kx{nm_x}', 'f1', 'o'):
            self.mega.cmd(m)
        time.sleep(0.5)
        self.z0_mega, self.x0_mega = self.mega.pos()
        self.z0_motor, self.x0_motor = st.var('Motor_Z_Pos'), st.var('Motor_X_Pos')
        rps = 2.0                                       # 120 об/мин в единицах этой механики
        self.sp.on(rps * enc)                           # линий в секунду
        time.sleep(0.8)

        # M1 — синхронная подача 0.20 мм/об, Z и X; туда и обратно, чтобы суппорт не уезжал
        esp.btn('M1'); time.sleep(0.6); esp.btn('S2'); time.sleep(0.3); esp.btn('FEED:20'); time.sleep(0.4)
        for pin, back, motor, s, m, w in (('JOY_LEFT', 'JOY_RIGHT', 'Motor_Z_Pos', zs, zm, zw),
                                          ('JOY_DOWN', 'JOY_UP', 'Motor_X_Pos', xs, xm, xw)):
            de, dm, _, loss = self.run_axis(pin, motor, steady=('OCR5A', 'Feed_Divisor'))
            exp = feed_steps(de, enc, s, m, w, 20)
            ideal = de / (2 * enc) * 0.20 * steps_per_mm(s, m, w)
            self.check(not loss and dm > 0 and abs(dm - exp) <= max(3, 0.01 * exp),
                       f'M1 подача 0.20 {motor[6]}: {dm} шагов, расчёт {exp:.0f} (точно 0.20 мм/об — {ideal:.0f})'
                       + (' — ПРОПУСК ШАГОВ' if loss else ''))
            self.back(back)
        if quick:
            self.sp.off(); return

        # M3 — резьба 1.50 мм по Z, туда и обратно
        esp.btn('M3'); time.sleep(0.6); esp.btn('S2'); time.sleep(0.3)
        esp.btn(f'THREAD_STEP:{THREAD_IDX}'); time.sleep(0.5)
        de, dm, _, loss = self.run_axis('JOY_LEFT', 'Motor_Z_Pos', secs=1.5, steady=('tmp_Ks_Divisor', 'Ks_Divisor'))
        exp = thread_steps(de, enc, sz, THREAD_PITCH)
        self.check(not loss and dm > 0 and abs(dm - exp) <= max(3, 0.005 * exp),
                   f'M3 резьба 1.50 Z: {dm} шагов, расчёт {exp:.0f}' + (' — ПРОПУСК ШАГОВ' if loss else ''))
        self.back('JOY_RIGHT')
        self.sp.off(); time.sleep(0.5)

        # M2 — асинхронная подача 200 мм/мин по Z
        esp.btn('M2'); time.sleep(0.6); esp.btn('S2'); time.sleep(0.3); esp.btn('AFEED:200'); time.sleep(0.4)
        _, dm, dt, loss = self.run_axis('JOY_LEFT', 'Motor_Z_Pos', secs=1.5, steady=('OCR4A', 'aFeed_Divisor'))
        rate = afeed_rate(zs, zm, zw, 200)
        self.check(not loss and dm > 0 and abs(dm / dt - rate) <= 0.02 * rate,
                   f'M2 асинхрон 200 мм/мин Z: {dm / dt:.0f} шаг/с, расчёт {rate:.0f} '
                   f'(точно — {200 / 60 * sz:.0f})' + (' — ПРОПУСК ШАГОВ' if loss else ''))
        self.back('JOY_RIGHT')
        self.dro_vs_motor(c, 'после движения')

    def close(self):
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            try:
                self.stm.release(p)
            except Exception:
                pass
        try:
            self.sp.off()
        except Exception:
            pass
        try:
            self.esp.btn('CFG_DEFAULT'); time.sleep(3)
            self.mega.cmd('f0'); self.mega.cmd('w1')
        except Exception:
            pass
        self.stm.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--only', help='номера допустимых наборов через запятую (с 1)')
    ap.add_argument('--quick', action='store_true', help='только подача M1')
    a = ap.parse_args()
    for c in VALID:
        r = check_rules(c)
        assert r is None, f'набор {c} не проходит правила: {r}'
    for c in INVALID:
        assert check_rules(c), f'набор {c} проходит правила, а должен нет'

    b = Bench(a)
    try:
        b.stm.press('SUB_MAN'); time.sleep(0.3)
        # Mega при открытии порта перезапускается и «качается»; знак линейки STM32 учит раз за включение —
        # сначала Mega следует за моторами, потом сброс STM32
        for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):
            b.mega.cmd(m)
        b.stm.cmd('reset run'); time.sleep(4)
        # первое движение после старта STM32 — разминка в асинхронной подаче (без замера)
        b.esp.btn('M2'); time.sleep(0.6); b.esp.btn('S2'); time.sleep(0.3)
        for pin in ('JOY_LEFT', 'JOY_RIGHT'):
            b.stm.press(pin); time.sleep(0.5); b.stm.release(pin); time.sleep(0.5)
        idx = [int(x) for x in a.only.split(',')] if a.only else range(1, len(VALID) + 1)
        for i in idx:
            c = VALID[i - 1]
            sz, sx = steps_per_mm(*c[1:4]), steps_per_mm(*c[4:7])
            print(f'[{i}] {c[9]}: энкодер {c[0]}, Z {c[1]}x{c[2]}/{c[3] / 100:.2f} мм = {sz:.0f} шаг/мм, '
                  f'X {c[4]}x{c[5]}/{c[6] / 100:.2f} мм = {sx:.0f} шаг/мм')
            m = b.apply(c)
            if not b.check(m == list(c[:9]), f'STM32 применил {m}'):
                continue
            b.motion(c, a.quick)

        print('[недопустимые] STM32 отказывает, механика не меняется')
        b.apply(VALID[0])
        for c in INVALID:
            m = b.apply(c)
            b.check(m == list(VALID[0][:9]), f'{c[9]}: {list(c[:9])} отклонён, на STM32 осталось {m}')

        print('[сброс] набор переживает перезапуск STM32')
        c = VALID[1]
        b.apply(c)
        b.stm.cmd('reset run'); time.sleep(4)
        b.check(b.mech() == list(c[:9]), f'после сброса STM32: {b.mech()}')
    finally:
        b.close()
    print(f'\nИТОГ: {"OK" if not b.fails else str(len(b.fails)) + " FAIL"}')
    for f in b.fails:
        print('  -', f)
    return 1 if b.fails else 0


if __name__ == '__main__':
    sys.exit(main())
