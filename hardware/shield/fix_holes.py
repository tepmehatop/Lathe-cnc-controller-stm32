#!/usr/bin/env python3
"""
Медь подальше от крепёжных отверстий (под головкой винта M3, шайбой и стойкой — ничего, кроме земли).
Ручная перекладка трёх дорожек после проверки перед заказом (2026-11-12):
  * DRO_RX у H3 (3.5, 122.5): дорожка и переход были в 2.4 мм от центра — под головкой винта;
  * LED_LEFT_K и LED_RIGHT_K у H2 (92.5, 3.5): огибали отверстие в 3.3–3.9 мм — под шайбой.
Новые пути — по нижнему слою от разъёма J12 влево и по верху у J9. Запуск Python-ом KiCad после fix_zones.py:
  $KPY hardware/shield/fix_holes.py
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
mm = pcbnew.FromMM
TOL = mm(0.02)

b = pcbnew.LoadBoard(PCB)
F, B = b.GetLayerID('F.Cu'), b.GetLayerID('B.Cu')


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def same(p, x, y):
    return abs(p.x - mm(x)) <= TOL and abs(p.y - mm(y)) <= TOL


TRACKS = list(b.GetTracks())


def find_seg(net, a, c):
    for t in TRACKS:
        if t.GetClass() == 'PCB_TRACK' and t.GetNetname() == net:
            s, e = t.GetStart(), t.GetEnd()
            if (same(s, *a) and same(e, *c)) or (same(s, *c) and same(e, *a)):
                return t
    raise SystemExit(f'не найден сегмент {net} {a}→{c}')


def find_via(net, x, y):
    for t in TRACKS:
        if t.GetClass() == 'PCB_VIA' and t.GetNetname() == net and same(t.GetPosition(), x, y):
            return t
    raise SystemExit(f'не найден переход {net} {x},{y}')


# Все точки цепей (концы дорожек, переходы, центры площадок) — один раз до правок: после Remove() список дорожек
# в Python KiCad перебрать заново нельзя
POINTS = []
for t in b.GetTracks():
    ps = [t.GetPosition()] if t.GetClass() == 'PCB_VIA' else [t.GetStart(), t.GetEnd()]
    POINTS += [(t.GetNetname(), p.x, p.y) for p in ps]
for fp in b.GetFootprints():
    POINTS += [(p.GetNetname(), p.GetPosition().x, p.GetPosition().y) for p in fp.Pads()]


def snap(net, x, y):
    """Точная координата уже существующей точки цепи — стык без зазора."""
    for n, px, py in POINTS:
        if n == net and abs(px - mm(x)) <= TOL and abs(py - mm(y)) <= TOL:
            return pcbnew.VECTOR2I(px, py)
    return pt(x, y)


def add_seg(net, layer, a, c, w=0.3):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(snap(net, *a)); t.SetEnd(snap(net, *c)); t.SetWidth(mm(w)); t.SetLayer(layer)
    t.SetNet(b.FindNet(net)); b.Add(t)


def add_via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pt(x, y)); v.SetWidth(mm(0.8)); v.SetDrill(mm(0.4))
    v.SetLayerPair(F, B); v.SetNet(b.FindNet(net)); b.Add(v)


def remove(items):
    for it in items:
        b.Remove(it)


# ── DRO_RX у H3 ───────────────────────────────────────────────────────────────────────────────────────
n = 'DRO_RX'
remove([find_seg(n, (7.88, 123.30), (4.01, 119.43)), find_via(n, 4.01, 119.43),
        find_seg(n, (4.01, 119.43), (2.73, 118.15)), find_seg(n, (2.73, 118.15), (2.73, 98.55)),
        find_seg(n, (27.24, 123.30), (7.88, 123.30))])
add_seg(n, B, (27.24, 123.30), (10.90, 123.30))
add_seg(n, B, (10.90, 123.30), (10.90, 118.90))
add_via(n, 10.90, 118.90)
add_seg(n, F, (10.90, 118.90), (10.90, 114.90))
add_seg(n, F, (10.90, 114.90), (2.73, 114.90))
add_seg(n, F, (2.73, 114.90), (2.73, 98.55))

# ── LED_LEFT_K у H2: обход J12.1 справа и сверху (под шайбой) → нижним слоем слева от J12.1 → переход на старую
#    диагональ x+y = 97.73 (её нижняя часть остаётся как была) ──
n = 'LED_LEFT_K'
remove([find_seg(n, (91.73, 11.36), (93.28, 9.81)), find_seg(n, (93.28, 9.81), (93.28, 7.96)),
        find_seg(n, (93.28, 7.96), (92.85, 7.53)), find_seg(n, (92.85, 7.53), (90.20, 7.53)),
        find_seg(n, (90.20, 7.53), (79.13, 18.60))])
add_seg(n, B, (91.73, 11.36), (89.80, 10.20))
add_seg(n, B, (89.80, 10.20), (89.80, 7.93))
add_via(n, 89.80, 7.93)
add_seg(n, F, (89.80, 7.93), (79.13, 18.60))

# ── LED_RIGHT_K у H2: обход J12 сверху → нижним слоем влево (между землёй и LED_FRONT_K) → переход на её же
#    вертикаль x = 78.21 ──
n = 'LED_RIGHT_K'
remove([find_seg(n, (91.73, 13.89), (93.83, 11.79)), find_seg(n, (93.83, 11.79), (93.83, 7.73)),
        find_seg(n, (93.83, 7.73), (93.08, 6.97)), find_seg(n, (93.08, 6.97), (89.97, 6.97)),
        find_seg(n, (89.97, 6.97), (78.21, 18.73))])
add_seg(n, B, (91.73, 13.89), (90.20, 15.20))
add_seg(n, B, (90.20, 15.20), (78.21, 15.20))
add_via(n, 78.21, 15.20)
add_seg(n, F, (78.21, 15.20), (78.21, 18.73))

b.Save(PCB)
print('переложено: DRO_RX (H3), LED_LEFT_K и LED_RIGHT_K (H2)')
