#!/usr/bin/env python3
"""
Функции мода 7e2 Лях V3.4–V3.8 на стенде (ядро STM32, BACKLOG-043): те же сценарии, что tests/e2_host/sim_v38.inc,
но на плате — кнопки пульта замыкаются через OpenOCD, шпиндель — TIM5, команды экрана — <BTN:…> через дисплей.

  keys   — «ускоренная»+◀/▶ обнуляет линейки; подэкран 3 — ввод ⌀ (▲▼, комбинации ◀+▲ / ▶+▲, ◀+▶ — ноль);
  dist   — дистанционные лимиты (подэкран 4): DIST:, кнопки упоров пульта и экрана, снятие, негатив «слишком близко»;
  kzr    — профиль резьбы: «ускоренная»+«Выбор», KZR:n (негатив 0/5), автоцикл трапеции P=1.5 — глубина;
  pause  — пауза резьбы: экран PAUSE/RESUME и кнопки ▼/▲; на паузе оси стоят; вне цикла — отказ;
  cone   — конусы V3.8: > наружн., < наружн., < внутр. (пауза-отвод 30 мм, RESUME), > внутр. — «невозможен»;
  cut    — отрезание вразбежку: уск+«Выбор», CUT_D/STAG/AP, упоры R+B, автоцикл, негатив RAZ вне M6;
  afeed  — асинхронный автоцикл: проходы сохраняются после цикла.

  ~/.platformio/penv/bin/python tools/hil_v38.py [--only keys,dist,...]
Стенд: STM32 (ST-Link), дисплей по USB, Mega — эмулятор DRO (следует за моторами).
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
from follow_hil import Mega
from hil_cycles import Spindle

ESP, MEGA = '/dev/cu.usbmodem14801', '/dev/cu.usbserial-1410'
res = []


def chk(ok, m):
    res.append((ok, m))
    print(('    OK   ' if ok else '    FAIL ') + m, flush=True)


class Bench:
    def __init__(self):
        self.stm, self.e = cd.Stm32(), cd.Esp(ESP)
        time.sleep(20)                                   # открытие порта перезапускает дисплей
        self.sp = Spindle(self.stm)
        self.mega = Mega(MEGA)
        for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):   # линейки следуют за моторами
            self.mega.cmd(m)

    def v(self, name):
        return self.stm.var(name)

    def b(self, cmd, w=0.35):
        self.e.btn(cmd); time.sleep(w)

    BIT = {'BTN_LEFT': 0, 'BTN_RIGHT': 1, 'BTN_UP': 2, 'BTN_DOWN': 3}

    def _seen(self, pressed, sec=0.6):
        """Ждать, пока Menu() ядра увидит ровно эти кнопки меню (Button_Old, активный уровень 0)."""
        want = 0x0F
        for p in pressed:
            if p in self.BIT:
                want &= ~(1 << self.BIT[p])
        t0 = time.time()
        while time.time() - t0 < sec:
            if (self.v('Button_Old') & 0x0F) == want:
                return True
        return False

    def keys(self, *pins, ms=150):
        """Кнопки пульта как пальцами: по очереди зажать (модификатор первым), дождаться, что ядро увидело,
        отпустить в обратном порядке. «Выбор», «ускоренная», упоры в Button_Old не видны — для них держим ms."""
        held = []
        for p in pins:
            self.stm.press(p); held.append(p)
            if p in self.BIT:
                self._seen(held)
            else:
                time.sleep(ms / 1000)
        for p in reversed(pins):
            self.stm.release(p); held.remove(p)
            if p in self.BIT:
                self._seen(held)
        time.sleep(0.1)

    def manual(self):
        self.b('S2', 0.5); self.b('ALERT_OK', 0.3)

    def clear_limits(self):
        for f, c in (('limit_Left_flag', 'EMUL:STOP_L'), ('limit_Right_flag', 'EMUL:STOP_R'),
                     ('limit_Front_flag', 'EMUL:STOP_F'), ('limit_Rear_flag', 'EMUL:STOP_B')):
            if self.v(f):
                self.b(c)
        time.sleep(0.2)

    def joy(self, pin, sec):
        self.stm.press(pin); time.sleep(sec); self.stm.release(pin); time.sleep(0.6)

    def wait(self, cond, sec, step=0.1):
        t0 = time.time()
        while time.time() - t0 < sec:
            if cond():
                return True
            time.sleep(step)
        return False


def t_keys(B):
    print('[keys]')
    B.b('M1'); B.manual()
    B.b('AXIS:Z:-12345'); B.b('AXIS:X:2500'); B.b('AXIS:D:40000')
    z0 = B.v('Z_pos')
    B.keys('BTN_LEFT')                                   # без ускоренной — проходы, линейка Z не трогается
    chk(B.v('Z_pos') == z0, 'без «ускоренной» ◀ не обнуляет Z')
    B.stm.press('JOY_RAPID'); B.keys('BTN_LEFT'); B.keys('BTN_RIGHT'); B.stm.release('JOY_RAPID'); time.sleep(0.3)
    chk(B.v('Z_pos') == 0 and B.v('X_pos') == 0, f'«ускоренная»+◀/▶ — линейки Z и X в ноль ({B.v("Z_pos")}, {B.v("X_pos")})')
    # подэкран 3: ввод диаметра. «Выбор» 1 → 3 в «Ручном» M1
    mx = lambda: B.v('MX_pos')
    B.b('AXIS:D:20000')
    B.keys('BTN_SELECT')
    chk(B.v('SelectMenu') == 3, f'M1 «Ручной»: «Выбор» 1 → {B.v("SelectMenu")} (ожидалось 3 — ввод ⌀)')
    lin = 4                                              # LINEIKA: шагов MX на 0.01 мм ⌀ при механике автора
    cases = ((('BTN_UP',), -10 * lin, '▲ +0.10'), (('BTN_DOWN',), 10 * lin, '▼ −0.10'),
             (('BTN_LEFT', 'BTN_UP'), -lin, '◀+▲ +0.01'), (('BTN_RIGHT', 'BTN_UP'), -100 * lin, '▶+▲ +1.00'),
             (('BTN_RIGHT', 'BTN_DOWN'), 100 * lin, '▶+▼ −1.00'))   # как у автора: сначала зажать ◀/▶, потом ▲/▼
    for pins, d, n in cases:
        m0 = mx(); B.keys(*pins)
        chk(mx() - m0 == d, f'⌀ {n}: MX_pos {mx() - m0:+d}, ожидалось {d:+d}')
    B.keys('BTN_LEFT', 'BTN_RIGHT')
    chk(mx() == 0, f'◀+▶ — ⌀ в ноль (MX_pos {mx()})')
    B.keys('BTN_SELECT'); chk(B.v('SelectMenu') == 4, f'3 → {B.v("SelectMenu")} (ожидалось 4)')
    B.keys('BTN_SELECT'); chk(B.v('SelectMenu') == 1, f'4 → {B.v("SelectMenu")} (ожидалось 1)')


def t_dist(B):
    print('[dist]')
    B.b('M1'); B.manual(); B.clear_limits()
    while B.v('SelectMenu') != 4:
        B.b('PARAM_OK', 0.3)
    B.b('DIST:0'); B.keys('BTN_UP')
    chk(B.v('Distance_mm') == 200, f'▲ на подэкране 4: +0.1 мм ({B.v("Distance_mm")} × 0.0005)')
    B.keys('BTN_RIGHT', 'BTN_UP'); chk(B.v('Distance_mm') == 2200, f'▶+▲: +1 мм ({B.v("Distance_mm")})')
    B.keys('BTN_LEFT', 'BTN_RIGHT'); chk(B.v('Distance_mm') == 0, '◀+▶ — расстояние 0')
    B.b('DIST:10000')
    z, x = B.v('Motor_Z_Pos'), B.v('Motor_X_Pos')
    zs, xs = 400, 800                                    # шагов/мм при механике автора (Z 2 мм × 800, X 1 мм × 800)
    B.keys('LIM_LEFT', ms=200)
    chk(B.v('limit_Left_flag') == 1 and abs(B.v('Limit_Pos_Left') - (z + 10 * zs)) <= 4,
        f'левый упор пультом на 10 мм левее: {B.v("Limit_Pos_Left") - z:+d} шагов (ожидалось {10 * zs:+d})')
    B.b('EMUL:STOP_R')
    chk(B.v('limit_Right_flag') == 1 and abs(B.v('Limit_Pos_Right') - (z - 10 * zs)) <= 4,
        f'правый упор с экрана на 10 мм правее: {B.v("Limit_Pos_Right") - z:+d}')
    B.b('EMUL:STOP_F'); B.keys('LIM_REAR', ms=200)
    chk(B.v('limit_Front_flag') == 1 and B.v('limit_Rear_flag') == 1 and
        abs(B.v('Limit_Pos_Front') - (x + 10 * xs)) <= 4 and abs(B.v('Limit_Pos_Rear') - (x - 10 * xs)) <= 4,
        f'упоры X на ±10 мм: {B.v("Limit_Pos_Front") - x:+d} / {B.v("Limit_Pos_Rear") - x:+d}')
    B.keys('LIM_LEFT', ms=200); B.b('EMUL:STOP_R'); B.b('EMUL:STOP_F'); B.keys('LIM_REAR', ms=200)
    chk(not any(B.v(f) for f in ('limit_Left_flag', 'limit_Right_flag', 'limit_Front_flag', 'limit_Rear_flag')),
        'повторное нажатие снимает упоры')
    B.b('DIST:500'); B.keys('LIM_LEFT', ms=200); B.b('DIST:0'); B.b('EMUL:STOP_R')
    chk(B.v('limit_Right_flag') == 0, 'негатив: правый упор ближе тормозного пути к левому не ставится')
    B.clear_limits()
    B.b('DIST:-5'); chk(B.v('Distance_mm') == 0, 'негатив: DIST:-5 не принят')
    while B.v('SelectMenu') != 1:
        B.b('PARAM_OK', 0.3)


def thread_cycle(B, step_idx, sub, kzr, rpm=150, tick=None, timeout=900):
    B.b('M1'); B.manual(); B.clear_limits()
    # упоры Z: правый здесь, левый — 20 мм левее (дистанционно)
    while B.v('SelectMenu') != 4:
        B.b('PARAM_OK', 0.3)
    B.b('DIST:20000'); B.b('EMUL:STOP_L'); B.b('DIST:0'); B.b('EMUL:STOP_R')
    while B.v('SelectMenu') != 1:
        B.b('PARAM_OK', 0.3)
    B.b('M3'); B.b(f'THREAD_STEP:{step_idx}'); B.b(f'KZR:{kzr}'); B.b('PH:1')
    B.b(sub, 0.8)
    x0 = B.v('Motor_X_Pos')
    B.sp.on(rpm); time.sleep(0.5)
    B.stm.press('JOY_LEFT')
    t0 = time.time(); xmax, xmin = x0, x0
    while time.time() - t0 < timeout and not B.v('Complete_flag'):
        x = B.v('Motor_X_Pos'); xmax, xmin = max(xmax, x), min(xmin, x)
        if tick:
            tick(time.time() - t0)
        time.sleep(0.05)
    done = B.v('Complete_flag')
    B.stm.release('JOY_LEFT'); time.sleep(0.8); B.sp.off()
    B.manual(); B.clear_limits()
    return done, x0, xmax, xmin, time.time() - t0


def t_kzr(B):
    print('[kzr]')
    B.b('M3'); B.manual(); B.b('KZR:1')
    for exp in (2, 3, 4, 1):
        B.stm.press('JOY_RAPID'); B.keys('BTN_SELECT'); B.stm.release('JOY_RAPID'); time.sleep(0.2)
        chk(B.v('Kzr') == exp and B.v('SelectMenu') == 1, f'«ускоренная»+«Выбор»: профиль {B.v("Kzr")} (ожидалось {exp})')
    B.b('KZR:0'); B.b('KZR:5'); chk(B.v('Kzr') == 1, 'негатив: KZR:0 и KZR:5 не приняты')
    # трапеция P=1.5 наружн.: глубина 0.5·P + 0.15 = 0.90 мм → round(0.9·200)·4 + 4 = 724 шага
    done, x0, xmax, xmin, sec = thread_cycle(B, 13, 'S3', 4)
    chk(done and abs((xmax - x0) - 724) <= 4, f'трапеция 1.5 наружн.: глубина {xmax - x0} шагов (ожидалось 724), {sec:.0f} с')
    B.b('KZR:1')


def t_pause(B):
    print('[pause]')
    B.b('M1'); B.manual()
    B.b('PAUSE'); time.sleep(0.2)
    chk(B.v('want_pause') == 0, 'негатив: PAUSE вне автоцикла не принят')
    B.b('KEY:DN'); B.b('KEY:UP')
    chk(B.v('button_down_flag') == 0 and B.v('button_up_flag') == 0, 'касания ▼▲ не оставляют флаги кнопок нажатыми')
    for via in ('screen', 'keys'):
        st = {'asked': False, 'p': None, 'still': None, 'resumed': False}

        def tick(t):
            if not st['asked'] and B.v('Pass_Nr') >= 3:
                st['asked'] = True
                if via == 'screen': B.e.btn('PAUSE')
                else: B.keys('BTN_DOWN')
            if st['p'] is None and B.v('isPausedAfterStep'):
                st['p'] = (time.time(), B.v('Motor_Z_Pos'), B.v('Motor_X_Pos'), B.v('Null_Z_Pos'))
            if st['p'] and not st['resumed'] and time.time() - st['p'][0] > 3:
                st['still'] = (B.v('Motor_Z_Pos'), B.v('Motor_X_Pos')) == st['p'][1:3]
                st['resumed'] = True
                if via == 'screen': B.e.btn('RESUME')
                else: B.keys('BTN_UP')

        done, *_ = thread_cycle(B, 13, 'S3', 1, rpm=300, tick=tick)
        n = 'экран PAUSE/RESUME' if via == 'screen' else 'кнопки ▼/▲'
        chk(st['p'] is not None, f'{n}: пауза наступила')
        chk(bool(st['still']), f'{n}: на паузе 3 с оси стоят')
        chk(st['p'] is not None and st['p'][1] == st['p'][3], f'{n}: пауза в начале прохода (Z = старт)')
        chk(done, f'{n}: после продолжения цикл завершён')


def cone_cycle(B, mode, sub, joy, cone, ap, passes, resume=True):
    B.b('M1'); B.manual(); B.clear_limits()
    B.b(mode); B.b(f'CONE:{cone}'); B.b('FEED:20'); B.b(f'AP:{ap}')
    for _ in range(20):
        if B.v('Pass_Total') <= 1: break
        B.b('KEY:LEFT', 0.15)
    for _ in range(passes - 1):
        B.b('KEY:RIGHT', 0.15)
    B.b(sub, 0.8)
    z0, x0 = B.v('Motor_Z_Pos'), B.v('Motor_X_Pos')
    B.sp.on(400); time.sleep(0.5)
    B.stm.press(joy)
    t0 = time.time(); paused = None
    while time.time() - t0 < 600 and not B.v('Complete_flag'):
        if B.v('isPausedAfterStep') and paused is None:
            paused = B.v('Motor_Z_Pos')
            if resume:
                time.sleep(1.5); B.e.btn('RESUME')
        time.sleep(0.1)
    done = B.v('Complete_flag')
    lims = (B.v('limit_Left_flag'), B.v('limit_Right_flag'))
    B.stm.release(joy); time.sleep(0.8); B.sp.off()
    B.manual(); B.clear_limits()
    return done, z0, paused, lims, time.time() - t0


def t_cone(B):
    print('[cone]')
    for mode, sub, joy, cone, ap, n, name in (('M5', 'S3', 'JOY_LEFT', 1, 20, 2, 'конус > наружн. 30°'),
                                              ('M4', 'S3', 'JOY_RIGHT', 57, 50, 2, 'конус < наружн. 1:10, съём = отскок'),
                                              ('M4', 'S1', 'JOY_LEFT', 49, 36, 2, 'конус < внутр. КМ2')):
        done, z0, paused, lims, sec = cone_cycle(B, mode, sub, joy, cone, ap, n)
        chk(done, f'{name}: цикл завершён без упоров ({sec:.0f} с)')
        chk(lims == (1, 1), f'{name}: после цикла упоры L/R поставлены сами {lims}')
        if sub == 'S1':
            chk(paused is not None and z0 - paused >= 30 * 400, f'{name}: пауза с отводом ≥ 30 мм вправо '
                f'({(z0 - paused) / 400 if paused is not None else 0:.1f} мм)')
    # «Внутренний» для конуса > — «Режим невозможен», оси не двигаются
    B.b('M5'); B.b('S1', 0.8)
    z, x = B.v('Motor_Z_Pos'), B.v('Motor_X_Pos')
    B.joy('JOY_LEFT', 1.5); B.joy('JOY_RIGHT', 1.5)
    chk((B.v('Motor_Z_Pos'), B.v('Motor_X_Pos')) == (z, x), 'конус > внутр.: «Режим невозможен», оси стоят')
    B.manual()


def t_cut(B):
    print('[cut]')
    B.b('M1'); B.manual(); B.clear_limits()
    B.b('RAZ:2'); chk(B.v('Raz') == 1, 'негатив: RAZ:2 вне режима «Шар» не принят')
    B.b('M6')
    chk(B.v('Raz') == 1 and B.v('Ap') == 100, f'M6 после включения — шар, шаг 1.00 (Raz={B.v("Raz")}, Ap={B.v("Ap")})')
    B.stm.press('JOY_RAPID'); B.keys('BTN_SELECT'); B.stm.release('JOY_RAPID'); time.sleep(0.2)
    chk(B.v('Raz') == 2, '«ускоренная»+«Выбор» — отрезание')
    B.stm.press('JOY_RAPID'); B.keys('BTN_RIGHT'); B.stm.release('JOY_RAPID'); time.sleep(0.2)
    chk(B.v('Staggered_Width') == 75, f'«ускоренная»+▶ — смещение 0.75 ({B.v("Staggered_Width")})')
    B.b('STAG:2'); B.b('AP:100'); B.b('CUT_D:400'); B.b('FEED:5')
    chk(B.v('Pass_Total') == 4 and B.v('Raz_R') == 4, f'CUT_D:400 → 4 врезания, ⌀4 ({B.v("Pass_Total")}, {B.v("Raz_R")})')
    B.b('STAG:9'); chk(B.v('Staggered_Width') == 50, 'негатив: STAG:9 не принят')
    B.b('EMUL:STOP_R'); B.b('EMUL:STOP_B')
    B.b('S3', 0.8)
    x0 = B.v('Motor_X_Pos')
    B.sp.on(600); time.sleep(0.5)
    B.stm.press('JOY_LEFT')
    t0 = time.time(); xmax = x0
    while time.time() - t0 < 300 and not B.v('Complete_flag'):
        xmax = max(xmax, B.v('Motor_X_Pos')); time.sleep(0.05)
    done = B.v('Complete_flag')
    B.stm.release('JOY_LEFT'); time.sleep(0.8); B.sp.off()
    chk(done and abs((xmax - x0) - 4 * 400) <= 2, f'отрезание ⌀4: завершено {done}, глубина {xmax - x0} шагов (ожидалось 1600 = 4 × 0.50 мм), {time.time() - t0:.0f} с')
    B.manual(); B.clear_limits(); B.b('RAZ:1'); B.b('M1')


def t_afeed(B):
    print('[afeed]')
    B.b('M1'); B.manual(); B.clear_limits()
    while B.v('SelectMenu') != 4:
        B.b('PARAM_OK', 0.3)
    B.b('DIST:15000'); B.b('EMUL:STOP_L'); B.b('DIST:0'); B.b('EMUL:STOP_R')
    B.b('M2'); B.b('AFEED:300'); B.b('AP:20')
    for _ in range(20):
        if B.v('Pass_Total') <= 1: break
        B.b('KEY:LEFT', 0.15)
    B.b('KEY:RIGHT', 0.15); B.b('KEY:RIGHT', 0.15)
    B.b('S3', 0.8)
    B.stm.press('JOY_LEFT')
    ok = B.wait(lambda: B.v('Complete_flag'), 120)
    B.stm.release('JOY_LEFT'); time.sleep(0.8)
    chk(ok and B.v('Pass_Total') == 3, f'асинхр.: завершён {ok}, проходов после цикла {B.v("Pass_Total")} (сохраняется 3)')
    B.manual(); B.clear_limits(); B.b('M1')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    a = ap.parse_args()
    allt = ['keys', 'dist', 'kzr', 'pause', 'cone', 'cut', 'afeed']
    only = a.only.split(',') if a.only else allt
    B = Bench()
    B.e.send('<UIRESET>'); time.sleep(0.5)
    for n in allt:
        if n in only:
            try:
                globals()['t_' + n](B)
            except Exception as ex:
                chk(False, f'{n}: исключение {ex!r}')
                for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_RAPID'):
                    B.stm.release(p)
                B.sp.off()
    B.stm.close()
    fails = [m for ok, m in res if not ok]
    print('\nИТОГ:', f'{len(res)} проверок,', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
