#!/usr/bin/env python3
"""
Проверка источников координат «в железе»: симулятор Arduino Mega → STM32 → дисплей.

Симулятор (Simulator/dro_simulator) выдаёт позиции в выбранном формате, STM32
пересчитывает их, дисплей показывает; скрипт сверяет показания дисплея (<UIDUMP>)
с заданными позициями.

Режимы:
  hs800  — блок DRO по UART (STM32: ELS_POS_SOURCE = POS_SOURCE_DRO_UART)
  q      — квадратура (SCALE_QUADRATURE, 5 мкм)
  s      — Sylvac 48 бит      b — BIN6      i — iGaging 21 бит      d — Digimatic
Для q/s/b/i/d нужна проводка Mega → STM32 из шапки Simulator/dro_simulator/src/main.cpp.

  python3 tools/scales_hil_test.py --mode hs800
  python3 tools/scales_hil_test.py --mode q --flash     # прошить STM32 под режим и проверить
"""
import argparse, os, re, subprocess, sys, time
import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO = os.path.expanduser('~/.platformio/penv/bin/pio')

STM32_FLAGS = {
    'hs800': '-DELS_POS_SOURCE=POS_SOURCE_DRO_UART',
    'q': '-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_QUADRATURE -DSCALE_X_TYPE=SCALE_QUADRATURE',
    's': '-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_SYLVAC48 -DSCALE_X_TYPE=SCALE_SYLVAC48',
    'b': '-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_BIN6 -DSCALE_X_TYPE=SCALE_BIN6',
    'i': '-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_IGAGING21 -DSCALE_X_TYPE=SCALE_IGAGING21',
    'd': '-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_DIGIMATIC -DSCALE_X_TYPE=SCALE_DIGIMATIC',
}
# Допуск, мм: дисплей отбрасывает третий знак (до 0.01) + шаг линейки
TOL_MM = {'hs800': 0.0101, 'q': 0.0151, 's': 0.0115, 'b': 0.0151, 'i': 0.0201, 'd': 0.0101}

POSITIONS = [(0, 0), (12345, 2500), (-12345, -2500), (100000, 30000), (-250005, 7777), (1, -1)]


def sim_cmd(sim, cmd):
    sim.write((cmd + '\n').encode()); sim.flush()
    time.sleep(0.15)
    return sim.read(4096).decode('utf-8', 'replace')


def display_rows(disp):
    disp.reset_input_buffer()
    disp.write(b'<UIDUMP>'); disp.flush()
    time.sleep(0.6)
    out = disp.read(200000).decode('utf-8', 'replace')
    lines = [l for l in out.splitlines() if l.startswith('[UI]')]
    if not lines:
        return None
    vals = {}
    for k in ('row1_val', 'row2_val', 'row1_title', 'row2_title'):
        m = re.search(k + r'="([^"]*)"', lines[-1])
        vals[k] = m.group(1) if m else ''
    return vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', required=True, choices=list(STM32_FLAGS))
    ap.add_argument('--sim', default='/dev/cu.usbserial-1410')
    ap.add_argument('--disp', default='/dev/cu.usbmodem14801')
    ap.add_argument('--flash', action='store_true', help='прошить STM32 под выбранный режим')
    a = ap.parse_args()

    if a.flash:
        print(f'Прошивка STM32: {STM32_FLAGS[a.mode]}')
        env = dict(os.environ, PLATFORMIO_BUILD_FLAGS=STM32_FLAGS[a.mode])
        r = subprocess.run([PIO, 'run', '-e', 'stm32f407_discovery', '-t', 'upload'],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        if 'Verified OK' not in r.stdout + r.stderr:
            print('ОШИБКА прошивки STM32'); sys.exit(2)
        time.sleep(3)

    sim = serial.Serial(a.sim, 115200, timeout=0.2)
    time.sleep(2.5); sim.read(4096)                     # Mega перезагружается при открытии порта
    disp = serial.Serial(a.disp, 115200, timeout=0.3)
    time.sleep(0.3)
    disp.write(b'<RXMUTE:0>')
    disp.write(b'<BTN:M1>'); time.sleep(0.8)           # позиции Y/X — в ячейках row1/row2 экрана «Подача»
    disp.write(b'<BTN:S2>'); time.sleep(0.8)

    sim_cmd(sim, 'w0')
    sim_cmd(sim, 'm' + (a.mode if a.mode != 'hs800' else '0'))
    fails = 0
    for z, x in POSITIONS:
        sim_cmd(sim, f'z{z}'); sim_cmd(sim, f'x{x}')
        # квадратура «доезжает» ~15 000 имп/с — ждём, пока показание перестанет меняться (до 10 с)
        rows, prev, t0 = None, None, time.time()
        while time.time() - t0 < 10:
            time.sleep(1.0)
            rows = display_rows(disp)
            cur = rows and (rows.get('row1_val'), rows.get('row2_val'))
            if cur and cur == prev:
                break
            prev = cur
        if not rows:
            print(f'FAIL z={z} x={x}: нет ответа дисплея'); fails += 1; continue
        try:
            gz, gx = float(rows['row1_val']), float(rows['row2_val'])
        except ValueError:
            print(f'FAIL z={z} x={x}: на экране {rows}'); fails += 1; continue
        ok = abs(gz - z / 1000) <= TOL_MM[a.mode] and abs(gx - x / 1000) <= TOL_MM[a.mode]
        fails += not ok
        print(f"{'OK  ' if ok else 'FAIL'} задано Z={z/1000:+.3f} X={x/1000:+.3f}  "
              f"экран Z={rows['row1_val']} X={rows['row2_val']}")
    sim_cmd(sim, 'm0'); sim_cmd(sim, 'w1')
    print(f"Итог {a.mode}: {'OK' if not fails else f'ошибок {fails}'} из {len(POSITIONS)}")
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
