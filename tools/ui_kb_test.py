#!/usr/bin/env python3
"""
Ввод с клавиатуры (BACKLOG-040): подача, асинхронная подача, съём — позитив и негатив; плитки Z / X / ⌀.

Дисплей по USB, STM32 подключена (значения читаются из [STATE] дисплея — эхо STM32).
  ~/.platformio/penv/bin/python tools/ui_kb_test.py [--shots папка]
"""
import argparse, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc

K = {'7': (89, 97), '8': (165, 97), '9': (242, 97), '4': (89, 143), '5': (165, 143), '6': (242, 143),
     '1': (89, 189), '2': (165, 189), '3': (242, 189), '0': (318, 189), '.': (81, 235)}
ENTER, POPUP_OK, MAIN, AP_TILE = (304, 236), (390, 241), (150, 78), (71, 230)
CANCEL = (174, 236)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shots')
    a = ap.parse_args()
    e = cd.Esp('/dev/cu.usbmodem14801'); time.sleep(1)
    res = []

    def chk(ok, m):
        res.append((ok, m)); print(('    OK   ' if ok else '    FAIL ') + m)

    def tap(xy, ms=120):
        e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(0.6 + ms / 1000)

    def typ(s):
        for c in s:
            tap(K[c])
        tap(ENTER)

    def state(key, want=None, sec=3.0):
        """Значение из [STATE]; want — ждать, пока станет таким (STM32 отвечает не сразу)."""
        t, v = time.time(), None
        while time.time() - t < sec:
            e.s.reset_input_buffer(); buf = b''; t1 = time.time()
            while time.time() - t1 < 0.6:
                buf += e.s.read(4096)
            m = re.findall(rb'\[STATE\] mode=\d+ sub=\d+ sm=\d+ feed=(\d+) afeed=(\d+) ap=(\d+)', buf)
            if m:
                v = int(dict(zip(('feed', 'afeed', 'ap'), m[-1]))[key])
                if want is None or v == want:
                    return v
        return v

    def shot(n):
        if a.shots:
            p = os.path.join(a.shots, n)
            hc.esp_shot(e, p + '_r.jpg')
            subprocess.run(['sips', '-r', '90', p + '_r.jpg', '--out', p + '.jpg'], capture_output=True)

    def ui(k):
        return e.uidump().get(k)

    e.send('<UIRESET>'); e.btn('M1'); time.sleep(1); e.btn('FEED:13'); time.sleep(0.5)
    print('[M1] подача')
    tap(MAIN); chk(ui('axis_kb') == '1', 'тап по подаче — клавиатура')
    typ('020'); chk(state('feed', 20) == 20, '«0», «2», «0» без точки — 0.20 принято')
    tap(MAIN); typ('0.40'); time.sleep(0.6)
    chk(ui('info') == '1', '0.40 — окно «НЕ ПРИНЯТО»'); shot('kb_reject')
    tap(POPUP_OK); chk(state('feed') == 20 and ui('info') == '0' and ui('axis_kb') == '1',
                       'значение не изменилось, окно закрыто, клавиатура осталась открытой')
    tap(CANCEL)
    tap(MAIN); typ('0'); time.sleep(0.6); chk(ui('info') == '1', '0.00 — не принято'); tap(POPUP_OK); tap(CANCEL)
    tap(MAIN); typ('0.25'); chk(state('feed', 25) == 25, 'граница 0.25 — принято')
    tap(MAIN); typ('0.01'); chk(state('feed', 1) == 1, 'граница 0.01 — принято')
    tap(MAIN); tap(CANCEL); chk(ui('axis_kb') == '0' and state('feed') == 1, 'ОТМЕНА — без изменений')
    print('[M1] съём')
    tap(AP_TILE); chk(ui('axis_kb') == '1', 'тап по съёму — клавиатура')
    typ('1.5'); chk(state('ap', 150) == 150, 'съём 1.50')
    tap(AP_TILE); typ('99.5'); time.sleep(0.6); chk(ui('info') == '1', '99.50 — не принято'); tap(POPUP_OK); tap(CANCEL)
    tap(AP_TILE); typ('0'); chk(state('ap', 0) == 0, 'съём 0 — принято')
    print('[M2] асинхронная подача')
    e.btn('M2'); time.sleep(1)
    tap(MAIN); typ('250'); chk(state('afeed', 250) == 250, '250 мм/мин')
    tap(MAIN); typ('450'); time.sleep(0.6); chk(ui('info') == '1', '450 — не принято'); tap(POPUP_OK); tap(CANCEL)
    tap(MAIN); typ('5'); time.sleep(0.6); chk(ui('info') == '1', '5 — не принято (меньше 10)'); tap(POPUP_OK); tap(CANCEL)
    tap(MAIN); typ('400'); chk(state('afeed', 400) == 400, 'граница 400')
    tap(MAIN); typ('10'); chk(state('afeed', 10) == 10, 'граница 10')
    e.btn('AFEED:200'); e.btn('M1'); time.sleep(0.8); e.btn('FEED:13')
    e.send('<UIRESET>')
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
