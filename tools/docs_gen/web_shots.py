#!/usr/bin/env python3
"""
Снимки веб-страницы дисплея для руководства (раздел «G-код с телефона или компьютера»).

  ~/.platformio/penv/bin/python tools/docs_gen/web_shots.py --ip 192.168.1.57             — страница и окно старта
  ~/.platformio/penv/bin/python tools/docs_gen/web_shots.py --ip 192.168.1.57 --run      — ещё ход программы на стенде

Страница открывается ссылками …/#file=имя (файл выбран) и …/#start=имя (окно «Перед стартом»).
Ход и конец программы снимает tools/gcode_wifi_hil.py (стенд: Discovery, дисплей, Mega).
Результат — docs/manual/raw/web_*.png; имя сети в шапке закрывает маска в annotate.py.
"""
import argparse, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RAW = os.path.join(ROOT, 'docs', 'manual', 'raw')
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
FILE = 'pawn_g71.nc'


def shot(url, name, w, h):
    os.makedirs(RAW, exist_ok=True)
    out = os.path.join(RAW, name + '.png')
    for _ in range(3):
        try:
            subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', f'--window-size={w},{h}',
                            '--virtual-time-budget=9000', f'--screenshot={out}', url], capture_output=True, timeout=60)
            if os.path.exists(out):
                break
        except subprocess.TimeoutExpired:
            subprocess.run(['pkill', '-f', 'Chrome --headless=new'], capture_output=True)
    print('снимок', name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', required=True)
    ap.add_argument('--run', action='store_true', help='прогнать программу на стенде: снимки хода и конца')
    a = ap.parse_args()
    base = f'http://{a.ip}/'
    shot(base + '#file=' + FILE, 'web_page', 1100, 1450)
    shot(base + '#start=' + FILE, 'web_prestart', 1100, 1000)
    if a.run:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'gcode_wifi_hil.py'), '--ip', a.ip,
                        '--file', os.path.join(ROOT, 'gcode_sample', FILE), '--z', '2', '--x', '32',
                        '--mid', os.path.join(RAW, 'web_run'), '--web', os.path.join(RAW, 'web_done.png')],
                       cwd=os.path.join(ROOT, 'tools'))
        d = os.path.join(RAW, 'web_run_d.jpg')                  # экран дисплея на середине — повернуть, как в manual_shots
        if os.path.exists(d):
            subprocess.run(['sips', '-r', '90', d, '--out', os.path.join(RAW, 'gcode_run.jpg')], capture_output=True)
            os.unlink(d)
        w = os.path.join(RAW, 'web_run_w.png')
        if os.path.exists(w):
            os.replace(w, os.path.join(RAW, 'web_run.png'))


if __name__ == '__main__':
    main()
