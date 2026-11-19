#!/usr/bin/env python3
"""
Автоцикл резьбы «как на станке» по проходам: заготовка известного ⌀ (блок DRO — эмулятор на Mega, ⌀ задаётся
положением линейки X), резец у заготовки, резьба из таблицы, профиль, наружная, джойстик — до «ОПЕРАЦИЯ ЗАВЕРШЕНА».

Каждый проход сверяется:
  - врезание (Limit_Pos_Front − ноль X) — по формуле автора для профиля (Thread.ino: глубина·√(n−1)/√(N−1),
    первый проход √0.3; шагов = round(глубина / мм-на-шаг)·микрошаг + микрошаг);
  - каретка X дошла до врезания;
  - ⌀ на экране в самой глубокой точке прохода = ⌀ заготовки − 2 × глубина (±0.02 мм);
  - «проходы» на экране = Pass_Nr/Pass_Total ядра (экран не отстаёт от ядра больше чем на проход);
  - Z — между упорами, позиция Z на экране — в тех же пределах;
и в конце: проходов столько, сколько в таблице + чистовой (PASS_FINISH = 1), итоговая глубина = полная глубина
профиля, ⌀ после резьбы = ⌀ − 2 × полная глубина, каретка вернулась к старту.

  ~/.platformio/penv/bin/python tools/hil_trace.py [--only m15,...]
Стенд: STM32 (ST-Link), дисплей по USB, Mega — эмулятор DRO (следует за моторами). Линейка X в тесте
повёрнута (kx−1250), как на станке, где блок DRO настроен: врезание уменьшает ⌀; после теста — как было.
"""
import argparse, math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_v38 as H
from hil_v38_ui import thread_table

chk = H.chk
REBOUND_X = 200                                          # отскок резца, микрошагов (els_config.h)
PROF_K = {1: 0.61342, 2: 0.640327, 3: 0.61342}
PROF_NAME = {1: 'метрич. 60°', 2: 'BSW 55°', 3: 'UNF 60°', 4: 'трапеция 30°'}


def full_depth(step, kzr):
    if kzr in PROF_K:
        return step * PROF_K[kzr]
    return step * 0.5 + (0.15 if step < 2.0 else 0.25 if step <= 5.0 else 0.5)


def infeed_steps(depth, mech):
    """Шагов врезания: как Infeed_Value в Thread.ino"""
    x_steps, x_mc, x_screw = mech[4], mech[5], mech[6]
    return int(depth / (x_screw / 100 / x_steps) + 0.5) * x_mc + x_mc


