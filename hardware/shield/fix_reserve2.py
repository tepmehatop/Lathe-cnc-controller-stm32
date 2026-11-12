#!/usr/bin/env python3
"""Доводка после DRC (2026-11-12): RES_IO1 у левого края — вертикаль обратно на x 0.75 (зазор до перехода LIM_FRONT),
переход — на (0.82, 14.0) (0.52 мм до края); хвост +5V — в узел старых дорожек (57.94, 35.81)."""
import os
import pcbnew
PCB = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'els_shield.kicad_pcb')
mm, ToMM = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(PCB)
TR = list(b.GetTracks())
near = lambda p, x, y: abs(ToMM(p.x) - x) < 0.03 and abs(ToMM(p.y) - y) < 0.03
V = lambda x, y: pcbnew.VECTOR2I(mm(x), mm(y))
for t in TR:
    n = t.GetNetname()
    if n == 'RES_IO1':
        if t.GetClass() == 'PCB_VIA' and near(t.GetPosition(), 0.85, 13.95):
            t.SetPosition(V(0.82, 14.0))
        elif t.GetClass() == 'PCB_TRACK':
            s, e = t.GetStart(), t.GetEnd()
            if near(s, 0.85, 13.95) and near(e, 0.85, 59.5):
                t.SetStart(V(0.75, 14.07)); t.SetEnd(V(0.75, 59.4))
            elif near(e, 0.85, 13.95) and near(s, 0.85, 59.5):
                t.SetEnd(V(0.75, 14.07)); t.SetStart(V(0.75, 59.4))
            else:
                for get, put in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
                    if near(get(), 0.85, 13.95):
                        put(V(0.82, 14.0))
                    elif near(get(), 0.85, 59.5):
                        put(V(0.75, 59.4))
    elif n == '+5V' and t.GetClass() == 'PCB_TRACK' and t.GetLayer() == pcbnew.B_Cu:
        for get, put in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
            if near(get(), 56.25, 35.81):
                put(V(57.94, 35.8064))
j = pcbnew.PCB_TRACK(b)
j.SetStart(V(0.82, 14.0)); j.SetEnd(V(0.75, 14.07)); j.SetWidth(mm(0.2)); j.SetLayer(pcbnew.F_Cu)
j.SetNet(b.FindNet('RES_IO1')); b.Add(j)
b.Save(PCB)
print('готово')
