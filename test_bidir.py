#!/usr/bin/env python3
"""
Bidirectional synchronization test runner.

Direction A (STM32 → ESP32):
  PC sends commands to STM32 via its USART1 VCP (Discovery USB).
  STM32 processes → sends to ESP32 via UART → ESP32 updates display.
  Screenshot taken from ESP32 via USB CDC.

Direction B (ESP32 → STM32):
  PC sends <BTN:...> to ESP32 via USB CDC.
  ESP32 forwards <TOUCH:...> to STM32.
  STM32 processes → sends back to ESP32 → ESP32 updates display.
  Screenshot taken from ESP32 USB CDC.

Usage:
  python3 test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX
  python3 test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --dir A
  python3 test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --dir B
  python3 test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --tc A-001

Notes:
  STM32 USART1: PA9=TX, PA10=RX, 115200 baud.
  Connect via STM32F407 Discovery built-in USB (ST-Link VCP, NOT standalone ST-Link V2 dongle).
  The standalone ST-Link V2 only does SWD — cannot be used for UART injection.
  If Discovery USB gives two COM ports, the VCP is the second one (not the debugger port).
"""

import serial, struct, time, os, sys, argparse, json, re
from datetime import datetime

SCREENSHOT_MAGIC = b'\xde\xad\xbe\xef'
OUT_DIR = os.path.join(os.path.dirname(__file__), "screenshots_bidir")
LOG_FILE = os.path.join(os.path.dirname(__file__), "test_bidir_results.jsonl")
os.makedirs(OUT_DIR, exist_ok=True)

# ─── Low-level helpers ───────────────────────────────────────────────────────

def flush_esp32(esp):
    """Drain any pending bytes from ESP32 USB CDC."""
    time.sleep(0.1)
    esp.reset_input_buffer()

def send_esp32_inject(esp, cmd: str):
    """Inject a simulated STM32→ESP32 command into ESP32 display."""
    esp.write((cmd + "\n").encode())
    esp.flush()
    time.sleep(0.05)

def send_esp32_btn(esp, btn: str):
    """Tell ESP32 to forward a TOUCH command to STM32. btn e.g. 'M2', 'KEY:UP'."""
    esp.write(("<BTN:" + btn + ">\n").encode())
    esp.flush()

def send_stm32(stm32, cmd: str):
    """Send a command to STM32 via its VCP. cmd e.g. 'MODE:1', 'AFEED:330'."""
    stm32.write((cmd + "\r\n").encode())
    stm32.flush()
    time.sleep(0.05)

def stm32_sendall(stm32):
    send_stm32(stm32, "SENDALL")
    time.sleep(0.2)   # give time for all packets to reach ESP32

def stm32_set_state(stm32, mode=0, sub=0, feed=25, afeed=330,
                    pos_z=0, pos_x=0, rpm=0, pass_nr=0, pass_total=0, ap=0):
    """Set STM32 state via serial commands, then SENDALL."""
    send_stm32(stm32, f"MODE:{mode}")
    send_stm32(stm32, f"FEED:{feed}")
    send_stm32(stm32, f"AFEED:{afeed}")
    send_stm32(stm32, f"AP:{ap}")
    stm32_sendall(stm32)

def take_screenshot(esp, name: str) -> str | None:
    flush_esp32(esp)
    esp.write(b'S')
    esp.flush()
    # scan for magic
    buf = b''
    deadline = time.time() + 5.0
    while time.time() < deadline:
        chunk = esp.read(esp.in_waiting or 1)
        buf += chunk
        idx = buf.find(SCREENSHOT_MAGIC)
        if idx >= 0:
            buf = buf[idx + 4:]
            break
    else:
        return None
    # read 4-byte size
    while len(buf) < 4 and time.time() < deadline + 2:
        buf += esp.read(4 - len(buf))
    if len(buf) < 4:
        return None
    size = struct.unpack('<I', buf[:4])[0]
    buf = buf[4:]
    # read JPEG
    deadline2 = time.time() + 10.0
    while len(buf) < size and time.time() < deadline2:
        buf += esp.read(min(512, size - len(buf)))
    if len(buf) < size:
        return None
    path = os.path.join(OUT_DIR, f"{name}.jpg")
    with open(path, 'wb') as f:
        f.write(buf[:size])
    return path

def log_result(tc_id, direction, desc, sent, expected, screenshot, passed, note=""):
    entry = {
        "tc": tc_id, "dir": direction, "desc": desc,
        "sent": sent, "expected": expected,
        "screenshot": screenshot, "passed": passed,
        "note": note, "ts": datetime.now().isoformat()
    }
    with open(LOG_FILE, 'a') as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    status = "PASS" if passed else "FAIL"
    print(f"  [{status}] {tc_id}: {desc}")

# ─── Direction A: STM32 → ESP32 test cases ──────────────────────────────────

