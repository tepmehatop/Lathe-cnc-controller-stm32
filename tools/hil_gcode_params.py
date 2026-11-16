#!/usr/bin/env python3
"""
Меняет ли запуск G-кода (M8) параметры других режимов? Вопрос владельца 2026-11-16: после G-кода в M1 подача 0.13.

1. Сброс STM32 (Mega — «следует за моторами», знак линеек учится заново), в каждом режиме — нестандартные
   значения с экрана (подача, асинхронная подача, съём, резьба, заходы, конус, шар, делилка, проходы).
2. Снимок параметров ядра по SWD.
3. M8, «пешка» через веб-страницу дисплея (PUT /api/file, POST /api/run), шпиндель — эмуляция; через 20 с — стоп
   (POST /api/stop); затем повторный запуск и стоп.
4. Снимок ещё раз, сравнение; затем по очереди M1…M7 — значения на экране дисплея.

Запуск: ~/.platformio/penv/bin/python tools/hil_gcode_params.py [--ip 192.168.1.57]
"""
import argparse, json, os, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_displays as cd
from follow_hil import Mega
from hil_mech import Quadrature

VARS = ('Mode', 'SelectMenu', 'Feed_mm', 'aFeed_mm', 'Ap', 'Thread_Step', 'Ph', 'Cone_Step', 'Sph_R_mm',
        'Bar_R_mm', 'Pass_Total', 'Total_Tooth', 'Current_Tooth', 'Sub_Mode_Feed', 'Sub_Mode_aFeed',
        'Sub_Mode_Thread', 'Sub_Mode_Cone', 'Sub_Mode_Sphere')


def http(method, url, data=None):
    req = urllib.request.Request(url, data=data, method=method)
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read().decode(errors='replace')


def snap(stm):
    out = {}
    for v in VARS:
        try:
            out[v] = stm.var(v)
        except KeyError:
            pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', default='192.168.1.57')
    ap.add_argument('--file', default='gcode_sample/pawn.nc')
    a = ap.parse_args()
    base = f'http://{a.ip}'
    stm, esp, mega = cd.Stm32(), cd.Esp('/dev/cu.usbmodem14801'), Mega('/dev/cu.usbserial-1410')
    sp = Quadrature(stm)
    fails = []
    try:
        for m in ('w0', 'lz0', 'lx0', 'kz2500', 'kx1250', 'f1', 'o'):
            mega.cmd(m)
        stm.press('SUB_MAN')
        stm.cmd('reset run'); time.sleep(5)
        print('после сброса STM32:', snap(stm))
        # нестандартные значения в каждом режиме
        for cmd in ('M1', 'S2', 'FEED:21', 'AP:35', 'M2', 'S2', 'AFEED:270', 'AP:45', 'M3', 'S2', 'THREAD_STEP:15',
                    'PH:2', 'M4', 'S2', 'CONE:12', 'M6', 'S2', 'SPHERE:1500', 'BAR:300', 'M7', 'DIVN:12', 'DIVM:3',
                    'M1'):
            esp.btn(cmd); time.sleep(0.5)
        before = snap(stm)
        print('до G-кода:  ', before)
        # знак линеек — оси проехали по 3 мм (как просит «ПРОВЕДИТЕ ОСИ ДЖОЙСТИКОМ»)
        esp.btn('M2'); time.sleep(0.5)
        sa = stm.sym['e2_scale_sign'][0]
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_DOWN', 'JOY_UP'):
            z0, x0 = stm.var('Motor_Z_Pos'), stm.var('Motor_X_Pos')
            stm.press(p); time.sleep(1.2); stm.release(p); time.sleep(0.8)
            print(f'  {p}: шагов Z {stm.var("Motor_Z_Pos") - z0}, X {stm.var("Motor_X_Pos") - x0}, '
                  f'знак линеек {stm.read(sa, 2, 8)}')
        esp.btn('M8'); time.sleep(1.0)
        name = os.path.basename(a.file)
        print('загрузка:', http('PUT', f'{base}/api/file?name={name}', open(a.file, 'rb').read())[:60])
        sp.on(2 * 1800)                                        # 120 об/мин — для G99
        for run in (1, 2):
            print(f'запуск {run}:', http('POST', f'{base}/api/run?name={name}')[:60])
            t0 = time.time(); last = None
            while time.time() - t0 < 20:
                time.sleep(1.0)
                ex = json.loads(http('GET', f'{base}/api/exec'))
                st = (ex.get('state'), ex.get('line'), ex.get('pct'), ex.get('error'))
                if st != last:
                    print('   ', st); last = st
                if str(ex.get('state')).upper() in ('ERROR', 'DONE'):
                    break
            print('  стоп:', http('POST', f'{base}/api/stop')[:60]); time.sleep(2)
            ex = json.loads(http('GET', f'{base}/api/exec'))
            print('  после стопа:', ex.get('state'), ex.get('error'))
            if run == 1 and ex.get('error'):
                fails.append(f'ошибка после первого запуска: {ex.get("error")}')
        sp.off()
        after = snap(stm)
        print('после G-кода:', after)
        for k in before:
            if k in ('Mode', 'SelectMenu'):
                continue
            if before[k] != after.get(k):
                fails.append(f'{k}: было {before[k]}, стало {after.get(k)}')
        # экран: значения в M1 и M2 после выхода из M8
        for m, key, want in (('M1', 'primary_val', '0.21'), ('M2', 'primary_val', '270')):
            esp.btn(m); time.sleep(1.2)
            u = esp.uidump()
            ok = u.get(key) == want
            print(f'  {m}: {key}={u.get(key)} (ожидалось {want})')
            if not ok:
                fails.append(f'{m} на экране {u.get(key)}, ожидалось {want}')
    finally:
        try:
            sp.off()
        except Exception:
            pass
        for p in ('JOY_LEFT', 'JOY_RIGHT', 'JOY_UP', 'JOY_DOWN', 'SUB_MAN'):
            try:
                stm.release(p)
            except Exception:
                pass
        stm.close()
    print('\nИТОГ:', 'OK' if not fails else f'{len(fails)} FAIL')
    for f in fails:
        print('  -', f)


if __name__ == '__main__':
    main()
