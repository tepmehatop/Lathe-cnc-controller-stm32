#!/usr/bin/env python3
"""
Плата шилда (els_shield.kicad_pcb) из design.py: размещение деталей, контур,
земляные полигоны, экспорт для автотрассировщика и импорт результата.

Запускается Python-ом, встроенным в KiCad (нужен модуль pcbnew):
  KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
  $KPY hardware/shield/gen_pcb.py place            — разместить, сохранить, выгрузить .dsn
  $KPY hardware/shield/gen_pcb.py import FILE.ses  — загрузить дорожки, залить полигоны
Полный цикл — hardware/shield/build.sh.
"""
import os, sys, uuid
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design

HERE = os.path.dirname(os.path.abspath(__file__))
FPDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints'
PCB = os.path.join(HERE, 'els_shield.kicad_pcb')
DSN = os.path.join(HERE, 'build', 'els_shield.dsn')
NS = uuid.UUID('6f1a3c52-9f3e-4b1e-9d6a-0e2c7e5a1b11')
MM = pcbnew.FromMM

W, H = 96.0, 126.0           # размер платы, мм (rev 2: Discovery сверху в гнёздах, разъёмы по краям)
DX, DY = 15.0, 11.0          # левый верхний угол платы Discovery на шилде (ST-LINK USB — к верхнему краю)
J1_POS = (DX + 6.33, DY + 33.82)            # вывод 1 разъёма P1 (UM1472, рис. 10)
J2_POS = (DX + 66 - 6.33 - 2.54, DY + 33.82)
EDGE = 0.8                   # отступ корпуса разъёма от края платы

# Разъёмы по краям — со стороны «своих» выводов Discovery (P1 слева, P2 справа), сверху вниз / слева направо.
# Верхний край свободен: там USB ST-LINK. Нижний micro-USB (OTG) прошивкой не используется — снизу разъёмы.
SIDE_LEFT = ['J11', 'J4', 'J13', 'J16', 'J5', 'J15', 'J6', 'J9']
SIDE_RIGHT = ['J12', 'J10', 'J14', 'J18', 'J8', 'J22', 'J23']
SIDE_BOTTOM = ['J17', 'J20', 'J19', 'J3', 'J21', 'J7']
HOLES = [(3.5, 3.5), (W - 3.5, 3.5), (3.5, H - 3.5), (W - 3.5, H - 3.5)]


def pt(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


def load(fpid):
    lib, name = fpid.split(':')
    f = pcbnew.FootprintLoad(os.path.join(FPDIR, lib + '.pretty'), name)
    f.SetFPID(pcbnew.LIB_ID(lib, name))
    return f


def pad1_to(f, x, y):
    p1 = [p for p in f.Pads() if p.GetNumber() == '1'][0].GetPosition()
    pos = f.GetPosition()
    f.SetPosition(pcbnew.VECTOR2I(pos.x + MM(x) - p1.x, pos.y + MM(y) - p1.y))


def bbox(f):
    """Габарит по слою крепления корпуса (courtyard), мм: x0, y0, x1, y1."""
    b = f.GetCourtyard(pcbnew.F_CrtYd).BBox() if f.GetCourtyard(pcbnew.F_CrtYd).OutlineCount() else f.GetBoundingBox()
    return pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()), pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())


def move_by(f, dx, dy):
    pos = f.GetPosition()
    f.SetPosition(pcbnew.VECTOR2I(pos.x + MM(dx), pos.y + MM(dy)))


