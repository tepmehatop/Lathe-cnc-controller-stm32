#!/usr/bin/env python3
"""
test_runner.py — ELS Display Level-2 Test Runner
Инжектирует UART команды на ESP32 через USB CDC, делает скриншоты, сохраняет результаты.

Использование:
  python3 test_runner.py [port]
  python3 test_runner.py /dev/cu.usbmodem14801
"""

import serial
import time
import struct
import os
import sys
import json
from datetime import datetime
from pathlib import Path

PORT     = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem14801"
BAUD     = 115200
TOOLS_DIR = Path(__file__).parent
RESULTS_DIR = TOOLS_DIR / "test_results"
RESULTS_DIR.mkdir(exist_ok=True)

# ─── Низкоуровневые функции ────────────────────────────────────────────────

def send_cmd(ser: serial.Serial, cmd: str):
    """Отправить UART команду (формат <CMD:value>) через USB CDC инжектор."""
    line = cmd.strip()
    if not line.startswith('<'):
        line = f"<{line}>"
    ser.write((line + "\n").encode())
    time.sleep(0.05)

def send_all_state(ser, mode, submode=2, feed=25, afeed=100,
                   thread=400, rpm=0, ap=0, ph=1,
                   pass_nr=1, pass_total=1, select_menu=1,
                   pos_z=0, pos_x=0,
                   thread_name="M4.0", rpm_lim=80, thread_cycl=36, thread_travel=400,
                   cone_idx=0, cone_angle=10,
                   sphere=1000, bar=0, divn=1, divm=1, angle=0,
                   limits="0,0,0,0"):
    """Отправить полное состояние как STM32 SendAll."""
    cmds = [
        f"<MODE:{mode}>",
        f"<SUBMODE:{submode}>",
        f"<FEED:{feed}>",
        f"<AFEED:{afeed}>",
        f"<AP:{ap}>",
        f"<PH:{ph}>",
        f"<PASS:{pass_nr},{pass_total}>",
        f"<SELECTMENU:{select_menu}>",
        f"<POS_Z:{pos_z}>",
        f"<POS_X:{pos_x}>",
        f"<RPM:{rpm}>",
        f"<THREAD_NAME:{thread_name}>",
        f"<THREAD:{thread}>",
        f"<RPM_LIM:{rpm_lim}>",
        f"<THREAD_CYCL:{thread_cycl}>",
        f"<THREAD_TRAVEL:{thread_travel}>",
        f"<CONE:{cone_idx}>",
        f"<CONE_ANGLE:{cone_angle}>",
        f"<SPHERE:{sphere}>",
        f"<BAR:{bar}>",
        f"<DIVN:{divn}>",
        f"<DIVM:{divm}>",
        f"<ANGLE:{angle}>",
        f"<LIMITS:{limits}>",
    ]
    for c in cmds:
        ser.write((c + "\n").encode())
        time.sleep(0.02)
    time.sleep(0.2)

def take_screenshot(ser: serial.Serial, name: str) -> str:
    """Запросить скриншот, сохранить в test_results/name.jpg. Вернуть путь."""
    # Сброс буфера
    ser.read(ser.in_waiting or 1)

    ser.write(b'S')
    ser.timeout = 5

    # Ждём magic header 0xDEADBEEF
    magic = ser.read(4)
    if magic != b'\xDE\xAD\xBE\xEF':
        print(f"  [ERR] No magic header: {magic.hex()}")
        return ""

    size_bytes = ser.read(4)
    jpeg_size = struct.unpack('<I', size_bytes)[0]
    if jpeg_size == 0 or jpeg_size > 2_000_000:
        print(f"  [ERR] Bad JPEG size: {jpeg_size}")
        return ""

    data = b""
    while len(data) < jpeg_size:
        chunk = ser.read(min(512, jpeg_size - len(data)))
        if not chunk:
            print(f"  [ERR] Timeout reading JPEG at {len(data)}/{jpeg_size}")
            return ""
        data += chunk

    path = str(RESULTS_DIR / f"{name}.jpg")
    with open(path, "wb") as f:
        f.write(data)
    print(f"  [SCR] {path} ({jpeg_size} bytes)")
    return path

