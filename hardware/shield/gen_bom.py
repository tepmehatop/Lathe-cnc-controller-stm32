#!/usr/bin/env python3
"""Список деталей шилда (bom.csv и bom.md) из design.py. Запуск: python3 hardware/shield/gen_bom.py"""
import csv, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design

HERE = os.path.dirname(os.path.abspath(__file__))

# Что покупать — человеческие названия
BUY = {
    'Connector_PinHeader_2.54mm:PinHeader_2x25_P2.54mm_Vertical': 'Гнездо (female) 2×25, шаг 2.54 мм, прямое',
    'Resistor_THT:R_Array_SIP9': 'Резисторная сборка SIP-9, 8 резисторов с общим выводом',
    'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal': 'Резистор 0.25 Вт выводной',
    'Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm': 'Конденсатор керамический, шаг 2.5 мм',
    'Capacitor_THT:CP_Radial_D6.3mm_P2.50mm': 'Конденсатор электролитический D6.3 мм, шаг 2.5 мм',
    'Package_TO_SOT_THT:TO-92_Inline_Wide': 'Транзистор NPN 2N3904, корпус TO-92 (выводы Э-Б-К)',
    'MountingHole:MountingHole_3.2mm_M3': 'Отверстие под винт M3 (не деталь)',
}


def buy_name(fp):
    if fp in BUY:
        return BUY[fp]
    if 'MKDS' in fp:
        return 'Клеммник винтовой 2 контакта, шаг 5.08 мм (Phoenix MKDS 1,5/2 или аналог KF301-2P)'
    if 'MPT' in fp:
        n = fp.split('_1x')[1][:2]
        return f'Клеммник винтовой {int(n)} контактов, шаг 2.54 мм (Phoenix MPT 0,5 или аналог KF128/DG128)'
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
