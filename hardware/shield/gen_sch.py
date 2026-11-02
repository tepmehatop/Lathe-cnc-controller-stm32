#!/usr/bin/env python3
"""
Схема шилда в KiCad 8 (els_shield.kicad_sch) из design.py.
Символы — из штатных библиотек KiCad; связи — метками цепей на выводах.
Запуск: python3 hardware/shield/gen_sch.py
"""
import os, sys, uuid
sys.path.insert(0, os.path.dirname(__file__))
import design
from sexpr import Q, parse, dump, find, walk

HERE = os.path.dirname(os.path.abspath(__file__))
SYMDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'
NS = uuid.UUID('6f1a3c52-9f3e-4b1e-9d6a-0e2c7e5a1b11')
ROOT_UUID = str(uuid.uuid5(NS, 'root-sheet'))


def uid(*parts):
    return Q(str(uuid.uuid5(NS, '/'.join(map(str, parts)))))


_libs = {}


def lib_symbol(lib_id):
    lib, name = lib_id.split(':')
    if lib not in _libs:
        _libs[lib] = parse(open(os.path.join(SYMDIR, lib + '.kicad_sym'), encoding='utf-8').read())
    for s in find(_libs[lib], 'symbol'):
        if s[1] == name:
            return s
    raise KeyError(lib_id)


def pins_of(sym):
    """Номер вывода → (x, y, угол) точки подключения в координатах символа."""
    out = {}
    for p in walk(sym, 'pin'):
        at = find(p, 'at')[0]
        num = find(p, 'number')[0][1]
        out[str(num)] = (float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0)
    return out


def bbox(pins):
    xs = [p[0] for p in pins.values()] or [0]
    ys = [p[1] for p in pins.values()] or [0]
    return min(xs), max(xs), min(ys), max(ys)


def snap(v, g=2.54):
    return round(v / g) * g


def main():
    used = {}
    for p in design.PARTS:
        used.setdefault(p['sym'], lib_symbol(p['sym']))

    # Размещение: строками слева направо, группами по префиксу ref
    order = sorted(design.PARTS, key=lambda p: ({'J': 0, 'R': 1, 'Q': 2, 'C': 3, 'H': 4}.get(p['ref'][0], 5) if not p['ref'].startswith('RN') else 1,
                                                 int(''.join(c for c in p['ref'] if c.isdigit()))))
    W, x, y, row_h = 560.0, 25.4, 30.48, 0.0
    placed = []
    for p in order:
        pins = pins_of(used[p['sym']])
        x0, x1, y0, y1 = bbox(pins)
        w = (x1 - x0) + 45.0          # место под метки цепей с обеих сторон
        h = (y1 - y0) + 20.0
        if x + w > W:
            x, y, row_h = 25.4, y + row_h, 0.0
        cx = snap(x - x0 + 22.0)
        cy = snap(y + y1 + 8.0)       # ось Y символа направлена вверх
        placed.append((p, cx, cy, pins))
        x += w
        row_h = max(row_h, h)

    sch = ['kicad_sch', ['version', '20231120'], ['generator', Q('els_shield_gen')], ['generator_version', Q('8.0')],
           ['uuid', Q(ROOT_UUID)], ['paper', Q('A2')],
           ['title_block', ['title', Q('Шилд ELS под STM32F4DISCOVERY')], ['rev', Q('1')],
            ['comment', '1', Q('Сгенерировано hardware/shield/gen_sch.py из design.py')],
            ['comment', '2', Q('Связи — метками цепей на выводах')]]]
    libs = ['lib_symbols']
    for lib_id, s in used.items():
        s2 = list(s)
        s2[1] = Q(lib_id)
        libs.append(s2)
    sch.append(libs)

    for p, cx, cy, pins in placed:
        ref = p['ref']
        su = uid('sym', ref)
        sym = ['symbol', ['lib_id', Q(p['sym'])], ['at', cx, cy, 0], ['unit', 1],
               ['exclude_from_sim', 'no'], ['in_bom', 'yes' if not ref.startswith('H') else 'no'], ['on_board', 'yes'], ['dnp', 'no'],
               ['uuid', su],
               ['property', Q('Reference'), Q(ref), ['at', cx, cy - 2.54, 0], ['effects', ['font', ['size', 1.27, 1.27]]]],
               ['property', Q('Value'), Q(p['value']), ['at', cx, cy + 2.54, 0], ['effects', ['font', ['size', 1.27, 1.27]]]],
               ['property', Q('Footprint'), Q(p['fp']), ['at', cx, cy, 0], ['effects', ['font', ['size', 1.27, 1.27]], ['hide', 'yes']]],
               ['property', Q('Datasheet'), Q('~'), ['at', cx, cy, 0], ['effects', ['font', ['size', 1.27, 1.27]], ['hide', 'yes']]],
               ['property', Q('Description'), Q(p['note']), ['at', cx, cy, 0], ['effects', ['font', ['size', 1.27, 1.27]], ['hide', 'yes']]]]
        for num in pins:
            sym.append(['pin', Q(num), ['uuid', uid('pin', ref, num)]])
        sym.append(['instances', ['project', Q('els_shield'), ['path', Q('/' + ROOT_UUID), ['reference', Q(ref)], ['unit', 1]]]])
        sch.append(sym)

        for num, (px, py, ang) in pins.items():
            X, Y = cx + px, cy - py
            net = p['pins'].get(int(num)) if num.isdigit() else None
            if net is None:
                sch.append(['no_connect', ['at', X, Y], ['uuid', uid('nc', ref, num)]])
                continue
            la = (ang + 180) % 360
            just = 'left' if la in (0.0, 90.0) else 'right'
            sch.append(['label', Q(net), ['at', X, Y, la],
                        ['effects', ['font', ['size', 1.0, 1.0]], ['justify', just, 'bottom']],
                        ['uuid', uid('lbl', ref, num)]])

        # подпись назначения клеммника
        if p['ref'].startswith('J') and p['note']:
            sch.append(['text', Q(p['note']), ['exclude_from_sim', 'no'], ['at', cx - 20.32, cy + bbox(pins)[3] * -1 - 7.62, 0],
                        ['effects', ['font', ['size', 1.27, 1.27]], ['justify', 'left', 'bottom']], ['uuid', uid('txt', ref)]])

    sch.append(['sheet_instances', ['path', Q('/'), ['page', Q('1')]]])
    out = os.path.join(HERE, 'els_shield.kicad_sch')
    open(out, 'w', encoding='utf-8').write(dump(sch) + '\n')
    print('схема:', out, '— деталей', len(placed))


if __name__ == '__main__':
    main()