# ─── Тестовые сценарии ────────────────────────────────────────────────────

TESTS = [
    # ── M1 FEED (mode=1) ────────────────────────────────────────────────────
    {
        "id": "M1-01", "desc": "Feed manual 0.25mm/rev SM=1",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, select_menu=1),
        "expect": {"primary": "0.25", "mode": "ПОДАЧА"},
    },
    {
        "id": "M1-02", "desc": "Feed manual 0.10mm/rev",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=10, select_menu=1),
        "expect": {"primary": "0.10"},
    },
    {
        "id": "M1-03", "desc": "Feed manual 2.50mm/rev (max)",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=250, select_menu=1),
        "expect": {"primary": "2.50"},
    },
    {
        "id": "M1-04", "desc": "Feed SM=2 (ввод параметров)",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, select_menu=2),
        "expect": {"select_menu": 2},
    },
    {
        "id": "M1-05", "desc": "Feed SM=3 (натяг)",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, select_menu=3),
        "expect": {"select_menu": 3},
    },
    {
        "id": "M1-06", "desc": "Feed Internal submode",
        "cmds": lambda s: send_all_state(s, mode=1, submode=1, feed=25, select_menu=1),
        "expect": {"submode": "Внутренняя"},
    },
    {
        "id": "M1-07", "desc": "Feed External submode",
        "cmds": lambda s: send_all_state(s, mode=1, submode=3, feed=25, select_menu=1),
        "expect": {"submode": "Наружная"},
    },
    {
        "id": "M1-08", "desc": "Feed with Ap=50 (0.50mm)",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, ap=50, select_menu=1),
        "expect": {"ap": "0.50"},
    },
    {
        "id": "M1-09", "desc": "Feed with passes 3/5",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, ap=20, pass_nr=3, pass_total=5, select_menu=1),
        "expect": {"pass": "3/5"},
    },
    {
        "id": "M1-10", "desc": "Feed with all limits ON",
        "cmds": lambda s: send_all_state(s, mode=1, submode=2, feed=25, limits="1,1,1,1", select_menu=1),
        "expect": {"limits": "1,1,1,1"},
    },

    # ── M2 AFEED (mode=2) — ОСНОВНАЯ ПРОВЕРКА -32512 ───────────────────────
    {
        "id": "M2-01", "desc": "aFeed 330 mm/min — проверка на -32512",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=330, select_menu=1),
        "expect": {"primary": "330"},  # должно быть "330", не "-32512"
    },
    {
        "id": "M2-02", "desc": "aFeed 100 mm/min (default)",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=100, select_menu=1),
        "expect": {"primary": "100"},
    },
    {
        "id": "M2-03", "desc": "aFeed 15 mm/min (MIN)",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=15, select_menu=1),
        "expect": {"primary": "15"},
    },
    {
        "id": "M2-04", "desc": "aFeed 400 mm/min (MAX)",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=400, select_menu=1),
        "expect": {"primary": "400"},
    },
    {
        "id": "M2-05", "desc": "aFeed SM=2 (угол шпинделя)",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=100, select_menu=2, angle=450),
        "expect": {"primary": "45.0"},
    },
    {
        "id": "M2-06", "desc": "aFeed SM=3",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=100, select_menu=3),
        "expect": {"select_menu": 3},
    },
    {
        "id": "M2-07", "desc": "aFeed с Ap=30",
        "cmds": lambda s: send_all_state(s, mode=2, submode=2, afeed=200, ap=30, select_menu=1),
        "expect": {"ap": "0.30"},
    },

    # ── M3 THREAD (mode=3) ──────────────────────────────────────────────────
    {
        "id": "M3-01", "desc": "Thread M4.0 step 4.00mm Ph=1 cycl=36",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=400, thread_name="M4.0", rpm_lim=80, thread_cycl=36, thread_travel=400,
            ph=1, select_menu=1),
        "expect": {"thread": "4.00", "rpm_lim": "80", "cycl": "36"},
    },
    {
        "id": "M3-02", "desc": "Thread M1.0 step 1.00mm Ph=1 cycl=30",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=100, thread_name="M1.0", rpm_lim=300, thread_cycl=30, thread_travel=100,
            ph=1, select_menu=1),
        "expect": {"thread": "1.00", "rpm_lim": "300"},
    },
    {
        "id": "M3-03", "desc": "Thread M4.0 Ph=2 travel=8.00mm",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=400, thread_name="M4.0", rpm_lim=80, thread_cycl=36, thread_travel=800,
            ph=2, select_menu=1),
        "expect": {"thread_travel": "8.00", "ph": "2"},
    },
    {
        "id": "M3-04", "desc": "Thread SM=2 (Ph изменение)",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=400, thread_name="M4.0", rpm_lim=80, thread_cycl=36, thread_travel=400,
            ph=1, select_menu=2),
        "expect": {"select_menu": 2},
    },
    {
        "id": "M3-05", "desc": "Thread 64tpi (imperial 0.40mm)",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=40, thread_name="64tpi", rpm_lim=300, thread_cycl=30, thread_travel=40,
            ph=1, select_menu=1),
        "expect": {"thread_name": "64tpi"},
    },
    {
        "id": "M3-06", "desc": "Thread G1/8 (pipe 0.907mm)",
        "cmds": lambda s: send_all_state(s, mode=3, submode=2,
            thread=91, thread_name="G1/8", rpm_lim=200, thread_cycl=30, thread_travel=91,
            ph=1, select_menu=1),
        "expect": {"thread_name": "G1/8"},
    },

    # ── M4 CONE_L (mode=4) ──────────────────────────────────────────────────
    {
        "id": "M4-01", "desc": "Cone L step=0 angle=1.0°",
        "cmds": lambda s: send_all_state(s, mode=4, submode=2,
            cone_idx=0, cone_angle=10, feed=25, ap=0, select_menu=1),
        "expect": {"cone": "0", "cone_angle": "1.0"},
    },
    {
        "id": "M4-02", "desc": "Cone L KM2 Morse taper",
        "cmds": lambda s: send_all_state(s, mode=4, submode=2,
            cone_idx=15, cone_angle=149, feed=25, select_menu=1),
        "expect": {"cone_angle": "14.9"},
    },
    {
        "id": "M4-03", "desc": "Cone L SM=2 (Ap ввод)",
        "cmds": lambda s: send_all_state(s, mode=4, submode=2,
            cone_idx=0, cone_angle=10, feed=25, ap=50, select_menu=2),
        "expect": {"ap": "0.50"},
    },

    # ── M5 CONE_R (mode=5) ──────────────────────────────────────────────────
    {
        "id": "M5-01", "desc": "Cone R step=5 angle=5.0°",
        "cmds": lambda s: send_all_state(s, mode=5, submode=2,
            cone_idx=5, cone_angle=50, feed=25, select_menu=1),
        "expect": {"cone_angle": "5.0"},
    },
    {
        "id": "M5-02", "desc": "Cone R with limits L+R",
        "cmds": lambda s: send_all_state(s, mode=5, submode=2,
            cone_idx=5, cone_angle=50, feed=25, limits="1,1,0,0", select_menu=1),
        "expect": {"limits": "1,1,0,0"},
    },

    # ── M6 SPHERE (mode=6) ──────────────────────────────────────────────────
    {
        "id": "M6-01", "desc": "Sphere R=5mm (ø10mm)",
        "cmds": lambda s: send_all_state(s, mode=6, submode=2,
            sphere=5000, bar=0, feed=10, select_menu=1),
        "expect": {"sphere": "5000"},
    },
    {
        "id": "M6-02", "desc": "Sphere R=1mm (ø2mm, min)",
        "cmds": lambda s: send_all_state(s, mode=6, submode=2,
            sphere=1000, bar=0, feed=10, select_menu=1),
        "expect": {"sphere": "1000"},
    },
    {
        "id": "M6-03", "desc": "Sphere SM=2 (Bar ввод)",
        "cmds": lambda s: send_all_state(s, mode=6, submode=2,
            sphere=5000, bar=2000, feed=10, select_menu=2),
        "expect": {"bar": "2000"},
    },

    # ── M7 DIVIDER (mode=7) ──────────────────────────────────────────────────
    {
        "id": "M7-01", "desc": "Divider 4 divisions current=1 angle=0°",
        "cmds": lambda s: send_all_state(s, mode=7, submode=2,
            divn=4, divm=1, angle=0, select_menu=1),
        "expect": {"divn": "4", "angle": "0"},
    },
    {
        "id": "M7-02", "desc": "Divider 4 divisions current=2 angle=90°",
        "cmds": lambda s: send_all_state(s, mode=7, submode=2,
            divn=4, divm=2, angle=900, select_menu=1),
        "expect": {"angle": "90.0"},
    },
    {
        "id": "M7-03", "desc": "Divider 100 divisions current=50",
        "cmds": lambda s: send_all_state(s, mode=7, submode=2,
            divn=100, divm=50, angle=1764, select_menu=1),
        "expect": {"divn": "100", "divm": "50"},
    },

    # ── LIMITS ───────────────────────────────────────────────────────────────
    {
        "id": "L-01", "desc": "No limits",
        "cmds": lambda s: (send_cmd(s, "<LIMITS:0,0,0,0>"), time.sleep(0.1)),
        "expect": {"limits": "0,0,0,0"},
    },
    {
        "id": "L-02", "desc": "Left limit only",
        "cmds": lambda s: (send_cmd(s, "<LIMITS:1,0,0,0>"), time.sleep(0.1)),
        "expect": {"limit_L": "1"},
    },
    {
        "id": "L-03", "desc": "All limits ON",
        "cmds": lambda s: (send_cmd(s, "<LIMITS:1,1,1,1>"), time.sleep(0.1)),
        "expect": {"limits": "1,1,1,1"},
    },
    {
        "id": "L-04", "desc": "Limits L+R for carriage",
        "cmds": lambda s: (send_cmd(s, "<LIMITS:1,1,0,0>"), time.sleep(0.1)),
        "expect": {"limits": "1,1,0,0"},
    },
    {
        "id": "L-05", "desc": "Limits F+B for cross slide",
        "cmds": lambda s: (send_cmd(s, "<LIMITS:0,0,1,1>"), time.sleep(0.1)),
        "expect": {"limits": "0,0,1,1"},
    },

    # ── ALERTS ───────────────────────────────────────────────────────────────
    {
        "id": "A-01", "desc": "Alert type 1 (УСТАНОВИТЕ УПОРЫ)",
        "cmds": lambda s: (send_cmd(s, "<ALERT:1>"), time.sleep(0.3)),
        "expect": {"alert": "visible"},
    },
    {
        "id": "A-02", "desc": "Alert dismiss",
        "cmds": lambda s: (send_cmd(s, "<ALERT:0>"), time.sleep(0.2)),
        "expect": {"alert": "hidden"},
    },
    {
        "id": "A-03", "desc": "Alert type 3 (ОПЕРАЦИЯ ЗАВЕРШЕНА)",
        "cmds": lambda s: (send_cmd(s, "<ALERT:3>"), time.sleep(0.3)),
        "expect": {"alert": "visible"},
    },

    # ── RPM ──────────────────────────────────────────────────────────────────
    {
        "id": "R-01", "desc": "RPM=0 (spindle stopped)",
        "cmds": lambda s: (send_cmd(s, "<RPM:0>"), time.sleep(0.1)),
        "expect": {"rpm": "0"},
    },
    {
        "id": "R-02", "desc": "RPM=500",
        "cmds": lambda s: (send_cmd(s, "<RPM:500>"), time.sleep(0.1)),
        "expect": {"rpm": "500"},
    },
    {
        "id": "R-03", "desc": "RPM=1500 (typical turning)",
        "cmds": lambda s: (send_cmd(s, "<RPM:1500>"), time.sleep(0.1)),
        "expect": {"rpm": "1500"},
    },

    # ── POSITIONS ────────────────────────────────────────────────────────────
    {
        "id": "P-01", "desc": "POS_Z=0, POS_X=0",
        "cmds": lambda s: (send_cmd(s, "<POS_Z:0>"), send_cmd(s, "<POS_X:0>"), time.sleep(0.1)),
        "expect": {"pos_z": "0.00", "pos_x": "0.00"},
    },
    {
        "id": "P-02", "desc": "POS_Z=-25000 (-25.000mm)",
        "cmds": lambda s: (send_cmd(s, "<POS_Z:-25000>"), time.sleep(0.1)),
        "expect": {"pos_z": "-25.00"},
    },
    {
        "id": "P-03", "desc": "POS_X=5000 (+5.000mm)",
        "cmds": lambda s: (send_cmd(s, "<POS_X:5000>"), time.sleep(0.1)),
        "expect": {"pos_x": "+5.00"},
    },
    {
        "id": "P-04", "desc": "POS_Z negative large (-999.99mm)",
        "cmds": lambda s: (send_cmd(s, "<POS_Z:-999990>"), time.sleep(0.1)),
        "expect": {"pos_z": "-999.99"},
    },

    # ── MODE SEQUENCE (M1→M8) ────────────────────────────────────────────────
    {
        "id": "S-01", "desc": "Full mode cycle M1→M2→M3→M4→M5→M6→M7→M8",
        "cmds": lambda s: [
            (send_cmd(s, "<MODE:1>"), time.sleep(0.15)) for _ in [1]
        ] + [
            (send_cmd(s, f"<MODE:{i}>"), time.sleep(0.2)) for i in range(1, 9)
        ],
        "expect": {"mode": "РЕЗЕРВ"},  # конечное состояние M8
    },
    {
        "id": "S-02", "desc": "Thread after Feed (bug check: thread data arrives)",
        "cmds": lambda s: [
            send_all_state(s, mode=1, submode=2, feed=25, select_menu=1),
            send_all_state(s, mode=3, submode=2,
                thread=400, thread_name="M4.0", rpm_lim=80,
                thread_cycl=36, thread_travel=400, ph=1, select_menu=1),
        ],
        "expect": {"thread": "4.00", "rpm_lim": "80", "cycl": "36"},
    },
]