DIR_A_CASES = [
# (id, description, stm32_cmds, sendall, expected_on_esp32)
# Modes
("A-001","M1/S1 Feed 0.25",  ["MODE:0","SUB:0","FEED:25","AFEED:100","AP:0"],True,"Mode M1, Feed 0.25"),
("A-002","M1/S2 Feed 0.50",  ["MODE:0","SUB:1","FEED:50","AFEED:100","AP:0"],True,"Mode M1 Manual, Feed 0.50"),
("A-003","M1/S3 Feed 1.00",  ["MODE:0","SUB:2","FEED:100","AFEED:100","AP:0"],True,"Mode M1 Ext, Feed 1.00"),
("A-004","M2/S1 aFeed 100",  ["MODE:1","SUB:0","FEED:25","AFEED:100","AP:0"],True,"Mode M2, aFeed 100"),
("A-005","M2/S2 aFeed 200",  ["MODE:1","SUB:1","FEED:25","AFEED:200","AP:0"],True,"Mode M2 Manual, aFeed 200"),
("A-006","M2/S3 aFeed 300",  ["MODE:1","SUB:2","FEED:25","AFEED:300","AP:0"],True,"Mode M2 Ext, aFeed 300"),
("A-007","M3/S1 Thread",     ["MODE:2","SUB:0"],True,"Mode M3 Thread"),
("A-008","M3/S2 Thread Man", ["MODE:2","SUB:1"],True,"Mode M3 Manual"),
("A-009","M3/S3 Thread Ext", ["MODE:2","SUB:2"],True,"Mode M3 Ext"),
("A-010","M4/S1 ConeL Int",  ["MODE:3","SUB:0"],True,"Mode M4 ConeL Int"),
("A-011","M4/S2 ConeL Man",  ["MODE:3","SUB:1"],True,"Mode M4 ConeL Manual"),
("A-012","M4/S3 ConeL Ext",  ["MODE:3","SUB:2"],True,"Mode M4 ConeL Ext"),
("A-013","M5/S1 ConeR Int",  ["MODE:4","SUB:0"],True,"Mode M5 ConeR Int"),
("A-014","M5/S2 ConeR Man",  ["MODE:4","SUB:1"],True,"Mode M5 ConeR Manual"),
("A-015","M5/S3 ConeR Ext",  ["MODE:4","SUB:2"],True,"Mode M5 ConeR Ext"),
("A-016","M6/S1 Sphere Int", ["MODE:5","SUB:0"],True,"Mode M6 Sphere Int"),
("A-017","M6/S2 Sphere Man", ["MODE:5","SUB:1"],True,"Mode M6 Sphere Manual"),
("A-018","M6/S3 Sphere Ext", ["MODE:5","SUB:2"],True,"Mode M6 Sphere Ext"),
("A-019","M7/S1 Divider Int",["MODE:6","SUB:0"],True,"Mode M7 Divider"),
("A-020","M7/S2 Divider Man",["MODE:6","SUB:1"],True,"Mode M7 Divider Manual"),
# Feed values
("A-021","Feed MIN=5",        ["MODE:0","FEED:5"],True,"Feed 0.05"),
("A-022","Feed 10",           ["MODE:0","FEED:10"],True,"Feed 0.10"),
("A-023","Feed 25",           ["MODE:0","FEED:25"],True,"Feed 0.25"),
("A-024","Feed 50",           ["MODE:0","FEED:50"],True,"Feed 0.50"),
("A-025","Feed 75",           ["MODE:0","FEED:75"],True,"Feed 0.75"),
("A-026","Feed 100",          ["MODE:0","FEED:100"],True,"Feed 1.00"),
("A-027","Feed 150",          ["MODE:0","FEED:150"],True,"Feed 1.50"),
("A-028","Feed MAX=200",      ["MODE:0","FEED:200"],True,"Feed 2.00"),
# aFeed values
("A-029","aFeed MIN=15",      ["MODE:1","AFEED:15"],True,"aFeed 15"),
("A-030","aFeed 50",          ["MODE:1","AFEED:50"],True,"aFeed 50"),
("A-031","aFeed 100",         ["MODE:1","AFEED:100"],True,"aFeed 100"),
("A-032","aFeed 150",         ["MODE:1","AFEED:150"],True,"aFeed 150"),
("A-033","aFeed 200",         ["MODE:1","AFEED:200"],True,"aFeed 200"),
("A-034","aFeed 250",         ["MODE:1","AFEED:250"],True,"aFeed 250"),
("A-035","aFeed 300",         ["MODE:1","AFEED:300"],True,"aFeed 300"),
("A-036","aFeed 330",         ["MODE:1","AFEED:330"],True,"aFeed 330"),
# AP съём
("A-037","M1 AP=0",           ["MODE:0","AP:0"],True,"AP 0.00"),
("A-038","M1 AP=10",          ["MODE:0","AP:10"],True,"AP 0.10"),
("A-039","M1 AP=25",          ["MODE:0","AP:25"],True,"AP 0.25"),
("A-040","M1 AP=50",          ["MODE:0","AP:50"],True,"AP 0.50"),
("A-041","M1 AP=100",         ["MODE:0","AP:100"],True,"AP 1.00"),
("A-042","M1 AP=200",         ["MODE:0","AP:200"],True,"AP 2.00"),
# Pass counts
("A-043","M1 Pass 0/0",       ["MODE:0","PASS:0,0"],True,"Pass --/--"),
("A-044","M1 Pass 1/8",       ["MODE:0","PASS:1,8"],True,"Pass 1/8"),
("A-045","M1 Pass 4/8",       ["MODE:0","PASS:4,8"],True,"Pass 4/8"),
("A-046","M1 Pass 8/8",       ["MODE:0","PASS:8,8"],True,"Pass 8/8"),
("A-047","M2 Pass 0/5",       ["MODE:1","PASS:0,5"],True,"Pass --/5"),
("A-048","M2 Pass 3/5",       ["MODE:1","PASS:3,5"],True,"Pass 3/5"),
# RPM
("A-049","RPM 0",             ["MODE:0","RPM:0"],True,"RPM 0"),
("A-050","RPM 500",           ["MODE:0","RPM:500"],True,"RPM 500"),
("A-051","RPM 1000",          ["MODE:0","RPM:1000"],True,"RPM 1000"),
("A-052","RPM 1500",          ["MODE:0","RPM:1500"],True,"RPM 1500"),
("A-053","RPM 2000",          ["MODE:0","RPM:2000"],True,"RPM 2000"),
# SelectMenu
("A-054","M1 SelectMenu=1",   ["MODE:0","SELECTMENU:1"],True,"SelectMenu 1"),
("A-055","M1 SelectMenu=2",   ["MODE:0","SELECTMENU:2"],True,"SelectMenu 2 params"),
("A-056","M1 SelectMenu=3",   ["MODE:0","SELECTMENU:3"],True,"SelectMenu 3 axes"),
("A-057","M2 SelectMenu=1",   ["MODE:1","SELECTMENU:1"],True,"M2 SelectMenu 1"),
("A-058","M2 SelectMenu=2",   ["MODE:1","SELECTMENU:2"],True,"M2 SelectMenu 2"),
("A-059","M3 SelectMenu=1",   ["MODE:2","SELECTMENU:1"],True,"M3 SelectMenu 1"),
("A-060","M3 SelectMenu=2",   ["MODE:2","SELECTMENU:2"],True,"M3 SelectMenu 2"),
# Limits
("A-061","Limits all OFF",    ["MODE:0","LIMITS:0,0,0,0"],True,"No limits"),
("A-062","Limit Left ON",     ["MODE:0","LIMITS:1,0,0,0"],True,"Limit L"),
("A-063","Limit Right ON",    ["MODE:0","LIMITS:0,1,0,0"],True,"Limit R"),
("A-064","Limit Front ON",    ["MODE:0","LIMITS:0,0,1,0"],True,"Limit F"),
("A-065","Limit Rear ON",     ["MODE:0","LIMITS:0,0,0,1"],True,"Limit Re"),
("A-066","Limits L+R",        ["MODE:0","LIMITS:1,1,0,0"],True,"Limits L+R"),
("A-067","Limits F+Re",       ["MODE:0","LIMITS:0,0,1,1"],True,"Limits F+Re"),
("A-068","All limits ON",     ["MODE:0","LIMITS:1,1,1,1"],True,"All limits"),
# Positions
("A-069","Pos Z=0 X=0",       ["MODE:0","POS_Z:0","POS_X:0"],True,"Z 0.000 X 0.000"),
("A-070","Pos Z+123.450",     ["MODE:0","POS_Z:123450"],True,"Z 123.450"),
("A-071","Pos Z-67.890",      ["MODE:0","POS_Z:-67890"],True,"Z -67.890"),
("A-072","Pos X+45.678",      ["MODE:0","POS_X:45678"],True,"X 45.678"),
("A-073","Pos X-12.345",      ["MODE:0","POS_X:-12345"],True,"X -12.345"),
("A-074","Z large +999.999",  ["MODE:0","POS_Z:999999"],True,"Z 999.999"),
("A-075","Z large -999.999",  ["MODE:0","POS_Z:-999999"],True,"Z -999.999"),
# Thread mode
("A-076","M3 PH=1",           ["MODE:2","PH:1"],True,"M3 zahodov 1"),
("A-077","M3 PH=2",           ["MODE:2","PH:2"],True,"M3 zahodov 2"),
("A-078","M3 PH=4",           ["MODE:2","PH:4"],True,"M3 zahodov 4"),
("A-079","M3 PH=8",           ["MODE:2","PH:8"],True,"M3 zahodov 8"),
# Thread CYCL
("A-080","M3 CYCL=10",        ["MODE:2","THREAD_CYCL:10"],True,"Cycl 10"),
("A-081","M3 CYCL=36",        ["MODE:2","THREAD_CYCL:36"],True,"Cycl 36"),
# Thread RPM_LIM
("A-082","M3 RPM_LIM=80",     ["MODE:2","RPM_LIM:80"],True,"RPM_LIM 80"),
("A-083","M3 RPM_LIM=300",    ["MODE:2","RPM_LIM:300"],True,"RPM_LIM 300"),
# Cone modes
("A-084","M4 ConeL Feed 25",  ["MODE:3","FEED:25"],True,"M4 Feed 0.25"),
("A-085","M5 ConeR Feed 50",  ["MODE:4","FEED:50"],True,"M5 Feed 0.50"),
# Sphere mode
("A-086","M6 Sphere 1000",    ["MODE:5","SPHERE:1000"],True,"M6 R 10.00"),
("A-087","M6 Sphere 2000",    ["MODE:5","SPHERE:2000"],True,"M6 R 20.00"),
("A-088","M6 BAR=250",        ["MODE:5","BAR:250"],True,"M6 bar 2.50"),
("A-089","M6 PASS_SPHR=8",    ["MODE:5","PASS_SPHR:8"],True,"M6 pass 10"),
# Divider mode
("A-090","M7 DIVN=12",        ["MODE:6","DIVN:12"],True,"M7 deleni 12"),
("A-091","M7 DIVN=24",        ["MODE:6","DIVN:24"],True,"M7 deleni 24"),
("A-092","M7 DIVM=1",         ["MODE:6","DIVN:12","DIVM:1"],True,"M7 metka 1"),
("A-093","M7 DIVM=6",         ["MODE:6","DIVN:12","DIVM:6"],True,"M7 metka 6"),
# State running/stopped
("A-094","State run",         ["MODE:0","STATE:run"],True,"Running indicator"),
("A-095","State stop",        ["MODE:0","STATE:stop"],True,"Stopped"),
# Motor enable
("A-096","Motor Z enabled",   ["MODE:0","MOTOR:Z,1"],True,"Motor Z on"),
("A-097","Motor Z disabled",  ["MODE:0","MOTOR:Z,0"],True,"Motor Z off"),
# Alert
("A-098","Alert 1 set stops", ["MODE:0","ALERT:1"],True,"Alert: set stops"),
("A-099","Alert 2 go home",   ["MODE:0","ALERT:2"],True,"Alert: go home"),
("A-100","Alert 3 done",      ["MODE:0","ALERT:3"],True,"Alert: done"),
# Combinations M1/S1 full
("A-101","M1/S1 full state feed=50 ap=25 pass=3/10 rpm=1200 z=45000 x=-12000",
         ["MODE:0","SUB:0","FEED:50","AFEED:200","AP:25","PASS:3,10","RPM:1200","POS_Z:45000","POS_X:-12000"],True,"M1 full state"),
("A-102","M1/S2 full state feed=100 ap=50 pass=0/0 rpm=800 z=-30000 x=5000",
         ["MODE:0","SUB:1","FEED:100","AP:50","RPM:800","POS_Z:-30000","POS_X:5000"],True,"M1 manual full"),
("A-103","M1/S3 full ext state",
         ["MODE:0","SUB:2","FEED:75","AP:10","PASS:7,12","RPM:600","POS_Z:100000","POS_X:-8000"],True,"M1 ext full"),
("A-104","M2/S1 afeed=150 ap=30 pass=2/6 rpm=900 z=23400 x=-7200",
         ["MODE:1","SUB:0","AFEED:150","AP:30","PASS:2,6","RPM:900","POS_Z:23400","POS_X:-7200"],True,"M2 full state"),
("A-105","M2/S2 afeed=250 manual",
         ["MODE:1","SUB:1","AFEED:250","RPM:1100","POS_Z:63200","POS_X:-9400"],True,"M2 manual"),
("A-106","M2/S3 afeed=80 ext",
         ["MODE:1","SUB:2","AFEED:80","AP:20","PASS:5,8","RPM:400","POS_Z:35000","POS_X:-8000"],True,"M2 ext"),
("A-107","M3/S1 thread full",
         ["MODE:2","SUB:0","PH:1","PASS:2,16","RPM:200","POS_Z:5000"],True,"M3 thread full"),
("A-108","M3/S2 thread manual",
         ["MODE:2","SUB:1","PH:2","RPM:400","POS_Z:-3000"],True,"M3 manual"),
("A-109","M3/S3 thread ext",
         ["MODE:2","SUB:2","PH:4","PASS:0,24","RPM:150","POS_Z:8000"],True,"M3 ext"),
("A-110","M4/S1 cone left int",
         ["MODE:3","SUB:0","FEED:30","AP:15","PASS:4,8","RPM:600","POS_Z:50000","POS_X:-3000"],True,"M4 cone L int"),
("A-111","M4/S2 cone left man",
         ["MODE:3","SUB:1","FEED:40","RPM:800"],True,"M4 cone L man"),
("A-112","M5/S1 cone right int",
         ["MODE:4","SUB:0","FEED:25","AP:10","PASS:1,6","RPM:700"],True,"M5 cone R int"),
("A-113","M6/S1 sphere int full",
         ["MODE:5","SUB:0","FEED:20","SPHERE:1500","BAR:300","PASS_SPHR:10","POS_Z:0","POS_X:0"],True,"M6 sphere full"),
("A-114","M7/S1 divider full",
         ["MODE:6","SUB:0","DIVN:36","DIVM:12","RPM:500"],True,"M7 divider full"),
# Limits + mode combos
("A-115","M1 + limit L",      ["MODE:0","FEED:25","LIMITS:1,0,0,0"],True,"M1 limit L"),
("A-116","M1 + limit R",      ["MODE:0","FEED:25","LIMITS:0,1,0,0"],True,"M1 limit R"),
("A-117","M2 + limit F",      ["MODE:1","AFEED:200","LIMITS:0,0,1,0"],True,"M2 limit F"),
("A-118","M3 + limit Re",     ["MODE:2","LIMITS:0,0,0,1"],True,"M3 limit Re"),
("A-119","M1 + all limits",   ["MODE:0","LIMITS:1,1,1,1"],True,"M1 all limits"),
("A-120","M4 + L+R limits",   ["MODE:3","LIMITS:1,1,0,0"],True,"M4 L+R limits"),
# SelectMenu combinations
("A-121","M1/S1 SM=1 feed view",  ["MODE:0","SUB:0","SELECTMENU:1","FEED:25","AP:0"],True,"M1 SM1"),
("A-122","M1/S1 SM=2 params",     ["MODE:0","SUB:0","SELECTMENU:2","FEED:25"],True,"M1 SM2 params"),
("A-123","M1/S1 SM=3 axes",       ["MODE:0","SUB:0","SELECTMENU:3"],True,"M1 SM3 axes"),
("A-124","M1/S3 SM=1 ext feed",   ["MODE:0","SUB:2","SELECTMENU:1","FEED:50"],True,"M1 S3 SM1"),
("A-125","M1/S3 SM=2 diam",       ["MODE:0","SUB:2","SELECTMENU:2","FEED:50"],True,"M1 S3 SM2"),
("A-126","M2/S2 SM=2 divider",    ["MODE:1","SUB:1","SELECTMENU:2"],True,"M2 SM2 divider"),
("A-127","M3/S1 SM=1 thread",     ["MODE:2","SUB:0","SELECTMENU:1"],True,"M3 SM1"),
("A-128","M3/S2 SM=2 params",     ["MODE:2","SUB:1","SELECTMENU:2"],True,"M3 SM2"),
# Edge cases — extreme values
("A-129","Feed min boundary 5",   ["MODE:0","FEED:5"],True,"Feed 0.05"),
("A-130","Feed max boundary 200", ["MODE:0","FEED:200"],True,"Feed 2.00"),
("A-131","aFeed min 15",          ["MODE:1","AFEED:15"],True,"aFeed 15"),
("A-132","aFeed max 300",         ["MODE:1","AFEED:300"],True,"aFeed 300"),
("A-133","AP=0 zero",             ["MODE:0","AP:0"],True,"AP 0"),
("A-134","AP=1 min",              ["MODE:0","AP:1"],True,"AP 0.01"),
("A-135","AP=500 large",          ["MODE:0","AP:500"],True,"AP 5.00"),
("A-136","Pass 0/0 empty",        ["MODE:0","PASS:0,0"],True,"Pass empty"),
("A-137","Pass 1/1 single",       ["MODE:0","PASS:1,1"],True,"Pass 1/1"),
("A-138","Pass 99/99",            ["MODE:0","PASS:99,99"],True,"Pass 99"),
("A-139","RPM=0",                 ["MODE:0","RPM:0"],True,"RPM 0"),
("A-140","RPM=9999",              ["MODE:0","RPM:9999"],True,"RPM 9999"),
("A-141","DIVN=2 min",            ["MODE:6","DIVN:2","DIVM:1"],True,"DIVN 2"),
("A-142","DIVN=360",              ["MODE:6","DIVN:360","DIVM:180"],True,"DIVN 360"),
("A-143","SPHERE R=100 (1mm)",    ["MODE:5","SPHERE:100"],True,"R 1.00"),
("A-144","SPHERE R=10000 (100mm)",["MODE:5","SPHERE:10000"],True,"R 100.00"),
# State transitions (quick succession)
("A-145","M1→M2 transition",      ["MODE:1","AFEED:200"],True,"Mode switch M1→M2"),
("A-146","M2→M3 transition",      ["MODE:2"],True,"Mode switch M2→M3"),
("A-147","M3→M4 transition",      ["MODE:3"],True,"Mode switch M3→M4"),
("A-148","M4→M5 transition",      ["MODE:4"],True,"Mode switch M4→M5"),
("A-149","M5→M6 transition",      ["MODE:5"],True,"Mode switch M5→M6"),
("A-150","M6→M7 transition",      ["MODE:6"],True,"Mode switch M6→M7"),
("A-151","M7→M1 transition",      ["MODE:0"],True,"Mode switch M7→M1"),
# M1 extended params
("A-152","M1/S3 OTSKOK_Z=500",    ["MODE:0","SUB:2","OTSKOK_Z:500"],True,"M1 S3 otskok"),
("A-153","M1/S3 TENSION_Z=200",   ["MODE:0","SUB:2","TENSION_Z:200"],True,"M1 S3 tension"),
("A-154","M1/S2 DIAM_X=1500",     ["MODE:0","SUB:1","DIAM_X:1500"],True,"M1 S2 diam X"),
# M3 pass_fin
("A-155","M3 PASS_FIN=0",         ["MODE:2","PASS_FIN:0"],True,"M3 pass_fin 0"),
("A-156","M3 PASS_FIN=2",         ["MODE:2","PASS_FIN:2"],True,"M3 pass_fin 2"),
# M4/M5 cone_thr
("A-157","M4 CONE_THR=0",         ["MODE:3","CONE_THR:0"],True,"M4 cone thr off"),
("A-158","M4 CONE_THR=1",         ["MODE:3","CONE_THR:1"],True,"M4 cone thr on"),
# M6 cutter width
("A-159","M6 CUTTER_W=100",       ["MODE:5","CUTTER_W:100"],True,"M6 cutter 1.00"),
("A-160","M6 CUTTING_W=50",       ["MODE:5","CUTTING_W:50"],True,"M6 cutting 0.50"),
# Various submode combos with limits
("A-161","M1/S1 feed=25 L+F limits", ["MODE:0","SUB:0","FEED:25","LIMITS:1,0,1,0"],True,"M1 S1 L+F"),
("A-162","M1/S2 feed=50 R+Re limits",["MODE:0","SUB:1","FEED:50","LIMITS:0,1,0,1"],True,"M1 S2 R+Re"),
("A-163","M2/S1 afeed=120 all lim",  ["MODE:1","SUB:0","AFEED:120","LIMITS:1,1,1,1"],True,"M2 S1 all lim"),
("A-164","M3/S1 thread L limit",     ["MODE:2","SUB:0","LIMITS:1,0,0,0"],True,"M3 S1 L lim"),
("A-165","M4/S1 cone L+R limits",    ["MODE:3","SUB:0","FEED:30","LIMITS:1,1,0,0"],True,"M4 S1 L+R"),
# SelectMenu=3 axis reset screens
("A-166","M2/S3 SM=3",  ["MODE:1","SUB:2","SELECTMENU:3"],True,"M2 S3 SM3 axes"),
("A-167","M3/S3 SM=3",  ["MODE:2","SUB:2","SELECTMENU:3"],True,"M3 S3 SM3 axes"),
("A-168","M4/S3 SM=3",  ["MODE:3","SUB:2","SELECTMENU:3"],True,"M4 S3 SM3 axes"),
("A-169","M5/S3 SM=3",  ["MODE:4","SUB:2","SELECTMENU:3"],True,"M5 S3 SM3 axes"),
("A-170","M6/S3 SM=3",  ["MODE:5","SUB:2","SELECTMENU:3"],True,"M6 S3 SM3 axes"),
# Thread name combos
("A-171","M3 thread 1.50mm",  ["MODE:2","THREAD_NAME:1.50mm","THREAD:150","RPM_LIM:300"],True,"M3 1.50mm"),
("A-172","M3 thread 4.00mm",  ["MODE:2","THREAD_NAME:4.00mm","THREAD:400","RPM_LIM:80"],True,"M3 4.00mm"),
("A-173","M3 thread 8tpi",    ["MODE:2","THREAD_NAME: 8tpi ","THREAD:317","RPM_LIM:200"],True,"M3 8tpi"),
("A-174","M3 thread G 1/8",   ["MODE:2","THREAD_NAME:G  1/8","THREAD:907","RPM_LIM:60"],True,"M3 G 1/8"),
# Angle (spindle for divider)
("A-175","M7 angle 0.0",      ["MODE:6","ANGLE:0"],True,"Angle 0.0"),
("A-176","M7 angle 90.0",     ["MODE:6","ANGLE:900"],True,"Angle 90.0"),
("A-177","M7 angle 180.0",    ["MODE:6","ANGLE:1800"],True,"Angle 180.0"),
("A-178","M7 angle 359.9",    ["MODE:6","ANGLE:3599"],True,"Angle 359.9"),
# Complex combos
("A-179","M1/S1 SM=2 feed=75 ap=20 pass=6/12 rpm=1000 lim=L",
         ["MODE:0","SUB:0","SELECTMENU:2","FEED:75","AP:20","PASS:6,12","RPM:1000","LIMITS:1,0,0,0"],True,"complex M1"),
("A-180","M2/S1 SM=1 afeed=180 ap=15 pass=3/8 rpm=900 lim=R",
         ["MODE:1","SUB:0","SELECTMENU:1","AFEED:180","AP:15","PASS:3,8","RPM:900","LIMITS:0,1,0,0"],True,"complex M2"),
("A-181","M3/S1 SM=1 ph=2 pass=4/32 rpm=300 lim=0",
         ["MODE:2","SUB:0","SELECTMENU:1","PH:2","PASS:4,32","RPM:300","LIMITS:0,0,0,0"],True,"complex M3"),
("A-182","M4/S1 SM=1 feed=20 ap=5 pass=2/6 rpm=800 lim=F",
         ["MODE:3","SUB:0","SELECTMENU:1","FEED:20","AP:5","PASS:2,6","RPM:800","LIMITS:0,0,1,0"],True,"complex M4"),
("A-183","M5/S3 SM=2 feed=30 ap=8 pass=1/4 rpm=600 lim=Re",
         ["MODE:4","SUB:2","SELECTMENU:2","FEED:30","AP:8","PASS:1,4","RPM:600","LIMITS:0,0,0,1"],True,"complex M5"),
("A-184","M6/S1 SM=1 full sphere pass=5/12 rpm=400",
         ["MODE:5","SUB:0","SELECTMENU:1","SPHERE:1500","BAR:200","PASS_SPHR:10","PASS:3,12","RPM:400"],True,"complex M6"),
("A-185","M7/S1 SM=1 div=6 metka=3 rpm=300",
         ["MODE:6","SUB:0","SELECTMENU:1","DIVN:6","DIVM:3","RPM:300"],True,"complex M7"),
# PH combinations with thread
("A-186","M3 PH=1 thread travel",["MODE:2","PH:1","THREAD:400","THREAD_TRAVEL:400","THREAD_CYCL:20"],True,"M3 PH1 travel"),
("A-187","M3 PH=2 thread travel",["MODE:2","PH:2","THREAD:400","THREAD_TRAVEL:800","THREAD_CYCL:40"],True,"M3 PH2 travel"),
("A-188","M3 PH=4 thread travel",["MODE:2","PH:4","THREAD:400","THREAD_TRAVEL:1600","THREAD_CYCL:80"],True,"M3 PH4 travel"),
# Positions with modes
("A-189","M1 pos Z+50.000 X-10.000",["MODE:0","POS_Z:50000","POS_X:-10000"],True,"M1 pos"),
("A-190","M2 pos Z-100.000 X+20.000",["MODE:1","POS_Z:-100000","POS_X:20000"],True,"M2 pos"),
("A-191","M3 pos Z+8.500 X-2.300",  ["MODE:2","POS_Z:8500","POS_X:-2300"],True,"M3 pos"),
("A-192","M4 pos Z+30.000 X-5.000", ["MODE:3","POS_Z:30000","POS_X:-5000"],True,"M4 pos"),
("A-193","M6 pos at center",        ["MODE:5","POS_Z:0","POS_X:0"],True,"M6 center"),
("A-194","M7 divider pos",          ["MODE:6","POS_Z:12500","POS_X:-3200"],True,"M7 pos"),
# Running state combos
("A-195","M1 running feed=50",      ["MODE:0","FEED:50","STATE:run"],True,"M1 running"),
("A-196","M1 stopped",              ["MODE:0","STATE:stop"],True,"M1 stopped"),
("A-197","M2 running afeed=200",    ["MODE:1","AFEED:200","STATE:run"],True,"M2 running"),
("A-198","M3 running thread",       ["MODE:2","STATE:run"],True,"M3 running"),
# Final SENDALL after various settings
("A-199","Full state M1 nominal",
         ["MODE:0","SUB:0","FEED:25","AFEED:100","AP:0","PASS:0,0","RPM:0","POS_Z:0","POS_X:0",
          "LIMITS:0,0,0,0","SELECTMENU:1"],True,"nominal state"),
("A-200","Full state M2 nominal afeed",
         ["MODE:1","SUB:0","AFEED:330","AP:0","PASS:0,0","RPM:0","POS_Z:0","POS_X:0",
          "LIMITS:0,0,0,0","SELECTMENU:1"],True,"M2 nominal afeed 330"),
]

