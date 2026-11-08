#!/usr/bin/env python3
"""
Поштырьковая проверка разведённого шилда (els_shield.kicad_pcb) → CHECK.md.

Для каждого контакта каждого разъёма: подпись на шелкографии рядом с контактом (как её увидит сборщик),
цепь по меди, путь через резисторы/транзисторы/диод до гнезда Discovery, вывод STM32 (таблица P1/P2 из
design.py, сверена с UM1472 табл. 5 и рис. 10) и функция в прошивке (Core/e2/e2_pins.h, Core/els_config.h).
Плюс: каждый вывод прошивки выведен на разъём; ширина дорожек питания; что подключено к питанию.

Запуск Python-ом KiCad:  $KPY hardware/shield/verify.py
"""
import os, re, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design

ROOT = design.ROOT
M = pcbnew.ToMM
POWER = ('GND', '+5V', '+3V', '+5V_DISCO')

board = pcbnew.LoadBoard(os.path.join(HERE, 'els_shield.kicad_pcb'))

# ── Функции выводов из прошивки ──────────────────────────────────────────────────────────────────────────
pins_h = open(os.path.join(ROOT, 'Core', 'e2', 'e2_pins.h'), encoding='utf-8').read()
FW = {}
for name, port, n, comment in re.findall(r'#define E2_PIN_(\w+)\s+([A-E]),\s*(\d+)\s*//\s*(.*)', pins_h):
    FW[name] = (f'P{port}{n}', comment.strip())
cfg = open(os.path.join(ROOT, 'Core', 'els_config.h'), encoding='utf-8').read()
for k, c in re.findall(r'#define\s+(SCALE_[ZX]_PIN_(?:A|B|REQ))\s+(P[A-E]\d+)', cfg):
    FW[k.replace('_PIN', '')] = (c, 'линейка напрямую (els_config.h)')

# ── Медь: цепь каждой площадки ───────────────────────────────────────────────────────────────────────────
fps = {f.GetReference(): f for f in board.GetFootprints()}
pad_net = {}
net_pads = {}
for f in board.GetFootprints():
    for p in f.Pads():
        key = (f.GetReference(), p.GetNumber())
        pad_net[key] = p.GetNetname()
        net_pads.setdefault(p.GetNetname(), []).append(key)

HDR = {}                                       # (J1|J2, вывод) → сигнал Discovery
for n, s in design.P1.items():
    HDR[('J1', str(n))] = s
for n, s in design.P2.items():
    HDR[('J2', str(n))] = s

problems = []


def to_header(net, seen=None, path=None):
    """Путь от цепи до гнезда Discovery через мелочь (R/C/Q/D). → (гнездо, вывод, [звенья]) или None."""
    seen = seen or set()
    path = path or []
    seen.add(net)
    for ref, num in net_pads.get(net, []):
        if ref in ('J1', 'J2'):
            return ref, num, path
    for ref, num in net_pads.get(net, []):
        if ref[0] not in 'RQD':
            continue
        for (r2, n2), net2 in pad_net.items():
            if r2 != ref or net2 == net or net2 in POWER or net2 in seen:
                continue
            fp = fps[ref]
            step = f'{ref} ({fp.GetValue()})'
            res = to_header(net2, seen, path + [step])
            if res:
                return res
    return None


# ── Подписи на шелкографии ───────────────────────────────────────────────────────────────────────────────
silk = []
for d in board.GetDrawings():
    if d.GetClass() == 'PCB_TEXT' and d.GetLayer() == pcbnew.F_SilkS:
        silk.append((d.GetText(), d.GetPosition()))


def label_at(pad):
    """Подпись контакта: ближайший текст в той же строке (боковые разъёмы) или в том же столбце (нижние)."""
    p = pad.GetPosition()
    best = None
    for text, q in silk:
        dx, dy = abs(M(q.x - p.x)), abs(M(q.y - p.y))
        if (dy < 0.3 and dx < 7) or (dx < 0.3 and dy < 7):
            d = dx + dy
            if best is None or d < best[0]:
                best = (d, text)
    return best[1] if best else '—'


def fw_name_of(sig):
    for k, (pin, _) in FW.items():
        if pin == sig and k not in ('DRO_TX', 'AUDIO_RESET'):
            return k
    return None


