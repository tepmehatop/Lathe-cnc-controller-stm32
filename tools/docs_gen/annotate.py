#!/usr/bin/env python3
"""
Снимки экрана для руководства: пронумерованные кружки со стрелками поверх снимка (headless Chrome).
Расшифровка номеров — текстом под рисунком в docs/manual/README.md.

  python3 tools/docs_gen/annotate.py            — все рисунки из MARKS
  python3 tools/docs_gen/annotate.py main_feed  — один

Исходные снимки — docs/manual/raw/<имя>.jpg (экран 1280×800, поворот уже сделан) или <имя>.png (веб-страница,
любой размер), результат — docs/manual/img/<имя>.jpg шириной до 960.
Метка: (номер, x, y, dx, dy) — стрелка от кружка в (x+dx, y+dy) к точке (x, y), координаты снимка в пикселях.
"""
import os, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RAW = os.path.join(ROOT, 'docs', 'manual', 'raw')
OUT = os.path.join(ROOT, 'docs', 'manual', 'img')
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'

# Координаты — по снимку 1280×800 дисплея 10.1" (шапка: режим слева, подрежим, упоры, S1/S2/S3, ⚡, ⚠)
HEAD = [(1, 190, 60, 60, 110), (2, 645, 60, 0, 120), (3, 940, 55, -30, 120), (4, 1093, 55, -40, 120),
        (5, 1167, 55, -10, 140), (6, 1241, 55, -20, 160)]
RIGHT = [(14, 1188, 215, -110, 0), (15, 1188, 447, -110, 0), (16, 1188, 680, -110, 0)]
V = (180, -95)          # кружок над значением ячейки — справа сверху, в пустом месте


def at(n, x, y, d=V):
    return (n, x, y, d[0], d[1])


MARKS = {
    'main_feed': HEAD + [at(7, 160, 255, (270, -70)), at(8, 1000, 250, (-150, -60)), at(9, 160, 485), at(10, 515, 485),
                         at(11, 865, 485), at(12, 120, 710), at(13, 470, 710), at(17, 655, 228, (0, 75))] + RIGHT,
    'limits': [(1, 853, 57, -30, 130), (2, 905, 57, 0, 150), (3, 965, 57, 20, 130), (4, 1020, 57, 30, 150)],
    'thread': [at(1, 160, 255, (270, -70)), at(2, 925, 710, (150, -90)), at(3, 470, 710), at(4, 120, 710)],
    'thread_sm2': [at(1, 120, 485), at(2, 470, 485), at(3, 120, 710), at(4, 655, 228, (0, 80))],
    'thread_type': [(1, 645, 240, 360, 0), (2, 645, 345, 360, 0), (3, 645, 450, 360, 0), (4, 645, 555, 360, 0)],
    'feed_sm2': [at(1, 655, 230, (0, 95)), at(2, 160, 485), at(3, 515, 485), at(4, 865, 485)],
    'afeed': [at(1, 160, 255, (270, -70)), at(2, 120, 710), at(3, 470, 710)],
    'cone_sm2': [at(1, 120, 485), at(2, 470, 485), at(3, 865, 710, (150, -90))],
    'sphere': [at(1, 120, 710), at(2, 470, 710), at(3, 865, 710, (150, -90))],
    # автоцикл в работе (tools/docs_gen/cycle_shots.py)
    'cone_cycle': [(1, 190, 60, 60, 110), (2, 1093, 55, -40, 120), at(3, 120, 710), at(4, 470, 710),
                   at(5, 865, 710, (150, -90)), (6, 940, 55, -30, 120)],
    'sphere_cycle': [(1, 665, 60, 0, 120), at(2, 120, 710), at(3, 470, 710), at(4, 865, 710, (150, -90)),
                     (5, 940, 55, -30, 120)],
    'sphere_sm2': [at(1, 120, 485), at(2, 470, 485)],
    'divider': [at(1, 160, 255, (270, -70)), at(2, 120, 485), at(3, 470, 485), at(4, 160, 710)],
    'joystick': [(1, 370, 287, 0, -110), (2, 167, 447, -10, 120), (3, 577, 447, 10, 120), (4, 370, 610, 140, 0),
                 (5, 370, 447, 0, 0), (6, 965, 270, -90, -100), (7, 965, 405, -320, -40), (8, 965, 540, -320, 40),
                 (9, 965, 657, -320, 40), (10, 1188, 158, -60, 70)],
    'settings': [(1, 335, 195, 0, 85), (2, 943, 195, 0, 85), (3, 335, 378, 0, 85), (4, 943, 378, 0, 85),
                 (5, 425, 625, 400, 40), (6, 1090, 52, -110, 0)],
    'set_mech': [(1, 440, 213, 0, 0), (2, 440, 307, 0, 0), (3, 440, 400, 0, 0), (4, 440, 494, 0, 0),
                 (5, 440, 588, 0, 0), (6, 1066, 588, 0, 70), (7, 228, 695, 0, 70), (8, 640, 695, 0, 70)],
    'set_cal': [(1, 700, 68, 0, 70), (2, 1075, 68, 0, 70), (3, 672, 371, 0, 0), (4, 1057, 371, 0, 0),
                (5, 659, 715, 0, -70), (6, 1070, 715, 0, -70)],
    'numpad': [(1, 640, 160, -360, 0), (2, 945, 282, 120, 0), (3, 560, 687, 0, 90), (4, 885, 687, 0, 90)],
    'cal_run': [(1, 699, 67, 0, 75), (2, 560, 365, -40, -60), (3, 1100, 345, 80, -45), (4, 570, 560, 80, 0),
                (5, 560, 715, -150, 0)],
    'cal_result': [(1, 800, 575, 0, 60), (2, 245, 700, 0, -75)],
    'cal_ind_zero': [(1, 1100, 345, 80, -45), (2, 845, 557, 70, 0), (3, 248, 705, 0, -80)],
    'cal_ind_input': [(1, 960, 168, 190, 0), (2, 885, 688, 230, 0), (3, 560, 688, -350, 0)],
    'gcode_run': [at(1, 160, 255, (270, -70)), at(2, 120, 710), at(3, 470, 710)],
    # веб-страница (снимок 1100 px шириной)
    'web_page': [(1, 420, 118, 180, -45), (2, 874, 112, -20, -55), (3, 957, 112, 0, -55), (4, 1037, 112, 20, -55),
                 (5, 455, 148, 200, 0), (6, 560, 430, 200, 60), (7, 105, 662, 130, 32), (8, 700, 820, 300, -20),
                 (9, 915, 1130, -60, -55), (10, 1029, 1130, 0, -55), (11, 200, 1365, 360, -35), (12, 400, 1380, 200, 20)],
    'web_prestart': [(1, 250, 560, -110, 0), (2, 610, 860, -80, 40), (3, 757, 860, 100, 40)],
    'web_run': [(1, 400, 162, 120, -60), (2, 300, 184, 0, 60), (3, 640, 375, 60, 90), (4, 512, 303, -60, -30),
                (5, 300, 905, 300, 0), (6, 957, 120, 0, 70), (7, 1037, 120, 20, 70)],
}

