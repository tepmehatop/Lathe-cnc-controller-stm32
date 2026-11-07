#!/usr/bin/env python3
"""
G-код на стенде (BACKLOG-001): строки G-кода через дисплей (USB <GCLINE:…> → <GCODE:…> в STM32), ответ <OK>/<ERR>
читается из USB дисплея. Mega следует за моторами и выдаёт координаты как блок DRO — после каждой строки положение
каретки (Mega) сверяется с целью. STM32 — прошивка stm32f407_dro, Mega — эмулятор (режим f1).

Запуск: python tools/gcode_hil.py   (Python из PlatformIO)
"""
import sys, time
import compare_displays as cd
from follow_hil import Mega

TOL_UM = 5


def main():
    stm, esp, mega = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801'), Mega('/dev/cu.usbserial-1410')
    fails = []

    def line(g, timeout=120):
        esp.s.reset_input_buffer()
        esp.send(f'<GCLINE:{g}>')
        t0 = time.time(); buf = b''
        while time.time() - t0 < timeout:
            buf += esp.s.read(4096)
            for l in buf.decode(errors='replace').splitlines():
                if l.startswith('[GC] '):
                    return l[5:].strip()
        return 'НЕТ ОТВЕТА'

    def check(g, z_um, x_um, timeout=120):
        r = line(g, timeout)
        time.sleep(0.4)
        mz, mx = mega.pos()
        ok = r == 'OK' and abs(mz - z_um) <= TOL_UM and abs(mx - x_um) <= TOL_UM
        print(f'  {"OK  " if ok else "FAIL"} {g:22s} → {r:3s} каретка Z {mz:+7d} X {mx:+7d} мкм (цель {z_um:+d} {x_um:+d})')
        if not ok:
            fails.append(g)

    try:
        for c in ('w0', 'kz2500', 'kx1250', 'lz0', 'lx0', 'f1'):
            mega.cmd(c)
        # направление линеек: ход осями джойстиком (асинхронная подача)
        stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
        for pin in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN'):
            stm.press(pin); time.sleep(1.0); stm.release(pin); time.sleep(0.8)
        mega.cmd('o'); time.sleep(0.5)                       # ноль: каретка Mega = 0, 0 → DRO 0, 0
        esp.btn('M8'); time.sleep(0.8)
        print('[G-код, координаты — блок DRO (Mega), X — диаметр]')
        r = line('G21 G90 G7')
        if r != 'OK':
            fails.append(f'модальные: {r}')
        check('G0 Z-5 X4', -5000, 2000)
        check('G1 Z-12 F300', -12000, 2000)
        check('G91 G1 X-1 F120', -12000, 1500)
        line('G90')
        check('G0 Z0 X0', 0, 0)
        check('G2 Z-5 X10 R5', -5000, 5000)
        check('G3 Z0 X0 R5', 0, 0)
        check('G28', 0, 0)
        # стоп посреди хода
        esp.s.reset_input_buffer()
        esp.send('<GCLINE:G1 Z-30 F200>'); time.sleep(2.0)
        esp.send('<GCLINE:STOP>'); time.sleep(1.5)
        buf = esp.s.read(8192).decode(errors='replace')
        mz = mega.pos()[0]
        stopped = 'ОСТАНОВЛЕНО' in buf and -30000 < mz < -2000
        print(f'  {"OK  " if stopped else "FAIL"} стоп посреди хода: каретка Z {mz:+d} мкм')
        if not stopped:
            fails.append('стоп')
        line('G0 Z0 X0')
        # вне M8 — отказ
        esp.btn('M1'); time.sleep(0.6)
        r = line('G0 Z-1')
        print(f'  {"OK  " if "M8" in r else "FAIL"} вне M8: {r}')
        if 'M8' not in r:
            fails.append('вне M8 принято')
    finally:
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            try:
                stm.release(p)
            except Exception:
                pass
        try:
            mega.cmd('f0'); mega.cmd('w1')
        except Exception:
            pass
        stm.close()
    print(f'\nИТОГ: {"OK" if not fails else "FAIL: " + ", ".join(fails)}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
