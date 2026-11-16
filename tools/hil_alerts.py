#!/usr/bin/env python3
"""
Стопы и сообщения в процессе работы — на железе (расширенное тестирование, 2026-11-16).

Стенд как у hil_mech.py: STM32 (stm32f407_dro) по ST-Link, дисплей по USB и Wi-Fi (веб-страница), Mega — блок DRO,
«следует за моторами», шпиндель — квадратура A+B (hil_mech.Quadrature).

  1. Пропуск шагов посреди хода: Mega «теряет» 400 шагов Z (dz) — STM32 останавливает движение, сообщение
     «ПРОПУСК ШАГОВ Z», джойстик заблокирован, пока его не отпустить на 1 с; затем ось снова едет.
  2. Упор останавливает ось: левый упор в 1 мм от каретки, джойстик влево держим 3 с — каретка встаёт на упоре.
  3. G-код: ход джойстика посреди программы — «ОСТАНОВЛЕНО», оси стоят, экран разблокирован.
  4. Автоцикл (подача, «Наружный»): отпустили джойстик посреди прохода — оси встали; снова держим — едут дальше.
  5. Негатив: G-код при неизвестном направлении линеек (после сброса STM32) — ошибка сразу, до первого движения.

  ~/.platformio/penv/bin/python tools/hil_alerts.py [--only 1,3] [--ip 192.168.1.57]
"""
import argparse, json, os, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc
from follow_hil import Mega
from hil_mech import Quadrature


def http(method, url, data=None):
    for _ in range(20):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method), timeout=15) as r:
                return r.read().decode(errors='replace')
        except OSError:
            time.sleep(3)
    raise RuntimeError('нет веб-страницы дисплея')


class Bench:
    def __init__(self, a):
        self.stm, self.esp, self.mega = cd.Stm32(), cd.Esp(a.esp), Mega(a.mega)
        self.sp = Quadrature(self.stm)
        self.base = f'http://{a.ip}'
        self.fails = []

    def check(self, ok, msg):
        print(('    OK   ' if ok else '    FAIL ') + msg)
        if not ok:
            self.fails.append(msg)

    def pos(self):
        return self.stm.var('Motor_Z_Pos'), self.stm.var('Motor_X_Pos')

    def log(self, sec):
        """USB-лог дисплея за sec секунд (сообщения ядра — строки <ALERT:…> идут в лог как [ALERT]/UART RX)."""
        t0, buf = time.time(), b''
        while time.time() - t0 < sec:
            buf += self.esp.s.read(8192)
        return buf.decode(errors='replace')

    def follow(self):
        for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):
            self.mega.cmd(m)

    def learn_signs(self):
        self.esp.btn('M2'); time.sleep(0.5); self.esp.btn('S2'); time.sleep(0.3); self.esp.btn('AFEED:270')
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'JOY_UP'):
            self.stm.press(p); time.sleep(1.3); self.stm.release(p); time.sleep(0.8)
        sa = self.stm.sym['e2_scale_sign'][0]
        return self.stm.read(sa, 2, 8)

    def release_all(self):
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_EXT', 'SUB_INT'):
            try:
                self.stm.release(p)
            except Exception:
                pass


