#!/usr/bin/env python3
"""
Добавить на плату путь, найденный route_grid.py: концы — точно в центры площадок (или переход).
  python3 route_grid.py ... > path.txt
  $KPY add_path.py NET x0 y0 x1 y1 path.txt      (x0 y0 — центр старта, x1 y1 — центр цели; ширина и переходы — класса Reserve)
"""
import os, sys
import pcbnew
PCB = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'els_shield.kicad_pcb')
mm = pcbnew.FromMM
net, x0, y0, x1, y1, path = sys.argv[1], *map(float, sys.argv[2:6]), sys.argv[6]
segs, vias = [], []
for line in open(path, encoding='utf-8'):
    w = line.split()
    if w and w[0] == 'SEG':
        segs.append([w[1], (float(w[2]), float(w[3])), (float(w[4]), float(w[5]))])
    elif w and w[0] == 'VIA':
        vias.append((float(w[1]), float(w[2])))
if not segs:
    raise SystemExit('в файле нет пути')
segs[0][1] = (x0, y0)                              # старт — центр площадки
segs[-1][2] = (x1, y1)                             # конец — центр цели
b = pcbnew.LoadBoard(PCB)
ni = b.FindNet(net)
for L, a, c in segs:
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(pcbnew.VECTOR2I(mm(c[0]), mm(c[1])))
    t.SetWidth(mm(0.2)); t.SetLayer(pcbnew.F_Cu if L == 'F' else pcbnew.B_Cu); t.SetNet(ni); b.Add(t)
for x, y in vias:
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(ni); b.Add(v)
b.Save(PCB)
print(f'{net}: отрезков {len(segs)}, переходов {len(vias)}')