# ── Разъёмы ──────────────────────────────────────────────────────────────────────────────────────────────
rows = []
used_fw = set()
for ref, title, nets, labels in design.TERMINALS:
    f = fps[ref]
    pads = sorted([p for p in f.Pads() if p.GetNumber().isdigit()], key=lambda p: int(p.GetNumber()))
    want = labels + (['GND'] if ref == 'J7' else [])
    for pad, lab_want in zip(pads, want):
        net = pad.GetNetname()
        lab = label_at(pad)
        if ref == 'J7':                          # легенда над коробкой: «N метка»
            legend = ' '.join(t for t, q in silk)
            lab = lab_want if re.search(r'(^|\s)' + pad.GetNumber() + r' ' + re.escape(lab_want) + r'(\s|$)', legend) else '—'
        if lab != lab_want:
            problems.append(f'{ref}.{pad.GetNumber()}: подпись «{lab}», ожидалась «{lab_want}»')
        if net in POWER:
            rows.append((ref, title, pad.GetNumber(), lab, net, 'питание / земля', '', '', ''))
            continue
        res = to_header(net)
        if not res:
            problems.append(f'{ref}.{pad.GetNumber()} ({net}): не дошёл до Discovery')
            rows.append((ref, title, pad.GetNumber(), lab, net, '— НЕТ ПУТИ', '', '', ''))
            continue
        h, hn, path = res
        sig = HDR.get((h, hn), '?')
        fwn = fw_name_of(sig)
        if fwn:
            used_fw.add(fwn)
        fwc = FW[fwn][1] if fwn else '— нет в прошивке'
        if not fwn:
            problems.append(f'{ref}.{pad.GetNumber()} ({net}) → {h}.{hn} {sig}: вывод не используется прошивкой')
        exp = net.replace('_N', '').replace('_K', '').replace('_OUT', '').replace('_T', '')
        if fwn and fwn not in (net, exp) and not net.endswith(('_PUL_N', '_DIR_N', '_ENA_N', 'BEEP_N')):
            problems.append(f'{ref}.{pad.GetNumber()}: цепь {net}, а вывод {sig} в прошивке — {fwn}')
        rows.append((ref, title, pad.GetNumber(), lab, net, ' → '.join(path) or 'напрямую',
                     f'{"P1" if h == "J1" else "P2"}-{hn}', sig, f'{fwn}: {fwc}' if fwn else fwc))

missing = [k for k in FW if k not in used_fw and k not in ('DRO_TX', 'AUDIO_RESET')]
for k in missing:
    problems.append(f'вывод прошивки {k} ({FW[k][0]}) не выведен ни на один разъём')

# ── Питание: ширина дорожек и что подключено ─────────────────────────────────────────────────────────────
pw = []
for net in POWER:
    ws = [M(t.GetWidth()) for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK' and t.GetNetname() == net]
    who = sorted({r for r, n in net_pads.get(net, [])}, key=lambda r: (r[0], int(re.sub(r'\D', '', r) or 0)))
    pw.append((net, len(ws), min(ws) if ws else 0, max(ws) if ws else 0, ', '.join(who)))
    if ws and min(ws) < 0.5 and net != 'GND':
        problems.append(f'{net}: есть дорожки уже 0.5 мм (мин. {min(ws):.2f})')

# ── Отчёт ────────────────────────────────────────────────────────────────────────────────────────────────
out = ['# Проверка шилда по контактам', '',
       'Сгенерировано `verify.py` из разведённой платы `els_shield.kicad_pcb`: подпись — текст шелкографии у контакта,',
       'путь — по меди через детали шилда, вывод Discovery — по таблице UM1472 (табл. 5, рис. 10).', '',
       f'**Замечаний: {len(problems)}**', '']
for p in problems:
    out.append(f'- {p}')
out += ['', '## Разъёмы', '', '| Разъём | Конт. | Подпись | Цепь | Через | Discovery | STM32 | Прошивка |',
        '|---|---|---|---|---|---|---|---|']
last = None
for ref, title, n, lab, net, path, hdr, sig, fw in rows:
    head = f'{ref} {title}' if ref != last else ''
    last = ref
    out.append(f'| {head} | {n} | {lab} | {net} | {path} | {hdr} | {sig} | {fw} |')
out += ['', '## Питание', '', '| Цепь | Дорожек | Ширина мин–макс, мм | Подключено |', '|---|---|---|---|']
for net, cnt, mn, mx, who in pw:
    out.append(f'| {net} | {cnt} | {mn:.2f}–{mx:.2f} | {who} |')
open(os.path.join(HERE, 'CHECK.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print(f'CHECK.md: строк {len(rows)}, замечаний {len(problems)}')
for p in problems:
    print(' ', p)