# ─── Direction B: ESP32 → STM32 test cases ──────────────────────────────────

DIR_B_CASES = [
# (id, description, setup_stm32_cmds, btn_sequence, expected_after)
# Mode switches
("B-001","Switch to M1 via touch",     ["MODE:1"],["M1"],"STM32 switches to M1, ESP32 shows M1"),
("B-002","Switch to M2 via touch",     ["MODE:0"],["M2"],"STM32 switches to M2, ESP32 shows M2"),
("B-003","Switch to M3 via touch",     ["MODE:0"],["M3"],"STM32 switches to M3, ESP32 shows M3"),
("B-004","Switch to M4 via touch",     ["MODE:0"],["M4"],"STM32 switches to M4, ESP32 shows M4"),
("B-005","Switch to M5 via touch",     ["MODE:0"],["M5"],"STM32 switches to M5, ESP32 shows M5"),
("B-006","Switch to M6 via touch",     ["MODE:0"],["M6"],"STM32 switches to M6, ESP32 shows M6"),
("B-007","Switch to M7 via touch",     ["MODE:0"],["M7"],"STM32 switches to M7, ESP32 shows M7"),
("B-008","M2→M1 back via touch",       ["MODE:1"],["M1"],"Back to M1"),
("B-009","M3→M1 back via touch",       ["MODE:2"],["M1"],"Back to M1"),
("B-010","M4→M2 via touch",            ["MODE:3"],["M2"],"M4→M2 transition"),
# Submode switches
("B-011","M1 switch to S1 via touch",  ["MODE:0","SUB:1"],["S1"],"M1 S1"),
("B-012","M1 switch to S2 via touch",  ["MODE:0","SUB:0"],["S2"],"M1 S2"),
("B-013","M1 switch to S3 via touch",  ["MODE:0","SUB:0"],["S3"],"M1 S3"),
("B-014","M2 switch to S1 via touch",  ["MODE:1","SUB:2"],["S1"],"M2 S1"),
("B-015","M2 switch to S2 via touch",  ["MODE:1","SUB:0"],["S2"],"M2 S2"),
("B-016","M2 switch to S3 via touch",  ["MODE:1","SUB:1"],["S3"],"M2 S3"),
("B-017","M3 switch to S1 via touch",  ["MODE:2","SUB:1"],["S1"],"M3 S1"),
("B-018","M3 switch to S2 via touch",  ["MODE:2","SUB:0"],["S2"],"M3 S2"),
("B-019","M3 switch to S3 via touch",  ["MODE:2","SUB:0"],["S3"],"M3 S3"),
("B-020","M4 switch to S2 via touch",  ["MODE:3","SUB:0"],["S2"],"M4 S2"),
# KEY:UP in various modes (increment feed/afeed)
("B-021","M1 KEY:UP once (feed++)",    ["MODE:0","FEED:25"],["KEY:UP"],"M1 feed incremented"),
("B-022","M1 KEY:UP x5",              ["MODE:0","FEED:25"],["KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"],"M1 feed +5 steps"),
("B-023","M2 KEY:UP once (afeed++)",  ["MODE:1","AFEED:100"],["KEY:UP"],"M2 afeed incremented"),
("B-024","M2 KEY:UP x5",             ["MODE:1","AFEED:100"],["KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"],"M2 afeed +5"),
("B-025","M3 KEY:UP once (thread step)", ["MODE:2"],["KEY:UP"],"M3 thread step up"),
("B-026","M3 KEY:UP x3",              ["MODE:2"],["KEY:UP","KEY:UP","KEY:UP"],"M3 thread +3 steps"),
("B-027","M4 KEY:UP once (cone step)",["MODE:3"],["KEY:UP"],"M4 cone step up"),
("B-028","M5 KEY:UP once",            ["MODE:4"],["KEY:UP"],"M5 step up"),
("B-029","M7 KEY:UP once (tooth++)",  ["MODE:6","DIVN:12","DIVM:1"],["KEY:UP"],"M7 tooth++"),
("B-030","M7 KEY:UP x3",              ["MODE:6","DIVN:12","DIVM:1"],["KEY:UP","KEY:UP","KEY:UP"],"M7 tooth+3"),
# KEY:DN in various modes
("B-031","M1 KEY:DN once (feed--)",   ["MODE:0","FEED:50"],["KEY:DN"],"M1 feed--"),
("B-032","M2 KEY:DN once (afeed--)",  ["MODE:1","AFEED:200"],["KEY:DN"],"M2 afeed--"),
("B-033","M3 KEY:DN once (thread--)", ["MODE:2"],["KEY:DN"],"M3 thread step--"),
("B-034","M4 KEY:DN once",            ["MODE:3"],["KEY:DN"],"M4 cone--"),
("B-035","M7 KEY:DN once (tooth--)",  ["MODE:6","DIVN:12","DIVM:6"],["KEY:DN"],"M7 tooth--"),
# PARAM_OK (select)
("B-036","M1 PARAM_OK SM1→SM2",       ["MODE:0","SELECTMENU:1"],["PARAM_OK"],"M1 SM→SM2"),
("B-037","M1 PARAM_OK SM2→SM3",       ["MODE:0","SELECTMENU:2"],["PARAM_OK"],"M1 SM→SM3"),
("B-038","M1 PARAM_OK SM3→SM1",       ["MODE:0","SELECTMENU:3"],["PARAM_OK"],"M1 SM→SM1"),
("B-039","M2 PARAM_OK SM1→SM2",       ["MODE:1","SELECTMENU:1"],["PARAM_OK"],"M2 SM→SM2"),
("B-040","M3 PARAM_OK SM1→SM2",       ["MODE:2","SELECTMENU:1"],["PARAM_OK"],"M3 SM→SM2"),
# KEY:LEFT / KEY:RIGHT for SelectMenu
("B-041","M1 KEY:RIGHT SM1→SM2",      ["MODE:0","SELECTMENU:1"],["KEY:RIGHT"],"M1 SM2"),
("B-042","M1 KEY:RIGHT SM2→SM3",      ["MODE:0","SELECTMENU:2"],["KEY:RIGHT"],"M1 SM3"),
("B-043","M1 KEY:LEFT SM3→SM2",       ["MODE:0","SELECTMENU:3"],["KEY:LEFT"],"M1 SM2"),
("B-044","M1 KEY:LEFT SM2→SM1",       ["MODE:0","SELECTMENU:2"],["KEY:LEFT"],"M1 SM1"),
("B-045","M2 KEY:RIGHT SM1→SM2",      ["MODE:1","SELECTMENU:1"],["KEY:RIGHT"],"M2 SM2"),
# Mode switch sequences
("B-046","M1→M2→M1 round trip",       ["MODE:0"],["M2","M1"],"Back to M1"),
("B-047","M1→M3→M1 round trip",       ["MODE:0"],["M3","M1"],"Back to M1"),
("B-048","M2→M3→M2 round trip",       ["MODE:1"],["M3","M2"],"Back to M2"),
("B-049","M1→M2→M3→M4→M1",           ["MODE:0"],["M2","M3","M4","M1"],"M4→M1"),
("B-050","M7→M6→M5→M4→M3",           ["MODE:6"],["M6","M5","M4","M3"],"M7→M3 sequence"),
# AFEED:N injection via BTN (parametric TOUCH)
("B-051","M2 BTN AFEED:100",          ["MODE:1"],["AFEED:100"],"M2 afeed=100"),
("B-052","M2 BTN AFEED:200",          ["MODE:1"],["AFEED:200"],"M2 afeed=200"),
("B-053","M2 BTN AFEED:300",          ["MODE:1"],["AFEED:300"],"M2 afeed=300"),
("B-054","M1 BTN FEED:25",            ["MODE:0"],["FEED:25"],"M1 feed=0.25"),
("B-055","M1 BTN FEED:50",            ["MODE:0"],["FEED:50"],"M1 feed=0.50"),
("B-056","M1 BTN FEED:100",           ["MODE:0"],["FEED:100"],"M1 feed=1.00"),
("B-057","M1 BTN AP:25",              ["MODE:0"],["AP:25"],"M1 ap=0.25"),
("B-058","M1 BTN AP:50",              ["MODE:0"],["AP:50"],"M1 ap=0.50"),
# Submode + mode combo via touch
("B-059","Switch M2, then S1",        ["MODE:0"],["M2","S1"],"M2/S1"),
("B-060","Switch M2, then S2",        ["MODE:0"],["M2","S2"],"M2/S2"),
("B-061","Switch M2, then S3",        ["MODE:0"],["M2","S3"],"M2/S3"),
("B-062","Switch M3, then S1",        ["MODE:0"],["M3","S1"],"M3/S1"),
("B-063","Switch M3, then S2",        ["MODE:0"],["M3","S2"],"M3/S2"),
("B-064","Switch M4, then S2",        ["MODE:0"],["M4","S2"],"M4/S2"),
("B-065","Switch M4, then S3",        ["MODE:0"],["M4","S3"],"M4/S3"),
("B-066","Switch M5, then S1",        ["MODE:0"],["M5","S1"],"M5/S1"),
("B-067","Switch M7, then S2",        ["MODE:0"],["M7","S2"],"M7/S2"),
("B-068","M6 S3 SM3",                 ["MODE:0"],["M6","S3","KEY:RIGHT","KEY:RIGHT"],"M6/S3/SM3"),
# KEY:UP min boundary (should clamp)
("B-069","M1 KEY:UP at max feed",     ["MODE:0","FEED:200"],["KEY:UP","KEY:UP","KEY:UP"],"M1 feed clamped"),
("B-070","M2 KEY:UP at max afeed",    ["MODE:1","AFEED:300"],["KEY:UP","KEY:UP"],"M2 afeed clamped"),
# KEY:DN min boundary
("B-071","M1 KEY:DN at min feed",     ["MODE:0","FEED:5"],["KEY:DN","KEY:DN"],"M1 feed at min"),
("B-072","M2 KEY:DN at min afeed",    ["MODE:1","AFEED:15"],["KEY:DN","KEY:DN"],"M2 afeed at min"),
# M3 thread step cycling
("B-073","M3 thread step full cycle UP", ["MODE:2"],
         ["KEY:UP"]*10,"M3 thread +10 steps"),
("B-074","M3 thread step full cycle DN", ["MODE:2"],
         ["KEY:DN"]*10,"M3 thread -10 steps"),
# Rapid succession mode switches
("B-075","Rapid M1→M2→M3",            ["MODE:0"],["M1","M2","M3"],"Rapid switches"),
("B-076","Rapid M4→M5→M6→M7",         ["MODE:0"],["M4","M5","M6","M7"],"Rapid M4-M7"),
# PARAM_OK sequences
("B-077","M1 3× PARAM_OK cycle",      ["MODE:0","SELECTMENU:1"],["PARAM_OK","PARAM_OK","PARAM_OK"],"SM cycle"),
("B-078","M3 3× PARAM_OK cycle",      ["MODE:2","SELECTMENU:1"],["PARAM_OK","PARAM_OK","PARAM_OK"],"M3 SM cycle"),
# JOY commands (axis movement simulation)
("B-079","JOY:LEFT then STOP",        ["MODE:0"],["JOY:LEFT","JOY:STOP"],"Joy L then stop"),
("B-080","JOY:RIGHT then STOP",       ["MODE:0"],["JOY:RIGHT","JOY:STOP"],"Joy R then stop"),
("B-081","JOY:UP then STOP",          ["MODE:0"],["JOY:UP","JOY:STOP"],"Joy U then stop"),
("B-082","JOY:DOWN then STOP",        ["MODE:0"],["JOY:DOWN","JOY:STOP"],"Joy D then stop"),
# AP via BTN in different modes
("B-083","M1 AP:0 reset",             ["MODE:0","AP:50"],["AP:0"],"M1 ap reset"),
("B-084","M2 AP:30",                  ["MODE:1"],["AP:30"],"M2 ap=0.30"),
("B-085","M4 AP:10",                  ["MODE:3"],["AP:10"],"M4 ap=0.10"),
("B-086","M5 AP:20",                  ["MODE:4"],["AP:20"],"M5 ap=0.20"),
# THR_CAT (thread category selector)
("B-087","M3 THR_CAT",                ["MODE:2"],["THR_CAT"],"M3 thr cat"),
# ALERT_OK (dismiss alert)
("B-088","ALERT:1 then ALERT_OK",     ["MODE:0","ALERT:1"],["ALERT_OK"],"Alert dismissed"),
("B-089","ALERT:2 then ALERT_OK",     ["MODE:0","ALERT:2"],["ALERT_OK"],"Alert 2 dismissed"),
# Mode switch with limits active
("B-090","Switch M2 with L limit",    ["MODE:0","LIMITS:1,0,0,0"],["M2"],"M2 with L limit"),
("B-091","Switch M3 with all limits", ["MODE:0","LIMITS:1,1,1,1"],["M3"],"M3 all limits"),
("B-092","Switch M1 after M4 limits", ["MODE:3","LIMITS:1,1,0,0"],["M1"],"M1 after M4"),
# Submode switch with limits
("B-093","M1 S3 with R limit",        ["MODE:0","LIMITS:0,1,0,0"],["S3"],"M1 S3 R limit"),
("B-094","M2 S2 with F limit",        ["MODE:1","LIMITS:0,0,1,0"],["S2"],"M2 S2 F limit"),
# Complex sequences
("B-095","M1→S3→SM2→KEY:UP x3",       ["MODE:0"],["M1","S3","KEY:RIGHT","KEY:RIGHT","KEY:UP","KEY:UP","KEY:UP"],"M1 S3 SM2 feed+++"),
("B-096","M2→AFEED:150→S2",           ["MODE:0"],["M2","AFEED:150","S2"],"M2 afeed150 S2"),
("B-097","M3→S1→KEY:UP x5→PARAM_OK",  ["MODE:0"],["M3","S1","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","PARAM_OK"],"M3 thread selected"),
("B-098","M4→S1→AP:15→PARAM_OK",      ["MODE:0"],["M4","S1","AP:15","PARAM_OK"],"M4 S1 ap set"),
("B-099","M7→DIVN=24→DIVM:12",        ["MODE:0"],["M7","DIVN:24","DIVM:12"],"M7 divider set"),
# Feed/afeed via BTN sequences
("B-100","M1 FEED:25→FEED:50→FEED:100",["MODE:0"],["FEED:25","FEED:50","FEED:100"],"M1 feed sequence"),
("B-101","M2 AFEED:100→200→300",       ["MODE:1"],["AFEED:100","AFEED:200","AFEED:300"],"M2 afeed sequence"),
# AP via BTN sequences
("B-102","M1 AP:0→25→50→100",          ["MODE:0"],["AP:0","AP:25","AP:50","AP:100"],"M1 ap sequence"),
# SM navigation complete
("B-103","M1 SM full cycle R",         ["MODE:0","SELECTMENU:1"],["KEY:RIGHT","KEY:RIGHT","KEY:RIGHT"],"M1 SM cycle right"),
("B-104","M2 SM full cycle R",         ["MODE:1","SELECTMENU:1"],["KEY:RIGHT","KEY:RIGHT","KEY:RIGHT"],"M2 SM cycle right"),
("B-105","M3 SM full cycle R",         ["MODE:2","SELECTMENU:1"],["KEY:RIGHT","KEY:RIGHT","KEY:RIGHT"],"M3 SM cycle right"),
# M3 thread step at extremes
("B-106","M3 thread step to index 0",  ["MODE:2"],["KEY:DN"]*20,"M3 thread min idx"),
("B-107","M3 thread step to max",      ["MODE:2"],["KEY:UP"]*50,"M3 thread max idx"),
# M7 divider full cycle
("B-108","M7 cycle through all 6 teeth",["MODE:6","DIVN:6","DIVM:1"],
         ["KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"],"M7 full 6-tooth cycle"),
("B-109","M7 wrap around tooth",       ["MODE:6","DIVN:6","DIVM:6"],["KEY:UP"],"M7 tooth wrap"),
# Start/stop via PARAM_OK (in M1/S1 state)
("B-110","M1 S1 SM3 then KEY:UP (joy Z+)", ["MODE:0","SUB:0","SELECTMENU:3"],["KEY:UP"],"M1 S3 axis Z move"),
("B-111","M1 S1 SM3 then KEY:DN (joy Z-)", ["MODE:0","SUB:0","SELECTMENU:3"],["KEY:DN"],"M1 S3 axis Z--"),
# Mode switch when running (should stop)
("B-112","Switch M2 while M1 set (stop)", ["MODE:0","STATE:run"],["M2"],"M2 after stop"),
("B-113","Switch M3 while M2 set",        ["MODE:1"],["M3"],"M3 after M2"),
# Consecutive mode switches back to same
("B-114","M1→M1 no-op",               ["MODE:0"],["M1"],"M1 same mode"),
("B-115","M2→M2 no-op",               ["MODE:1"],["M2"],"M2 same mode"),
# SelectMenu + submode interplay
("B-116","M1 S3 then SM2",            ["MODE:0","SUB:2"],["KEY:RIGHT"],"M1 S3 SM2"),
("B-117","M1 S2 then SM2",            ["MODE:0","SUB:1"],["KEY:RIGHT"],"M1 S2 SM2"),
("B-118","M3 S3 then SM3",            ["MODE:2","SUB:2"],["KEY:RIGHT","KEY:RIGHT"],"M3 S3 SM3"),
# AFEED bug verification (the -32512 scenario)
("B-119","aFeed bug: M1→M2 afeed=330",["MODE:0","AFEED:330"],["M2"],"M2 afeed=330 not -32512"),
("B-120","aFeed bug: M3→M2 afeed=100",["MODE:2","AFEED:100"],["M2"],"M2 afeed=100 no overflow"),
("B-121","aFeed bug: M4→M2 afeed=200",["MODE:3","AFEED:200"],["M2"],"M2 afeed=200 no overflow"),
("B-122","aFeed bug: M1 SM2→M2",      ["MODE:0","SELECTMENU:2","AFEED:150"],["M2"],"M2 from SM2 afeed=150"),
("B-123","aFeed bug: M2→M1→M2 cycle", ["MODE:1","AFEED:250"],["M1","M2"],"M2 round-trip afeed=250"),
# THREAD_STEP via BTN
("B-124","M3 THREAD_STEP:0",          ["MODE:2"],["THREAD_STEP:0"],"M3 step 0"),
("B-125","M3 THREAD_STEP:10",         ["MODE:2"],["THREAD_STEP:10"],"M3 step 10"),
("B-126","M3 THREAD_STEP:20",         ["MODE:2"],["THREAD_STEP:20"],"M3 step 20"),
# CONE step via BTN
("B-127","M4 CONE:0",                 ["MODE:3"],["CONE:0"],"M4 cone idx 0"),
("B-128","M4 CONE:3",                 ["MODE:3"],["CONE:3"],"M4 cone idx 3"),
("B-129","M5 CONE:5",                 ["MODE:4"],["CONE:5"],"M5 cone idx 5"),
# SPHERE via BTN
("B-130","M6 SPHERE:1000",            ["MODE:5"],["SPHERE:1000"],"M6 sphere 10mm"),
("B-131","M6 BAR:500",                ["MODE:5"],["BAR:500"],"M6 bar 5mm"),
# DIVN via BTN
("B-132","M7 DIVN:8",                 ["MODE:6"],["DIVN:8"],"M7 divn 8"),
("B-133","M7 DIVM:4",                 ["MODE:6","DIVN:8"],["DIVM:4"],"M7 divm 4"),
# SUBSEL via BTN
("B-134","M1 SUBSEL:AP",              ["MODE:0"],["SUBSEL:AP"],"M1 subsel AP"),
("B-135","M2 SUBSEL:AP",              ["MODE:1"],["SUBSEL:AP"],"M2 subsel AP"),
# RAPID
("B-136","RAPID_ON",                  ["MODE:0"],["RAPID_ON"],"Rapid on"),
("B-137","RAPID_OFF",                 ["MODE:0"],["RAPID_OFF"],"Rapid off"),
# Full sequence with screenshot verification for each step
("B-138","M1→set feed→switch to M2→check afeed",
         ["MODE:0","FEED:50","AFEED:200"],["FEED:50","M2"],"M2 afeed correct after M1 feed"),
("B-139","M2→set afeed→switch to M3",
         ["MODE:1","AFEED:150"],["M3"],"M3 after M2 afeed set"),
("B-140","M3→set thread step→switch to M4",
         ["MODE:2"],["KEY:UP","KEY:UP","M4"],"M4 after M3 thread"),
# Combined: mode + submode + selectmenu via BTN
("B-141","M1 S1 SM1 → M2 S1",        ["MODE:0","SUB:0","SELECTMENU:1"],["M2","S1"],"M2/S1"),
("B-142","M1 S3 SM3 → M3 S1",        ["MODE:0","SUB:2","SELECTMENU:3"],["M3","S1"],"M3/S1"),
("B-143","M4 S1 SM2 → M5 S3",        ["MODE:3","SUB:0","SELECTMENU:2"],["M5","S3"],"M5/S3"),
# SM navigation in M6 (sphere)
("B-144","M6 SM cycle",              ["MODE:5"],["KEY:RIGHT","KEY:RIGHT","KEY:LEFT"],"M6 SM nav"),
# AP reset
("B-145","AP reset via AP:0",        ["MODE:0","AP:100"],["AP:0"],"AP reset to 0"),
# M7 divider at various counts
("B-146","M7 DIVN:2 min",            ["MODE:6"],["DIVN:2","DIVM:1"],"M7 DIVN=2"),
("B-147","M7 DIVN:360",              ["MODE:6"],["DIVN:360","DIVM:180"],"M7 DIVN=360"),
("B-148","M7 DIVN:12 DIVM:12",       ["MODE:6","DIVN:12"],["DIVM:12"],"M7 last tooth"),
# M3 PH via BTN
("B-149","M3 PH:2",                  ["MODE:2"],["PH:2"],"M3 PH=2"),
("B-150","M3 PH:4",                  ["MODE:2"],["PH:4"],"M3 PH=4"),
# Feed edge via BTN in M1
("B-151","M1 FEED:5 min",            ["MODE:0"],["FEED:5"],"M1 feed min 0.05"),
("B-152","M1 FEED:200 max",          ["MODE:0"],["FEED:200"],"M1 feed max 2.00"),
# aFeed edge via BTN in M2
("B-153","M2 AFEED:15 min",          ["MODE:1"],["AFEED:15"],"M2 afeed min 15"),
("B-154","M2 AFEED:400 over max",    ["MODE:1"],["AFEED:400"],"M2 afeed 400"),
# SM + limits combo
("B-155","M1 SM2 with L limit",      ["MODE:0","LIMITS:1,0,0,0"],["KEY:RIGHT"],"M1 SM2 L limit"),
("B-156","M1 SM3 with all limits",   ["MODE:0","LIMITS:1,1,1,1"],["KEY:RIGHT","KEY:RIGHT"],"M1 SM3 all lim"),
# Mode + state interplay
("B-157","M1 KEY:UP then M2",        ["MODE:0","FEED:25"],["KEY:UP","M2"],"M2 after KEY:UP"),
("B-158","M2 KEY:DN then M1",        ["MODE:1","AFEED:200"],["KEY:DN","M1"],"M1 after KEY:DN"),
# Multiple UP/DN then mode switch
("B-159","M1 3×UP then switch M3",   ["MODE:0","FEED:25"],["KEY:UP","KEY:UP","KEY:UP","M3"],"M3 after 3up"),
("B-160","M3 5×UP then switch M1",   ["MODE:2"],["KEY:UP"]*5+["M1"],"M1 after M3 5up"),
# PARAM_OK + mode switch
("B-161","M1 PARAM_OK then M2",      ["MODE:0","SELECTMENU:1"],["PARAM_OK","M2"],"M2 after SM++"),
("B-162","M2 PARAM_OK then M3",      ["MODE:1","SELECTMENU:1"],["PARAM_OK","M3"],"M3 after SM++"),
# aFeed: set via BTN then verify display
("B-163","M2 AFEED:50 verify display",  ["MODE:1"],["AFEED:50"],"ESP32 shows 50"),
("B-164","M2 AFEED:180 verify display", ["MODE:1"],["AFEED:180"],"ESP32 shows 180"),
("B-165","M2 AFEED:270 verify display", ["MODE:1"],["AFEED:270"],"ESP32 shows 270"),
# Feed: set via BTN then verify
("B-166","M1 FEED:75 verify",           ["MODE:0"],["FEED:75"],"ESP32 shows 0.75"),
("B-167","M1 FEED:125 verify",          ["MODE:0"],["FEED:125"],"ESP32 shows 1.25"),
("B-168","M1 FEED:175 verify",          ["MODE:0"],["FEED:175"],"ESP32 shows 1.75"),
# Cone index sequence
("B-169","M4 CONE:0→1→2→3",            ["MODE:3"],["CONE:0","CONE:1","CONE:2","CONE:3"],"M4 cone sequence"),
("B-170","M5 CONE:5→4→3",              ["MODE:4"],["CONE:5","CONE:4","CONE:3"],"M5 cone sequence"),
# Thread step sequence
("B-171","M3 THREAD_STEP 5→10→15",     ["MODE:2"],["THREAD_STEP:5","THREAD_STEP:10","THREAD_STEP:15"],"M3 thread sequence"),
# M6 CUTTER/CUTTING via BTN
("B-172","M6 CUTTER_W:200",            ["MODE:5"],["CUTTER_W:200"],"M6 cutter 2.00"),
("B-173","M6 CUTTING_W:100",           ["MODE:5"],["CUTTING_W:100"],"M6 cutting 1.00"),
# BAR sequence
("B-174","M6 BAR:0→100→300",           ["MODE:5"],["BAR:0","BAR:100","BAR:300"],"M6 bar sequence"),
# M1 extended params via BTN
("B-175","M1 S3 OTSKOK_Z:500",         ["MODE:0","SUB:2","SELECTMENU:3"],["OTSKOK_Z:500"],"M1 S3 otskok 5.00"),
("B-176","M1 S3 TENSION_Z:200",        ["MODE:0","SUB:2","SELECTMENU:3"],["TENSION_Z:200"],"M1 S3 tension 2.00"),
("B-177","M1 S2 DIAM_X:1000",          ["MODE:0","SUB:1","SELECTMENU:2"],["DIAM_X:1000"],"M1 S2 diam 100mm"),
# CONE_THR
("B-178","M4 CONE_THR:1 on",           ["MODE:3"],["CONE_THR:1"],"M4 cone thread on"),
("B-179","M4 CONE_THR:0 off",          ["MODE:3"],["CONE_THR:0"],"M4 cone thread off"),
("B-180","M5 CONE_THR:1 on",           ["MODE:4"],["CONE_THR:1"],"M5 cone thread on"),
# PASS_FIN
("B-181","M3 PASS_FIN:1",              ["MODE:2"],["PASS_FIN:1"],"M3 pass fin 1"),
("B-182","M3 PASS_FIN:3",              ["MODE:2"],["PASS_FIN:3"],"M3 pass fin 3"),
# AP in M4/M5 (cone)
("B-183","M4 AP:15 then KEY:UP",       ["MODE:3","AP:15"],["KEY:UP"],"M4 ap++"),
("B-184","M5 AP:20 then KEY:DN",       ["MODE:4","AP:20"],["KEY:DN"],"M5 ap--"),
# M7 tooth cycle with wrap
("B-185","M7 DIVN:4 cycle all",        ["MODE:6"],["DIVN:4","DIVM:1","KEY:UP","KEY:UP","KEY:UP","KEY:UP"],"M7 4-tooth full cycle"),
("B-186","M7 DIVN:8 to last then wrap",["MODE:6"],["DIVN:8","DIVM:8","KEY:UP"],"M7 wrap to 1"),
# SelectMenu navigation on M5/M6/M7
("B-187","M5 SM1→SM2→SM3 R",          ["MODE:4","SELECTMENU:1"],["KEY:RIGHT","KEY:RIGHT"],"M5 SM3"),
("B-188","M6 SM3→SM2→SM1 L",          ["MODE:5","SELECTMENU:3"],["KEY:LEFT","KEY:LEFT"],"M6 SM1"),
("B-189","M7 SM1→SM2 R",              ["MODE:6","SELECTMENU:1"],["KEY:RIGHT"],"M7 SM2"),
# Full scenario: start from M1, navigate through multiple modes, verify each
("B-190","Navigate M1→M2→M3 with feeds",
         ["MODE:0","FEED:25","AFEED:200"],
         ["M1","M2","M3"],"M3 display after M1/M2 with feeds"),
("B-191","Navigate M4→M5→M6 with AP",
         ["MODE:3","AP:15"],
         ["M4","M5","M6"],"M6 display after M4/M5"),
("B-192","M1 S1→S2→S3 submode cycle",
         ["MODE:0","FEED:50"],
         ["S1","S2","S3"],"M1 S3 after cycle"),
("B-193","M3 PH=1→2→4 cycle",
         ["MODE:2"],
         ["PH:1","PH:2","PH:4"],"M3 PH=4"),
("B-194","M7 DIVN=6 tooth traverse",
         ["MODE:6"],
         ["DIVN:6","DIVM:1","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"],"M7 DIVN=6 tooth 6"),
# Stress: rapid repeated presses
("B-195","M1 rapid KEY:UP×10",        ["MODE:0","FEED:25"],["KEY:UP"]*10,"M1 feed after 10 UP"),
("B-196","M1 rapid KEY:DN×10",        ["MODE:0","FEED:100"],["KEY:DN"]*10,"M1 feed after 10 DN"),
("B-197","M2 rapid afeed 5×UP",       ["MODE:1","AFEED:100"],["KEY:UP"]*5,"M2 afeed after 5 UP"),
# Final: reset to known state
("B-198","Reset to M1 S1 SM1 nominal",["MODE:0","SUB:0","SELECTMENU:1","FEED:25","AP:0"],["M1","S1"],"M1 S1 reset"),
("B-199","Final M2 afeed stability",  ["MODE:0","AFEED:330"],["M2"],"M2 afeed=330 stable"),
("B-200","Final full sync verify",    ["MODE:0","FEED:25","AFEED:100","AP:0","LIMITS:0,0,0,0"],
         ["M1","S1"],"Full sync nominal state"),
]

