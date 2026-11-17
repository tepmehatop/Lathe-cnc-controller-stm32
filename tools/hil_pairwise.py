#!/usr/bin/env python3
"""
Попарное тестирование на стенде (pairwise, tools/pairwise.py): режим × подрежим × источник координат × механика ×
подача × направление шпинделя — 18 сочетаний вместо 648, каждая пара значений встречается хотя бы раз.

Для каждого сочетания:
  - «Ручной»: держим джойстик, шаги мотора против расчёта ядра (подача, асинхрон, резьба; конус — соотношение осей,
    шар — ось едет);
  - «Наружный» / «Внутренний»: автоцикл до конца (hil_cycles.run_case — проходы, «ОПЕРАЦИЯ ЗАВЕРШЕНА», упоры);
  - негатив (шар «Внутренний»): отказ ядра, оси стоят.
Источник координат меняется через POS_SET (STM32 перезапускается), механика — CFG_SET; в конце — блок DRO и механика
из els_config.h.

Стенд как у hil_mech.py.  ~/.platformio/penv/bin/python tools/hil_pairwise.py [--only 3,7] [--timeout 200]
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
import hil_cycles as hc
import pairwise as pw
from follow_hil import Mega
from hil_mech import Quadrature, steps_per_mm, feed_steps, thread_steps, afeed_rate

MECH = {'default': (1800, 200, 4, 200, 200, 4, 100, 0, 0),
        'enc3600': (3600, 200, 8, 200, 200, 8, 100, 0, 0),
        'screw3':  (1800, 200, 8, 300, 200, 8, 200, 0, 0)}
SOURCE = {'STEPS': 0, 'DRO': 1}
FEED = {'min': 5, 'mid': 13, 'max': 25}                 # 0.01 мм/об
AFEED = {'min': 50, 'mid': 200, 'max': 400}             # мм/мин
THREAD = {'min': (6, 0.5), 'mid': (13, 1.5), 'max': (17, 3.0)}   # индекс Thread_Info, шаг мм
POS_TAIL = '1,5000,0,0,1,5000,0,0,1,1,150'              # квадратура 5 мкм по обеим осям, доводка, контроль шагов


class Spin:
    """Шпиндель для hil_cycles.run_case: on(об/мин) с нужным направлением и энкодером."""
    def __init__(self, q, enc, cw):
        self.q, self.enc, self.cw = q, enc, cw

    def on(self, rpm):
        self.q.on(rpm * self.enc / 60.0, lead=self.cw)

    def off(self):
        self.q.off()


class Run:
    def __init__(self, a):
        self.stm, self.esp, self.mega = cd.Stm32(), cd.Esp(a.esp), Mega(a.mega)
        self.q = Quadrature(self.stm)
        self.dec = cd.lcd_decoder()
        self.a = a
        self.fails = []
        self.source = None

    def check(self, ok, msg):
        print(('    OK   ' if ok else '    FAIL ') + msg)
        if not ok:
            self.fails.append(msg)
        return ok

    def pos(self):
        return self.stm.var('Motor_Z_Pos'), self.stm.var('Motor_X_Pos')

    def edges(self):
        return self.stm.var('s_spindle_edges', signed=False)

    def follow(self, m):
        sz, sx = steps_per_mm(*m[1:4]), steps_per_mm(*m[4:7])
        for c in ('w0', 'lz0', 'lx0', f'kz{round(1e6 / sz)}', f'kx{round(1e6 / sx)}', 'f1', 'o'):
            self.mega.cmd(c)

    def setup(self, row):
        st, esp = self.stm, self.esp
        m = MECH[row['mech']]
        self.follow(m)
        if self.source != row['source']:
            esp.btn(f'POS_SET:{SOURCE[row["source"]]},{POS_TAIL}'); time.sleep(6)   # STM32 перезапускается
            self.source = row['source']
        esp.btn('CFG_SET:' + ','.join(str(x) for x in m)); time.sleep(3)
        mech = st.read(st.sym['e2_mech'][0], 9, 32)
        self.check(tuple(mech) == m, f'механика {row["mech"]} применена')
        st.press('SUB_MAN'); esp.send('<UIRESET>'); esp.btn('ALERT_OK')
        if row['source'] == 'DRO':                       # направление линеек — проезд каждой осью
            esp.btn('M2'); time.sleep(0.5); esp.btn('S2'); esp.btn('AFEED:270'); time.sleep(0.3)
            for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'JOY_UP'):
                st.press(p); time.sleep(1.3); st.release(p); time.sleep(0.8)

    def hold(self, pin, steady=None, warm=1.2, secs=2.0):
        st = self.stm
        t_press = time.time()
        st.press(pin)
        try:
            time.sleep(warm)
            t_end = time.time() + 20
            while steady and time.time() < t_end and st.var(steady[0], signed=False) != st.var(steady[1], signed=False):
                time.sleep(0.2)
            e0, p0, t0 = self.edges(), self.pos(), time.time()
            time.sleep(secs)
            e1, p1, t1 = self.edges(), self.pos(), time.time()
        finally:
            st.release(pin)
            held = time.time() - t_press
            time.sleep(0.6)
        back = 'JOY_RIGHT' if pin == 'JOY_LEFT' else 'JOY_LEFT'
        st.press(back); time.sleep(held); st.release(back); time.sleep(0.6)      # вернуть каретку
        return (e1 - e0) % (1 << 32), abs(p1[0] - p0[0]), abs(p1[1] - p0[1]), t1 - t0

    def manual(self, row):
        st, esp = self.stm, self.esp
        m = MECH[row['mech']]
        enc, sz, sx = m[0], steps_per_mm(*m[1:4]), steps_per_mm(*m[4:7])
        mode, cw = row['mode'], row['spindle'] == 'CW'
        esp.btn(mode); time.sleep(0.8); esp.btn('S2'); time.sleep(0.4)
        if mode == 'M2':
            af = AFEED[row['feed']]
            esp.btn(f'AFEED:{af}'); time.sleep(0.4)
            _, dz, _, dt = self.hold('JOY_LEFT', steady=('OCR4A', 'aFeed_Divisor'))
            rate = afeed_rate(*m[1:4], af)
            self.check(dz > 0 and abs(dz / dt - rate) <= 0.03 * rate, f'асинхрон {af} мм/мин: {dz / dt:.0f} шаг/с, расчёт {rate:.0f}')
            return
        self.q.on(2 * enc, lead=cw); time.sleep(0.8)
        try:
            if mode == 'M1':
                f = FEED[row['feed']]
                esp.btn(f'FEED:{f}'); time.sleep(0.4)
                de, dz, _, _ = self.hold('JOY_LEFT', steady=('OCR5A', 'Feed_Divisor'))
                exp = feed_steps(de, enc, *m[1:4], f)
                self.check(dz > 0 and abs(dz - exp) <= max(3, 0.02 * exp), f'подача 0.{f:02d}: {dz} шагов, расчёт {exp:.0f}')
            elif mode == 'M3':
                idx, pitch = THREAD[row['feed']]
                esp.btn(f'THREAD_STEP:{idx}'); time.sleep(0.4)
                de, dz, _, _ = self.hold('JOY_LEFT', steady=('tmp_Ks_Divisor', 'Ks_Divisor'), secs=1.5)
                exp = thread_steps(de, enc, sz, pitch)
                self.check(dz > 0 and abs(dz - exp) <= max(3, 0.01 * exp),
                           f'резьба {pitch} мм, шпиндель {row["spindle"]}: {dz} шагов, расчёт {exp:.0f}')
            elif mode in ('M4', 'M5'):
                f = FEED[row['feed']]
                esp.btn('CONE:0'); esp.btn(f'FEED:{f}'); time.sleep(0.4)          # 45°: X проходит столько же, сколько Z
                _, dz, dx, _ = self.hold('JOY_LEFT', secs=3.0)
                rz, rx = dz / sz, dx / sx
                self.check(dz > 0 and dx > 0 and abs(rx - rz) <= 0.04 * rz + 0.01,
                           f'конус 45°: Z {rz:.3f} мм, X {rx:.3f} мм (должны совпасть)')
            elif mode == 'M6':
                f = FEED[row['feed']]
                esp.btn(f'FEED:{f}'); time.sleep(0.4)
                _, dz, _, _ = self.hold('JOY_LEFT', secs=2.0)
                self.check(dz > 0, f'шар «Ручной»: ось Z едет ({dz} шагов)')
        finally:
            self.q.off()

    def cycle(self, row):
        st, esp = self.stm, self.esp
        m = MECH[row['mech']]
        mode, sub = row['mode'], 'S3' if row['sub'] == 'EXT' else 'S1'
        fz = FEED[row['feed']]
        prep = {'M1': [f'FEED:{fz}', 'AP:20', 'PASSES:2'],
                'M2': [f'AFEED:{AFEED[row["feed"]]}', 'AP:10', 'PASSES:2'],
                'M3': [f'THREAD_STEP:{THREAD[row["feed"]][0]}', 'PH:1'],
                'M4': ['CONE:1', f'FEED:{fz}', 'AP:20', 'PASSES:2'],
                'M5': ['CONE:0', f'FEED:{fz}', 'AP:20', 'PASSES:2'],
                'M6': [f'FEED:{fz}']}[mode]
        rpm = 0 if mode == 'M2' else 400
        case = (f'{mode} {row["sub"]}', mode, sub, rpm, prep)
        esp.btn('M2'); time.sleep(0.4); esp.btn('AFEED:300'); time.sleep(0.3)
        problems = hc.run_case(st, esp, self.dec, Spin(self.q, m[0], row['spindle'] == 'CW'), case, self.a, None)
        self.check(not problems, f'автоцикл {mode} {row["sub"]}: ' + ('завершён' if not problems else '; '.join(problems)))
        for pin, flag in hc.LIMS:
            cd.run_actions(st, esp, [('lim', (pin, flag, 0))])

    def negative(self, row):
        st, esp = self.stm, self.esp
        esp.btn(row['mode']); time.sleep(0.8)
        p0 = self.pos()
        esp.btn('S1'); time.sleep(1.0)
        st.press('JOY_LEFT'); time.sleep(1.5); st.release('JOY_LEFT'); time.sleep(0.5)
        u = esp.uidump()
        refused = st.var('err_1_flag') or st.var('err_2_flag') or 'НЕВОЗМОЖЕН' in cd.norm(u.get('alert_msg', '')).upper()
        self.check(refused and self.pos() == p0, f'{pw.negative(row)}: отказ, оси стоят')
        esp.btn('S2'); esp.btn('ALERT_OK'); time.sleep(0.5)

    def finish(self):
        st, esp = self.stm, self.esp
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_EXT', 'SUB_INT'):
            try:
                st.release(p)
            except Exception:
                pass
        try:
            self.q.off()
            if self.source != 'DRO':
                esp.btn(f'POS_SET:1,{POS_TAIL}'); time.sleep(6)
            esp.btn('CFG_DEFAULT'); time.sleep(3)
            st.release('SUB_MAN')
            self.mega.cmd('f0'); self.mega.cmd('w1')
        except Exception:
            pass
        st.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mega', default='/dev/cu.usbserial-1410')
    ap.add_argument('--esp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--only')
    ap.add_argument('--timeout', type=float, default=200)
    a = ap.parse_args()
    rows = pw.pairwise(pw.PARAMS)
    idx = [int(x) for x in a.only.split(',')] if a.only else range(1, len(rows) + 1)
    r = Run(a)
    try:
        for i in idx:
            row = rows[i - 1]
            print(f'[{i}] ' + ' '.join(f'{k}={v}' for k, v in row.items()))
            try:
                r.setup(row)
                if pw.negative(row):
                    r.negative(row)
                elif row['sub'] == 'MAN':
                    r.manual(row)
                else:
                    r.cycle(row)
            except Exception as ex:
                r.check(False, f'сочетание {i}: {ex}')
    finally:
        r.finish()
    print('\nИТОГ:', 'OK' if not r.fails else f'{len(r.fails)} FAIL')
    for f in r.fails:
        print('  -', f)
    return 1 if r.fails else 0


if __name__ == '__main__':
    sys.exit(main())
