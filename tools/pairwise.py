#!/usr/bin/env python3
"""
Попарное (pairwise) покрытие: набор строк, в котором каждая пара значений любых двух параметров встречается хотя бы
раз. Полный перебор параметров ниже — 6 × 3 × 2 × 3 × 3 × 2 = 648 прогонов; попарно — около 20.

  python3 tools/pairwise.py            — таблица сочетаний для tools/hil_pairwise.py

Жадный алгоритм (как AETG): каждый раз берём строку, которая закрывает больше всего ещё не покрытых пар.
Сочетания, которых не бывает на станке (шар «Внутренний» — «Режим невозможен!»), остаются в таблице как негативные.
"""
import itertools, random

PARAMS = {
    'mode':   ['M1', 'M2', 'M3', 'M4', 'M5', 'M6'],                    # подача, асинхрон, резьба, конусы, шар
    'sub':    ['MAN', 'EXT', 'INT'],                                    # Ручной / Наружный / Внутренний
    'source': ['STEPS', 'DRO'],                                         # без линеек / блок DRO
    'mech':   ['default', 'enc3600', 'screw3'],                         # механика (наборы 1, 2, 3 из hil_mech)
    'feed':   ['min', 'mid', 'max'],                                    # подача: 0.05 / 0.13 / 0.25 мм/об (M2 — 50/200/400)
    'spindle': ['CW', 'CCW'],                                           # направление шпинделя
}


def pairwise(params, seed=1, tries=60):
    names = list(params)
    uncovered = set()
    for a, b in itertools.combinations(range(len(names)), 2):
        for va in params[names[a]]:
            for vb in params[names[b]]:
                uncovered.add((a, va, b, vb))
    rnd = random.Random(seed)
    rows = []
    while uncovered:
        best, best_cov = None, -1
        for _ in range(tries):
            # начать с непокрытой пары, остальное — жадно по числу закрываемых пар
            a, va, b, vb = rnd.choice(sorted(uncovered, key=str))
            row = [None] * len(names)
            row[a], row[b] = va, vb
            order = [i for i in range(len(names)) if row[i] is None]
            rnd.shuffle(order)
            for i in order:
                def gain(v):
                    row[i] = v
                    g = sum(1 for j in range(len(names)) if row[j] is not None and j != i and
                            ((min(i, j), row[min(i, j)], max(i, j), row[max(i, j)]) in uncovered))
                    row[i] = None
                    return g
                row[i] = max(params[names[i]], key=gain)
            cov = sum(1 for x, y in itertools.combinations(range(len(names)), 2)
                      if (x, row[x], y, row[y]) in uncovered)
            if cov > best_cov:
                best, best_cov = list(row), cov
        for x, y in itertools.combinations(range(len(names)), 2):
            uncovered.discard((x, best[x], y, best[y]))
        rows.append(dict(zip(names, best)))
    return rows


def negative(row):
    """Сочетание, которого не бывает: ожидается отказ ядра, а не движение."""
    if row['mode'] == 'M6' and row['sub'] == 'INT':
        return 'шар «Внутренний» — «Режим невозможен!»'
    return None


if __name__ == '__main__':
    rows = pairwise(PARAMS)
    full = 1
    for v in PARAMS.values():
        full *= len(v)
    print(f'полный перебор — {full}, попарно — {len(rows)} строк')
    print('  №  ' + '  '.join(f'{k:8s}' for k in PARAMS) + '  ожидание')
    for i, r in enumerate(rows, 1):
        print(f'{i:3d}  ' + '  '.join(f'{r[k]:8s}' for k in PARAMS) + '  ' + (negative(r) or 'движение по расчёту'))