# ─── Test runner ─────────────────────────────────────────────────────────────

def run_dir_a(stm32: serial.Serial, esp32: serial.Serial, filter_id=None):
    """Run Direction A tests: STM32 → ESP32."""
    print("\n═══ DIRECTION A: STM32 → ESP32 ═══")
    passed = failed = 0
    for case in DIR_A_CASES:
        tc_id, desc, cmds, do_sendall, expected = case
        if filter_id and tc_id != filter_id:
            continue
        # Send commands to STM32
        for cmd in cmds:
            send_stm32(stm32, cmd)
        if do_sendall:
            stm32_sendall(stm32)
        time.sleep(0.3)
        # Take screenshot
        shot = take_screenshot(esp32, tc_id)
        ok = shot is not None
        if ok:
            passed += 1
        else:
            failed += 1
        log_result(tc_id, "A", desc, cmds, expected, shot, ok)
    print(f"\nDir A: {passed} passed, {failed} failed / {len(DIR_A_CASES)} total")
    return passed, failed

def run_dir_b(stm32: serial.Serial, esp32: serial.Serial, filter_id=None):
    """Run Direction B tests: ESP32 → STM32."""
    print("\n═══ DIRECTION B: ESP32 → STM32 ═══")
    passed = failed = 0
    for case in DIR_B_CASES:
        tc_id, desc, setup_cmds, btns, expected = case
        if filter_id and tc_id != filter_id:
            continue
        # Set up STM32 initial state
        for cmd in setup_cmds:
            send_stm32(stm32, cmd)
        stm32_sendall(stm32)
        time.sleep(0.3)
        flush_esp32(esp32)
        # Send BTN commands to ESP32
        for btn in btns:
            send_esp32_btn(esp32, btn)
            time.sleep(0.15)   # let STM32 process and respond
        time.sleep(0.5)        # final settle
        # Take screenshot
        shot = take_screenshot(esp32, tc_id)
        ok = shot is not None
        if ok:
            passed += 1
        else:
            failed += 1
        log_result(tc_id, "B", desc, {"setup": setup_cmds, "btns": btns}, expected, shot, ok)
    print(f"\nDir B: {passed} passed, {failed} failed / {len(DIR_B_CASES)} total")
    return passed, failed

