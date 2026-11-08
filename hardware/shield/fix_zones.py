#!/usr/bin/env python3
"""
После трассировки и переходов к земле (KiCad MCP: autoroute, add_gnd_stitching_vias):
  * переходы GND без дорожек, стоящие ближе 0.25 мм к чужой меди, — удалить (расстановщик MCP проверяет
    зазор не по всем формам);
  * земляные полигоны — сплошное подключение площадок (у тесных разъёмов и 0805 тепловые связки «голодают»:
    соседние дорожки перекрывают перемычки), обрывки полигона без связи — убрать;
  * залить полигоны и сохранить.
Запуск Python-ом KiCad:  $KPY hardware/shield/fix_zones.py
"""
import os
import pcbnew

PCB = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'els_shield.kicad_pcb')
CLEAR = pcbnew.FromMM(0.25)
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)

board = pcbnew.LoadBoard(PCB)
gnd = board.GetNetcodeFromNetname('GND')

# полигоны — настроить заранее: после GetEffectiveShape() SWIG отдаёт объекты зон «сырыми»
zones = [z for z in board.Zones()]
for z in zones:
    if z.GetNetname() == 'GND':
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)

# концы дорожек GND — переходы на них не трогаем (это часть трассы)
ends = set()
for t in board.GetTracks():
    if t.GetClass() == 'PCB_TRACK' and t.GetNetCode() == gnd:
        for p in (t.GetStart(), t.GetEnd()):
            ends.add((p.x, p.y))

# своя геометрия (GetEffectiveShape() ломает SWIG-обёртки KiCad): переход — круг, дорожка — отрезок
# с шириной, площадка — габаритный прямоугольник; всё в нм
def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return ((px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2) ** 0.5


def rect_dist(px, py, bb):
    dx = max(bb.GetLeft() - px, 0, px - bb.GetRight())
    dy = max(bb.GetTop() - py, 0, py - bb.GetBottom())
    return (dx * dx + dy * dy) ** 0.5


items = list(board.GetTracks())
segs = [(t.GetStart(), t.GetEnd(), t.GetWidth()) for t in items if t.GetClass() == 'PCB_TRACK' and t.GetNetCode() != gnd]
fsegs = [(t.GetStart(), t.GetEnd(), t.GetWidth()) for t in items
         if t.GetClass() == 'PCB_TRACK' and t.GetNetCode() != gnd and t.GetLayer() == pcbnew.F_Cu]
fpads = [p.GetBoundingBox() for p in board.GetPads() if p.GetNetCode() != gnd and p.IsOnLayer(pcbnew.F_Cu)]
ovias = [(t.GetPosition(), t.GetWidth(pcbnew.F_Cu)) for t in items if t.GetClass() == 'PCB_VIA' and t.GetNetCode() != gnd]
pads = [p.GetBoundingBox() for p in board.GetPads() if p.GetNetCode() != gnd]
removed = 0
for v in [t for t in items if t.GetClass() == 'PCB_VIA' and t.GetNetCode() == gnd]:
    pos = v.GetPosition()
    if (pos.x, pos.y) in ends:
        continue
    r = v.GetWidth(pcbnew.F_Cu) / 2 + CLEAR
    bad = any(seg_dist(pos.x, pos.y, a.x, a.y, b.x, b.y) < r + w / 2 for a, b, w in segs) or \
          any(((pos.x - o.x) ** 2 + (pos.y - o.y) ** 2) ** 0.5 < r + w / 2 for o, w in ovias) or \
          any(rect_dist(pos.x, pos.y, bb) < r for bb in pads)
    if bad:
        board.Remove(v)
        removed += 1

pcbnew.ZONE_FILLER(board).Fill(zones)

# куски верхнего полигона без связи с нижней землёй — по переходу в каждый (свободное место внутри куска
# и внутри нижнего полигона, с зазором до чужой меди)
VIA_D, VIA_DRILL = pcbnew.FromMM(0.8), pcbnew.FromMM(0.4)


def clear_at(x, y):
    r = VIA_D / 2 + CLEAR
    return not (any(seg_dist(x, y, a.x, a.y, b.x, b.y) < r + w / 2 for a, b, w in segs) or
                any(((x - o.x) ** 2 + (y - o.y) ** 2) ** 0.5 < r + w / 2 for o, w in ovias) or
                any(rect_dist(x, y, bb) < r for bb in pads))


def inside(poly, x, y, rad):
    import math
    return all(poly.Contains(pcbnew.VECTOR2I(int(x + rad * math.cos(a)), int(y + rad * math.sin(a))))
               for a in [k * math.pi / 4 for k in range(8)]) and poly.Contains(pcbnew.VECTOR2I(int(x), int(y)))


def path_clear(ax, ay, bx, by, w):
    """Дорожка по верхнему слою: зазор только до меди верхнего слоя (и переходов — они сквозные)."""
    n = max(2, int(((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5 / pcbnew.FromMM(0.1)))
    r = w / 2 + CLEAR
    for k in range(n + 1):
        x, y = ax + (bx - ax) * k / n, ay + (by - ay) * k / n
        if any(seg_dist(x, y, a.x, a.y, b.x, b.y) < r + ww / 2 for a, b, ww in fsegs) or \
           any(((x - o.x) ** 2 + (y - o.y) ** 2) ** 0.5 < r + ww / 2 for o, ww in ovias) or \
           any(rect_dist(x, y, bb) < r for bb in fpads):
            return False
    return True


def stitch_pad(piece):
    import math
    d, drill, tw = pcbnew.FromMM(0.6), pcbnew.FromMM(0.3), pcbnew.FromMM(0.3)
    for pad in board.GetPads():
        if pad.GetNetCode() != gnd or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        c = pad.GetPosition()
        if not piece.Contains(pcbnew.VECTOR2I(c.x, c.y)):
            continue
        for rad_mm in [1.2 + 0.4 * k for k in range(13)]:     # 1.2 … 6.0 мм
            for k in range(24):
                a = k * math.pi / 12
                x, y = c.x + pcbnew.FromMM(rad_mm) * math.cos(a), c.y + pcbnew.FromMM(rad_mm) * math.sin(a)
                r = d / 2 + CLEAR
                if any(seg_dist(x, y, s0.x, s0.y, s1.x, s1.y) < r + w / 2 for s0, s1, w in segs) or \
                   any(((x - o.x) ** 2 + (y - o.y) ** 2) ** 0.5 < r + w / 2 for o, w in ovias) or \
                   any(rect_dist(x, y, bb) < r for bb in pads) or not inside(bpoly, x, y, d / 2):
                    continue
                if not path_clear(c.x, c.y, x, y, tw):
                    continue
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pcbnew.VECTOR2I(int(x), int(y))); v.SetWidth(d); v.SetDrill(drill); v.SetNetCode(gnd)
                board.Add(v)
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(c.x, c.y)); t.SetEnd(pcbnew.VECTOR2I(int(x), int(y)))
                t.SetWidth(tw); t.SetLayer(pcbnew.F_Cu); t.SetNetCode(gnd)
                board.Add(t)
                ovias.append((v.GetPosition(), 0))
                print('  переход у', pad.GetParentFootprint().GetReference(), pad.GetNumber())
                return True
        # переход не встал — дорожкой по верхнему слою в соседний кусок верхней земли
        for rad_mm in [0.8 + 0.2 * k for k in range(26)]:
            for k in range(36):
                a = k * math.pi / 18
                x, y = c.x + pcbnew.FromMM(rad_mm) * math.cos(a), c.y + pcbnew.FromMM(rad_mm) * math.sin(a)
                if piece.Contains(pcbnew.VECTOR2I(int(x), int(y))) or not inside(fpoly, x, y, tw / 2 + pcbnew.FromMM(0.1)):
                    continue
                if not path_clear(c.x, c.y, x, y, tw):
                    continue
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(c.x, c.y)); t.SetEnd(pcbnew.VECTOR2I(int(x), int(y)))
                t.SetWidth(tw); t.SetLayer(pcbnew.F_Cu); t.SetNetCode(gnd)
                board.Add(t)
                print('  дорожка от', pad.GetParentFootprint().GetReference(), pad.GetNumber(), f'{rad_mm:.1f} мм')
                return True
    return False


