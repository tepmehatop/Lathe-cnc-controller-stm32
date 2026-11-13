#!/usr/bin/env python3
"""
Снимки автоцикла в работе для руководства (docs/manual): конус 1:10 и шар — как в примерах руководства.

Стенд как у tools/hil_cycles.py: STM32 по ST-Link, дисплей 10.1" по USB, Mega — блок DRO, шпиндель — TIM5 на PA1.
Упоры ставятся кнопками пульта (SWD), параметры — командами экрана, цикл — джойстиком влево.
Снимок — на заданном проходе; заодно проверяется, куда идёт резец по X на проходе конуса.

  ~/.platformio/penv/bin/python tools/docs_gen/cycle_shots.py [cone|sphere]
Результат — docs/manual/raw/cone_cycle.jpg, sphere_cycle.jpg; стрелки — annotate.py.
"""
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import compare_displays as cd
import hil_cycles as hc
from manual_shots import Bench

CONE_1_10 = 55          # индекс «1:10» в Cone_Info (e2_core.cpp)


def run(b, name, mode, prep, sm2_keys, rpm, shot_at, until_done=True):
    stm, esp = b.stm, b.esp
    problems = []
    esp.btn('M2'); time.sleep(0.5); esp.btn('AFEED:300'); time.sleep(0.4)   # отгон между упорами — быстро
    hc.set_limits(stm, esp, problems)
    hc.prepare(stm, esp, mode, prep)
    if sm2_keys:                                   # подэкран 2 — параметры, потом обратно на 1
        b.mode(int(mode[1]), 2)
        for k in sm2_keys:
            esp.btn(k); time.sleep(0.25)
        b.mode(int(mode[1]), 1)
    # подрежим — галетником, как на станке (иначе на снимке подсказка «галетник: Ручной»)
    stm.release('SUB_MAN'); time.sleep(0.3); stm.press('SUB_EXT'); time.sleep(1.0)
    if stm.var('err_1_flag') or stm.var('err_2_flag'):
        problems.append('подрежим «Наружный» не включился')
        stm.release('SUB_EXT')
        return problems
    if rpm:
        b.sp.on(rpm); time.sleep(0.8)
    x_track = []
    stm.press('JOY_LEFT')
    t0, shot = time.time(), False
    try:
        while time.time() - t0 < 400:
            time.sleep(0.3)
            if stm.var('Complete_flag'):
                break
            nr = stm.var('Pass_Nr')
            if os.environ.get('CS_DEBUG'):
                print('   ', round(time.time() - t0, 1), nr, stm.var('Motor_Z_Pos') - stm.var('Limit_Pos_Right'), stm.var('Motor_X_Pos'))
            x_track.append((nr, stm.var('Motor_Z_Pos'), stm.var('Motor_X_Pos')))
            if not shot and nr == shot_at:
                # середина прохода: конус — суппорт ушёл от правого упора; шар — резец врезается по X
                # (в шаре ядро ставит правый упор = левому на каждом проходе)
                busy = stm.var('Step_X_flag') if mode == 'M6' else \
                    abs(stm.var('Motor_Z_Pos') - stm.var('Limit_Pos_Right')) > 300
                if busy:
                    b.shot(name); shot = True
                    if not until_done:
                        break
    finally:
        stm.release('JOY_LEFT')
        b.sp.off()
        stm.release('SUB_EXT'); time.sleep(0.3); stm.press('SUB_MAN'); time.sleep(0.6); stm.release('SUB_MAN')
        esp.btn('ALERT_OK'); time.sleep(0.4)
    if not shot:
        problems.append('снимок не сделан')
    # направление X на проходе: Z уменьшается/увеличивается → X растёт/падает
    pz = [t for t in x_track if t[0] == shot_at]
    if len(pz) > 3:
        print(f'  проход {shot_at}: Z {pz[0][1]}→{pz[-1][1]}, X {pz[0][2]}→{pz[-1][2]}')
    return problems


def cone(b):
    return run(b, 'cone_cycle', 'M5', [f'CONE:{CONE_1_10}', 'FEED:10', 'AP:50', 'PASSES:8'], [], 400, 3)


def sphere(b):
    # шар ⌀20 (радиус 10.00), ножка ⌀8 (радиус 4.00), резец 3.00 (▲ ×4 от 2.00), шаг Z 0.50 (по умолчанию)
    return run(b, 'sphere_cycle', 'M6', ['SPHERE:1000', 'BAR:400'], ['KEY:UP'] * 4, 800, 3, until_done=False)


if __name__ == '__main__':
    b = Bench()
    # Mega — как настоящий блок DRO: следует за моторами (открытие порта её перезапускает в «качание», и тогда
    # доводка X по линейке и контроль пропуска шагов справедливо останавливают цикл)
    for c in ('w0', 'kz2500', 'kx1250', 'lz0', 'lx0', 'f1', 'o'):
        b.mega.cmd(c)
    try:
        for n in (sys.argv[1:] or ['cone', 'sphere']):
            print('[', n, ']')
            for p in globals()[n](b):
                print('   ❗', p)
    finally:
        b.close()
