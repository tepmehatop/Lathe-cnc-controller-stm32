#!/usr/bin/env python3
"""
Дотрассировка новых цепей при закреплённых старых дорожках.
  $KPY route_reserve.py prep SNAPSHOT.kicad_pcb  — старые (по снимку до добавления) — закрепить, прочие дорожки
                                                    убрать, полигоны снять, выгрузить build/res2.dsn (плата не сохраняется)
"""
import os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
TOL = 1000


def key(t):
    r = lambda v: (round(v.x / TOL), round(v.y / TOL))
    if t.GetClass() == 'PCB_VIA':
        return ('V', r(t.GetPosition()), t.GetWidth(), t.GetNetname())
    a, b = sorted([r(t.GetStart()), r(t.GetEnd())])
    return ('T', a, b, t.GetWidth(), t.GetLayer(), t.GetNetname())


snap = {key(t) for t in pcbnew.LoadBoard(sys.argv[2]).GetTracks()}
b = pcbnew.LoadBoard(PCB)
locked = removed = 0
for t in list(b.GetTracks()):
    if key(t) in snap:
        t.SetLocked(True); locked += 1
    else:
        b.Remove(t); removed += 1
for z in list(b.Zones()):
    b.Remove(z)
out = os.path.join(HERE, 'build', 'res2.dsn')
ok = pcbnew.ExportSpecctraDSN(b, out)
print(f'закреплено {locked}, убрано новых {removed}, DSN {out}: {ok}')
