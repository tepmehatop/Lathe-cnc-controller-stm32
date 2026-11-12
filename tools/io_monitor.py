#!/usr/bin/env python3
"""
Монитор входов через ST-Link — проверка проводки на макетке без USB-UART.

Читает уровни входов STM32 прямо из регистров GPIO (SWD, тот же USB-кабель ST-Link, что для прошивки) и печатает
каждое изменение: кнопки, переключатели, упоры, джойстик, РГИ. Если рядом есть сборка прошивки (firmware.elf) —
ещё щелчки РГИ и угол шпинделя. Прошивка при этом работает как обычно, ничего не останавливается.

Зачем: отладочный TX (PA9) на плате Discovery подключён к VBUS разъёма USB OTG через конденсатор 4.7 мкФ (C49),
поэтому строки [IO] через USB-UART на 115200 могут не приходить. ST-Link — всегда под рукой.

Запуск — из корня проекта, в терминале PlatformIO (VS Code: PlatformIO → New Terminal):
  python tools/io_monitor.py                 — входы + РГИ/шпиндель по .pio/build/stm32f407_discovery
  python tools/io_monitor.py --env stm32f407_dro
Остановить — Ctrl+C.
"""
import argparse, os, re, socket, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PIO = os.environ.get('PLATFORMIO_CORE_DIR') or os.path.expanduser(os.path.join('~', '.platformio'))
EXE = '.exe' if os.name == 'nt' else ''
OCD = os.path.join(PIO, 'packages', 'tool-openocd')
NM = os.path.join(PIO, 'packages', 'toolchain-gccarmnoneeabi', 'bin', 'arm-none-eabi-nm' + EXE)
GPIO_BASE = {c: 0x40020000 + 0x400 * i for i, c in enumerate('ABCDE')}
IDR = 0x10


def inputs():
    """[(название, порт, бит)] — список и названия как у монитора в прошивке (e2_core.cpp, IO_MONITOR)."""
    pins = {}
    for m in re.finditer(r'#define\s+E2_PIN_(\w+)\s+([A-E]),\s*(\d+)',
                         open(os.path.join(ROOT, 'Core', 'e2', 'e2_pins.h'), encoding='utf-8').read()):
        pins[m.group(1)] = (m.group(2), int(m.group(3)))
    src = open(os.path.join(ROOT, 'Core', 'e2', 'e2_core.cpp'), encoding='utf-8').read()
    out = []
    for m in re.finditer(r'E2_IN\("([^"]+)",\s*E2_PIN_(\w+)\)', src):
        if m.group(2) in pins:
            port, bit = pins[m.group(2)]
            out.append((m.group(1), port, bit, 'P%s%d' % (port, bit)))
    return out


class Ocd:
    def __init__(self, port=6666):
        exe = os.path.join(OCD, 'bin', 'openocd' + EXE)
        if not os.path.exists(exe):
            sys.exit('Нет OpenOCD (%s). Один раз прошейте STM32 из PlatformIO — он скачается сам.' % exe)
        self.proc = subprocess.Popen(
            [exe, '-s', os.path.join(OCD, 'openocd', 'scripts'), '-f', 'interface/stlink.cfg',
             '-f', 'target/stm32f4x.cfg', '-c', 'tcl_port %d' % port, '-c', 'gdb_port disabled',
             '-c', 'telnet_port disabled', '-c', 'init'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.sock = socket.create_connection(('127.0.0.1', port), timeout=5)
                return
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.1)
        sys.exit('ST-Link не отвечает: плата подключена кабелем к разъёму ST-Link (CN1)? Закрыт ли другой '
                 'отладчик или STM32CubeProgrammer?')

    def cmd(self, c):
        self.sock.sendall(c.encode() + b'\x1a')
        buf = b''
        while not buf.endswith(b'\x1a'):
            buf += self.sock.recv(65536)
        return buf[:-1].decode(errors='replace')

    def word(self, addr):
        return int(self.cmd('read_memory 0x%08x 32 1' % addr).split()[0], 16)

    def close(self):
        try:
            self.cmd('shutdown')
        except Exception:
            pass
        self.proc.wait(timeout=5)


def symbols(env):
    """Адреса счётчиков РГИ и шпинделя из сборки — если она есть."""
    elf = os.path.join(ROOT, '.pio', 'build', env, 'firmware.elf')
    if not (os.path.exists(elf) and os.path.exists(NM)):
        return {}
    out = subprocess.run([NM, '-S', '-C', elf], capture_output=True, text=True).stdout
    sym = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] in ('Hand_Count', 'Spindle_Angle', 'Spindle_Dir', 's_spindle_edges'):
            sym[p[3]] = int(p[0], 16)
    return sym if len(sym) == 4 else {}


def enc_lines():
    m = re.search(r'#define\s+ENC_LINE_PER_REV\s+(\d+)',
                  open(os.path.join(ROOT, 'Core', 'els_config.h'), encoding='utf-8').read())
    return int(m.group(1)) if m else 1800


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--env', default='stm32f407_discovery', help='сборка, из которой брать адреса РГИ/шпинделя')
    ap.add_argument('--period', type=float, default=0.02, help='период опроса, с')
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    ins = inputs()
    ocd = Ocd()
    sym = symbols(a.env)
    lines = enc_lines()
    print('Монитор входов через ST-Link: %d входов%s. Ctrl+C — выход.' %
          (len(ins), ', РГИ и энкодер шпинделя' if sym else ' (РГИ и шпиндель — после сборки прошивки)'))
    print('Сейчас:')
    ports = sorted({p for _, p, _, _ in ins})
    prev = {}
    try:
        first = True
        last_slow = 0
        slow_prev = {}
        while True:
            idr = {p: ocd.word(GPIO_BASE[p] + IDR) for p in ports}
            for name, p, bit, pin in ins:
                v = (idr[p] >> bit) & 1
                if first:
                    if not v:
                        print('  %-30s %-5s замкнуто на GND (0)' % (name, pin))
                elif v != prev[name]:
                    print('[IO] %s (%s): %s' % (name, pin, 'отпущено (1)' if v else 'замкнуто на GND (0)'))
                prev[name] = v
            if first:
                if all(prev.values()):
                    print('  всё отпущено (1) — ни один вход не замкнут')
                print('Ждём изменений…')
                first = False
            if sym and time.time() - last_slow >= 1.0:
                last_slow = time.time()
                hand = ocd.word(sym['Hand_Count'])
                hand -= (1 << 32) if hand >= 1 << 31 else 0
                edges = ocd.word(sym['s_spindle_edges'])
                if 'hand' in slow_prev and hand != slow_prev['hand']:
                    print('[IO] РГИ: %+d щелчков (всего %d)' % (hand - slow_prev['hand'], hand))
                if 'edges' in slow_prev and edges != slow_prev['edges']:
                    d = (edges - slow_prev['edges']) & 0xffffffff
                    ang = ocd.word(sym['Spindle_Angle'])
                    cw = (ocd.word(sym['Spindle_Dir']) & 0xff) == 0
                    print('[IO] Энкодер шпинделя: %d фронтов A за 1 с (%d об/мин при %d линиях), угол %d.%03d°, %s'
                          % (d, d * 60 // (2 * lines), lines, ang // 1000, ang % 1000,
                             'по часовой' if cw else 'против часовой'))
                slow_prev['hand'], slow_prev['edges'] = hand, edges
            time.sleep(a.period)
    except KeyboardInterrupt:
        print()
    finally:
        ocd.close()


if __name__ == '__main__':
    main()