def place_side(fps, refs, side, lo, hi):
    """Разъёмы вдоль края: равные промежутки между корпусами в отрезке [lo, hi] мм."""
    for ref in refs:
        f = fps[ref]
        f.SetOrientationDegrees(0 if side == 'bottom' else 270)   # вывод 1 — сверху / слева
        if ref == 'J7':                          # IDC: ключ корпуса — наружу (к краю платы)
            f.SetOrientationDegrees(90)
        pad1_to(f, 0, 0)
    sizes = []
    for ref in refs:
        x0, y0, x1, y1 = bbox(fps[ref])
        sizes.append((x1 - x0) if side == 'bottom' else (y1 - y0))
    gap = (hi - lo - sum(sizes)) / max(len(refs) - 1, 1)
    if gap < 0.5:
        raise SystemExit(f'край {side}: разъёмы не помещаются ({sum(sizes):.1f} мм на {hi - lo:.1f})')
    pos = lo
    for ref, sz in zip(refs, sizes):
        f = fps[ref]
        x0, y0, x1, y1 = bbox(f)
        if side == 'left':
            move_by(f, EDGE - x0, pos - y0)
        elif side == 'right':
            move_by(f, (W - EDGE) - x1, pos - y0)
        else:                                    # у IDC метка вывода 1 — за корпусом: отступ больше
            move_by(f, pos - x0, (H - EDGE - (1.2 if ref == 'J7' else 0)) - y1)
        pos += sz + gap


def place_smd(fps, parts):
    """Мелочь под Discovery: каждая деталь — в ближайшую свободную клетку к среднему положению выводов,
    с которыми она соединена (кроме питания и земли). Клетки — вне гнёзд P1/P2."""
    def pad_xy(ref, num):
        for p in fps[ref].Pads():
            if p.GetNumber() == str(num):
                v = p.GetPosition(); return pcbnew.ToMM(v.x), pcbnew.ToMM(v.y)
    nets = design.nets()
    SX, SY = 4.6, 3.9
    cells = []
    x = DX + 2.5
    while x < DX + 66 - 2.5:
        y = DY + 3.0
        while y < DY + 97 - 3.0:
            in_hdr = (J1_POS[1] - 2.5 < y < J1_POS[1] + 24 * 2.54 + 2.5) and \
                     (J1_POS[0] - 4.2 < x < J1_POS[0] + 2.54 + 4.2 or J2_POS[0] - 4.2 < x < J2_POS[0] + 2.54 + 4.2)   # SOD-123 длиннее 0805
            if not in_hdr:
                cells.append((x, y))
            y += SY
        x += SX
    free = set(range(len(cells)))
    # сначала ключи (у них больше связей), потом остальное — порядок по числу сигнальных связей
    def targets(p):
        pts = []
        for pin, net in p['pins'].items():
            if net in ('GND', '+3V', '+5V'):
                continue
            for ref, num in nets[net]:
                if ref != p['ref'] and not ref.startswith(('R', 'C', 'Q')):
                    xy = pad_xy(ref, num)
                    if xy: pts.append(xy)
        return pts
    order = sorted(parts, key=lambda p: -len(targets(p)))
    placed = {}
    for p in order:
        pts = targets(p)
        if not pts:                              # связана только через другую мелочь — рядом с ней
            for pin, net in p['pins'].items():
                for ref, num in nets.get(net, []):
                    if ref in placed and ref != p['ref']:
                        pts.append(placed[ref])
        tx = sum(a for a, b in pts) / len(pts) if pts else DX + 33
        ty = sum(b for a, b in pts) / len(pts) if pts else DY + 20
        best = min(free, key=lambda i: (cells[i][0] - tx) ** 2 + (cells[i][1] - ty) ** 2)
        free.discard(best)
        if p['ref'].startswith('Q'):             # SOT-23 выше 0805: клетки над и под ним свободны — под надписи
            near = [i for i in free if abs(cells[i][0] - cells[best][0]) < 0.1 and abs(abs(cells[i][1] - cells[best][1]) - SY) < 0.1]
            free.difference_update(near)
        cx, cy = cells[best]
        f = fps[p['ref']]
        f.SetOrientationDegrees(0)
        f.SetPosition(pt(cx, cy))
        placed[p['ref']] = (cx, cy)


def silk_text(board, text, x, y, size=0.9, angle=0, just='center'):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size))); t.SetTextThickness(MM(size * 0.15))
    t.SetPosition(pt(x, y)); t.SetTextAngleDegrees(angle)
    t.SetHorizJustify({'left': pcbnew.GR_TEXT_H_ALIGN_LEFT, 'right': pcbnew.GR_TEXT_H_ALIGN_RIGHT,
                       'center': pcbnew.GR_TEXT_H_ALIGN_CENTER}[just])
    board.Add(t)


