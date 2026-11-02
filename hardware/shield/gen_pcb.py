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

W, H = 180.0, 146.0          # размер платы, мм
DX, DY = 52.0, 14.0          # левый верхний угол платы Discovery на шилде
J1_POS = (DX + 6.33, DY + 33.82)            # вывод 1 разъёма P1 (UM1472, рис. 10)
J2_POS = (DX + 66 - 6.33 - 2.54, DY + 33.82)


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


def orient_terminal(f, side):
    """Повернуть клеммник входом провода к краю платы. У клеммников Phoenix из библиотеки
    KiCad при повороте 0° вход провода — в сторону +Y (там метка 1-го вывода и лицевые
    линии корпуса на слое Fab). Перед заказом стоит проверить по 3D-виду."""
    f.SetOrientationDegrees({'left': 270, 'right': 90, 'bottom': 0}[side])


def main_place():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = MM(0.25)
    ds.m_ViasMinSize = MM(0.6)
    ds.m_MinClearance = MM(0.2)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(MM(0.3)); nc.SetClearance(MM(0.25)); nc.SetViaDiameter(MM(0.8)); nc.SetViaDrill(MM(0.4))
    try:  # класс «Питание» — дорожки 0.6 мм
        pw = pcbnew.NETCLASS('Power')
        pw.SetTrackWidth(MM(0.6)); pw.SetClearance(MM(0.25)); pw.SetViaDiameter(MM(1.0)); pw.SetViaDrill(MM(0.5))
        ds.m_NetSettings.m_NetClasses['Power'] = pw
    except Exception as e:
        print('класс цепей Power не задан:', e)
        pw = None

    nets = {}
    for name in sorted(design.nets()):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
        if pw is not None and name in ('+5V', '+3V', 'GND'):
            ni.SetNetClass(pw)

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

    # ── Разъёмы под Discovery ─────────────────────────────────────────────────
    pad1_to(fps['J1'], *J1_POS)
    pad1_to(fps['J2'], *J2_POS)

    # ── Клеммники по краям ────────────────────────────────────────────────────
    def column(refs, side, x, y, gap=4.0):
        for ref in refs:
            f = fps[ref]
            orient_terminal(f, side)
            pad1_to(f, 0, 0)
            bb = f.GetBoundingBox(False, False)       # габарит корпуса относительно вывода 1
            if side == 'bottom':
                lo, hi = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetRight())
                pad1_to(f, y - lo, x)
            else:
                lo, hi = pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetBottom())
                pad1_to(f, x, y - lo)
            y += (hi - lo) + gap
    column(['J7', 'J8', 'J9', 'J10', 'J11', 'J12'], 'left', 9.0, 9.0, gap=2.0)
    column(['J5', 'J6', 'J15', 'J16', 'J4', 'J13', 'J14'], 'right', W - 9.0, 9.0, gap=2.0)
    column(['J17', 'J18', 'J19', 'J20', 'J21', 'J22', 'J23', 'J3'], 'bottom', H - 9.0, 24.0, gap=2.0)

    # ── Подтяжки и фильтры — слева от Discovery ───────────────────────────────
    for i in range(4):
        f = fps[f'RN{i + 1}']; f.SetOrientationDegrees(270)
        pad1_to(f, 23.0 + i * 5.5, 22.0)
    for i, ref in enumerate(['R1', 'C1', 'C2', 'C3', 'C4', 'C5']):
        f = fps[ref]; f.SetOrientationDegrees(270)
        pad1_to(f, 23.0 + (i % 6) * 4.6, 55.0)
    # ── NPN-ключи — справа от Discovery, по ключу в строке ────────────────────
    keys = ['Z_STEP', 'Z_DIR', 'Z_ENA', 'X_STEP', 'X_DIR', 'X_ENA', 'BEEPER']
    for i, sig in enumerate(keys):
        rb, rpd, q = f'R{2 + i * 2}', f'R{3 + i * 2}', f'Q{i + 1}'
        col, row = i % 2, i // 2
        x0, y0 = 124.0 + col * 21.0, 16.0 + row * 14.0
        pad1_to(fps[rb], x0, y0)
        pad1_to(fps[rpd], x0, y0 + 4.5)
        fps[q].SetOrientationDegrees(0)
        pad1_to(fps[q], x0 + 11.0, y0 + 2.2)
    # ── Резисторы светодиодов, тахометра, линеек — под Discovery снизу ────────
    rest = [f'R{n}' for n in range(16, 27)]
    for i, ref in enumerate(rest):
        f = fps[ref]; f.SetOrientationDegrees(270)
        pad1_to(f, 40.0 + i * 5.0, 114.0)
    # ── Питание ───────────────────────────────────────────────────────────────
    for i, ref in enumerate(['C6', 'C7', 'C8']):
        f = fps[ref]; f.SetOrientationDegrees(0)
        pad1_to(f, 132.0 + i * 8.0, 112.0)
    # ── Крепёж ────────────────────────────────────────────────────────────────
    for ref, (x, y) in zip(['H1', 'H2', 'H3', 'H4'], [(4.5, 4.5), (W - 4.5, 4.5), (4.5, H - 4.5), (W - 4.5, H - 4.5)]):
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

    # ── Подписи клеммников ────────────────────────────────────────────────────
    for k, (ref, title, nets_, labels) in enumerate(design.TERMINALS):
        f = fps[ref]
        pads = sorted([p for p in f.Pads() if p.GetNumber().isdigit()], key=lambda p: int(p.GetNumber()))
        for pad, lab in zip(pads, labels):
            t = pcbnew.PCB_TEXT(board)
            t.SetText(lab); t.SetLayer(pcbnew.F_SilkS)
            t.SetTextSize(pcbnew.VECTOR2I(MM(0.9), MM(0.9))); t.SetTextThickness(MM(0.13))
            pos = pad.GetPosition()
            side = 'left' if pcbnew.ToMM(pos.x) < 30 else 'right' if pcbnew.ToMM(pos.x) > W - 30 else 'bottom'
            if side == 'left':
                t.SetPosition(pcbnew.VECTOR2I(pos.x + MM(4.4), pos.y)); t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
            elif side == 'right':
                t.SetPosition(pcbnew.VECTOR2I(pos.x - MM(4.4), pos.y)); t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
            else:
                t.SetPosition(pcbnew.VECTOR2I(pos.x, pos.y - MM(4.4))); t.SetTextAngleDegrees(90)
                t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
            board.Add(t)
        # название блока
        t = pcbnew.PCB_TEXT(board)
        t.SetText(f'{ref} {title}'); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(MM(1.1), MM(1.1))); t.SetTextThickness(MM(0.16))
        p0 = pads[0].GetPosition()
        if pcbnew.ToMM(p0.y) > H - 20:
            # нижний ряд: подписи блоков на двух уровнях, чтобы не налезали
            xs = [pcbnew.ToMM(p.GetPosition().x) for p in pads]
            t.SetPosition(pt(min(xs) - 1.0, pcbnew.ToMM(p0.y) - (10.0 if k % 2 else 13.0)))
            t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT); t.SetText(title)
            t.SetTextAngleDegrees(0)
        else:
            # слева/справа: подпись вдоль клеммника, по центру его высоты
            dx = MM(10.0) if pcbnew.ToMM(p0.x) < 30 else -MM(10.0)
            ys = [p.GetPosition().y for p in pads]
            t.SetText(title); t.SetTextSize(pcbnew.VECTOR2I(MM(0.9), MM(0.9))); t.SetTextThickness(MM(0.14))
            t.SetPosition(pcbnew.VECTOR2I(p0.x + dx, (min(ys) + max(ys)) // 2)); t.SetTextAngleDegrees(90)
            t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
        board.Add(t)
    t = pcbnew.PCB_TEXT(board)
    t.SetText('ELS shield for STM32F4DISCOVERY  rev 1'); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(1.5), MM(1.5))); t.SetTextThickness(MM(0.2))
    t.SetPosition(pt(DX + 33, DY - 5)); board.Add(t)
    t = pcbnew.PCB_TEXT(board)
    t.SetText('DISCOVERY: USB вверх, лицом вверх'); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(1.2), MM(1.2))); t.SetTextThickness(MM(0.18))
    t.SetPosition(pt(DX + 33, DY + 15)); board.Add(t)

    # ── Земляные полигоны с двух сторон ───────────────────────────────────────
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(nets['GND'])
        z.SetLocalClearance(MM(0.4)); z.SetMinThickness(MM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.4)); z.SetThermalReliefSpokeWidth(MM(0.5))
        ol = z.Outline(); ol.NewOutline()
        for x, y in [(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)]:
            ol.Append(MM(x), MM(y))
        board.Add(z)

    os.makedirs(os.path.dirname(DSN), exist_ok=True)
    board.Save(PCB)
    write_project()
    if not pcbnew.ExportSpecctraDSN(board, DSN):
        print('ошибка выгрузки DSN'); sys.exit(1)
    print('размещено:', PCB, '\nдля трассировки:', DSN)


def write_project():
    """Файл проекта KiCad: классы цепей (питание — дорожки 0.6 мм) и правила."""
    import json
    cls = lambda name, tw, vd, vdr: {'name': name, 'track_width': tw, 'clearance': 0.25, 'via_diameter': vd,
                                     'via_drill': vdr, 'microvia_diameter': 0.3, 'microvia_drill': 0.1,
                                     'diff_pair_gap': 0.25, 'diff_pair_width': 0.2, 'diff_pair_via_gap': 0.25,
                                     'wire_width': 6, 'bus_width': 12, 'line_style': 0, 'schematic_color': 'rgba(0, 0, 0, 0.000)',
                                     'pcb_color': 'rgba(0, 0, 0, 0.000)', 'priority': 2147483647 if name == 'Default' else 0}
    pro = {'meta': {'filename': 'els_shield.kicad_pro', 'version': 1},
           'net_settings': {'classes': [cls('Default', 0.3, 0.8, 0.4), cls('Power', 0.6, 1.0, 0.5)],
                            'meta': {'version': 3},
                            'netclass_patterns': [{'netclass': 'Power', 'pattern': n} for n in ('+5V', '+3V', 'GND')]}}
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
