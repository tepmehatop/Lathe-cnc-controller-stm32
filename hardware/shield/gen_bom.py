#!/usr/bin/env python3
"""Список деталей шилда (bom.csv и bom.md) из design.py. Запуск: python3 hardware/shield/gen_bom.py"""
import csv, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design

HERE = os.path.dirname(os.path.abspath(__file__))

# Что покупать — человеческие названия
BUY = {
    'Connector_PinHeader_2.54mm:PinHeader_2x25_P2.54mm_Vertical': 'Гнездо (female) 2×25, шаг 2.54 мм, прямое — под Discovery',
    'Resistor_SMD:R_0805_2012Metric': 'Резистор SMD 0805, 5 %',
    'Capacitor_SMD:C_0805_2012Metric': 'Конденсатор керамический SMD 0805, X7R, 16 В и выше',
    'Capacitor_SMD:C_1206_3216Metric': 'Конденсатор керамический SMD 1206, X5R',
    'Package_TO_SOT_SMD:SOT-23': 'Транзистор NPN SMD SOT-23 (MMBT3904, маркировка 1A; аналог MMBT2222A)',
    'Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical': 'Вилка IDC 2×5 в корпусе (box header), шаг 2.54 мм, прямая',
    'MountingHole:MountingHole_3.2mm_M3': 'Отверстие под винт M3 (не деталь)',
}


def buy_name(fp):
    if fp in BUY:
        return BUY[fp]
    if 'KK-254' in fp:
        n = int(fp.split('_1x')[1][:2])
        return f'Вилка {n} конт., шаг 2.54 мм, прямая, с защёлкой (KF2510-{n}P / Molex KK-254) + ответная часть'
    return fp


def main():
    groups = {}
    for p in design.PARTS:
        if p['ref'].startswith('H'):
            continue
        key = (buy_name(p['fp']), p['value'] if not p['ref'].startswith('J') or p['ref'] in ('J1', 'J2') else '')
        groups.setdefault(key, []).append(p['ref'])
    rows = sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1]))
    with open(os.path.join(HERE, 'bom.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Количество', 'Деталь', 'Номинал', 'Позиции'])
        for (name, val), refs in rows:
            w.writerow([len(refs), name, val, ' '.join(refs)])
    with open(os.path.join(HERE, 'bom.md'), 'w', encoding='utf-8') as f:
        f.write('# Список деталей шилда\n\nСгенерировано `gen_bom.py` из `design.py`.\n\n')
        f.write('| Кол-во | Деталь | Номинал | Позиции |\n|---|---|---|---|\n')
        for (name, val), refs in rows:
            f.write(f'| {len(refs)} | {name} | {val} | {" ".join(refs)} |\n')
    print('bom.csv, bom.md —', sum(len(r) for _, r in rows), 'деталей')


if __name__ == '__main__':
    main()