# ─── Entry point ─────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Bidirectional sync test runner")
    ap.add_argument("--esp32",  required=True, help="ESP32 USB CDC port (e.g. /dev/cu.usbmodem14801)")
    ap.add_argument("--stm32",  required=True, help="STM32 USART1 VCP port (Discovery USB, e.g. /dev/cu.usbmodemXXXX)")
    ap.add_argument("--dir",    choices=["A","B","both"], default="both")
    ap.add_argument("--tc",     default=None, help="Run single test case e.g. A-119")
    ap.add_argument("--baud-esp32", type=int, default=115200)
    ap.add_argument("--baud-stm32", type=int, default=115200)
    args = ap.parse_args()

    print(f"ESP32 port : {args.esp32}")
    print(f"STM32 port : {args.stm32}")
    print(f"Screenshots: {OUT_DIR}")
    print(f"Log file   : {LOG_FILE}")

    with serial.Serial(args.esp32, args.baud_esp32, timeout=2) as esp32, \
         serial.Serial(args.stm32, args.baud_stm32, timeout=2) as stm32:
        time.sleep(1.0)
        flush_esp32(esp32)
        stm32.reset_input_buffer()

        filter_id = args.tc
        dir_filter = args.dir

        a_pass = a_fail = b_pass = b_fail = 0
        if dir_filter in ("A", "both"):
            a_pass, a_fail = run_dir_a(stm32, esp32, filter_id)
        if dir_filter in ("B", "both"):
            b_pass, b_fail = run_dir_b(stm32, esp32, filter_id)

    print("\n═══ SUMMARY ═══")
    if dir_filter in ("A", "both"):
        print(f"  Dir A (STM32→ESP32): {a_pass}/{a_pass+a_fail} passed")
    if dir_filter in ("B", "both"):
        print(f"  Dir B (ESP32→STM32): {b_pass}/{b_pass+b_fail} passed")
    print(f"  Results saved to: {LOG_FILE}")

if __name__ == "__main__":
    main()