def main_place():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = MM(0.2)
    ds.m_ViasMinSize = MM(0.6)
    ds.m_MinClearance = MM(0.2)

    nets = {}
    for name in sorted(design.nets()):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni

    fps = {}
    for p in design.PARTS:
        f = load(p['fp'])
        f.SetReference(p['ref']); f.SetValue(p['value'])
        f.SetPath(pcbnew.KIID_PATH('/' + str(uuid.uuid5(NS, 'sym/' + p['ref']))))
        for pad in f.Pads():
            n = pad.GetNumber()
            if n.isdigit() and int(n) in p['pins']:
                pad.SetNet(nets[p['pins'][int(n)]])
        board.Add(f)
        fps[p['ref']] = f

    # ── Гнёзда под Discovery ──────────────────────────────────────────────────
    pad1_to(fps['J1'], *J1_POS)
    pad1_to(fps['J2'], *J2_POS)
    for ref in ('J1', 'J2'):                     # под Discovery — обозначения не нужны (на слое Fab)
        fps[ref].Reference().SetLayer(pcbnew.F_Fab)
    # ── Разъёмы по краям ──────────────────────────────────────────────────────
    place_side(fps, SIDE_LEFT, 'left', 7.0, H - 12.0)       # нижние углы — под подписи нижнего ряда
    place_side(fps, SIDE_RIGHT, 'right', 7.0, H - 12.0)
    place_side(fps, SIDE_BOTTOM, 'bottom', 15.0, W - 15.0)
    # ── Мелочь под Discovery ──────────────────────────────────────────────────
    smd = [p for p in design.PARTS if p['ref'][0] in 'RCQD']
    place_smd(fps, smd)
    # для ручной сборки на шелкографии мелочи — номинал («1k», «3904»), позиция — в BOM и на слое Fab
    for p in smd:
        f = fps[p['ref']]
        f.Reference().SetLayer(pcbnew.F_Fab)
        v = f.Value()
        v.SetText(p['value'].replace('MMBT', '').replace(' 10V', ''))
        v.SetLayer(pcbnew.F_SilkS); v.SetVisible(True)
        v.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8))); v.SetTextThickness(MM(0.12))
        v.SetPosition(pcbnew.VECTOR2I(f.GetPosition().x, MM(bbox(f)[1] - 0.55)))   # над корпусом (SOT-23 выше 0805)
    for ref, *_ in design.TERMINALS:
        fps[ref].Reference().SetLayer(pcbnew.F_Fab)
    # ── Крепёж ────────────────────────────────────────────────────────────────
    for ref, (x, y) in zip(['H1', 'H2', 'H3', 'H4'], HOLES):
        fps[ref].SetPosition(pt(x, y)); fps[ref].Reference().SetVisible(False)

    # ── Контур платы ──────────────────────────────────────────────────────────
    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for (x1, y1), (x2, y2) in zip(corners, corners[1:] + corners[:1]):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x1, y1)); s.SetEnd(pt(x2, y2)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
        board.Add(s)
    # Контур Discovery на шелкографии
    for (x1, y1), (x2, y2) in zip([(DX, DY), (DX + 66, DY), (DX + 66, DY + 97), (DX, DY + 97)],
                                  [(DX + 66, DY), (DX + 66, DY + 97), (DX, DY + 97), (DX, DY)]):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x1, y1)); s.SetEnd(pt(x2, y2)); s.SetLayer(pcbnew.F_SilkS); s.SetWidth(MM(0.15))
        board.Add(s)

    # ── Подписи разъёмов: название — над/рядом с корпусом, выводы — у каждого контакта ─────────
    for ref, title, nets_, labels in design.TERMINALS:
        f = fps[ref]
        pads = sorted([p for p in f.Pads() if p.GetNumber().isdigit()], key=lambda p: int(p.GetNumber()))
        x0, y0, x1, y1 = bbox(f)
        if ref == 'J7':
            # IDC: легенда «номер вывода — режим» (вывод 1 — треугольник на корпусе, квадратная площадка)
            labs = labels + ['GND']
            half = (len(labs) + 1) // 2
            l1 = '  '.join(f'{i + 1} {labs[i]}' for i in range(half))
            l2 = '  '.join(f'{i + 1} {labs[i]}' for i in range(half, len(labs)))
            silk_text(board, l1, (x0 + x1) / 2, y0 - 2.3, 0.8)
            silk_text(board, l2, (x0 + x1) / 2, y0 - 1.0, 0.8)
            silk_text(board, title + ' (вывод N = режим M N)', (x0 + x1) / 2, y0 - 3.7, 0.9)
            continue
        side = 'left' if ref in SIDE_LEFT else 'right' if ref in SIDE_RIGHT else 'bottom'
        for pad, lab in zip(pads, labels):
            v = pad.GetPosition(); px, py = pcbnew.ToMM(v.x), pcbnew.ToMM(v.y)
            if side == 'left':
                silk_text(board, lab, x1 + 0.6, py, 0.8, 0, 'left')
            elif side == 'right':
                silk_text(board, lab, x0 - 0.6, py, 0.8, 0, 'right')
            else:
                silk_text(board, lab, px, y0 - 0.6, 0.8, 90, 'left')
        if side == 'bottom':
            silk_text(board, title, (x0 + x1) / 2, y0 - 5.4, 0.9)
        else:                                    # название — вертикально, за подписями выводов
            silk_text(board, title, x1 + 5.6 if side == 'left' else x0 - 5.6, (y0 + y1) / 2, 0.9, 90)
    silk_text(board, 'ELS shield STM32F4DISCOVERY rev 2', W / 2, 3.2, 1.2)
    silk_text(board, 'DISCOVERY: USB ST-LINK вверх, лицом вверх', W / 2, DY + 6, 1.0)

    # Земляные полигоны — после трассировки (fix_zones.py): GND разводится дорожками, как остальные цепи,
    # иначе площадки в тесных местах (эмиттеры ключей) остаются только на оторванных кусках полигона

    os.makedirs(os.path.dirname(DSN), exist_ok=True)
    board.Save(PCB)
    write_project()
    print('размещено:', PCB)


