#!/usr/bin/env python3
"""
Подменю и тапы по плиткам (BACKLOG-040): в каждом режиме M1–M7 и на каждом подэкране —
  1) двойной тап по ⌀ (два касания через 150 мс) переключает подэкран и ничего не открывает;
  2) одиночный тап по каждой плитке — что открылось (клавиатура, барабан, правка, меню, сообщение).
Печатает таблицу; FAIL — двойной тап открыл что-то или не переключил подэкран.

  ~/.platformio/penv/bin/python tools/ui_submenu_test.py [--modes M1,M3]
"""
import argparse, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd

TILES = {'Z': (71, 153), 'X': (210, 153), 'D': (347, 153), 'R3': (71, 230), 'R4': (210, 230), 'T': (347, 230),
         'MAIN': (150, 78)}
OPEN = ('axis_kb', 'roller', 'edit', 'sub_edit', 'alert', 'info', 'mode_menu', 'thr_type_menu', 'settings', 'joystick')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modes', default='M1,M2,M3,M4,M5,M6,M7')
    a = ap.parse_args()
    e = cd.Esp('/dev/cu.usbmodem14801'); time.sleep(1)
    fails = []

    def sm():
        e.s.reset_input_buffer(); t = time.time(); buf = b''
        while time.time() - t < 3:
            buf += e.s.read(4096)
            m = re.findall(rb'\[STATE\] mode=\d+ sub=\d+ sm=(\d+)', buf)
            if len(m) >= 2: return int(m[-1])
        return -1

    def opened():
        u = e.uidump()
        return [k for k in OPEN if u.get(k) == '1']

    def reset():
        e.send('<UIRESET>'); time.sleep(0.5)

    for mode in a.modes.split(','):
        reset(); e.btn(mode); time.sleep(1.0)
        for _ in range(4):                                 # на первый подэкран
            if sm() == 1: break
            e.btn('PARAM_OK'); time.sleep(0.6)
        seen = []
        for k in range(3):
            s0 = sm(); seen.append(s0)
            row = []
            for name, (x, y) in TILES.items():
                reset()
                e.send(f'<TAP:{x},{y},80>'); time.sleep(0.9)
                o = opened()
                row.append(f'{name}:{"+".join(o) if o else "-"}')
            reset()
            print(f'{mode} подэкран {s0}: ' + '  '.join(row))
            e.send('<TAP:347,153,80>'); time.sleep(0.15); e.send('<TAP:347,153,80>'); time.sleep(1.2)
            o = opened(); s1 = sm()
            # M7 (делилка) и M8: подэкранов нет, «ОК» там — следующая метка; двойной тап по ⌀ ничего не делает
            ok = (s1 == s0 if mode in ('M7', 'M8') else s1 != s0) and not o
            print(f'   двойной тап по ⌀: подэкран {s0} -> {s1}, открыто: {o or "ничего"} {"OK" if ok else "FAIL"}')
            if not ok:
                fails.append(f'{mode} подэкран {s0}: двойной тап по ⌀ -> {s1}, открыто {o}')
            if s1 == seen[0]: break
    reset()
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails: print('  -', f)


if __name__ == '__main__':
    main()
