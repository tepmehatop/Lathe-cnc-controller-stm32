#!/usr/bin/env python3
"""
Расширенное тестирование на стенде — все наборы подряд, сводка в конце (2026-11-16).

  ~/.platformio/penv/bin/python tools/run_hil_suite.py [--out папка] [--only alerts,gcode]

Наборы (порядок — сначала движение и стопы):
  mech     — tools/hil_mech.py: 16 механик, реальное движение M1/M2/M3, отказы, после сброса STM32
  cycles   — tools/hil_cycles.py: автоциклы всех режимов, проходы, LCD2004 = дисплей
  alerts   — tools/hil_alerts.py: пропуск шагов, упор, стоп G-кода джойстиком, отпускание в автоцикле, негатив
  gcode    — tools/hil_gcode_ui.py: запуск не из M8, блокировка, пауза посреди кадра, продолжить, стопы
  submenu  — tools/ui_submenu_test.py: двойной тап по ⌀ и тапы по плиткам во всех режимах и подэкранах
  kb       — tools/ui_kb_test.py: клавиатура подачи и съёма — пределы, отказ, ОТМЕНА
Стенд: STM32 (stm32f407_dro) по ST-Link, дисплей 10.1" по USB и Wi-Fi, Mega — эмулятор DRO.
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
SUITES = [
    ('mech', ['hil_mech.py']),
    ('cycles', ['hil_cycles.py', '--out', '{out}/cycles']),
    ('alerts', ['hil_alerts.py']),
    ('gcode', ['hil_gcode_ui.py', '--shots', '{out}']),
    ('submenu', ['ui_submenu_test.py']),
    ('kb', ['ui_kb_test.py', '--shots', '{out}']),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '.pio', 'hil_suite'))
    ap.add_argument('--only')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(os.path.join(out, 'cycles'), exist_ok=True)
    only = set(a.only.split(',')) if a.only else None
    summary = []
    for name, args in SUITES:
        if only and name not in only:
            continue
        cmd = [PY, '-u', os.path.join(HERE, args[0])] + [x.format(out=out) for x in args[1:]]
        print(f'=== {name}: {" ".join(cmd[2:])}', flush=True)
        t0 = time.time()
        with open(os.path.join(out, name + '.log'), 'w') as f:
            p = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=os.path.join(HERE, '..'))
        log = open(os.path.join(out, name + '.log'), encoding='utf-8', errors='replace').read()
        fails = [l.strip() for l in log.splitlines() if 'FAIL' in l and not l.startswith('ИТОГ')]
        oks = sum(1 for l in log.splitlines() if l.strip().startswith('OK'))
        summary.append((name, p.returncode, oks, fails, time.time() - t0))
        print(f'    код {p.returncode}, OK {oks}, FAIL {len(fails)}, {time.time() - t0:.0f} с', flush=True)
        for l in fails[:20]:
            print('      ', l[:160], flush=True)
        time.sleep(3)
    print('\nСВОДКА')
    for name, rc, oks, fails, dt in summary:
        print(f'  {name:8s} {"OK  " if rc == 0 and not fails else "FAIL"}  OK {oks:3d}  FAIL {len(fails):3d}  {dt:5.0f} с')
    return 0 if all(rc == 0 and not f for _, rc, _, f, _ in summary) else 1


if __name__ == '__main__':
    sys.exit(main())
