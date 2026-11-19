#!/usr/bin/env python3
"""
G-код с экрана станка (M8) на железе: запуск с веб-страницы, блокировка экрана, ПАУЗА посреди кадра, ПРОДОЛЖИТЬ,
СТОП с экрана и со страницы, запуск не из M8 (экран сам включает M8).

Стенд как у hil_mech.py: STM32 по ST-Link, дисплей по USB и Wi-Fi, Mega — блок DRO, шпиндель — квадратура.
  ~/.platformio/penv/bin/python tools/hil_gcode_ui.py [--ip 192.168.1.57] [--shots папка]
"""
import argparse, json, os, subprocess, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega
from hil_mech import Quadrature

PAUSE_XY, STOP_XY, MODE_XY = (347, 230), (445, 152), (54, 20)


def http(method, url, data=None):
    for _ in range(20):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method), timeout=15) as r:
                return r.read().decode(errors='replace')
        except OSError:
            time.sleep(3)
    raise RuntimeError('нет веб-страницы дисплея')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', default='192.168.1.57')
    ap.add_argument('--shots')
    a = ap.parse_args()
    B = f'http://{a.ip}'
    s, e, mega = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801'), Mega('/dev/cu.usbserial-1410')
    sp = Quadrature(s)
    res = []

    def chk(ok, m):
        res.append((ok, m)); print(('    OK   ' if ok else '    FAIL ') + m)

    def tap(xy, ms=120):
        e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(0.6 + ms / 1000)

    def ex():
        return json.loads(http('GET', B + '/api/exec'))

    def pos():
        return s.var('Motor_Z_Pos'), s.var('Motor_X_Pos')

    def shot(n):
        if a.shots:
            p = os.path.join(a.shots, n)
            hc.esp_shot(e, p + '_r.jpg')
            subprocess.run(['sips', '-r', '90', p + '_r.jpg', '--out', p + '.jpg'], capture_output=True)

    try:
        for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):
            mega.cmd(m)
        s.press('SUB_MAN'); e.send('<UIRESET>'); e.btn('M2'); time.sleep(0.6); e.btn('AFEED:270')
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'JOY_UP'):         # направление линеек
            s.press(p); time.sleep(1.3); s.release(p); time.sleep(0.8)
        http('PUT', B + '/api/file?name=pawn.nc', open('gcode_sample/pawn.nc', 'rb').read())
        e.btn('M1'); time.sleep(1)
        sp.on(3600)
        print('[1] запуск со страницы, на экране M1')
        ps = pos()
        http('POST', B + '/api/run?name=pawn.nc'); time.sleep(5)
        u = e.uidump(); x = ex()
        # без этого «программа не тронулась» неотличимо от «пауза не снялась» в [4]
        chk(pos() != ps, f'после запуска оси поехали {ps} -> {pos()} (кадр {x.get("step")}, '
                         f'STM32: пауза {s.var("e2_gc_hold")}, отбой {s.var("e2_cal_abort")})')
        chk(u.get('mode_lbl') == 'G-КОД' and s.var('Mode') == 8, f'экран сам перешёл в M8 ({u.get("mode_lbl")})')
        chk(x.get('state') in ('WAITING_ACK', 'RUNNING') and u.get('glock') == '1',
            f'программа идёт ({x.get("state")}), экран заблокирован')
        shot('gcode_lock')
        print('[2] заблокированный экран')
        tap(MODE_XY); chk(e.uidump().get('mode_menu') == '0', 'тап по названию режима — меню не открылось')
        print('[3] ПАУЗА посреди кадра')
        tap(PAUSE_XY); time.sleep(1.0); p0 = pos(); time.sleep(2.0); p1 = pos()
        chk(ex().get('state') == 'PAUSED' and p0 == p1, f'пауза: оси стоят {p0} == {p1}')
        shot('gcode_pause')
        print('[4] ПРОДОЛЖИТЬ')
        pct0 = ex().get('pct', 0)
        tap(PAUSE_XY); time.sleep(2.5); p2 = pos()
        x = ex()
        chk(x.get('state') != 'PAUSED' and p2 != p1, f'оси поехали {p1} -> {p2}' + (
            '' if p2 != p1 else f' [экран: {x.get("state")}, кадр {x.get("step")}; STM32: пауза {s.var("e2_gc_hold")}, '
                                f'режим {s.var("Mode")}, отбой {s.var("e2_cal_abort")}]'))
        # проценты — по расчёту времени и после паузы могут пересчитаться назад; идёт ли программа — по осям и кадрам
        st0 = ex().get('step', 0); time.sleep(10); x = ex(); p6 = pos()
        chk(x.get('state') not in ('ERROR', 'IDLE', 'DONE') and p6 != p2,
            f'программа идёт дальше: оси {p2} -> {p6}, {x.get("pct")}%, {x.get("state")}')
        print('[5] СТОП с экрана')
        tap(STOP_XY); time.sleep(2.0); p3 = pos(); time.sleep(1.5); p4 = pos(); u = e.uidump()
        chk(ex().get('state') == 'IDLE' and p3 == p4 and u.get('glock') == '0',
            f'остановлена ({ex().get("state")}), оси стоят, блокировка снята')
        print('[6] повторный запуск и СТОП со страницы')
        http('POST', B + '/api/run?name=pawn.nc'); time.sleep(5)
        chk(ex().get('state') in ('WAITING_ACK', 'RUNNING'), f'снова идёт ({ex().get("state")})')
        http('POST', B + '/api/stop'); time.sleep(2); p5 = pos(); time.sleep(1.5)
        chk(ex().get('state') == 'IDLE' and pos() == p5 and e.uidump().get('glock') == '0', 'стоп со страницы')
    finally:
        sp.off()
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            s.release(p)
        s.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
