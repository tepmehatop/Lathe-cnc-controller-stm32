#!/usr/bin/env python3
"""
Доводка резервных разъёмов после Freerouting (2026-11-12). Старые (до резерва) дорожки не трогаются.
  * PD12/PD14 не выводятся: убрать R27, R29, их дорожки, J26 6-контактный → 4-контактный (+5V, GND, D13, D15),
    вывод 1 на том же месте; дорожки D13/D15 к J26 — заново (route_grid.py).
  * PROBE: J1.14 → висящий переход Freerouting (33.23, 38.98) — путь route_grid.py (3 перехода).
  * RES_IO1: переход у левого края платы — 0.45 мм до края → 0.55 мм.
  * +5V: конец новой дорожки — точно на старую дорожку (T-стык без зазора).
  $KPY fix_reserve.py
"""
import os, sys, uuid
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design
from gen_pcb import load, pad1_to, bbox, silk_text, NS, MM

PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
b = pcbnew.LoadBoard(PCB)
TRACKS = list(b.GetTracks())
FPS = list(b.GetFootprints())
DRAWS = list(b.GetDrawings())   # списки — до правок: после Remove() перебрать заново нельзя
j1 = next(f for f in FPS if f.GetReference() == 'J1')
mm = pcbnew.FromMM
ToMM = pcbnew.ToMM


def near(p, x, y, tol=0.03):
    return abs(ToMM(p.x) - x) <= tol and abs(ToMM(p.y) - y) <= tol


# 1–2. Детали и дорожки PD12/PD14 и старого J26
for f in FPS:
    if f.GetReference() in ('R27', 'R29', 'J26'):
        b.Remove(f)
gone = {'RES_OUT1', 'RES_OUT1_T', 'RES_OUT3', 'RES_OUT3_T', 'RES_OUT2_T', 'RES_OUT4_T'}
n = 0
for t in TRACKS:
    if t.GetNetname() in gone:
        b.Remove(t); n += 1
print('убрано дорожек/переходов PD12/PD14 и старого J26:', n)
# 3. J1.44 и J1.46 — снова без цепи
for p in j1.Pads():
    if p.GetNumber() in ('44', '46'):
        p.SetNetCode(0)
# 4. Подписи старого J26
for d in DRAWS:
    if d.GetClass() != 'PCB_TEXT':
        continue
    x, y = ToMM(d.GetPosition().x), ToMM(d.GetPosition().y)
    if (d.GetText().startswith('РЕЗЕРВ ВЫХОДЫ') or (56 < x < 74 and 9.5 < y < 11 and d.GetText() in
                                                    ('+5V', 'GND', 'D12', 'D13', 'D14', 'D15'))):
        b.Remove(d)
# 5. Новый J26 на 4 контакта
p = next(q for q in design.PARTS if q['ref'] == 'J26')
f = load(p['fp'])
f.SetReference('J26'); f.SetValue(p['value'])
f.SetPath(pcbnew.KIID_PATH('/' + str(uuid.uuid5(NS, 'sym/J26'))))
for pad in f.Pads():
    k = int(pad.GetNumber())
    if k in p['pins']:
        pad.SetNet(b.FindNet(p['pins'][k]))
b.Add(f)
f.SetOrientationDegrees(0)
pad1_to(f, 58.5, 5.95)
f.Reference().SetLayer(pcbnew.F_Fab)
title, labels = next((t, l) for r, t, _, l in design.TERMINALS if r == 'J26')
x0, y0, x1, y1 = bbox(f)
for pad, lab in zip(sorted(f.Pads(), key=lambda q: int(q.GetNumber())), labels):
    silk_text(b, lab, ToMM(pad.GetPosition().x), y1 + 0.75, 0.8)
silk_text(b, title, (x0 + x1) / 2, 1.0, 0.8)
print(f'J26 4-контактный: x {x0:.2f}…{x1:.2f}')


def seg(net, layer, a, c, w=0.2):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(c[0]), mm(c[1])))
    t.SetWidth(mm(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net)); b.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(b.FindNet(net)); b.Add(v)


# 6. PROBE: J1.14 → переход (33.23, 38.98)
F, B = pcbnew.F_Cu, pcbnew.B_Cu
pad14 = next(q for q in j1.Pads() if q.GetNumber() == '14').GetPosition()
s0 = (ToMM(pad14.x), ToMM(pad14.y))
pts = [(F, s0, (24.20, 60.10)), (F, (24.20, 60.10), (25.70, 58.60)), (F, (25.70, 58.60), (25.70, 57.60)),
       (F, (25.70, 57.60), (26.80, 56.50)), (F, (26.80, 56.50), (26.80, 51.10)), (F, (26.80, 51.10), (27.60, 50.30)),
       (B, (27.60, 50.30), (29.70, 50.30)), (B, (29.70, 50.30), (30.00, 50.00)),
       (F, (30.00, 50.00), (30.00, 42.00)), (F, (30.00, 42.00), (31.70, 40.30))]
pv = next(t for t in TRACKS if t.GetClass() == 'PCB_VIA' and t.GetNetname() == 'PROBE' and near(t.GetPosition(), 33.23, 38.98))
pts.append((B, (31.70, 40.30), (ToMM(pv.GetPosition().x), ToMM(pv.GetPosition().y))))
for L, a, c in pts:
    seg('PROBE', L, a, c)
for x, y in ((27.60, 50.30), (30.00, 50.00), (31.70, 40.30)):
    via('PROBE', x, y)
print('PROBE: J1.14 → переход Freerouting, 3 перехода')

# 7. RES_IO1 у левого края
for t in TRACKS:
    if t.GetNetname() != 'RES_IO1':
        continue
    if t.GetClass() == 'PCB_VIA' and near(t.GetPosition(), 0.75, 14.05):
        t.SetPosition(pcbnew.VECTOR2I(mm(0.85), mm(13.95)))
    elif t.GetClass() == 'PCB_TRACK':
        for get, put in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
            if near(get(), 0.75, 14.05):
                put(pcbnew.VECTOR2I(mm(0.85), mm(13.95)))
            elif near(get(), 0.75, 59.4):
                put(pcbnew.VECTOR2I(mm(0.85), mm(59.5)))
print('RES_IO1: переход у края → (0.85, 13.95)')

# 8. +5V: T-стык точно на старую дорожку y = 35.81
old = next(t for t in TRACKS if t.GetClass() == 'PCB_TRACK' and t.GetNetname() == '+5V' and t.GetLayer() == B
           and near(t.GetStart(), 57.94, 35.81) and near(t.GetEnd(), 4.22, 35.81))
for t in TRACKS:
    if t.GetClass() == 'PCB_TRACK' and t.GetNetname() == '+5V':
        for get, put in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
            if near(get(), 56.25, 35.81) and t is not old:
                put(pcbnew.VECTOR2I(get().x, old.GetStart().y))
                print('+5V: стык на y', ToMM(old.GetStart().y))
b.Save(PCB)