def write_project():
    """Файл проекта KiCad: классы цепей (питание — дорожки 0.6 мм) и правила."""
    import json
    cls = lambda name, tw, vd, vdr: {'name': name, 'track_width': tw, 'clearance': 0.25, 'via_diameter': vd,
                                     'via_drill': vdr, 'microvia_diameter': 0.3, 'microvia_drill': 0.1,
                                     'diff_pair_gap': 0.25, 'diff_pair_width': 0.2, 'diff_pair_via_gap': 0.25,
                                     'wire_width': 6, 'bus_width': 12, 'line_style': 0, 'schematic_color': 'rgba(0, 0, 0, 0.000)',
                                     'pcb_color': 'rgba(0, 0, 0, 0.000)', 'priority': 2147483647 if name == 'Default' else 0}
    # +5V — дисплей 10.1" (до 1.5 А): дорожки 1.0 мм; +3V и GND — 0.6 мм (GND ещё и полигоны)
    pro = {'meta': {'filename': 'els_shield.kicad_pro', 'version': 1},
           'net_settings': {'classes': [cls('Default', 0.3, 0.8, 0.4), cls('Power', 0.6, 1.0, 0.5),
                                        cls('Power5', 1.0, 1.2, 0.6)],
                            'meta': {'version': 3},
                            'netclass_patterns': [{'netclass': 'Power', 'pattern': n} for n in ('+3V', 'GND')] +
                                                 [{'netclass': 'Power5', 'pattern': n} for n in ('+5V', '+5V_DISCO')]}}
    with open(os.path.join(HERE, 'els_shield.kicad_pro'), 'w', encoding='utf-8') as f:
        json.dump(pro, f, indent=2, ensure_ascii=False)


def main_import(ses):
    board = pcbnew.LoadBoard(PCB)
    if not pcbnew.ImportSpecctraSES(board, ses):
        print('ошибка загрузки SES'); sys.exit(1)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(PCB)
    tracks = [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']
    vias = [t for t in board.GetTracks() if t.GetClass() == 'PCB_VIA']
    print(f'дорожек {len(tracks)}, переходов {len(vias)}')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'import':
        main_import(sys.argv[2])
    else:
        main_place()
