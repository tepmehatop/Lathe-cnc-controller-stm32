#!/usr/bin/env python3
"""
Замечания форума 2026-11-17 (BACKLOG-042) — проверка на стенде:

  wifi   — режим Wi-Fi ВЫКЛ / РОУТЕР / ТОЧКА ДОСТУПА (<WIFIMODE:n>), веб-страница есть только в РОУТЕР, выбор
           сохраняется после перезагрузки дисплея; в конце стенд снова в РОУТЕР;
  rapid  — БЫСТРО в экранном джойстике — переключатель: с ним M2 едет в разы быстрее; повторный тап и закрытие
           джойстика выключают; джойстик сам не закрывается (раньше — через 30 с);
  cfg    — «ИЗ КОНФИГА» (держать 2 с): сохранённый люфт 30 → заводские (люфт 0), окно «ЗАВОДСКИЕ ЗНАЧЕНИЯ»;
           механика стенда возвращается.

  ~/.platformio/penv/bin/python tools/hil_forum_fixes.py [--only wifi,rapid,cfg]
Стенд: STM32 по ST-Link (OpenOCD), дисплей 10.1" по USB, Mega — эмулятор DRO (следует за моторами).
"""
import argparse, os, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
from follow_hil import Mega

ESP, MEGA, IP = '/dev/cu.usbmodem14801', '/dev/cu.usbserial-1410', '192.168.1.57'
# координаты базы 480×272; джойстик — в окне ниже шапки
JOY, JOY_CLOSE, J_RIGHT, J_LEFT, J_RAPID = (438, 19), (439, 59), (319, 170), (165, 170), (105, 235)
GEAR, SET_MECH, CFG_DEFAULT, POPUP_OK = (465, 19), (126, 66), (240, 237), (390, 241)

res = []


def chk(ok, m):
    res.append((ok, m))
    print(('    OK   ' if ok else '    FAIL ') + m, flush=True)


def http_ok(tries=1):
    for i in range(tries):                           # сразу после подключения адрес отвечает не с первого раза
        try:
            urllib.request.urlopen(f'http://{IP}/', timeout=4)
            return True
        except Exception:
            time.sleep(3)
    return False


def wait_log(e, text, sec):
    t0, buf = time.time(), b''
    while time.time() - t0 < sec:
        buf += e.s.read(8192)
        if text.encode() in buf:
            return True
    return False


def reboot(e):
    e.s.dtr = False; time.sleep(0.1); e.s.dtr = True
    time.sleep(20)


def t_wifi(e):
    print('[wifi]')
    for mode, name in ((0, 'ВЫКЛ'), (2, 'ТОЧКА ДОСТУПА'), (1, 'РОУТЕР')):
        e.s.reset_input_buffer()
        e.send(f'<WIFIMODE:{mode}>')
        if mode == 1:
            chk(wait_log(e, f'WiFiServer started: http://{IP}', 60), 'РОУТЕР: подключился, сервер запущен')   # не строка точки доступа
            time.sleep(2)
        else:
            time.sleep(8)
        web = http_ok(12 if mode == 1 else 1)
        chk(web == (mode == 1), f'{name}: веб-страница {"есть" if web else "нет"} по адресу роутера')
        reboot(e)
        time.sleep(0 if mode != 1 else 25)
        web = http_ok(12 if mode == 1 else 1)
        chk(web == (mode == 1), f'{name} после перезагрузки дисплея: веб-страница {"есть" if web else "нет"}')


def t_rapid(e, stm):
    print('[rapid]')
    def tap(xy, ms=120, w=0.6):
        e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(w + ms / 1000)

    def move(xy):
        z0 = stm.var('Motor_Z_Pos')
        tap(xy, 1500, 1.5)
        return abs(stm.var('Motor_Z_Pos') - z0)
    e.send('<UIRESET>'); time.sleep(0.5); e.btn('M2'); time.sleep(0.8)
    for _ in range(3):
        if stm.var('SelectMenu') == 1:
            break
        e.btn('PARAM_OK'); time.sleep(0.5)
    e.btn('AFEED:100'); time.sleep(0.5)
    tap(JOY, w=1.0)
    chk(e.uidump().get('joystick') == '1', 'джойстик открыт')
    slow = move(J_RIGHT); move(J_LEFT)
    tap(J_RAPID)
    chk(stm.var('g_emul_rapid') == 1, 'тап по БЫСТРО — ядро получило JOY:RAPID_ON')
    fast = move(J_RIGHT); move(J_LEFT)
    chk(slow > 0 and fast > 2 * slow, f'ускоренный ход: {fast} шагов против {slow} без БЫСТРО')
    tap(J_RAPID)
    chk(stm.var('g_emul_rapid') == 0, 'повторный тап — БЫСТРО выкл')
    time.sleep(35)                                   # раньше закрывался сам через 30 с — «моргал» на главный
    chk(e.uidump().get('joystick') == '1', 'через 35 с без касаний джойстик открыт')
    tap(J_RAPID); tap(JOY_CLOSE, w=1.0)
    chk(stm.var('g_emul_rapid') == 0, 'закрытие джойстика выключает БЫСТРО')
    e.send('<UIRESET>'); e.btn('M1')


def t_cfg(e, stm):
    print('[cfg]')
    def tap(xy, ms=120, w=0.6):
        e.send(f'<TAP:{xy[0]},{xy[1]},{ms}>'); time.sleep(w + ms / 1000)

    def mech():
        return [x if x < 2 ** 31 else x - 2 ** 32 for x in stm.read(stm.sym['e2_mech'][0], 9, 32)]
    orig = mech()
    cust = orig[:7] + [30, 30]
    e.btn('CFG_SET:' + ','.join(map(str, cust))); time.sleep(1.5)
    chk(mech() == cust, 'сохранён люфт 30/30')
    e.send('<UIRESET>'); time.sleep(0.5)
    tap(GEAR); tap(SET_MECH, w=1.0)
    tap(CFG_DEFAULT, 2600, 1.5)
    chk(e.uidump().get('info') == '1', 'окно «ЗАВОДСКИЕ ЗНАЧЕНИЯ»')
    d = mech()
    chk(d[7] == 0 and d[8] == 0, f'в STM32 заводские, люфт 0: {d}')
    tap(POPUP_OK, w=1.0)
    chk(e.uidump().get('info') == '0', 'ПОНЯТНО закрыло окно')
    e.btn('CFG_SET:' + ','.join(map(str, orig))); time.sleep(1.5)
    chk(mech() == orig, 'механика стенда восстановлена')
    e.send('<UIRESET>')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    a = ap.parse_args()
    only = set(a.only.split(',')) if a.only else {'wifi', 'rapid', 'cfg'}
    stm, e = cd.Stm32(), cd.Esp(ESP)
    time.sleep(20)                                   # открытие порта перезапускает дисплей
    mega = Mega(MEGA)
    for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):   # линейки следуют за моторами
        mega.cmd(m)
    if 'rapid' in only:
        t_rapid(e, stm)
    if 'cfg' in only:
        t_cfg(e, stm)
    if 'wifi' in only:
        t_wifi(e)
    stm.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
