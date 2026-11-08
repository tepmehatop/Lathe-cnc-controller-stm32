#!/usr/bin/env python3
"""
Дорожки +5V (дисплей 10.1" — до 1.5 А) — шире: 1.0 мм там, где хватает места (DRC KiCad без нарушений
зазора), иначе остаётся ширина трассировщика. Запуск после fix_zones.py Python-ом KiCad:
  $KPY hardware/shield/widen_power.py
"""
import json, os, subprocess
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
KCLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
NETS = ('+5V', '+5V_DISCO')
WIDE = pcbnew.FromMM(1.0)


def drc_bad_uuids():
    rpt = os.path.join(HERE, 'build', 'widen_drc.json')
    subprocess.run([KCLI, 'pcb', 'drc', '--format', 'json', '--refill-zones', '--save-board', '-o', rpt, PCB],
                   capture_output=True)
    d = json.load(open(rpt, encoding='utf-8'))
    bad = set()
    for v in d.get('violations', []):
        if v.get('severity') == 'error':
            for it in v.get('items', []):
                bad.add(it.get('uuid'))
    return bad


board = pcbnew.LoadBoard(PCB)
wid = {}
for t in board.GetTracks():
    if t.GetClass() == 'PCB_TRACK' and t.GetNetname() in NETS and t.GetWidth() < WIDE:
        wid[t.m_Uuid.AsString()] = t.GetWidth()
        t.SetWidth(WIDE)
board.Save(PCB)
print('расширено до 1.0 мм:', len(wid))
for it in range(8):                              # откатывать дорожки с нарушениями, пока они есть
    bad = drc_bad_uuids() & set(wid)
    if not bad:
        break
    board = pcbnew.LoadBoard(PCB)
    for t in board.GetTracks():
        u = t.m_Uuid.AsString()
        if u in bad:
            t.SetWidth(wid.pop(u))
    board.Save(PCB)
    print(f'  проход {it + 1}: возвращено к исходной ширине {len(bad)}')
board = pcbnew.LoadBoard(PCB)
ws = [pcbnew.ToMM(t.GetWidth()) for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK' and t.GetNetname() in NETS]
print(f'+5V: дорожек {len(ws)}, шириной 1.0 мм — {sum(1 for w in ws if w >= 0.99)}')