# ─── Основной runner ──────────────────────────────────────────────────────

def run_all_tests():
    print(f"\n{'='*60}")
    print(f"ELS Display Level-2 Test Runner")
    print(f"Port: {PORT}")
    print(f"Tests: {len(TESTS)}")
    print(f"Results dir: {RESULTS_DIR}")
    print(f"{'='*60}\n")

    results = []
    try:
        ser = serial.Serial(PORT, BAUD, timeout=5)
    except Exception as e:
        print(f"[FATAL] Cannot open {PORT}: {e}")
        return

    time.sleep(0.5)
    ser.read(ser.in_waiting or 1)  # сброс буфера

    for i, test in enumerate(TESTS):
        tid  = test["id"]
        desc = test["desc"]
        print(f"[{i+1:03d}/{len(TESTS)}] {tid}: {desc}")

        try:
            # Выполняем команды
            fn = test["cmds"]
            fn(ser)
            time.sleep(0.3)

            # Скриншот
            scr_name = f"{tid}_{datetime.now().strftime('%H%M%S')}"
            scr_path = take_screenshot(ser, scr_name)

            results.append({
                "id": tid,
                "desc": desc,
                "screenshot": scr_path,
                "expect": test.get("expect", {}),
                "status": "CAPTURED",
            })
        except Exception as e:
            print(f"  [ERR] {e}")
            results.append({"id": tid, "desc": desc, "status": "ERROR", "error": str(e)})

        time.sleep(0.1)

    ser.close()

    # Сохраняем сводный отчёт
    report_path = str(RESULTS_DIR / f"report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=2)

    print(f"\n{'='*60}")
    print(f"Done. {len([r for r in results if r['status']=='CAPTURED'])} screenshots captured.")
    print(f"Report: {report_path}")
    print(f"{'='*60}")

if __name__ == "__main__":
    run_all_tests()