fz = [z for z in zones if z.GetNetname() == 'GND' and z.GetLayer() == pcbnew.F_Cu][0]
bz = [z for z in zones if z.GetNetname() == 'GND' and z.GetLayer() == pcbnew.B_Cu][0]
fpoly, bpoly = fz.GetFilledPolysList(pcbnew.F_Cu), bz.GetFilledPolysList(pcbnew.B_Cu)
links = [t.GetPosition() for t in board.GetTracks() if t.GetClass() == 'PCB_VIA' and t.GetNetCode() == gnd]
links += [p.GetPosition() for p in board.GetPads() if p.GetNetCode() == gnd and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
added = 0
for i in range(fpoly.OutlineCount()):
    piece = pcbnew.SHAPE_POLY_SET()
    piece.AddOutline(fpoly.Outline(i))
    for h in range(fpoly.HoleCount(i)):
        piece.AddHole(fpoly.Hole(i, h))
    if any(piece.Contains(pcbnew.VECTOR2I(q.x, q.y)) for q in links):
        continue
    bb = fpoly.Outline(i).BBox()
    step = pcbnew.FromMM(0.4)
    done = False
    y = bb.GetTop()
    while y <= bb.GetBottom() and not done:
        x = bb.GetLeft()
        while x <= bb.GetRight():
            if inside(piece, x, y, VIA_D / 2 + pcbnew.FromMM(0.15)) and inside(bpoly, x, y, VIA_D / 2) and clear_at(x, y):
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
                v.SetWidth(VIA_D); v.SetDrill(VIA_DRILL); v.SetNetCode(gnd)
                board.Add(v)
                ovias.append((v.GetPosition(), 0)); added += 1; done = True
                break
            x += step
        y += step
    if not done:                                 # кусок мал для перехода: переход 0.6 рядом с SMD-площадкой GND
        done = stitch_pad(piece)
        added += done
if added:
    pcbnew.ZONE_FILLER(board).Fill(zones)
print(f'переходов в куски верхней земли: {added}')
board.Save(PCB)
tracks = [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']
vias = [t for t in board.GetTracks() if t.GetClass() == 'PCB_VIA']
print(f'удалено переходов с малым зазором: {removed}; дорожек {len(tracks)}, переходов {len(vias)}; полигоны залиты')