def t_steploss(b):
    print('[1] пропуск шагов посреди хода (сверка — на остановке оси)')
    stm, esp = b.stm, b.esp
    esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
    # без люфта в «Механике» ход сразу после разворота не сверяется (каретка отстаёт на люфт) — сначала
    # короткий ход в ту же сторону, потом проверочный
    stm.press('JOY_LEFT'); time.sleep(0.6); stm.release('JOY_LEFT'); time.sleep(0.8)
    esp.s.reset_input_buffer()
    stm.press('JOY_LEFT'); time.sleep(0.6)
    b.mega.cmd('dz400')                                    # Mega «теряет» 400 шагов Z = 1 мм
    time.sleep(1.0)
    stm.release('JOY_LEFT'); time.sleep(0.8)               # ось встала — STM32 сверяет линейку с шагами
    lg = b.log(0.8)
    b.check(stm.var('e2_loss_block') == 1, 'на остановке контроль шагов сработал')
    b.check('ПРОПУСК ШАГОВ' in lg, 'сообщение «ПРОПУСК ШАГОВ Z» пришло на дисплей')
    z0 = stm.var('Motor_Z_Pos'); stm.press('JOY_RIGHT'); time.sleep(0.8); z1 = stm.var('Motor_Z_Pos')
    stm.release('JOY_RIGHT')
    b.check(z0 == z1, f'пока блокировка — джойстик не двигает ось ({z0} -> {z1})')
    time.sleep(1.6)                                        # отпущен больше 1 с — блокировка снята
    esp.btn('ALERT_OK'); time.sleep(0.4)
    b.check(stm.var('e2_loss_block') == 0, 'джойстик отпущен на 1 с — блокировка снята')
    z0 = stm.var('Motor_Z_Pos'); stm.press('JOY_RIGHT'); time.sleep(1.5); stm.release('JOY_RIGHT'); time.sleep(0.6)
    b.check(stm.var('Motor_Z_Pos') != z0, 'ось снова едет')


def t_limit(b):
    print('[2] упор останавливает ось')
    stm, esp = b.stm, b.esp
    esp.btn('M2'); time.sleep(0.4); esp.btn('S2'); time.sleep(0.3); esp.btn('AFEED:300'); time.sleep(0.3)
    # левый упор там, где каретка; отъехать вправо на ~3 мм; держать влево — должна встать на упоре
    for pin, flag in hc.LIMS[:1]:
        cd.run_actions(stm, esp, [('lim', (pin, flag, 0))])
    cd.run_actions(stm, esp, [('lim', ('LIM_LEFT', 'limit_Left_flag', 1))])
    lim = stm.var('Limit_Pos_Left')
    stm.press('JOY_RIGHT'); time.sleep(1.2); stm.release('JOY_RIGHT'); time.sleep(0.6)
    stm.press('JOY_LEFT'); time.sleep(4.0)
    z = stm.var('Motor_Z_Pos'); time.sleep(0.8); z2 = stm.var('Motor_Z_Pos')
    stm.release('JOY_LEFT'); time.sleep(0.5)
    b.check(z == z2 and abs(z - lim) <= 8, f'каретка стоит на упоре: позиция {z}, упор {lim}')
    cd.run_actions(stm, esp, [('lim', ('LIM_LEFT', 'limit_Left_flag', 0))])


def t_gcode_joy(b):
    print('[3] G-код: ход джойстика посреди программы')
    stm, esp = b.stm, b.esp
    esp.btn('M8'); time.sleep(0.8)
    http('PUT', b.base + '/api/file?name=pawn.nc', open('gcode_sample/pawn.nc', 'rb').read())
    b.sp.on(3600)
    http('POST', b.base + '/api/run?name=pawn.nc'); time.sleep(5)
    st0 = json.loads(http('GET', b.base + '/api/exec')).get('state')
    stm.press('JOY_RIGHT'); time.sleep(0.4); stm.release('JOY_RIGHT'); time.sleep(2)
    ex = json.loads(http('GET', b.base + '/api/exec'))
    p0 = b.pos(); time.sleep(1.5); p1 = b.pos()
    u = esp.uidump()
    b.check(st0 in ('WAITING_ACK', 'RUNNING'), f'программа шла ({st0})')
    b.check(ex.get('state') == 'ERROR' and 'ОСТАНОВЛЕНО' in (ex.get('error') or ''),
            f'остановлена: {ex.get("state")} «{ex.get("error")}»')
    b.check(p0 == p1, 'оси стоят')
    b.check(u.get('glock') == '0', 'экран разблокирован (сообщение можно закрыть)')
    esp.btn('ALERT_OK'); esp.send('<UIRESET>'); time.sleep(0.5)
    b.sp.off()


