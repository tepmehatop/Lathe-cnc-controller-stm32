#!/usr/bin/env python3
"""
Подменю и тапы по плиткам (BACKLOG-040): в каждом режиме M1–M7 и на каждом подэкране —
  1) кнопка «ЭКРАН N» справа (на месте OK) переключает подэкран одним нажатием и ничего не открывает,
     подпись — номер нового подэкрана; в M7 (делилка) кнопка — «OK» (следующая метка), подэкран не меняется;
  2) одиночный тап по каждой плитке — что открылось (клавиатура, барабан, правка, меню, сообщение).
  3) пока открыт барабан (тап по главному значению) — кнопка снова «OK» и подтверждает выбор.
Печатает таблицу; FAIL — кнопка открыла что-то, не переключила подэкран или подпись не та.

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
            l0 = (e.uidump().get('ok_lbl') or '').replace('\\n', ' ')
            want0 = 'OK' if mode in ('M7', 'M8') else f'ЭКРАН {s0}'
            e.send('<TAP:446,154,80>'); time.sleep(1.2)
            o = opened(); s1 = sm()
            l1 = (e.uidump().get('ok_lbl') or '').replace('\\n', ' ')
            want1 = 'OK' if mode in ('M7', 'M8') else f'ЭКРАН {s1}'
            # M7 (делилка) и M8: подэкранов нет, кнопка — «OK» (в делилке — следующая метка)
            ok = (s1 == s0 if mode in ('M7', 'M8') else s1 != s0) and not o and l0 == want0 and l1 == want1
            print(f'   «{l0}»: подэкран {s0} -> {s1}, подпись «{l1}», открыто: {o or "ничего"} {"OK" if ok else "FAIL"}')
            if not ok:
                fails.append(f'{mode} подэкран {s0}: кнопка «{l0}» (ожидалось «{want0}») -> подэкран {s1}, «{l1}», открыто {o}')
            if s1 == seen[0]: break
    # барабан открыт — кнопка «OK»
    reset(); e.btn('M3'); time.sleep(1.0)
    for _ in range(4):
        if sm() == 1: break
        e.btn('PARAM_OK'); time.sleep(0.6)
    e.send('<TAP:150,78,80>'); time.sleep(1.2)           # тап по шагу резьбы — правка (барабан)
    u = e.uidump()
    if u.get('roller') == '1' or u.get('edit') == '1':
        lb = u.get('ok_lbl'); s0 = sm()
        e.send('<TAP:446,154,80>'); time.sleep(1.0)
        u2 = e.uidump(); s1 = sm()
        ok = lb == 'OK' and u2.get('roller') == '0' and u2.get('edit') == '0' and s1 == s0
        print(f'правка открыта: кнопка «{lb}», после нажатия барабан закрыт={u2.get("roller") == "0"}, подэкран {s0} -> {s1} {"OK" if ok else "FAIL"}')
        if not ok: fails.append(f'при правке кнопка «{lb}», после нажатия {u2.get("roller")}/{u2.get("edit")}, подэкран {s0}->{s1}')
    else:
        print('правка: тап по шагу не открыл правку — проверка «OK» пропущена')
    reset()
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails: print('  -', f)


if __name__ == '__main__':
    main()
