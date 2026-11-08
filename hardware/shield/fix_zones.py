#!/usr/bin/env python3
"""
После трассировки (KiCad MCP autoroute; GND разведена дорожками) и переходов к земле (add_gnd_stitching_vias):
  * земляные полигоны с двух сторон — создать, если их нет;
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

# земляные полигоны с двух сторон — после трассировки (GND разведена дорожками; полигоны — дополнение)
if not [z for z in board.Zones() if z.GetNetname() == 'GND']:
    W, H = [pcbnew.ToMM(v) for v in (board.GetBoardEdgesBoundingBox().GetWidth(), board.GetBoardEdgesBoundingBox().GetHeight())]
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNetCode(gnd)
        z.SetLocalClearance(pcbnew.FromMM(0.3)); z.SetMinThickness(pcbnew.FromMM(0.25))
        ol = z.Outline(); ol.NewOutline()
        for x, y in [(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)]:
            ol.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        board.Add(z)
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
holes = [(p.GetPosition(), max(p.GetDrillSize().x, p.GetSize().x)) for p in board.GetPads()
         if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH or p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH]
removed = 0
for v in [t for t in items if t.GetClass() == 'PCB_VIA' and t.GetNetCode() == gnd]:
    pos = v.GetPosition()
    if (pos.x, pos.y) in ends:
        continue
    r = v.GetWidth(pcbnew.F_Cu) / 2 + CLEAR
    # отверстие к отверстию (любая цепь, и своя земля тоже: расстановщик ставит переход на вывод разъёма)
    hole = any(((pos.x - h.x) ** 2 + (pos.y - h.y) ** 2) ** 0.5 < hd / 2 + v.GetDrillValue() / 2 + pcbnew.FromMM(0.5)
               for h, hd in holes)
    bad = hole or any(seg_dist(pos.x, pos.y, a.x, a.y, b.x, b.y) < r + w / 2 for a, b, w in segs) or \
          any(((pos.x - o.x) ** 2 + (pos.y - o.y) ** 2) ** 0.5 < r + w / 2 for o, w in ovias) or \
          any(rect_dist(pos.x, pos.y, bb) < r for bb in pads)
    if bad:
        board.Remove(v)
        removed += 1

pcbnew.ZONE_FILLER(board).Fill(zones)

# Связность земли: куски залитых полигонов обоих слоёв — узлы; переходы и выводы GND — связи между слоями,
# дорожки GND — внутри слоя. Всё, что не связано с основной (самой большой) землёй, — подключить: переходом туда,
# где кусок лежит над основной землёй другого слоя, или короткой дорожкой к основной земле того же слоя.
import math
VIA_D, VIA_DRILL = pcbnew.FromMM(0.8), pcbnew.FromMM(0.4)
LSEGS = {pcbnew.F_Cu: fsegs,
         pcbnew.B_Cu: [(t.GetStart(), t.GetEnd(), t.GetWidth()) for t in items
                       if t.GetClass() == 'PCB_TRACK' and t.GetNetCode() != gnd and t.GetLayer() == pcbnew.B_Cu]}
LPADS = {pcbnew.F_Cu: fpads,
         pcbnew.B_Cu: [p.GetBoundingBox() for p in board.GetPads() if p.GetNetCode() != gnd and p.IsOnLayer(pcbnew.B_Cu)]}


def clear_at(x, y, d=VIA_D, drill=VIA_DRILL):
    r = d / 2 + CLEAR
    return not (any(seg_dist(x, y, a.x, a.y, b.x, b.y) < r + w / 2 for a, b, w in segs) or
                any(((x - o.x) ** 2 + (y - o.y) ** 2) ** 0.5 < r + w / 2 for o, w in ovias) or
                any(rect_dist(x, y, bb) < r for bb in pads) or
                any(((x - h.x) ** 2 + (y - h.y) ** 2) ** 0.5 < hd / 2 + drill / 2 + pcbnew.FromMM(0.5) for h, hd in holes))


def inside(poly, x, y, rad):
    return poly.Contains(pcbnew.VECTOR2I(int(x), int(y))) and \
        all(poly.Contains(pcbnew.VECTOR2I(int(x + rad * math.cos(a)), int(y + rad * math.sin(a))))
            for a in [k * math.pi / 4 for k in range(8)])


def path_clear(layer, ax, ay, bx, by, w):
    """Дорожка по слою layer: зазор до меди этого слоя и до переходов (они сквозные)."""
    n = max(2, int(((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5 / pcbnew.FromMM(0.1)))
    r = w / 2 + CLEAR
    for k in range(n + 1):
        x, y = ax + (bx - ax) * k / n, ay + (by - ay) * k / n
        if any(seg_dist(x, y, a.x, a.y, b.x, b.y) < r + ww / 2 for a, b, ww in LSEGS[layer]) or \
           any(((x - o.x) ** 2 + (y - o.y) ** 2) ** 0.5 < r + ww / 2 for o, ww in ovias) or \
           any(rect_dist(x, y, bb) < r for bb in LPADS[layer]):
            return False
    return True


def split(poly):
    out = []
    for i in range(poly.OutlineCount()):
        s = pcbnew.SHAPE_POLY_SET(); s.AddOutline(poly.Outline(i))
        for h in range(poly.HoleCount(i)):
            s.AddHole(poly.Hole(i, h))
        out.append(s)
    return out


def ground_parts():
    """[(слой, кусок)], номер компоненты связности каждого куска и номер основной (наибольшей по площади)."""
    fz = [z for z in zones if z.GetNetname() == 'GND' and z.GetLayer() == pcbnew.F_Cu][0]
    bz = [z for z in zones if z.GetNetname() == 'GND' and z.GetLayer() == pcbnew.B_Cu][0]
    parts = [(pcbnew.F_Cu, p) for p in split(fz.GetFilledPolysList(pcbnew.F_Cu))] + \
            [(pcbnew.B_Cu, p) for p in split(bz.GetFilledPolysList(pcbnew.B_Cu))]
    par = list(range(len(parts)))

    def find(a):
        while par[a] != a:
            par[a] = par[par[a]]; a = par[a]
        return a

    def at(pt, layer=None):
        return [i for i, (l, p) in enumerate(parts) if (layer is None or l == layer) and p.Contains(pt)]
    for t in board.GetTracks():
        if t.GetNetCode() != gnd:
            continue
        if t.GetClass() == 'PCB_VIA':
            hit = at(t.GetPosition())
        else:
            hit = at(t.GetStart(), t.GetLayer()) + at(t.GetEnd(), t.GetLayer())
        for h in hit[1:]:
            par[find(h)] = find(hit[0])
    for p in board.GetPads():
        if p.GetNetCode() == gnd and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            hit = at(p.GetPosition())
            for h in hit[1:]:
                par[find(h)] = find(hit[0])
    comp = [find(i) for i in range(len(parts))]
    area = {}
    for i, c in enumerate(comp):
        area[c] = area.get(c, 0) + parts[i][1].Area()
    return parts, comp, max(area, key=area.get)


def add_via(x, y, d, drill):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pcbnew.VECTOR2I(int(x), int(y))); v.SetWidth(d); v.SetDrill(drill); v.SetNetCode(gnd)
    board.Add(v)
    ovias.append((v.GetPosition(), 0)); holes.append((v.GetPosition(), drill))


def link(parts, comp, main, c):
    """Подключить компоненту c к основной. True — получилось."""
    mine = [parts[i] for i in range(len(parts)) if comp[i] == c]
    mains = [parts[i] for i in range(len(parts)) if comp[i] == main]
    step = pcbnew.FromMM(0.4)
    for d, drill in ((VIA_D, VIA_DRILL), (pcbnew.FromMM(0.6), pcbnew.FromMM(0.3))):
        for layer, piece in mine:                  # переход: кусок над основной землёй другого слоя
            other = [p for l, p in mains if l != layer]
            bb = piece.BBox()
            y = bb.GetTop()
            while y <= bb.GetBottom():
                x = bb.GetLeft()
                while x <= bb.GetRight():
                    if inside(piece, x, y, d / 2 + pcbnew.FromMM(0.1)) and \
                       any(inside(o, x, y, d / 2 + pcbnew.FromMM(0.1)) for o in other) and clear_at(x, y, d, drill):
                        add_via(x, y, d, drill)
                        return True
                    x += step
                y += step
    tw = pcbnew.FromMM(0.3)                        # дорожка к основной земле того же слоя (до 6 мм)
    for layer, piece in mine:
        same = [p for l, p in mains if l == layer]
        bb = piece.BBox()
        y = bb.GetTop()
        while y <= bb.GetBottom():
            x = bb.GetLeft()
            while x <= bb.GetRight():
                if inside(piece, x, y, tw):
                    for rad_mm in [0.6 + 0.3 * k for k in range(19)]:
                        for k in range(24):
                            a = k * math.pi / 12
                            ex, ey = x + pcbnew.FromMM(rad_mm) * math.cos(a), y + pcbnew.FromMM(rad_mm) * math.sin(a)
                            if any(inside(o, ex, ey, tw) for o in same) and path_clear(layer, x, y, ex, ey, tw):
                                t = pcbnew.PCB_TRACK(board)
                                t.SetStart(pcbnew.VECTOR2I(int(x), int(y))); t.SetEnd(pcbnew.VECTOR2I(int(ex), int(ey)))
                                t.SetWidth(tw); t.SetLayer(layer); t.SetNetCode(gnd)
                                board.Add(t)
                                LSEGS[layer].append((t.GetStart(), t.GetEnd(), 0))
                                return True
                x += step * 2
            y += step * 2
    return False


added, failed = 0, 0
for it in range(15):
    parts, comp, main = ground_parts()
    others = sorted({c for c in comp if c != main})
    if not others:
        break
    progress = False
    for c in others:
        if link(parts, comp, main, c):
            added += 1; progress = True
            break                                  # после каждой связи — перезалить и пересчитать
    pcbnew.ZONE_FILLER(board).Fill(zones)
    if not progress:
        failed = len(others)
        break
print(f'связей с основной землёй добавлено: {added}; не удалось подключить: {failed}')
# висящие дорожки (хвосты Freerouting): конец ни к чему не подключён — убрать, пока такие есть; это «листья»,
# связность от их удаления не рвётся
TOL = pcbnew.FromMM(0.01)


def attached(t, p):
    me = t.m_Uuid.AsString()                     # «o is t» не работает: SWIG каждый раз создаёт новые обёртки
    for o in board.GetTracks():
        if o.GetNetCode() != t.GetNetCode() or o.m_Uuid.AsString() == me:
            continue
        if o.GetClass() == 'PCB_VIA':
            if (o.GetPosition() - p).EuclideanNorm() <= o.GetWidth(pcbnew.F_Cu) / 2:
                return True
        elif o.GetLayer() == t.GetLayer():       # KiCad связывает дорожки конец к концу (не конец к середине)
            if min((o.GetStart() - p).EuclideanNorm(), (o.GetEnd() - p).EuclideanNorm()) <= TOL + o.GetWidth() / 2:
                return True
    for pd in board.GetPads():
        if pd.GetNetCode() == t.GetNetCode() and pd.IsOnLayer(t.GetLayer()) and pd.HitTest(p):
            return True
    return False


def via_layers(v):
    """Сколько слоёв меди подключено к переходу дорожками (у GND ещё полигоны — их не считаем, не трогаем)."""
    p, r = v.GetPosition(), v.GetWidth(pcbnew.F_Cu) / 2
    n = set()
    for o in board.GetTracks():
        if o.GetClass() == 'PCB_TRACK' and o.GetNetCode() == v.GetNetCode() and \
           min((o.GetStart() - p).EuclideanNorm(), (o.GetEnd() - p).EuclideanNorm()) <= r:
            n.add(o.GetLayer())
    for pd in board.GetPads():
        if pd.GetNetCode() == v.GetNetCode() and pd.HitTest(p):
            n.update((pcbnew.F_Cu, pcbnew.B_Cu))
    return len(n)


# точные дубликаты дорожек (Freerouting иногда кладёт отрезок дважды, навстречу) — оставить один
seen_seg, dups = set(), 0
for t in [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']:
    a, b2 = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
    key = (t.GetNetCode(), t.GetLayer(), min(a, b2), max(a, b2))
    if key in seen_seg:
        board.Remove(t); dups += 1
    else:
        seen_seg.add(key)
print(f'дубликатов дорожек убрано: {dups}')

stubs = 0
while True:
    dead = [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK' and
            not (attached(t, t.GetStart()) and attached(t, t.GetEnd()))]
    dead += [v for v in board.GetTracks() if v.GetClass() == 'PCB_VIA' and v.GetNetCode() != gnd and via_layers(v) < 2]
    if not dead:
        break
    for t in dead:
        board.Remove(t)
    stubs += len(dead)
if stubs:
    pcbnew.ZONE_FILLER(board).Fill(zones)
print(f'висящих дорожек и переходов убрано: {stubs}')
board.Save(PCB)
tracks = [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']
vias = [t for t in board.GetTracks() if t.GetClass() == 'PCB_VIA']
print(f'удалено переходов с малым зазором: {removed}; дорожек {len(tracks)}, переходов {len(vias)}; полигоны залиты')
