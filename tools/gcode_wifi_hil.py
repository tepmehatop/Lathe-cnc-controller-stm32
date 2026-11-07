#!/usr/bin/env python3
"""
G-код через Wi-Fi целиком (BACKLOG-001): файл загружается на веб-страницу дисплея (PUT /api/file), запускается
(POST /api/run), ход — по /api/exec. Mega следует за моторами (блок DRO), шпиндель — эмуляция (TIM5 по SWD),
нужен для подачи на оборот G99. В конце положение каретки сверяется с последним кадром программы.

Запуск: python tools/gcode_wifi_hil.py --ip 192.168.1.57 --file gcode_sample/history.nc --z 100 --x 100
        (--z/--x — конечная точка программы, мм; X — диаметр)
"""
import argparse, json, os, sys, time, urllib.request
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega


def http(method, url, data=None):
    req = urllib.request.Request(url, data=data, method=method)
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read().decode(errors='replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', required=True)
    ap.add_argument('--file', required=True)
    ap.add_argument('--z', type=float, required=True)
    ap.add_argument('--x', type=float, required=True)
    ap.add_argument('--rpm', type=int, default=600)
    ap.add_argument('--shot', default=None, help='снимок экрана во время программы (jpg)')
    a = ap.parse_args()
    base = f'http://{a.ip}'
    name = os.path.basename(a.file)
    stm, esp, mega = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801'), Mega('/dev/cu.usbserial-1410')
    sp = hc.Spindle(stm)
    ok = True
    try:
        for c in ('w0', 'kz2500', 'kx1250', 'lz0', 'lx0', 'f1'):
            mega.cmd(c)
        stm.press('SUB_MAN'); esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
        for pin in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN'):      # направление линеек
            stm.press(pin); time.sleep(1.0); stm.release(pin); time.sleep(0.8)
        mega.cmd('o'); time.sleep(0.5)
        esp.btn('M8'); time.sleep(0.8)
        print('загрузка:', http('PUT', f'{base}/api/file?name={name}', open(a.file, 'rb').read())[:80])
        print('файлы:', http('GET', f'{base}/api/files')[:120])
        sp.on(a.rpm); time.sleep(1.0)
        print('запуск:', http('POST', f'{base}/api/run?name={name}')[:80])
        t0, shot_done, last = time.time(), False, None
        while time.time() - t0 < 600:
            time.sleep(1.0)
            ex = json.loads(http('GET', f'{base}/api/exec'))
            st = (ex.get('state'), ex.get('line'), ex.get('progress'), ex.get('error'))
            if st != last:
                print('  ', st, 'каретка', mega.pos()); last = st
            if a.shot and not shot_done and ex.get('line', 0) >= 8:
                hc.esp_shot(esp, a.shot); shot_done = True
            if str(ex.get('state')) in ('done', 'error', 'idle', '5', '6', '0') and time.time() - t0 > 3:
                break
        z, x = mega.pos()
        want_z, want_x = round(a.z * 1000), round(a.x * 1000 / 2)
        ok = abs(z - want_z) <= 5 and abs(x - want_x) <= 5 and 'rror' not in str(last)
        print(f'конец: каретка Z {z} X {x} мкм (цель {want_z} {want_x}), состояние {last}')
    finally:
        try:
            sp.off()
        except Exception:
            pass
        stm.release('SUB_MAN')
        try:
            mega.cmd('f0'); mega.cmd('w1')
        except Exception:
            pass
        stm.close()
    print('ИТОГ:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
