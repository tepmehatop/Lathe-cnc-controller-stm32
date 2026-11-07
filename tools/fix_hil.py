#!/usr/bin/env python3
"""
Доводка X по линейке и контроль пропуска шагов на стенде (BACKLOG-025).

Mega следует за моторами (STEP/DIR → D2–D5, см. follow_hil.py) с люфтом X и выдаёт положение каретки как блок
DRO. Цикл «подача наружн., 3 прохода × 0.20» (по радиусу 0.30 мм) — ноль взят после отхода от детали (худший
случай для люфта). Реальная глубина — по каретке Mega: максимум за цикл минус положение на нуле цикла.
Прогоны: доводка выключена, доводка включена (POS_SET; STM32 перезапускается). Затем контроль пропуска шагов:
Mega теряет 200 шагов X посреди хода (dx200) — на экране «ПРОПУСК ШАГОВ X», джойстик заблокирован, пока его не
отпустят. В конце — настройки из els_config.h (POS_DEFAULT).

Запуск: python tools/fix_hil.py [--bl 100]  (Python из PlatformIO: нужны pyserial и OpenOCD)
"""
import argparse, sys, threading, time
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega

CASE = ('подача наружн., 3 прохода × 0.20', 'M1', 'S3', 600, ['FEED:20', 'AP:20', 'PASSES:3'])
EXPECT_UM = 300


def pos_set(esp, fix, loss):
    # источник — блок DRO, линейки не используются (значения из els_config.h)
    esp.btn(f'POS_SET:1,1,5000,0,0,1,5000,0,0,{fix},{loss},150')
    time.sleep(4)                                     # STM32 сохраняет и перезапускается


def run(stm, esp, mega, sp, fix):
    problems = []
    # после перезапуска STM32 направление линейки неизвестно — провести X больше 2 мм, как перед работой
    stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
    for pin in ('JOY_UP', 'JOY_DOWN'):
        stm.press(pin); time.sleep(1.2); stm.release(pin); time.sleep(0.6)
    hc.set_limits(stm, esp, problems)
    hc.prepare(stm, esp, CASE[1], CASE[4])
    esp.btn(CASE[2]); time.sleep(1.0)
    x_null = mega.pos()[1]
    if 'e2_scale_sign' in stm.sym:
        print(f'    перед циклом: знак линейки {stm.read(stm.sym["e2_scale_sign"][0], 2, 8)}, X {stm.var("Motor_X_Pos")}, '
              f'Null_X {stm.var("Null_X_Pos")}, Mega x {x_null}')
    xs, stop = [], threading.Event()

    def sample():
        while not stop.is_set():
            try:
                xs.append(mega.pos()[1])
            except Exception:
                pass
    th = threading.Thread(target=sample); th.start()
    sp.on(CASE[3]); time.sleep(0.8)
    stm.press('JOY_LEFT')
    t0 = time.time()
    while time.time() - t0 < 240 and not stm.var('Complete_flag'):
        time.sleep(0.5)
    done = bool(stm.var('Complete_flag'))
    time.sleep(2.5)
    stm.release('JOY_LEFT'); sp.off()
    stop.set(); th.join()
    esp.btn('S2'); time.sleep(0.6)
    depth = max(xs) - x_null if xs else 0
    st = {}
    for n, c, w in (('e2_scale_sign', 2, 8), ('e2_null_ok', 1, 8), ('e2_fixed_pass', 1, 32), ('e2_loss_block', 1, 8)):
        if n in stm.sym:
            st[n] = stm.read(stm.sym[n][0], c, w)
    print(f'    состояние доводки: {st}')
    print(f'    доводка {"ВКЛ " if fix else "ВЫКЛ"}: глубина по каретке {depth} мкм (задано {EXPECT_UM}), '
          f'цикл {"завершён" if done else "НЕ завершён"}, проблемы: {problems or "нет"}')
    return depth, done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--bl', type=int, default=100, help='люфт X на Mega, мкм')
    a = ap.parse_args()
    stm, esp, mega = cd.Stm32(), cd.Esp(a.esp), Mega(a.mega)
    sp = hc.Spindle(stm)
    fails = []
    try:
        for c in ('w0', 'kz2500', 'kx1250', 'lz0', f'lx{a.bl}', 'f1', 'o'):
            mega.cmd(c)
        print(f'[люфт X на Mega {a.bl} мкм, компенсации нет]')
        for fix in (0, 1):
            pos_set(esp, fix, fix)              # с доводкой — и контроль шагов: ложных срабатываний быть не должно
            depth, done = run(stm, esp, mega, sp, fix)
            if not done:
                fails.append(f'доводка {fix}: цикл не завершился')
            if fix and abs(depth - EXPECT_UM) > 10:
                fails.append(f'с доводкой глубина {depth} мкм, задано {EXPECT_UM}')
        # контроль пропуска шагов: без люфта на Mega (ложных срабатываний на развороте нет)
        mega.cmd('lx0')
        pos_set(esp, 1, 1)
        stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
        # направление линейки, опорная точка; последний ход — в ту же сторону, что и ход с пропуском
        # (без заданного люфта разворот не сверяется)
        for pin in ('JOY_UP', 'JOY_DOWN', 'JOY_DOWN'):
            stm.press(pin); time.sleep(1.2); stm.release(pin); time.sleep(0.8)
        stm.press('JOY_DOWN'); time.sleep(0.4); mega.cmd('dx200'); time.sleep(0.6); stm.release('JOY_DOWN')
        time.sleep(1.2)
        ui = esp.uidump()
        blocked = stm.read(stm.sym['e2_loss_block'][0], 1, 8)[0]
        print(f'    пропуск 200 шагов X: блок {blocked}, сообщение «{cd.norm(ui.get("alert_msg", ""))}»')
        if not blocked or 'ПРОПУСК' not in cd.norm(ui.get('alert_msg', '')):
            fails.append('пропуск шагов не замечен')
        m = stm.var('Motor_X_Pos')
        stm.press('JOY_UP'); time.sleep(0.5)
        if stm.var('Motor_X_Pos') != m:
            fails.append('при блокировке джойстик двигает X')
        stm.release('JOY_UP'); time.sleep(1.6)
        if stm.read(stm.sym['e2_loss_block'][0], 1, 8)[0]:
            fails.append('блокировка не снялась после отпускания джойстика')
        esp.btn('ALERT_OK'); time.sleep(0.5)
    finally:
        try:
            sp.off()
        except Exception:
            pass
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            try:
                stm.release(p)
            except Exception:
                pass
        try:
            esp.btn('POS_DEFAULT'); time.sleep(4); mega.cmd('lx0'); mega.cmd('f0'); mega.cmd('w1')   # настройки из els_config.h
        except Exception:
            pass
        stm.close()
    print(f'\nИТОГ: {"OK" if not fails else "; ".join(fails)}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
