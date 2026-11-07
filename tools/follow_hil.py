#!/usr/bin/env python3
"""
Стенд «эмулятор следует за моторами» (BACKLOG-026) и компенсация люфта на железе (BACKLOG-025/028).

Провода STM32 → Mega через 1 кОм: PE9 → D2 (STEP Z), PE10 → D4 (DIR Z), PE13 → D3 (STEP X), PE14 → D5 (DIR X).
STM32 — прошивка stm32f407_dro (Mega выдаёт координаты как блок DRO HS800-2).

1. Шаги: джойстиком (SWD) в асинхронной подаче туда-обратно по Z и X — Mega насчитывает столько же шагов,
   сколько STM32 (Motor_Z_Pos / Motor_X_Pos), позиция DRO на STM32 совпадает с позицией Mega.
2. Люфт: на Mega люфт X; без компенсации после разворота каретка отстаёт от мотора на люфт.
3. Компенсация с экрана (CFG_SET, сохраняется во флеш STM32): после разворота каретка там, где мотор.
Механика STM32 после теста возвращается к els_config.h (CFG_DEFAULT).

Запуск: python3 tools/follow_hil.py [--mega /dev/cu.usbserial-1410] [--esp /dev/cu.usbmodem14801]
"""
import argparse, re, sys, time
import serial
import compare_displays as cd

NM_PER_STEP = {'z': 2500, 'x': 1250}          # винт Z 2 мм / 800, X 1 мм / 800 — как в els_config.h


class Mega:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(2.0)                        # Mega перезагружается при открытии порта
        self.s.reset_input_buffer()

    def cmd(self, c):
        self.s.write((c + '\n').encode())
        time.sleep(0.15)

    def pos(self):
        self.s.reset_input_buffer()
        self.s.write(b'?\n')
        t0 = time.time()
        while time.time() - t0 < 2:
            line = self.s.readline().decode(errors='replace')
            m = re.search(r'z=(-?\d+).*?x=(-?\d+)', line)
            if '[SIM]' in line and m:
                return int(m.group(1)), int(m.group(2))
        raise RuntimeError('Mega не ответила на ?')


def jog(stm, pin, sec):
    stm.press(pin); time.sleep(sec); stm.release(pin); time.sleep(0.6)


def snapshot(stm, mega):
    mz, mx = mega.pos()
    return {'mz': stm.var('Motor_Z_Pos'), 'mx': stm.var('Motor_X_Pos'),
            'dz': stm.var('dro_pos_y'), 'dx': stm.var('dro_pos_x'), 'gz': mz, 'gx': mx}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--bl', type=int, default=100, help='люфт X на Mega, мкм')
    a = ap.parse_args()
    stm, esp, mega = cd.Stm32(), cd.Esp(a.esp), Mega(a.mega)
    fails = []

    def check(ok, msg):
        print(('  OK   ' if ok else '  FAIL ') + msg)
        if not ok:
            fails.append(msg)

    try:
        mega.cmd('w0'); mega.cmd('lz0'); mega.cmd('lx0')
        mega.cmd(f'kz{NM_PER_STEP["z"]}'); mega.cmd(f'kx{NM_PER_STEP["x"]}'); mega.cmd('f1'); mega.cmd('o')
        stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.6); esp.btn('S2'); time.sleep(0.4)
        esp.btn('AFEED:300'); time.sleep(0.3)
        esp.btn('CFG_DEFAULT'); time.sleep(3)

        # 1. Шаги один в один
        print('[1] Mega считает шаги моторов')
        for pin, ax, sec in (('JOY_LEFT', 'z', 2), ('JOY_RIGHT', 'z', 3), ('JOY_DOWN', 'x', 2), ('JOY_UP', 'x', 3)):
            s0 = snapshot(stm, mega); jog(stm, pin, sec); time.sleep(0.4); s1 = snapshot(stm, mega)
            dm = s1['m' + ax] - s0['m' + ax]
            dg = (s1['g' + ax] - s0['g' + ax]) * 1000 // NM_PER_STEP[ax]          # мкм Mega → шаги
            dd = s1['d' + ax] - s0['d' + ax]
            print(f'    {pin:9s} мотор {dm:+6d} шагов, Mega {dg:+6d} шагов ({s1["g" + ax] - s0["g" + ax]:+d} мкм), '
                  f'DRO на STM32 {dd:+d} мкм')
            # ±4: после каждой остановки ядро округляет счётчик до полного шага (±2, McSTEP 4) без импульсов —
            # разность двух остановок отличается от импульсов до 4
            check(dm != 0 and abs(abs(dg) - abs(dm)) <= 4, f'{ax.upper()} {pin}: шаги мотора {dm}, Mega {dg}')
            check(abs(dd - (s1['g' + ax] - s0['g' + ax])) <= 10, f'{ax.upper()} {pin}: DRO на STM32 {dd} = Mega')

        # 2-3. Люфт X: без компенсации и с компенсацией
        def reversal(label):
            jog(stm, 'JOY_DOWN', 1.5)                                    # выбрать люфт в одну сторону
            s0 = snapshot(stm, mega)
            jog(stm, 'JOY_UP', 1.5)                                      # разворот
            s1 = snapshot(stm, mega)
            motor_um = (s1['mx'] - s0['mx']) * NM_PER_STEP['x'] // 1000
            car_um = s1['gx'] - s0['gx']
            print(f'    {label}: мотор {motor_um:+d} мкм (по счётчику STM32), каретка {car_um:+d} мкм')
            return motor_um, car_um

        print(f'[2] люфт X {a.bl} мкм на Mega, компенсации нет')
        mega.cmd(f'lx{a.bl}')
        jog(stm, 'JOY_UP', 1); jog(stm, 'JOY_DOWN', 1)
        m, c = reversal('без компенсации')
        check(abs(abs(m) - abs(c) - a.bl) <= 5, f'каретка отстаёт на люфт: {abs(m) - abs(c)} мкм, ожидалось {a.bl}')

        print(f'[3] компенсация X {a.bl} мкм с экрана (CFG_SET → флеш STM32)')
        esp.btn(f'CFG_SET:1800,200,4,200,200,4,100,0,{a.bl}'); time.sleep(3)
        addr, _ = stm.sym['e2_mech']
        mech = stm.read(addr, 9, 32)                                     # поля E2Mech (int)
        check(mech[8] == a.bl, f'механика на STM32: {mech} — люфт X {mech[8]}, ожидалось {a.bl}')
        jog(stm, 'JOY_UP', 1); jog(stm, 'JOY_DOWN', 1)                 # после смены — сторона люфта известна
        m, c = reversal('с компенсацией')
        # счётчик STM32 — положение каретки: компенсирующие шаги не входят в его приращение
        check(abs(m - c) <= 5, f'каретка там, где счётчик STM32: мотор {m}, каретка {c}')
    finally:
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            try:
                stm.release(p)
            except Exception:
                pass
        try:
            esp.btn('CFG_DEFAULT'); time.sleep(3); mega.cmd('lx0'); mega.cmd('f0'); mega.cmd('w1')
        except Exception:
            pass
        stm.close()
    print(f'\nИТОГ: {"OK" if not fails else str(len(fails)) + " FAIL"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
