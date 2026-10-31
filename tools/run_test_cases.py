#!/usr/bin/env python3
"""
Прогон TEST_CASES.md на дисплее (4.3" или 10.1") со скриншотами.

Для каждого «### TC-NNN»:
  - из **Input** берутся все команды `<...>` по порядку:
      `<TOUCH:X>` → `<BTN:X>` (уходит в STM32 как нажатие), остальные — инъекция как от STM32;
  - если в кейсе нет TOUCH — данные от STM32 на время кейса глушатся (<RXMUTE:1>),
    чтобы живые POS_Z/RPM от симулятора DRO не перетирали инжектированное состояние;
  - из **Expected** берутся пары  поле = "значение"  и сверяются с <UIDUMP> дисплея;
  - снимается скриншот.
Кейсы без пар поле="значение" помечаются VISUAL — проверка по скриншоту/логу вручную.

Запуск:
  python3 tools/run_test_cases.py --port /dev/cu.usbmodem14801 --out <папка> [--only TC-031]
"""

import argparse, io, os, re, struct, time
import serial

MAGIC = b'\xde\xad\xbe\xef'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BASELINE = ["<ALERT:0>", "<MODE:1>", "<SUBMODE:1>", "<SELECTMENU:1>", "<FEED:25>",
            "<AFEED:100>", "<AP:0>", "<PASS:0,0>", "<RPM:0>", "<POS_Z:0>", "<POS_X:0>",
            "<LIMITS:0,0,0,0>", "<STATE:stop>"]


def parse_cases(path):
    cases, cur = [], None
    for line in open(path, encoding='utf-8'):
        m = re.match(r'### (TC-\d+): (.*)', line)
        if m:
            cur = {'id': m.group(1), 'title': m.group(2).strip(), 'input': '', 'expected': ''}
            cases.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith('**Input**'):
            cur['input'] = line.split(':', 1)[1].strip()
        elif line.startswith('**Expected**'):
            cur['expected'] = line.split(':', 1)[1].strip()
    for c in cases:
        c['cmds'] = re.findall(r'`(<[^`]*>)`', c['input'])
        c['live'] = any(x.startswith('<TOUCH:') for x in c['cmds'])
        c['checks'] = re.findall(r'\b([a-z0-9_]+) = "([^"]*)"', c['expected'])
    return cases


def clean(text):
    """Убрать LVGL recolor-разметку (#RRGGBB текст#) и крайние пробелы."""
    text = re.sub(r'#[0-9a-fA-F]{6} ', '', text).replace('#', '')
    return text.replace('\\n', '\n').strip()


def parse_ui(line):
    ui = {k: clean(v) for k, v in re.findall(r' (\w+)="((?:[^"\\]|\\.)*)"', line)}
    ui.update({k: v for k, v in re.findall(r' (\w+)=([0-9A-F,]+)(?= |$)', line)})
    return ui


