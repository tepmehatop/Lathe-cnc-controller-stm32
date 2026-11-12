#!/usr/bin/env python3
"""
Добавить в уже разведённую плату разъёмы верхнего края (2026-11-12): J24 СБРОС, J25 ДАТЧИК (РЕЗЕРВ),
J26 РЕЗЕРВ ВЫХОДЫ, J27 РЕЗЕРВ ВХ/ВЫХ и их мелочь (R27–R32, R134, C9). Существующие детали, дорожки и
переходы не меняются; новые цепи потом разводит трассировщик (с закреплёнными старыми дорожками).

  $KPY hardware/shield/add_reserve.py
Данные — из design.py (те же посадочные места, номиналы и цепи, что в схеме).
"""
import os, sys, uuid
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design
from gen_pcb import load, pad1_to, bbox, silk_text, pt, NS, MM, W

PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
NEW_J = {'J24': 13.0, 'J25': 20.9, 'J27': 31.4, 'J26': 58.5}     # x вывода 1; ряд выводов — y = 5.95
PAD_Y = 5.95
# мелочь — ряд под Discovery на y = 22.95 (между рядами сшивающих переходов 19.95 и 25.95), слева направо
NEW_SMD = [('R134', 22.0), ('C9', 26.0), ('R31', 30.0), ('R32', 34.0), ('R27', 38.0), ('R28', 42.0),
           ('R29', 46.0), ('R30', 50.0)]
SMD_Y = 22.95

b = pcbnew.LoadBoard(PCB)
have = {f.GetReference() for f in b.GetFootprints()}
for ref in list(NEW_J) + [r for r, _ in NEW_SMD]:
    if ref in have:
        raise SystemExit(f'{ref} уже есть на плате — ничего не делаю')

parts = {p['ref']: p for p in design.PARTS}
nets = {}


def net(name):
    if name not in nets:
        ni = b.FindNet(name)
        if not ni:
            ni = pcbnew.NETINFO_ITEM(b, name); b.Add(ni)
        nets[name] = ni
    return nets[name]


def add_part(ref):
    p = parts[ref]
    f = load(p['fp'])
    f.SetReference(ref); f.SetValue(p['value'])
    f.SetPath(pcbnew.KIID_PATH('/' + str(uuid.uuid5(NS, 'sym/' + ref))))
    for pad in f.Pads():
        n = pad.GetNumber()
        if n.isdigit() and int(n) in p['pins']:
            pad.SetNet(net(p['pins'][int(n)]))
    b.Add(f)
    return f


# ── Новые цепи на гнезде J1 (выводы P1, которые раньше никуда не шли) ─────────────────────────────────
j1 = next(f for f in b.GetFootprints() if f.GetReference() == 'J1')
for pad in j1.Pads():
    n = int(pad.GetNumber())
    want = parts['J1']['pins'].get(n)
    if want and pad.GetNetname() != want:
        if pad.GetNetname():
            raise SystemExit(f'J1.{n} уже в цепи {pad.GetNetname()} — ожидалась пустая')
        pad.SetNet(net(want))
        print(f'J1.{n} → {want}')

# ── Разъёмы верхнего края ───────────────────────────────────────────────────────────────────────────────
for ref, x in NEW_J.items():
    f = add_part(ref)
    f.SetOrientationDegrees(0)
    pad1_to(f, x, PAD_Y)
    f.Reference().SetLayer(pcbnew.F_Fab)
    title, _, labels = next((t, n, l) for r, t, n, l in design.TERMINALS if r == ref)
    x0, y0, x1, y1 = bbox(f)
    pads = sorted([p for p in f.Pads() if p.GetNumber().isdigit()], key=lambda p: int(p.GetNumber()))
    for pad, lab in zip(pads, labels):           # подписи выводов — под корпусом, до контура Discovery (y 11)
        silk_text(b, lab, pcbnew.ToMM(pad.GetPosition().x), y1 + 0.75, 0.8)
    silk_text(b, title, (x0 + x1) / 2, 1.0, 0.8)  # название — между краем платы и корпусом
    print(f'{ref} {title}: x {x0:.2f}…{x1:.2f}, y {y0:.2f}…{y1:.2f}')

# ── Мелочь ─────────────────────────────────────────────────────────────────────────────────────────────
for ref, x in NEW_SMD:
    f = add_part(ref)
    f.SetOrientationDegrees(0)
    f.SetPosition(pt(x, SMD_Y))
    f.Reference().SetLayer(pcbnew.F_Fab)
    v = f.Value()
    v.SetText(parts[ref]['value'])
    v.SetLayer(pcbnew.F_SilkS); v.SetVisible(True)
    v.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8))); v.SetTextThickness(MM(0.12))
    v.SetPosition(pcbnew.VECTOR2I(f.GetPosition().x, MM(bbox(f)[1] - 0.55)))

# ── Название платы уходит с верхнего края (там теперь разъёмы) — под Discovery, над её подписью ──────────
for d in b.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetText().startswith('ELS shield'):
        d.SetPosition(pt(W / 2, 13.6))
        print('название платы → y 13.6')

b.Save(PCB)
print('добавлено: J24–J27, R27–R32, R134, C9')
