#!/usr/bin/env python3
"""Шелкография верхнего края (2026-11-12): подписи выводов J24–J27 — узкие буквы 0.6 × 0.8 мм (при 0.8 × 0.8 слипались «+5VGND»; ниже 0.8 — нельзя по правилам платы),
название платы — на y 13.9 (между новыми переходами)."""
import os
import pcbnew
PCB = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'els_shield.kicad_pcb')
mm, ToMM = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(PCB)
n = 0
for d in list(b.GetDrawings()):
    if d.GetClass() != 'PCB_TEXT':
        continue
    x, y = ToMM(d.GetPosition().x), ToMM(d.GetPosition().y)
    if 9.8 < y < 10.5 and 10 < x < 70:
        d.SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.8))); d.SetTextThickness(mm(0.12)); n += 1
    if d.GetText().startswith('ELS shield'):
        d.SetPosition(pcbnew.VECTOR2I(d.GetPosition().x, mm(13.9)))
b.Save(PCB)
print('подписей выводов уменьшено:', n)