class Display:
    def __init__(self, port, raw_log):
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(1.0)
        self.buf = ''
        self.raw = open(raw_log, 'w', encoding='utf-8')   # весь вывод дисплея (кроме JPEG)

    def drain(self, t):
        end = time.time() + t
        while time.time() < end:
            d = self.ser.read(4096)
            if d:
                txt = d.decode('utf-8', 'replace')
                self.buf += txt
                self.raw.write(txt); self.raw.flush()

    def send(self, s, wait=0.06):
        self.ser.write(s.encode()); self.ser.flush()
        self.drain(wait)

    def ui(self):
        self.buf = ''
        self.send('<UIDUMP>', 0.4)
        lines = [l for l in self.buf.splitlines() if l.startswith('[UI]')]
        return parse_ui(lines[-1]) if lines else {}

    def shot(self, path):
        self.drain(0.05); self.ser.reset_input_buffer()
        self.ser.write(b'S'); self.ser.flush()
        buf, end = b'', time.time() + 8
        while MAGIC not in buf:
            buf += self.ser.read(4096)
            if time.time() > end:
                return False
        buf = buf[buf.index(MAGIC) + 4:]
        while len(buf) < 4:
            buf += self.ser.read(4)
        n = struct.unpack('<I', buf[:4])[0]
        data, end = buf[4:], time.time() + 20
        while len(data) < n and time.time() < end:
            data += self.ser.read(n - len(data))
        if len(data) < n:
            return False
        from PIL import Image
        img = Image.open(io.BytesIO(data[:n]))
        if img.height > img.width:      # 10.1": физический портретный кадр
            img = img.rotate(-90, expand=True)
        img.save(path, quality=85)
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='/dev/cu.usbmodem14801')
    ap.add_argument('--out', required=True)
    ap.add_argument('--cases', default=os.path.join(ROOT, 'TEST_CASES.md'))
    ap.add_argument('--only', default=None)
    a = ap.parse_args()
    shots = os.path.join(a.out, 'screenshots')
    os.makedirs(shots, exist_ok=True)

    d = Display(a.port, os.path.join(a.out, 'device.log'))
    results = []
    for c in parse_cases(a.cases):
        if a.only and c['id'] != a.only:
            continue
        d.raw.write(f"\n===== {c['id']} {c['title']}\n")
        d.send('<RXMUTE:%d>' % (0 if c['live'] else 1), 0.1)
        if not c['live']:
            for cmd in BASELINE:
                d.send(cmd)
        d.drain(0.3)
        log_start = len(d.buf)
        for cmd in c['cmds']:
            d.send('<BTN:' + cmd[7:] if cmd.startswith('<TOUCH:') else cmd,
                   0.35 if cmd.startswith('<TOUCH:') else 0.08)
        d.drain(1.2 if c['live'] else 0.5)
        log = d.buf[log_start:]
        ui = d.ui()
        diffs = []
        for k, v in c['checks']:
            got = ui.get(k)
            if got is None:
                diffs.append(f'{k}: поле не найдено')
            elif got != v.strip():
                diffs.append(f'{k}: ожидалось "{v}", на экране "{got}"')
        status = 'VISUAL' if not c['checks'] else ('PASS' if not diffs else 'FAIL')
        shot = f"{c['id']}.jpg"
        if not d.shot(os.path.join(shots, shot)):
            shot += ' (ошибка скриншота)'
        btn = re.findall(r'\[BTN→STM32\][^\n]*', log)
        results.append((c, status, diffs, shot, ui, btn))
        print(f"{c['id']} {status:6} {c['title']}" + (f"  ← {'; '.join(diffs)}" if diffs else ''))
    d.send('<RXMUTE:0>', 0.1)

    cnt = {s: sum(1 for r in results if r[1] == s) for s in ('PASS', 'FAIL', 'VISUAL')}
    with open(os.path.join(a.out, 'TEST_CASES_REPORT.md'), 'w', encoding='utf-8') as f:
        f.write(f"# TEST_CASES.md — прогон\n\nPASS {cnt['PASS']} · FAIL {cnt['FAIL']} · "
                f"VISUAL {cnt['VISUAL']} (проверка по скриншоту) · всего {len(results)}\n\n")
        f.write("| TC | Статус | Кейс | Расхождения / ожидание | Скриншот |\n|---|---|---|---|---|\n")
        for c, st, diffs, shot, ui, btn in results:
            note = '; '.join(diffs) if diffs else (c['expected'] if st == 'VISUAL' else '')
            if btn:
                note += ' · ' + ', '.join(b.replace('[BTN→STM32] ', '') for b in btn)
            f.write(f"| {c['id']} | {st} | {c['title']} | {note.replace('|', '/')} | {shot} |\n")
    print(f"\nPASS {cnt['PASS']}  FAIL {cnt['FAIL']}  VISUAL {cnt['VISUAL']}  / {len(results)}")


if __name__ == '__main__':
    main()