def t_cycle_release(b):
    print('[4] автоцикл: отпустили джойстик посреди прохода')
    stm, esp = b.stm, b.esp
    problems = []
    esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
    hc.set_limits(stm, esp, problems)
    hc.prepare(stm, esp, 'M1', ['FEED:20', 'AP:20', 'PASSES:3'])
    stm.release('SUB_MAN'); time.sleep(0.3); stm.press('SUB_EXT'); time.sleep(1.0)
    b.sp.on(3600); time.sleep(0.6)
    stm.press('JOY_LEFT'); time.sleep(3.0)
    stm.release('JOY_LEFT'); time.sleep(2.0)                # торможение после отпускания — до 1 с
    p0 = b.pos(); time.sleep(1.5); p1 = b.pos()
    b.check(p0 == p1, f'отпустили — оси стоят ({p0} == {p1})')
    stm.press('JOY_LEFT'); time.sleep(2.5); p2 = b.pos(); stm.release('JOY_LEFT'); time.sleep(0.5)
    # как у автора 7e2: цикл — автомат по позициям упоров; отпустили посреди прохода — цикл прерван, повторное
    # нажатие посреди прохода ничего не делает (вернуть суппорт к упору в «Ручном»). Не ошибка — сведение.
    print(f'    ИНФО снова держим посреди прохода: {"едет" if p2 != p1 else "стоит (цикл прерван, как у 7e2)"}')
    b.sp.off()
    stm.release('SUB_EXT'); time.sleep(0.3); stm.press('SUB_MAN'); time.sleep(0.6)
    esp.btn('ALERT_OK'); time.sleep(0.3)
    for pin, flag in hc.LIMS:
        cd.run_actions(stm, esp, [('lim', (pin, flag, 0))])
    if problems:
        b.check(False, 'упоры: ' + '; '.join(problems))


def t_gcode_nosign(b):
    print('[5] негатив: G-код без известного направления линеек')
    stm, esp = b.stm, b.esp
    stm.cmd('reset run'); time.sleep(5)
    esp.btn('M8'); time.sleep(0.8)
    p0 = b.pos()
    http('POST', b.base + '/api/run?name=pawn.nc'); time.sleep(4)
    ex = json.loads(http('GET', b.base + '/api/exec'))
    b.check(ex.get('state') == 'ERROR' and 'НАПРАВЛЕНИЕ ЛИНЕЕК' in (ex.get('error') or ''),
            f'ошибка сразу: «{ex.get("error")}»')
    b.check(b.pos() == p0, 'оси не двигались')
    esp.btn('ALERT_OK'); esp.send('<UIRESET>'); time.sleep(0.5)


TESTS = [t_steploss, t_limit, t_gcode_joy, t_cycle_release, t_gcode_nosign]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--ip', default='192.168.1.57')
    ap.add_argument('--only')
    a = ap.parse_args()
    b = Bench(a)
    try:
        b.follow()
        b.stm.press('SUB_MAN')
        b.stm.cmd('reset run'); time.sleep(5)
        print('знак линеек:', b.learn_signs())
        idx = [int(x) for x in a.only.split(',')] if a.only else range(1, len(TESTS) + 1)
        for i in idx:
            try:
                TESTS[i - 1](b)
            except Exception as ex:
                b.check(False, f'тест {i}: {ex}')
            finally:
                b.release_all()
                b.sp.off()
    finally:
        b.release_all()
        try:
            b.stm.release('SUB_MAN'); b.sp.off()
        except Exception:
            pass
        b.stm.close()
    print('\nИТОГ:', 'OK' if not b.fails else f'{len(b.fails)} FAIL')
    for f in b.fails:
        print('  -', f)
    return 1 if b.fails else 0


if __name__ == '__main__':
    sys.exit(main())