def run(B, name, idx, kzr, d0_mm, rpm=200):
    tab = thread_table()
    label, step, passes, _ = tab[idx]
    N = passes                                           # Pass_Total = Pass из таблицы (+ Thr_Pass_Summ = 0)
    fd = full_depth(step, kzr)
    print(f'[{name}] резьба {label.strip()} (шаг {step} мм), {PROF_NAME[kzr]}, ⌀{d0_mm:.2f}; глубина {fd:.3f} мм, '
          f'проходов {N} + чистовой', flush=True)
    mech = B.stm.read(B.stm.sym['e2_mech'][0], 9, 32)
    mm_per_step_x = mech[6] / 100 / mech[4] / mech[5]   # мм радиуса на шаг мотора X
    # упоры Z (20 мм), резец у заготовки: линейка X — радиус заготовки (⌀ на экране = d0)
    B.b('M1'); B.manual(); B.clear_limits()
    # линейка X — как на настроенном станке: врезание (шаги X+) уменьшает показание; x — радиус, мкм (⌀ = 2·X)
    B.mega.cmd('kx-1250'); B.mega.cmd('o'); B.mega.cmd(f'x{int(d0_mm * 500)}'); time.sleep(0.8)
    u = B.e.uidump()
    chk(abs(float(u.get('diam_val') or 0) - d0_mm) <= 0.01, f'⌀ заготовки на экране {u.get("diam_val")} (ожидалось {d0_mm:.2f})')
    done, x0, xmax, xmin, sec = None, 0, 0, 0, 0
    log = {}                                             # проход → {lim, xmax, diam_min, ui_pass}
    zbad, uibad = [], []

    def tick(t):
        n = B.v('Pass_Nr') - 1                           # Pass_Nr++ сразу после расчёта врезания
        if n < 1:
            return
        r = log.setdefault(n, {'lims': [], 'xmax': -10**9, 'dmin': 1e9, 'dmax': -1e9, 'ui': set(), 'z': []})
        # Pass_Nr и врезание — два чтения: между ними ядро может перейти на следующий проход (а после цикла упор
        # уходит в Limit_Pos_Max) — берём самое частое значение за проход
        r['lims'].append(B.v('Limit_Pos_Front') - B.v('Null_X_Pos'))
        x = B.v('Motor_X_Pos'); r['xmax'] = max(r['xmax'], x - B.v('Null_X_Pos'))
        z = B.v('Motor_Z_Pos')
        L, R = B.v('Limit_Pos_Left'), B.v('Limit_Pos_Right')
        if not (min(L, R) - 8 <= z <= max(L, R) + 8):
            zbad.append(z)
        if int(t * 10) % 5 == 0:                         # экран — раз в ~0.5 с (UIDUMP небыстрый)
            u = B.e.uidump()
            try:
                dv = float(u.get('diam_val')); r['dmin'] = min(r['dmin'], dv); r['dmax'] = max(r['dmax'], dv)
            except (TypeError, ValueError):
                pass
            r['ui'].add(u.get('row4_val'))
            # на экране «осталось/всего»: всего = Pass_Total + чистовой; осталось = всего − (Pass_Nr − 1)
            # (экран — эхо ядра раз в ~100 мс: допускаем «на проход позади»)
            T = B.v('Pass_Total') + 1; cn = B.v('Pass_Nr') - 1
            got = u.get('row4_val') or ''
            try:
                gr, gt = (int(x) for x in got.split('/'))
                if gt != T or not (T - cn <= gr <= T - cn + 1):
                    uibad.append(f'экран {got}, ядро проход {cn} из {T}')
            except ValueError:
                uibad.append(f'экран {got!r}')

    B.b('KZR:%d' % kzr)
    done, x0, xmax, xmin, sec = H.thread_cycle(B, idx, 'S3', kzr, rpm=rpm, tick=tick, timeout=900)
    chk(done, f'цикл завершён за {sec:.0f} с')
    rows = []
    for n in sorted(log):
        r = log[n]
        r['lim'] = max(set(r['lims']), key=r['lims'].count) if r['lims'] else None
        if n <= N:
            dep = fd / math.sqrt(N - 1) * math.sqrt(0.3 if n == 1 else n - 1)
        else:
            dep = fd                                     # чистовой — на полную глубину ещё раз
        want = infeed_steps(dep, mech)
        want_d = d0_mm - 2 * (want * mm_per_step_x)
        ok = r['lim'] == want and abs(r['xmax'] - want) <= mech[5] and abs(r['dmin'] - want_d) <= 0.02 and r['dmax'] <= d0_mm + 2 * REBOUND_X * mm_per_step_x + 0.02
        rows.append(ok)
        chk(ok, f'проход {n}{" (чистовой)" if n > N else ""}: глубина {dep:.3f} мм → врезание {r["lim"]} шагов '
                f'(ожидалось {want}), X дошёл до {r["xmax"]}, ⌀ на экране {r["dmin"]:.2f}..{r["dmax"]:.2f} (в резе ожидалось {want_d:.2f}, на отводе не больше {d0_mm + 2 * REBOUND_X * mm_per_step_x:.2f}), '
                f'проходы на экране {sorted(x for x in r["ui"] if x)}')
    chk(len(log) == N + 1, f'проходов {len(log)} (таблица {N} + чистовой 1)')
    chk(not zbad, f'Z всё время между упорами' + ('' if not zbad else f': выход {zbad[:5]}'))
    chk(not uibad, f'проходы на экране совпадают с ядром' + ('' if not uibad else f': {uibad[:5]}'))
    fin = infeed_steps(fd, mech)
    chk(abs((xmax - x0) - fin) <= mech[5], f'итоговая глубина {xmax - x0} шагов = полная глубина профиля {fd:.3f} мм ({fin} шагов)')
    u = B.e.uidump()
    chk(abs(float(u.get('diam_val') or 0) - d0_mm) <= 0.02, f'после цикла резец вернулся: ⌀ на экране {u.get("diam_val")} (⌀ заготовки {d0_mm:.2f})')
    B.mega.cmd('o'); B.mega.cmd('kx1250')


CASES = {
    'm15': ('М ⌀10 × 1.5, метрический', 13, 1, 10.0),
    'm10': ('М ⌀6 × 1.0, метрический', 10, 1, 6.0),
    'bsw': ('дюймовая 20tpi BSW ⌀12.7', None, 2, 12.7),
    'unf': ('дюймовая 20tpi UNF ⌀12.7', None, 3, 12.7),
    'tr2': ('трапеция 2.0 ⌀16', None, 4, 16.0),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    a = ap.parse_args()
    tab = thread_table()
    find = lambda lbl: next(i for i, t in enumerate(tab) if t[0].strip() == lbl)
    idx = {'bsw': find('20tpi'), 'unf': find('20tpi'), 'tr2': find('2.00mm'),
           'm15': find('1.50mm'), 'm10': find('1.00mm')}
    B = H.Bench()
    only = a.only.split(',') if a.only else list(CASES)
    for k in only:
        name, _, kzr, d0 = CASES[k]
        try:
            run(B, name, idx[k], kzr, d0)
        except Exception as ex:
            chk(False, f'{k}: исключение {ex!r}')
            B.stm.release('JOY_LEFT'); B.sp.off()
    B.b('KZR:1'); B.b('M1')
    B.stm.close()
    fails = [m for ok, m in H.res if not ok]
    print('\nИТОГ:', f'{len(H.res)} проверок,', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
