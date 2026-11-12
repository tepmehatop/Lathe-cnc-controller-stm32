#!/usr/bin/env python3
"""
Волновой трассировщик по сетке 0.1 мм для нескольких оставшихся соединений (там, где Freerouting не довёл).
Препятствия — вся медь платы без полигонов (картинки слоёв F.Cu / B.Cu, 40 точек на мм), расширенная с запасом:
дорожка 0.2 мм + зазор 0.25 → 0.4 мм; переход ⌀0.6 + зазор 0.25 → 0.6 мм; край платы — 0.6 / 0.8 мм.
Ищет путь Дейкстрой (8 направлений, смена слоя — переход, дороже), печатает отрезки. Решение проверяет DRC KiCad.

  python3 route_grid.py F.png B.png  x0 y0 r0  target  → печать пути (r0 < 0 — старт с SMD-площадки на F)
  target: «pad x y r» (круглая площадка, оба слоя) | «smd x y w h» (площадка на F) | «via x y» (переход, оба слоя)
"""
import heapq, sys
from PIL import Image, ImageFilter

PX = 40                      # точек на мм в картинках
G = 10                       # клеток на мм в сетке (0.1 мм)
W_MM, H_MM = 96, 126
VIA_COST = 40


def mask(path, erase):
    im = Image.open(path).convert('L').resize((W_MM * PX, H_MM * PX))
    m = im.point(lambda v: 255 if v < 235 else 0)          # медь — не белое
    from PIL import ImageDraw
    d = ImageDraw.Draw(m)
    for kind, *a in erase:                                    # своя медь старта и цели — не препятствие
        if kind == 'c':
            x, y, r = a
            d.ellipse([(x - r) * PX, (y - r) * PX, (x + r) * PX, (y + r) * PX], fill=0)
        else:
            x, y, w, h = a
            d.rectangle([(x - w / 2) * PX, (y - h / 2) * PX, (x + w / 2) * PX, (y + h / 2) * PX], fill=0)
    small = m.resize((W_MM * G, H_MM * G), Image.BOX).point(lambda v: 255 if v > 0 else 0)
    return small


def dil(m, cells):
    return m.filter(ImageFilter.MaxFilter(2 * cells + 1))


def edge_block(cells):
    m = Image.new('L', (W_MM * G, H_MM * G), 0)
    from PIL import ImageDraw
    d = ImageDraw.Draw(m)
    d.rectangle([0, 0, W_MM * G - 1, H_MM * G - 1], outline=255, width=cells)
    return m


def main():
    fpng, bpng = sys.argv[1], sys.argv[2]
    sx, sy, sr = map(float, sys.argv[3:6])          # sr < 0 — старт с SMD-площадки (только слой F), радиус |sr|
    slayers = 'F' if sr < 0 else 'FB'
    sr = abs(sr)
    t = sys.argv[6:]
    erase = [('c', sx, sy, sr)]
    if t[0] == 'pad':
        tx, ty, tr = map(float, t[1:4]); erase.append(('c', tx, ty, tr)); tlayers = 'FB'
    elif t[0] == 'via':
        tx, ty = map(float, t[1:3]); tr = 0.3; erase.append(('c', tx, ty, 0.35)); tlayers = 'FB'
    else:
        tx, ty, tw, th = map(float, t[1:5]); erase.append(('r', tx, ty, tw, th)); tlayers = 'F'
    raw = {'F': mask(fpng, erase),
           'B': mask(bpng, [e for i, e in enumerate(erase) if (i == 0 and slayers == 'FB') or (i > 0 and t[0] != 'smd')])}
    e6, e8 = edge_block(6), edge_block(8)
    trk = {L: dil(raw[L], 4) for L in 'FB'}
    via = {L: dil(raw[L], 6) for L in 'FB'}
    Wc, Hc = W_MM * G, H_MM * G
    tp = {L: trk[L].load() for L in 'FB'}
    vp = {L: via[L].load() for L in 'FB'}
    ep6, ep8 = e6.load(), e8.load()

    def ok(L, x, y):
        return 0 <= x < Wc and 0 <= y < Hc and tp[L][x, y] == 0 and ep6[x, y] == 0

    def via_ok(x, y):
        return vp['F'][x, y] == 0 and vp['B'][x, y] == 0 and ep8[x, y] == 0

    def in_target(L, x, y):
        X, Y = x / G, y / G
        if L not in tlayers:
            return False
        if t[0] == 'smd':
            return abs(X - tx) <= tw / 2 - 0.05 and abs(Y - ty) <= th / 2 - 0.05
        return (X - tx) ** 2 + (Y - ty) ** 2 <= (tr * 0.8) ** 2

    start = [(L, round(sx * G), round(sy * G)) for L in slayers]
    dist = {}
    prev = {}
    pq = []
    for s in start:
        dist[s] = 0; heapq.heappush(pq, (0, s))
    steps = [(1, 0, 10), (-1, 0, 10), (0, 1, 10), (0, -1, 10), (1, 1, 14), (1, -1, 14), (-1, 1, 14), (-1, -1, 14)]
    found = None
    while pq:
        d, n = heapq.heappop(pq)
        if d > dist.get(n, 1e18):
            continue
        L, x, y = n
        if in_target(L, x, y) and d > 0:
            found = n; break
        for dx, dy, c in steps:
            m = (L, x + dx, y + dy)
            if ok(L, x + dx, y + dy) or in_target(L, x + dx, y + dy):
                nd = d + c
                if nd < dist.get(m, 1e18):
                    dist[m] = nd; prev[m] = n; heapq.heappush(pq, (nd, m))
        if via_ok(x, y):
            m = ('B' if L == 'F' else 'F', x, y)
            nd = d + VIA_COST * 10
            if nd < dist.get(m, 1e18):
                dist[m] = nd; prev[m] = n; heapq.heappush(pq, (nd, m))
    if not found:
        print('ПУТИ НЕТ'); return
    path = [found]
    while path[-1] in prev:
        path.append(prev[path[-1]])
    path.reverse()
    # отрезки: по слоям и направлениям
    segs, vias = [], []
    cur = [path[0]]
    for a, b in zip(path, path[1:]):
        if a[0] != b[0]:
            segs.append(cur); vias.append((a[1] / G, a[2] / G)); cur = [b]; continue
        if len(cur) >= 2:
            d0 = (cur[-1][1] - cur[-2][1], cur[-1][2] - cur[-2][2])
            d1 = (b[1] - a[1], b[2] - a[2])
            if d0 != d1:
                segs.append(cur); cur = [cur[-1]]
        cur.append(b)
    segs.append(cur)
    print(f'длина пути {dist[found] / 100:.1f} мм (усл.), переходов {len(vias)}')
    for s in segs:
        if len(s) >= 2:
            print(f'SEG {s[0][0]} {s[0][1] / G:.2f} {s[0][2] / G:.2f} {s[-1][1] / G:.2f} {s[-1][2] / G:.2f}')
    for v in vias:
        print(f'VIA {v[0]:.2f} {v[1]:.2f}')


if __name__ == '__main__':
    main()
