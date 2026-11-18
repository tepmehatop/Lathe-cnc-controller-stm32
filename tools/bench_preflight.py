#!/usr/bin/env python3
"""
Проверка стенда перед автотестами: прерванный тест не должен оставить станок в чужом состоянии.

  - входы пульта не «нажаты» (Stm32() отпускает оставшиеся);
  - источник координат — блок DRO (POS_SOURCE_DRO_UART = 1); иначе POS_DEFAULT (из els_config.h сборки stm32f407_dro);
  - пакеты от блока DRO (Mega) приходят;
  - ось Z проехала (M2, джойстик влево 1.5 с) — и координата Z на экране изменилась.
Код выхода 0 — стенд готов.

  ~/.platformio/penv/bin/python tools/bench_preflight.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega

ESP, MEGA = '/dev/cu.usbmodem14801', '/dev/cu.usbserial-1410'


def main():
    stm = cd.Stm32()
    problems = []
    try:
        hc.Spindle(stm).off()
        esp = cd.Esp(ESP); time.sleep(20)
        src = lambda: stm.read(stm.sym['e2_pos'][0], 1, 8)[0]
        pkts = lambda: stm.read(stm.sym['s_pkt_ok'][0], 1, 32)[0]
        if src() != 1:
            print(f'[предполёт] источник координат {src()} (не блок DRO) — возвращаю из конфига', flush=True)
            esp.btn('POS_DEFAULT'); time.sleep(12)
            if src() != 1:
                problems.append(f'источник координат {src()}, POS_DEFAULT не помог (станок «занят»? Joy_Z={stm.var("Joy_Z_flag")})')
        mega = Mega(MEGA)
        for c in ('w0', 'kz2500', 'kx1250', 'lz0', 'lx0', 'f1', 'o'):
            mega.cmd(c)
        p0 = pkts(); time.sleep(1.0)
        if pkts() == p0:
            problems.append('пакеты от блока DRO не приходят')
        esp.send('<UIRESET>'); time.sleep(0.5)
        esp.btn('M2'); time.sleep(0.6); esp.btn('S2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.4)
        for _ in range(4):                      # подэкран 1: на 3/4 в первой строке не позиция Z (ввод ⌀, «ДО УПОРА»)
            if stm.var('SelectMenu') == 1:
                break
            esp.btn('PARAM_OK'); time.sleep(0.6)
        z0 = esp.uidump().get('row1_val')
        m0, d0 = stm.var('Motor_Z_Pos'), stm.var('dro_pos_y')
        stm.press('JOY_LEFT'); time.sleep(1.5); stm.release('JOY_LEFT'); time.sleep(1.5)
        u = esp.uidump(); z1 = u.get('row1_val')
        diag = (f'режим {stm.var("Mode")}, подэкран {stm.var("SelectMenu")}, {u.get("row1_title")}, шагов Z '
                f'{stm.var("Motor_Z_Pos") - m0:+d}, DRO {stm.var("dro_pos_y") - d0:+d} мкм, окно: {u.get("alert_msg") if u.get("alert") == "1" else "-"}')
        stm.press('JOY_RIGHT'); time.sleep(1.5); stm.release('JOY_RIGHT'); time.sleep(1.0)
        if z0 == z1:
            problems.append(f'Z на экране не изменилась при движении ({z0} → {z1}; {diag})')
        if stm.var('Joy_Z_flag') or stm.var('Joy_X_flag'):
            problems.append('после отпускания джойстика ядро считает ось в движении')
        print(f'[предполёт] источник {src()}, DRO пакетов {pkts()}, Z на экране {z0} → {z1}', flush=True)
        esp.btn('M1'); time.sleep(0.5)
    finally:
        stm.close()
    for p in problems:
        print('[предполёт] FAIL', p, flush=True)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