# Маски: (x, y, w, h, текст) — закрыть имя сети Wi-Fi владельца и найденные сети
NET = (280, 14, 330, 30, 'Wi-Fi: МОЯ_СЕТЬ · 192.168.1.57 · свободно 856 КБ')    # шапка веб-страницы
MASKS = {
    'web_page': [NET], 'web_prestart': [NET], 'web_run': [NET], 'web_done': [NET],
    'settings': [(40, 500, 900, 50, 'Роутер: МОЯ_СЕТЬ')],
    'set_wifi': [(40, 105, 900, 46, 'Подключено к МОЯ_СЕТЬ: http://192.168.1.57'), (262, 182, 618, 76, 'МОЯ_СЕТЬ'),
                 (46, 445, 1186, 232, '')],
}


def size(path):
    out = subprocess.run(['sips', '-g', 'pixelWidth', '-g', 'pixelHeight', path], capture_output=True, text=True).stdout
    return [int(l.split()[-1]) for l in out.splitlines() if 'pixel' in l]


def render(name, marks, masks=()):
    src = next((os.path.join(RAW, name + e) for e in ('.jpg', '.png') if os.path.exists(os.path.join(RAW, name + e))), None)
    if not src:
        print('нет снимка', name); return False
    W, H = size(src)
    svg = []
    for x, y, w, h, t in masks:
        svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0e1822"/>')
        if t:
            svg.append(f'<text x="{x + 6}" y="{y + h * 0.72}" font-size="{int(h * 0.62)}" fill="#8aa4b8" font-family="DejaVu Sans, Arial">{t}</text>')
    for n, x, y, dx, dy in marks:
        bx, by = x + dx, y + dy
        svg.append(f'<line x1="{bx}" y1="{by}" x2="{x}" y2="{y}" stroke="#ff2d55" stroke-width="5" marker-end="url(#a)"/>')
        svg.append(f'<circle cx="{bx}" cy="{by}" r="26" fill="#ff2d55" stroke="#fff" stroke-width="4"/>')
        svg.append(f'<text x="{bx}" y="{by + 10}" text-anchor="middle" font-size="30" font-weight="700" fill="#fff" '
                   f'font-family="Arial">{n}</text>')
    html = f'''<!doctype html><html><head><meta charset="utf-8"><style>
html,body{{margin:0;padding:0;background:#000}}div{{position:relative;width:{W}px;height:{H}px}}
img,svg{{position:absolute;left:0;top:0;width:{W}px;height:{H}px}}</style></head><body><div>
<img src="file://{src}"><svg viewBox="0 0 {W} {H}"><defs><marker id="a" viewBox="0 0 10 10" refX="8" refY="5"
markerWidth="4" markerHeight="4" orient="auto"><path d="M0 0L10 5L0 10z" fill="#ff2d55"/></marker></defs>
{''.join(svg)}</svg></div></body></html>'''
    with tempfile.NamedTemporaryFile('w', suffix='.html', delete=False, encoding='utf-8') as f:
        f.write(html); page = f.name
    os.makedirs(OUT, exist_ok=True)
    png = os.path.join(OUT, name + '.png')
    jpg = os.path.join(OUT, name + '.jpg')
    for attempt in range(3):                     # headless Chrome иногда зависает — таймаут и повтор
        try:
            subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                            '--allow-file-access-from-files', f'--window-size={W},{H}', f'--screenshot={png}',
                            'file://' + page], capture_output=True, timeout=40)
            if os.path.exists(png):
                break
        except subprocess.TimeoutExpired:
            subprocess.run(['pkill', '-f', 'Chrome --headless=new'], capture_output=True)
    os.unlink(page)
    subprocess.run(['sips', '--resampleWidth', str(min(W, 960)), '-s', 'format', 'jpeg', '-s', 'formatOptions', '82',
                    png, '--out', jpg],
                   capture_output=True)                                 # компактнее для репозитория
    os.unlink(png)
    print('рисунок', jpg)
    return True


def main():
    names = sys.argv[1:] or sorted(f[:-4] for f in os.listdir(RAW) if f.endswith(('.jpg', '.png')))
    for n in names:
        render(n, MARKS.get(n, []), MASKS.get(n, ()))


if __name__ == '__main__':
    main()
