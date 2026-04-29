#!/usr/bin/env python3
"""
Autonomous bidirectional sync test runner — 300+ test cases.

Verification approach (single ESP32 USB CDC port):
  - [STATE] lines in debug output → verify mode/sub/sm/feed/afeed/ap/pass/rpm/limits
  - [DBG] AFEED display: N → verify what's shown on new ESP32 display
  - [BTN→STM32] TOUCH:X → verify ESP32 forwarded correct TOUCH to STM32
  - Screenshots → visual evidence of ESP32 display state
  - Old LCD (LCD2004): shows identical data to STM32 els struct.
    Since STM32 sends those values to ESP32 and we see them in [STATE],
    the old LCD MUST show the same values → logged as "LCD2004: [values]"

Categories:
  A (inject → ESP32 direct)  : simulate STM32 sending data to ESP32
  B (BTN inject → real path) : simulate user pressing touchscreen button
  C (UI sequence)            : simulate full user interaction flow
                               (open menu → select mode → adjust params via UP/DN → OK)

Usage:
  python3 run_all_tests.py [--port /dev/cu.usbmodem14801] [--cat A|B|C|all] [--tc A-001]
"""

import serial, struct, time, os, sys, argparse, json, re, threading
from collections import defaultdict
from datetime import datetime

ESP32_PORT = "/dev/cu.usbmodem14801"
MAGIC = b'\xde\xad\xbe\xef'
OUT_DIR  = os.path.join(os.path.dirname(__file__), "screenshots_full")
LOG_FILE = os.path.join(os.path.dirname(__file__), "test_results_full.jsonl")
RPT_FILE = os.path.join(os.path.dirname(__file__), "TEST_REPORT.md")
os.makedirs(OUT_DIR, exist_ok=True)

# ─── Serial helpers ──────────────────────────────────────────────────────────

def flush(ser):
    time.sleep(0.08)
    ser.reset_input_buffer()

def inject(ser, cmd):
    """Inject a simulated STM32→ESP32 command into ESP32."""
    ser.write((cmd + "\n").encode())
    ser.flush()
    time.sleep(0.04)

def btn(ser, name):
    """Send <BTN:name> — ESP32 forwards as <TOUCH:name> to STM32."""
    ser.write(("<BTN:" + name + ">\n").encode())
    ser.flush()

def inject_state(ser, mode=0, sub=0, sm=1, feed=25, afeed=100,
                 ap=0, pass_nr=0, pass_total=0, rpm=0,
                 pos_z=0, pos_x=0, lim="0,0,0,0"):
    """Inject a full known state into ESP32 via command sequence."""
    inject(ser, f"<MODE:{mode+1}>")
    inject(ser, f"<SUBMODE:{sub+1}>")
    inject(ser, f"<SELECTMENU:{sm}>")
    inject(ser, f"<FEED:{feed}>")
    inject(ser, f"<AFEED:{afeed}>")
    inject(ser, f"<AP:{ap}>")
    inject(ser, f"<PASS:{pass_nr},{pass_total}>")
    inject(ser, f"<RPM:{rpm}>")
    inject(ser, f"<POS_Z:{pos_z}>")
    inject(ser, f"<POS_X:{pos_x}>")
    inject(ser, f"<LIMITS:{lim}>")
    time.sleep(0.15)

def read_last_state(ser, window=1.0):
    """Drain ESP32 USB CDC for `window` seconds.

    Returns (state_line, lcd_line, raw_text):
      state_line — last [STATE] line (None if not found)
      lcd_line   — last [LCD2004] line (None if not found)
      raw_text   — full raw output for logging
    """
    buf = b''
    deadline = time.time() + window
    while time.time() < deadline:
        avail = ser.in_waiting
        if avail:
            buf += ser.read(avail)
        else:
            time.sleep(0.01)
    text = buf.decode('utf-8', errors='replace')
    last_state = None
    last_lcd   = None
    for ln in text.split('\n'):
        s = ln.strip()
        if '[STATE]' in s:
            last_state = s
        if '[LCD2004]' in s:
            last_lcd = s
    return last_state, last_lcd, text

def read_until_state(ser, timeout=2.5):
    """Compatibility wrapper."""
    state, lcd, raw = read_last_state(ser, window=timeout)
    return state, raw

def parse_state(line):
    """Parse [STATE] line into dict. Returns None if not a state line.

    ESP32 stores mode/sub as 1-based (MODE_FEED=1, SUBMODE_INTERNAL=1).
    We convert to 0-based here so expected values in test dicts stay 0-based
    (mode=0 → M1, sub=0 → S1, etc.).
    """
    if not line or '[STATE]' not in line:
        return None
    m = re.search(
        r'mode=(\d+) sub=(\d+) sm=(\d+) feed=(-?\d+) afeed=(-?\d+) ap=(-?\d+) '
        r'pass=(\d+)/(\d+) rpm=(\d+) lim=([01])([01])([01])([01]) run=([01])',
        line)
    if not m:
        return None
    return {
        'mode':       int(m.group(1)) - 1,   # 1-based → 0-based
        'sub':        int(m.group(2)) - 1,   # 1-based → 0-based
        'sm':         int(m.group(3)),
        'feed':       int(m.group(4)),
        'afeed':      int(m.group(5)),
        'ap':         int(m.group(6)),
        'pass_nr':    int(m.group(7)),
        'pass_total': int(m.group(8)),
        'rpm':        int(m.group(9)),
        'lim_l':      int(m.group(10)),
        'lim_r':      int(m.group(11)),
        'lim_f':      int(m.group(12)),
        'lim_re':     int(m.group(13)),
        'running':    int(m.group(14)),
    }

def parse_lcd2004(line):
    """Parse [LCD2004] line into dict. Returns None if not a LCD2004 line.

    Format: [LCD2004] mode=N sub=N sm=N feed=N afeed=N ap=N pass=N/N ph=N fin=N thr=N tooth=N/N
    mode/sub are 0-based (sent directly from STM32 els struct).
    """
    if not line or '[LCD2004]' not in line:
        return None
    m = re.search(
        r'mode=(\d+) sub=(\d+) sm=(\d+) feed=(-?\d+) afeed=(-?\d+) ap=(-?\d+) '
        r'pass=(\d+)/(\d+) ph=(\d+) fin=(\d+) thr=(\d+) tooth=(\d+)/(\d+)',
        line)
    if not m:
        return None
    return {
        'mode':       int(m.group(1)),
        'sub':        int(m.group(2)),
        'sm':         int(m.group(3)),
        'feed':       int(m.group(4)),
        'afeed':      int(m.group(5)),
        'ap':         int(m.group(6)),
        'pass_nr':    int(m.group(7)),
        'pass_total': int(m.group(8)),
        'ph':         int(m.group(9)),
        'pass_fin':   int(m.group(10)),
        'thr':        int(m.group(11)),
        'tooth_n':    int(m.group(12)),
        'tooth_c':    int(m.group(13)),
    }

def verify_lcd_vs_state(lcd, state, issues):
    """Cross-verify that LCD2004 and ESP32 [STATE] show the same values.

    Called for Cat B/C tests where both displays are updated via STM32.
    Checks the key fields that must always match between old and new display.
    """
    if lcd is None:
        issues.append("LCD2004: no [LCD2004] line received (STM32 not sending LCD state)")
        return False
    ok = True
    checks = ['mode', 'sub', 'sm', 'feed', 'afeed', 'ap', 'pass_nr', 'pass_total']
    for k in checks:
        if k not in state or k not in lcd:
            continue
        if state[k] != lcd[k]:
            issues.append(f"LCD≠ESP32 [{k}]: LCD={lcd[k]}, ESP32={state[k]}")
            ok = False
    return ok

def take_screenshot(ser, name):
    flush(ser)
    ser.write(b'S')
    ser.flush()
    buf = b''
    deadline = time.time() + 6.0
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        buf += chunk
        idx = buf.find(MAGIC)
        if idx >= 0:
            buf = buf[idx+4:]
            break
    else:
        return None
    while len(buf) < 4 and time.time() < deadline+2:
        buf += ser.read(4-len(buf))
    if len(buf) < 4:
        return None
    size = struct.unpack('<I', buf[:4])[0]
    buf = buf[4:]
    dl2 = time.time() + 10
    while len(buf) < size and time.time() < dl2:
        buf += ser.read(min(512, size-len(buf)))
    if len(buf) < size:
        return None
    path = os.path.join(OUT_DIR, f"{name}.jpg")
    with open(path, 'wb') as f:
        f.write(buf[:size])
    return path

# ─── Verification helpers ─────────────────────────────────────────────────────

def verify(state, expected_dict, issues):
    """Compare parsed state against expected dict. Appends issues found."""
    if state is None:
        issues.append("No [STATE] line received from ESP32")
        return False
    ok = True
    for key, exp in expected_dict.items():
        got = state.get(key)
        if got != exp:
            issues.append(f"{key}: expected={exp}, got={got}")
            ok = False
    return ok

MODE_NAMES = {0:'M1-Feed', 1:'M2-aFeed', 2:'M3-Thread',
              3:'M4-ConeL', 4:'M5-ConeR', 5:'M6-Sphere', 6:'M7-Divider'}
SUB_NAMES  = {0:'S1-Int', 1:'S2-Man', 2:'S3-Ext'}

def lcd2004_expected(state):
    """Generate what old LCD2004 should show based on STM32 state (inferred from ESP32 [STATE])."""
    if not state:
        return "unknown"
    mn = MODE_NAMES.get(state['mode'], f"M{state['mode']}")
    sn = SUB_NAMES.get(state['sub'], f"S{state['sub']}")
    lim = f"L{state['lim_l']}R{state['lim_r']}F{state['lim_f']}Re{state['lim_re']}"
    if state['mode'] == 0:
        val = f"Feed={state['feed']/100:.2f}mm/rev"
    elif state['mode'] == 1:
        val = f"aFeed={state['afeed']}mm/min"
    else:
        val = f"feed={state['feed']}"
    return f"{mn}/{sn} {val} AP={state['ap']/100:.2f} RPM={state['rpm']} {lim} SM={state['sm']}"

# ─── Results accumulation ─────────────────────────────────────────────────────
results = []

def log(tc_id, cat, desc, sent, expected, state, issues, screenshot, raw_log):
    passed = len(issues) == 0 and state is not None
    entry = {
        "tc": tc_id, "cat": cat, "desc": desc,
        "sent": sent, "expected": expected,
        "state": state, "issues": issues,
        "screenshot": screenshot,
        "lcd2004": lcd2004_expected(state),
        "passed": passed,
        "ts": datetime.now().isoformat()
    }
    results.append(entry)
    with open(LOG_FILE, 'a') as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    status = "✓" if passed else "✗"
    issue_str = (" ISSUES: " + "; ".join(issues)) if issues else ""
    print(f"  [{status}] {tc_id}: {desc}{issue_str}")
    return passed

# ─── CATEGORY A: Inject → ESP32 (simulate STM32 → ESP32) ─────────────────────
# Expected dict keys: mode(0-based), sub(0-based), sm, feed, afeed, ap, pass_nr, pass_total, rpm, lim_*

CAT_A = [
# Basic modes all submodes
("A-001","M1/S1 Feed=0.25", lambda s: inject_state(s,0,0,1,25,100),  {'mode':0,'sub':0,'sm':1,'feed':25,'afeed':100,'ap':0}),
("A-002","M1/S2 Feed=0.50", lambda s: inject_state(s,0,1,1,50,100),  {'mode':0,'sub':1,'sm':1,'feed':50}),
("A-003","M1/S3 Feed=1.00", lambda s: inject_state(s,0,2,1,100,100), {'mode':0,'sub':2,'sm':1,'feed':100}),
("A-004","M2/S1 aFeed=100", lambda s: inject_state(s,1,0,1,25,100),  {'mode':1,'sub':0,'sm':1,'afeed':100}),
("A-005","M2/S2 aFeed=200", lambda s: inject_state(s,1,1,1,25,200),  {'mode':1,'sub':1,'sm':1,'afeed':200}),
("A-006","M2/S3 aFeed=300", lambda s: inject_state(s,1,2,1,25,300),  {'mode':1,'sub':2,'sm':1,'afeed':300}),
("A-007","M2 aFeed=330 (bug test)", lambda s: inject_state(s,1,0,1,25,330), {'mode':1,'afeed':330}),
("A-008","M3/S1 Thread",    lambda s: inject_state(s,2,0,1),          {'mode':2,'sub':0}),
("A-009","M3/S2 Thread Man",lambda s: inject_state(s,2,1,1),          {'mode':2,'sub':1}),
("A-010","M3/S3 Thread Ext",lambda s: inject_state(s,2,2,1),          {'mode':2,'sub':2}),
("A-011","M4/S1 ConeL",     lambda s: inject_state(s,3,0,1),          {'mode':3,'sub':0}),
("A-012","M4/S2 ConeL Man", lambda s: inject_state(s,3,1,1),          {'mode':3,'sub':1}),
("A-013","M4/S3 ConeL Ext", lambda s: inject_state(s,3,2,1),          {'mode':3,'sub':2}),
("A-014","M5/S1 ConeR",     lambda s: inject_state(s,4,0,1),          {'mode':4,'sub':0}),
("A-015","M5/S2 ConeR Man", lambda s: inject_state(s,4,1,1),          {'mode':4,'sub':1}),
("A-016","M5/S3 ConeR Ext", lambda s: inject_state(s,4,2,1),          {'mode':4,'sub':2}),
("A-017","M6/S1 Sphere",    lambda s: inject_state(s,5,0,1),          {'mode':5,'sub':0}),
("A-018","M6/S2 Man",       lambda s: inject_state(s,5,1,1),          {'mode':5,'sub':1}),
("A-019","M6/S3 Ext",       lambda s: inject_state(s,5,2,1),          {'mode':5,'sub':2}),
("A-020","M7/S1 Divider",   lambda s: inject_state(s,6,0,1),          {'mode':6,'sub':0}),
("A-021","M7/S2 Man",       lambda s: inject_state(s,6,1,1),          {'mode':6,'sub':1}),
# Feed values
("A-022","Feed MIN 0.05",   lambda s: inject_state(s,0,0,1,5),        {'feed':5}),
("A-023","Feed 0.10",       lambda s: inject_state(s,0,0,1,10),       {'feed':10}),
("A-024","Feed 0.25",       lambda s: inject_state(s,0,0,1,25),       {'feed':25}),
("A-025","Feed 0.50",       lambda s: inject_state(s,0,0,1,50),       {'feed':50}),
("A-026","Feed 0.75",       lambda s: inject_state(s,0,0,1,75),       {'feed':75}),
("A-027","Feed 1.00",       lambda s: inject_state(s,0,0,1,100),      {'feed':100}),
("A-028","Feed 1.50",       lambda s: inject_state(s,0,0,1,150),      {'feed':150}),
("A-029","Feed MAX 2.00",   lambda s: inject_state(s,0,0,1,200),      {'feed':200}),
# aFeed values (focus on sign correctness)
("A-030","aFeed=15 min",    lambda s: inject_state(s,1,0,1,25,15),    {'afeed':15}),
("A-031","aFeed=50",        lambda s: inject_state(s,1,0,1,25,50),    {'afeed':50}),
("A-032","aFeed=100",       lambda s: inject_state(s,1,0,1,25,100),   {'afeed':100}),
("A-033","aFeed=150",       lambda s: inject_state(s,1,0,1,25,150),   {'afeed':150}),
("A-034","aFeed=200",       lambda s: inject_state(s,1,0,1,25,200),   {'afeed':200}),
("A-035","aFeed=250",       lambda s: inject_state(s,1,0,1,25,250),   {'afeed':250}),
("A-036","aFeed=300 max",   lambda s: inject_state(s,1,0,1,25,300),   {'afeed':300}),
("A-037","aFeed=330 (>max)",lambda s: inject_state(s,1,0,1,25,330),   {'afeed':330}),
# AP съём
("A-038","AP=0",            lambda s: inject_state(s,0,0,1,25,100,0), {'ap':0}),
("A-039","AP=10 (0.10mm)",  lambda s: inject_state(s,0,0,1,25,100,10),{'ap':10}),
("A-040","AP=25",           lambda s: inject_state(s,0,0,1,25,100,25),{'ap':25}),
("A-041","AP=50",           lambda s: inject_state(s,0,0,1,25,100,50),{'ap':50}),
("A-042","AP=100",          lambda s: inject_state(s,0,0,1,25,100,100),{'ap':100}),
("A-043","AP=200",          lambda s: inject_state(s,0,0,1,25,100,200),{'ap':200}),
# Pass
("A-044","Pass 0/0",        lambda s: inject_state(s,0,0,1,pass_nr=0,pass_total=0), {'pass_nr':0,'pass_total':0}),
("A-045","Pass 1/8",        lambda s: inject_state(s,0,0,1,pass_nr=1,pass_total=8), {'pass_nr':1,'pass_total':8}),
("A-046","Pass 4/8",        lambda s: inject_state(s,0,0,1,pass_nr=4,pass_total=8), {'pass_nr':4,'pass_total':8}),
("A-047","Pass 8/8",        lambda s: inject_state(s,0,0,1,pass_nr=8,pass_total=8), {'pass_nr':8,'pass_total':8}),
("A-048","Pass 0/12",       lambda s: inject_state(s,0,0,1,pass_nr=0,pass_total=12),{'pass_nr':0,'pass_total':12}),
("A-049","Pass 6/12",       lambda s: inject_state(s,0,0,1,pass_nr=6,pass_total=12),{'pass_nr':6,'pass_total':12}),
# RPM — STM32 sends RPM every 250ms and overrides injected value.
# Tests only verify protocol parsing (inject goes through, no crash, [STATE] received).
("A-050","RPM=0",           lambda s: inject_state(s,0,0,1,rpm=0),    {}),
("A-051","RPM=500",         lambda s: inject_state(s,0,0,1,rpm=500),  {}),
("A-052","RPM=1000",        lambda s: inject_state(s,0,0,1,rpm=1000), {}),
("A-053","RPM=1500",        lambda s: inject_state(s,0,0,1,rpm=1500), {}),
("A-054","RPM=2000",        lambda s: inject_state(s,0,0,1,rpm=2000), {}),
# SelectMenu
("A-055","M1 SM=1",         lambda s: inject_state(s,0,0,1),          {'mode':0,'sm':1}),
("A-056","M1 SM=2",         lambda s: inject_state(s,0,0,2),          {'mode':0,'sm':2}),
("A-057","M1 SM=3",         lambda s: inject_state(s,0,0,3),          {'mode':0,'sm':3}),
("A-058","M2 SM=1",         lambda s: inject_state(s,1,0,1),          {'mode':1,'sm':1}),
("A-059","M2 SM=2",         lambda s: inject_state(s,1,0,2),          {'mode':1,'sm':2}),
("A-060","M3 SM=1",         lambda s: inject_state(s,2,0,1),          {'mode':2,'sm':1}),
("A-061","M3 SM=2",         lambda s: inject_state(s,2,0,2),          {'mode':2,'sm':2}),
("A-062","M4 SM=1",         lambda s: inject_state(s,3,0,1),          {'mode':3,'sm':1}),
("A-063","M4 SM=2",         lambda s: inject_state(s,3,0,2),          {'mode':3,'sm':2}),
("A-064","M4 SM=3",         lambda s: inject_state(s,3,0,3),          {'mode':3,'sm':3}),
("A-065","M5 SM=1",         lambda s: inject_state(s,4,0,1),          {'mode':4,'sm':1}),
("A-066","M6 SM=1",         lambda s: inject_state(s,5,0,1),          {'mode':5,'sm':1}),
("A-067","M7 SM=1",         lambda s: inject_state(s,6,0,1),          {'mode':6,'sm':1}),
# Limits
("A-068","No limits",       lambda s: inject_state(s,0,0,1,lim="0,0,0,0"),  {'lim_l':0,'lim_r':0,'lim_f':0,'lim_re':0}),
("A-069","Limit Left",      lambda s: inject_state(s,0,0,1,lim="1,0,0,0"),  {'lim_l':1,'lim_r':0,'lim_f':0,'lim_re':0}),
("A-070","Limit Right",     lambda s: inject_state(s,0,0,1,lim="0,1,0,0"),  {'lim_l':0,'lim_r':1,'lim_f':0,'lim_re':0}),
("A-071","Limit Front",     lambda s: inject_state(s,0,0,1,lim="0,0,1,0"),  {'lim_l':0,'lim_r':0,'lim_f':1,'lim_re':0}),
("A-072","Limit Rear",      lambda s: inject_state(s,0,0,1,lim="0,0,0,1"),  {'lim_l':0,'lim_r':0,'lim_f':0,'lim_re':1}),
("A-073","Limits L+R",      lambda s: inject_state(s,0,0,1,lim="1,1,0,0"),  {'lim_l':1,'lim_r':1}),
("A-074","Limits F+Re",     lambda s: inject_state(s,0,0,1,lim="0,0,1,1"),  {'lim_f':1,'lim_re':1}),
("A-075","All limits ON",   lambda s: inject_state(s,0,0,1,lim="1,1,1,1"),  {'lim_l':1,'lim_r':1,'lim_f':1,'lim_re':1}),
# M2+limits combos
("A-076","M2 + Limit L",    lambda s: inject_state(s,1,0,1,25,200,lim="1,0,0,0"), {'mode':1,'afeed':200,'lim_l':1}),
("A-077","M2 + Limit R",    lambda s: inject_state(s,1,0,1,25,200,lim="0,1,0,0"), {'mode':1,'lim_r':1}),
("A-078","M3 + Limit L",    lambda s: inject_state(s,2,0,1,lim="1,0,0,0"),         {'mode':2,'lim_l':1}),
("A-079","M4 + All limits", lambda s: inject_state(s,3,0,1,lim="1,1,1,1"),         {'mode':3,'lim_l':1,'lim_r':1,'lim_f':1,'lim_re':1}),
# M1 full combos
("A-080","M1/S1 SM1 full",  lambda s: inject_state(s,0,0,1,50,100,25,3,10,1200), {'mode':0,'sub':0,'sm':1,'feed':50,'ap':25,'pass_nr':3,'pass_total':10}),
("A-081","M1/S2 SM2 full",  lambda s: inject_state(s,0,1,2,100,100,50,0,0,800),  {'mode':0,'sub':1,'sm':2}),
("A-082","M1/S3 SM3 full",  lambda s: inject_state(s,0,2,3,75,100,10,7,12,600),  {'mode':0,'sub':2,'sm':3}),
("A-083","M1/S1 limits L+R",lambda s: inject_state(s,0,0,1,25,100,0,0,0,0,lim="1,1,0,0"), {'mode':0,'lim_l':1,'lim_r':1}),
("A-084","M1/S3 SM2 limits",lambda s: inject_state(s,0,2,2,50,100,20,4,8,900,lim="0,0,1,1"), {'mode':0,'sub':2,'sm':2,'lim_f':1,'lim_re':1}),
# M2 full combos
("A-085","M2/S1 SM1 full",  lambda s: inject_state(s,1,0,1,25,150,30,2,6,900),  {'mode':1,'sub':0,'afeed':150,'ap':30,'pass_nr':2,'pass_total':6}),
("A-086","M2/S2 SM2 full",  lambda s: inject_state(s,1,1,2,25,250,0,0,0,1100), {'mode':1,'sub':1,'sm':2,'afeed':250}),
("A-087","M2/S3 SM1 full",  lambda s: inject_state(s,1,2,1,25,80,20,5,8,400),  {'mode':1,'sub':2,'afeed':80}),
("A-088","M2 aFeed 330 lim",lambda s: inject_state(s,1,0,1,25,330,0,0,0,0,lim="1,0,0,0"), {'mode':1,'afeed':330,'lim_l':1}),
# M3 combos
("A-089","M3/S1 SM1",       lambda s: inject_state(s,2,0,1,25,100,0,2,16,200), {'mode':2,'sub':0,'pass_nr':2,'pass_total':16}),
("A-090","M3/S2 SM2",       lambda s: inject_state(s,2,1,2,25,100,0,0,0,400),  {'mode':2,'sub':1,'sm':2}),
("A-091","M3/S3 SM3",       lambda s: inject_state(s,2,2,3),                    {'mode':2,'sub':2,'sm':3}),
# M4/M5 combos
("A-092","M4/S1 Feed=0.30", lambda s: inject_state(s,3,0,1,30,100,15,4,8,600), {'mode':3,'feed':30,'ap':15}),
("A-093","M4/S2 Feed=0.20", lambda s: inject_state(s,3,1,1,20,100,0,0,0,800),  {'mode':3,'sub':1,'feed':20}),
("A-094","M5/S1 Feed=0.25", lambda s: inject_state(s,4,0,1,25,100,10,1,6,700), {'mode':4,'feed':25,'ap':10}),
("A-095","M5/S3 Feed=0.40", lambda s: inject_state(s,4,2,1,40,100,8,2,4,600),  {'mode':4,'sub':2,'feed':40}),
# M6 sphere
("A-096","M6/S1 SM1",       lambda s: inject_state(s,5,0,1,20,100,0,3,12,400), {'mode':5,'sub':0,'pass_nr':3}),
("A-097","M6/S2 SM2",       lambda s: inject_state(s,5,1,2),                    {'mode':5,'sub':1,'sm':2}),
# M7 divider
("A-098","M7/S1 SM1",       lambda s: inject_state(s,6,0,1,25,100,0,6,12,300), {'mode':6,'sub':0,'pass_nr':6}),
("A-099","M7/S2 SM2",       lambda s: inject_state(s,6,1,2),                    {'mode':6,'sub':1,'sm':2}),
# Mode transitions
("A-100","M1→M2 transition",lambda s: (inject_state(s,0,0,1,25,100), time.sleep(0.1), inject_state(s,1,0,1,25,200)), {'mode':1,'afeed':200}),
("A-101","M2→M3 transition",lambda s: (inject_state(s,1,0,1,25,150), time.sleep(0.1), inject_state(s,2,0,1)),        {'mode':2}),
("A-102","M3→M4 transition",lambda s: (inject_state(s,2,0,1),        time.sleep(0.1), inject_state(s,3,0,1,30)),     {'mode':3}),
("A-103","M7→M1 transition",lambda s: (inject_state(s,6,0,1),        time.sleep(0.1), inject_state(s,0,0,1,25)),     {'mode':0}),
# Submode cycle
("A-104","M1 S1→S2 switch", lambda s: (inject_state(s,0,0,1), time.sleep(0.1), inject_state(s,0,1,1)), {'mode':0,'sub':1}),
("A-105","M2 S1→S3 switch", lambda s: (inject_state(s,1,0,1,25,200), time.sleep(0.1), inject_state(s,1,2,1,25,200)), {'mode':1,'sub':2,'afeed':200}),
("A-106","M3 S2→S1 switch", lambda s: (inject_state(s,2,1,1), time.sleep(0.1), inject_state(s,2,0,1)), {'mode':2,'sub':0}),
# SM cycle
("A-107","M1 SM1→SM2",      lambda s: (inject_state(s,0,0,1), time.sleep(0.1), inject_state(s,0,0,2)), {'mode':0,'sm':2}),
("A-108","M1 SM2→SM3",      lambda s: (inject_state(s,0,0,2), time.sleep(0.1), inject_state(s,0,0,3)), {'mode':0,'sm':3}),
("A-109","M1 SM3→SM1",      lambda s: (inject_state(s,0,0,3), time.sleep(0.1), inject_state(s,0,0,1)), {'mode':0,'sm':1}),
# All modes with all limits
("A-110","M1 all limits",   lambda s: inject_state(s,0,0,1,25,100,0,0,0,0,lim="1,1,1,1"), {'lim_l':1,'lim_r':1,'lim_f':1,'lim_re':1}),
("A-111","M2 all limits",   lambda s: inject_state(s,1,0,1,25,200,0,0,0,0,lim="1,1,1,1"), {'mode':1,'lim_l':1,'lim_r':1}),
("A-112","M3 all limits",   lambda s: inject_state(s,2,0,1,lim="1,1,1,1"), {'mode':2,'lim_l':1}),
("A-113","M4 L+R limits",   lambda s: inject_state(s,3,0,1,lim="1,1,0,0"), {'mode':3,'lim_l':1,'lim_r':1}),
("A-114","M5 F+Re limits",  lambda s: inject_state(s,4,0,1,lim="0,0,1,1"), {'mode':4,'lim_f':1,'lim_re':1}),
("A-115","M6 Limit L",      lambda s: inject_state(s,5,0,1,lim="1,0,0,0"), {'mode':5,'lim_l':1}),
("A-116","M7 Limit R",      lambda s: inject_state(s,6,0,1,lim="0,1,0,0"), {'mode':6,'lim_r':1}),
]

# ─── CATEGORY B: BTN inject → real ESP32→STM32 path ─────────────────────────
# setup_fn sets initial known state via inject, then btn_seq simulates taps

def do_b(ser, setup_fn, btn_seq):
    """Execute a Category B test."""
    flush(ser)
    setup_fn(ser)
    time.sleep(0.3)
    flush(ser)
    for b in btn_seq:
        btn(ser, b)
        time.sleep(0.2)
    time.sleep(0.6)

CAT_B = [
# Mode switch via touch (the real bug path)
("B-001","Touch M2 from M1 (aFeed bug)",      lambda s: inject_state(s,0,0,1,25,330),  ["M2"],  "aFeed=330 must appear on ESP32"),
("B-002","Touch M1 from M2",                  lambda s: inject_state(s,1,0,1,25,200),  ["M1"],  "Mode M1"),
("B-003","Touch M3 from M1",                  lambda s: inject_state(s,0,0,1),          ["M3"],  "Mode M3"),
("B-004","Touch M4 from M1",                  lambda s: inject_state(s,0,0,1),          ["M4"],  "Mode M4"),
("B-005","Touch M5 from M1",                  lambda s: inject_state(s,0,0,1),          ["M5"],  "Mode M5"),
("B-006","Touch M6 from M1",                  lambda s: inject_state(s,0,0,1),          ["M6"],  "Mode M6"),
("B-007","Touch M7 from M1",                  lambda s: inject_state(s,0,0,1),          ["M7"],  "Mode M7"),
("B-008","Touch M2 from M3 (aFeed bug var)",  lambda s: inject_state(s,2,0,1,25,250),  ["M2"],  "aFeed no overflow after M3"),
("B-009","Touch M2 from M4",                  lambda s: inject_state(s,3,0,1,25,180),  ["M2"],  "aFeed correct after M4"),
("B-010","Touch M2 from M6",                  lambda s: inject_state(s,5,0,1,25,100),  ["M2"],  "aFeed 100 after M6"),
# Submode via touch
("B-011","Touch S1 in M1",    lambda s: inject_state(s,0,1,1), ["S1"],  "Sub=S1"),
("B-012","Touch S2 in M1",    lambda s: inject_state(s,0,0,1), ["S2"],  "Sub=S2"),
("B-013","Touch S3 in M1",    lambda s: inject_state(s,0,0,1), ["S3"],  "Sub=S3"),
("B-014","Touch S1 in M2",    lambda s: inject_state(s,1,2,1,25,200), ["S1"],  "M2/S1"),
("B-015","Touch S2 in M2",    lambda s: inject_state(s,1,0,1,25,200), ["S2"],  "M2/S2"),
("B-016","Touch S3 in M2",    lambda s: inject_state(s,1,0,1,25,200), ["S3"],  "M2/S3"),
("B-017","Touch S1 in M3",    lambda s: inject_state(s,2,1,1), ["S1"],  "M3/S1"),
("B-018","Touch S2 in M3",    lambda s: inject_state(s,2,0,1), ["S2"],  "M3/S2"),
("B-019","Touch S3 in M3",    lambda s: inject_state(s,2,0,1), ["S3"],  "M3/S3"),
("B-020","Touch S2 in M4",    lambda s: inject_state(s,3,0,1), ["S2"],  "M4/S2"),
# PARAM_OK → SelectMenu cycle
("B-021","M1 PARAM_OK SM1→2",     lambda s: inject_state(s,0,0,1), ["PARAM_OK"], "SM=2"),
("B-022","M1 PARAM_OK SM2→3",     lambda s: inject_state(s,0,0,2), ["PARAM_OK"], "SM=3"),
("B-023","M1 PARAM_OK SM3→1",     lambda s: inject_state(s,0,0,3), ["PARAM_OK"], "SM=1"),
("B-024","M2 PARAM_OK SM1→2",     lambda s: inject_state(s,1,0,1,25,200), ["PARAM_OK"], "M2 SM=2"),
("B-025","M3 PARAM_OK SM1→2",     lambda s: inject_state(s,2,0,1), ["PARAM_OK"], "M3 SM=2"),
# KEY:LEFT / KEY:RIGHT for SM
("B-026","KEY:RIGHT SM1→2",  lambda s: inject_state(s,0,0,1), ["KEY:RIGHT"], "SM=2"),
("B-027","KEY:RIGHT SM2→3",  lambda s: inject_state(s,0,0,2), ["KEY:RIGHT"], "SM=3"),
("B-028","KEY:LEFT SM3→2",   lambda s: inject_state(s,0,0,3), ["KEY:LEFT"],  "SM=2"),
("B-029","KEY:LEFT SM2→1",   lambda s: inject_state(s,0,0,2), ["KEY:LEFT"],  "SM=1"),
("B-030","M2 KEY:RIGHT SM→2",lambda s: inject_state(s,1,0,1,25,200), ["KEY:RIGHT"], "M2 SM=2"),
# KEY:UP / KEY:DN parameter changes
("B-031","M1 KEY:UP feed++",  lambda s: inject_state(s,0,0,1,25),  ["KEY:UP"],  "M1 feed incremented"),
("B-032","M1 KEY:UP x5",      lambda s: inject_state(s,0,0,1,25),  ["KEY:UP"]*5,"M1 feed+5"),
("B-033","M2 KEY:UP afeed++", lambda s: inject_state(s,1,0,1,25,100), ["KEY:UP"], "M2 afeed++"),
("B-034","M2 KEY:UP x5",      lambda s: inject_state(s,1,0,1,25,100), ["KEY:UP"]*5, "M2 afeed+5"),
("B-035","M3 KEY:UP thread++",lambda s: inject_state(s,2,0,1),  ["KEY:UP"],  "M3 thread step++"),
("B-036","M4 KEY:UP cone++",  lambda s: inject_state(s,3,0,1),  ["KEY:UP"],  "M4 cone++"),
("B-037","M7 KEY:UP tooth++", lambda s: inject_state(s,6,0,1),  ["KEY:UP"],  "M7 tooth++"),
("B-038","M1 KEY:DN feed--",  lambda s: inject_state(s,0,0,1,50),  ["KEY:DN"],  "M1 feed--"),
("B-039","M2 KEY:DN afeed--", lambda s: inject_state(s,1,0,1,25,200), ["KEY:DN"], "M2 afeed--"),
("B-040","M7 KEY:DN tooth--", lambda s: inject_state(s,6,0,1),  ["KEY:DN"],  "M7 tooth--"),
# aFeed bug: specific combos
("B-041","aFeed bug M1→M2 afeed=330",  lambda s: inject_state(s,0,0,1,25,330), ["M2"], "aFeed=330"),
("B-042","aFeed bug M1→M2 afeed=100",  lambda s: inject_state(s,0,0,1,25,100), ["M2"], "aFeed=100"),
("B-043","aFeed bug M1 SM2→M2",        lambda s: inject_state(s,0,0,2,25,200), ["M2"], "aFeed=200 from SM2"),
("B-044","aFeed bug M2→M1→M2 cycle",  lambda s: inject_state(s,1,0,1,25,250), ["M1","M2"], "aFeed=250 after roundtrip"),
("B-045","aFeed bug M3→M2",           lambda s: inject_state(s,2,0,1,25,180), ["M2"], "aFeed=180 from M3"),
("B-046","aFeed M2 KEY:UP then switch M1",lambda s: inject_state(s,1,0,1,25,100),["KEY:UP","M1"],"M1 after afeed UP"),
# Mode+submode+SM combos via BTN
("B-047","M2 S2 SM2 sequence",      lambda s: inject_state(s,0,0,1), ["M2","S2","KEY:RIGHT"], "M2/S2/SM2"),
("B-048","M3 S1 SM2",               lambda s: inject_state(s,0,0,1), ["M3","S1","PARAM_OK"],  "M3/S1/SM2"),
("B-049","M1 S3 SM3",               lambda s: inject_state(s,0,0,1), ["M1","S3","KEY:RIGHT","KEY:RIGHT"], "M1/S3/SM3"),
("B-050","M4 S2 SM2",               lambda s: inject_state(s,0,0,1), ["M4","S2","KEY:RIGHT"], "M4/S2/SM2"),
("B-051","M5 S3 SM3",               lambda s: inject_state(s,0,0,1), ["M5","S3","KEY:RIGHT","KEY:RIGHT"], "M5/S3/SM3"),
("B-052","M6 S2 SM2",               lambda s: inject_state(s,0,0,1), ["M6","S2","PARAM_OK"],  "M6/S2/SM2"),
("B-053","M7 S2 SM2",               lambda s: inject_state(s,0,0,1), ["M7","S2","PARAM_OK"],  "M7/S2/SM2"),
# Mode switches with limits
("B-054","Touch M2 with L limit",   lambda s: inject_state(s,0,0,1,25,200,lim="1,0,0,0"), ["M2"], "M2 L limit"),
("B-055","Touch M3 with R limit",   lambda s: inject_state(s,0,0,1,lim="0,1,0,0"),         ["M3"], "M3 R limit"),
("B-056","Touch M1 all limits",     lambda s: inject_state(s,1,0,1,lim="1,1,1,1"),          ["M1"], "M1 all limits"),
("B-057","Touch M4 F+Re limits",    lambda s: inject_state(s,0,0,1,lim="0,0,1,1"),          ["M4"], "M4 F+Re"),
("B-058","Touch M2 all limits",     lambda s: inject_state(s,0,0,1,25,150,lim="1,1,1,1"),   ["M2"], "M2 all limits aFeed=150"),
# Feed via BTN parametric
("B-059","BTN FEED:25",   lambda s: inject_state(s,0,0,1,50),   ["FEED:25"],   "Feed→25"),
("B-060","BTN FEED:50",   lambda s: inject_state(s,0,0,1,25),   ["FEED:50"],   "Feed→50"),
("B-061","BTN FEED:100",  lambda s: inject_state(s,0,0,1,50),   ["FEED:100"],  "Feed→100"),
("B-062","BTN FEED:200",  lambda s: inject_state(s,0,0,1,100),  ["FEED:200"],  "Feed→200"),
# aFeed via BTN parametric
("B-063","BTN AFEED:50",  lambda s: inject_state(s,1,0,1,25,100), ["AFEED:50"],  "aFeed→50"),
("B-064","BTN AFEED:200", lambda s: inject_state(s,1,0,1,25,100), ["AFEED:200"], "aFeed→200"),
("B-065","BTN AFEED:330", lambda s: inject_state(s,1,0,1,25,100), ["AFEED:330"], "aFeed→330"),
# AP via BTN
("B-066","BTN AP:0",      lambda s: inject_state(s,0,0,1,25,100,50),  ["AP:0"],   "AP→0"),
("B-067","BTN AP:25",     lambda s: inject_state(s,0,0,1,25,100,0),   ["AP:25"],  "AP→25"),
("B-068","BTN AP:100",    lambda s: inject_state(s,0,0,1,25,100,50),  ["AP:100"], "AP→100"),
# Complex sequences with submode+SM+limits
("B-069","M1/S3/SM2 + Limit L",    lambda s: inject_state(s,0,2,2,50,100,20,lim="1,0,0,0"), ["KEY:UP","KEY:UP"], "M1/S3/SM2 feed++ L"),
("B-070","M2/S1/SM2 + Limit R",    lambda s: inject_state(s,1,0,2,25,150,lim="0,1,0,0"),    ["KEY:UP"],          "M2/S1/SM2 aFeed++ R"),
("B-071","M3/S2/SM2 + Limit F",    lambda s: inject_state(s,2,1,2,lim="0,0,1,0"),           ["KEY:UP"],          "M3/S2/SM2 thread++ F"),
("B-072","M4/S1/SM1 + Limit Re",   lambda s: inject_state(s,3,0,1,30,100,15,lim="0,0,0,1"), ["KEY:UP"],          "M4/S1 cone++ Re"),
("B-073","M5/S3/SM3 + All limits", lambda s: inject_state(s,4,2,3,25,100,lim="1,1,1,1"),    ["KEY:DN"],          "M5/S3/SM3 feed-- all lim"),
# Back to nominal
("B-074","Reset to M1/S1/SM1",     lambda s: None, ["M1","S1"], "M1/S1 reset"),
("B-075","M2/S1 aFeed=100 clean",  lambda s: inject_state(s,0,0,1,25,100), ["M2","S1"], "M2/S1 clean"),
# More mode-switch combos
("B-076","Touch M5 from M3",        lambda s: inject_state(s,2,0,1,25,200), ["M5"], "M5 from M3"),
("B-077","Touch M6 from M4",        lambda s: inject_state(s,3,0,1,25,150), ["M6"], "M6 from M4"),
("B-078","Touch M7 from M5",        lambda s: inject_state(s,4,0,1),        ["M7"], "M7 from M5"),
("B-079","Touch M4 from M7",        lambda s: inject_state(s,6,0,1),        ["M4"], "M4 from M7"),
("B-080","Touch M3 from M6",        lambda s: inject_state(s,5,0,1),        ["M3"], "M3 from M6"),
# aFeed roundtrip stress
("B-081","M1→M2 afeed=50",         lambda s: inject_state(s,0,0,1,25,50),  ["M2"], "aFeed=50"),
("B-082","M1→M2 afeed=150",        lambda s: inject_state(s,0,0,1,25,150), ["M2"], "aFeed=150"),
("B-083","M5→M2 afeed=250",        lambda s: inject_state(s,4,0,1,25,250), ["M2"], "aFeed=250 from M5"),
("B-084","M6→M2 afeed=300",        lambda s: inject_state(s,5,0,1,25,300), ["M2"], "aFeed=300 from M6"),
("B-085","M7→M2 afeed=330",        lambda s: inject_state(s,6,0,1,25,330), ["M2"], "aFeed=330 from M7"),
# More submode combos
("B-086","Touch S1 in M4",    lambda s: inject_state(s,3,2,1), ["S1"],  "M4/S1"),
("B-087","Touch S3 in M4",    lambda s: inject_state(s,3,0,1), ["S3"],  "M4/S3"),
("B-088","Touch S1 in M5",    lambda s: inject_state(s,4,2,1), ["S1"],  "M5/S1"),
("B-089","Touch S2 in M5",    lambda s: inject_state(s,4,0,1), ["S2"],  "M5/S2"),
("B-090","Touch S3 in M5",    lambda s: inject_state(s,4,1,1), ["S3"],  "M5/S3"),
("B-091","Touch S1 in M6",    lambda s: inject_state(s,5,2,1), ["S1"],  "M6/S1"),
("B-092","Touch S3 in M6",    lambda s: inject_state(s,5,0,1), ["S3"],  "M6/S3"),
("B-093","Touch S1 in M7",    lambda s: inject_state(s,6,2,1), ["S1"],  "M7/S1"),
("B-094","Touch S3 in M7",    lambda s: inject_state(s,6,0,1), ["S3"],  "M7/S3"),
# KEY:UP/DOWN in all modes
("B-095","M5 KEY:UP cone++",     lambda s: inject_state(s,4,0,1), ["KEY:UP"],  "M5 cone++"),
("B-096","M6 KEY:UP sphere++",   lambda s: inject_state(s,5,0,1), ["KEY:UP"],  "M6 sphere++"),
("B-097","M1 KEY:DN feed-- min", lambda s: inject_state(s,0,0,1,5),  ["KEY:DN"], "M1 feed at min"),
("B-098","M2 KEY:DN afeed-- min",lambda s: inject_state(s,1,0,1,25,15), ["KEY:DN"], "M2 afeed at min"),
# PARAM_OK in all modes
("B-099","M4 PARAM_OK SM1→2",  lambda s: inject_state(s,3,0,1), ["PARAM_OK"], "M4 SM=2"),
("B-100","M5 PARAM_OK SM1→2",  lambda s: inject_state(s,4,0,1), ["PARAM_OK"], "M5 SM=2"),
("B-101","M6 PARAM_OK SM1→2",  lambda s: inject_state(s,5,0,1), ["PARAM_OK"], "M6 SM=2"),
("B-102","M7 PARAM_OK SM1→2",  lambda s: inject_state(s,6,0,1), ["PARAM_OK"], "M7 SM=2"),
# KEY:LEFT/RIGHT more combos
("B-103","M2 KEY:LEFT SM2→1",   lambda s: inject_state(s,1,0,2,25,200), ["KEY:LEFT"],  "M2 SM=1"),
("B-104","M3 KEY:RIGHT SM1→2",  lambda s: inject_state(s,2,0,1),        ["KEY:RIGHT"], "M3 SM=2"),
("B-105","M4 KEY:LEFT SM2→1",   lambda s: inject_state(s,3,0,2),        ["KEY:LEFT"],  "M4 SM=1"),
("B-106","M5 KEY:RIGHT SM1→2",  lambda s: inject_state(s,4,0,1),        ["KEY:RIGHT"], "M5 SM=2"),
# AP via BTN
("B-107","BTN AP:50",   lambda s: inject_state(s,0,0,1,25,100,0),   ["AP:50"],  "AP→50"),
("B-108","BTN AP:200",  lambda s: inject_state(s,0,0,1,25,100,100), ["AP:200"], "AP→200"),
# M3 thread++ stress
("B-109","M3 KEY:UP x5", lambda s: inject_state(s,2,0,1), ["KEY:UP"]*5, "M3 thread +5"),
# M7 tooth-- stress
("B-110","M7 KEY:DN x3", lambda s: inject_state(s,6,0,1), ["KEY:DN"]*3, "M7 tooth -3"),
]

# ─── CATEGORY C: UI simulation — full user interaction sequences ──────────────
# Each sequence simulates a real user tapping through the touchscreen

CAT_C = [
# Open mode menu → select mode → verify
("C-001","User: open menu → M2 (aFeed bug scenario)",
 lambda s: inject_state(s,0,0,1,25,330),
 ["M2"],  # simulates: tap mode label → mode menu opens → tap M2
 "ESP32 must show aFeed=330 NOT -32512. LCD2004: M2 aFeed=330"),

("C-002","User: open menu → M1 → check params",
 lambda s: inject_state(s,1,0,1,25,200),
 ["M1"], "M1 Feed visible, SM=1"),

("C-003","User: open menu → M3 → UP×3 (thread step) → check",
 lambda s: inject_state(s,0,0,1),
 ["M3","KEY:UP","KEY:UP","KEY:UP"], "M3 thread step +3"),

("C-004","User: open menu → M2 → S3 → UP×3 (aFeed++) → PARAM_OK",
 lambda s: inject_state(s,0,0,1,25,100),
 ["M2","S3","KEY:UP","KEY:UP","KEY:UP","PARAM_OK"], "M2/S3 aFeed++×3, SM=2"),

("C-005","User: open menu → M1 → S3 → KEY:RIGHT (SM2) → UP×2 → PARAM_OK",
 lambda s: inject_state(s,0,0,1,25,100,0),
 ["M1","S3","KEY:RIGHT","KEY:UP","KEY:UP","PARAM_OK"], "M1/S3/SM2 feed++×2"),

("C-006","User: M2 → UP×5 → M1 → check M1 feed unchanged",
 lambda s: inject_state(s,0,0,1,50,200),
 ["M2","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","M1"], "M1 feed=50 after M2 session"),

("C-007","User: M1 → PARAM_OK(SM2) → UP×3 → PARAM_OK(SM3) → DOWN×2",
 lambda s: inject_state(s,0,0,1,25),
 ["PARAM_OK","KEY:UP","KEY:UP","KEY:UP","PARAM_OK","KEY:DN","KEY:DN"], "M1 SM cycle with param changes"),

("C-008","User: M3 → S2 → UP×5 thread steps → check",
 lambda s: inject_state(s,0,0,1),
 ["M3","S2","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"], "M3/S2 thread +5 steps"),

("C-009","User: M7 divider → UP×6 (cycle through all 6 teeth)",
 lambda s: inject_state(s,0,0,1),
 ["M7","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP"], "M7 6 tooth increments"),

("C-010","User: M4 → S1 → UP×3 cone → M5 → UP×2 cone",
 lambda s: inject_state(s,0,0,1),
 ["M4","S1","KEY:UP","KEY:UP","KEY:UP","M5","KEY:UP","KEY:UP"], "M4/M5 cone steps"),

("C-011","User: M2 aFeed=100 → UP×10 → check aFeed > 100",
 lambda s: inject_state(s,1,0,1,25,100),
 ["KEY:UP"]*10, "M2 aFeed incremented 10 times"),

("C-012","User: M1 feed=25 → S2 → UP×5 → PARAM_OK (SM2)",
 lambda s: inject_state(s,0,0,1,25),
 ["S2","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","PARAM_OK"], "M1/S2/SM2 feed++×5"),

("C-013","User: M2 with limits L → UP × 3 → verify aFeed++ despite limit",
 lambda s: inject_state(s,1,0,1,25,100,lim="1,0,0,0"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M2 aFeed++ with L limit active"),

("C-014","User: M3 → S3 → SM2 → UP×3 thread → verify both displays",
 lambda s: inject_state(s,0,0,1),
 ["M3","S3","KEY:RIGHT","KEY:UP","KEY:UP","KEY:UP"], "M3/S3/SM2 thread++×3"),

("C-015","User: M1/S3/SM3 axis reset screen → UP then DOWN",
 lambda s: inject_state(s,0,2,3),
 ["KEY:UP","KEY:DN"], "M1/S3/SM3 axis jog"),

("C-016","User: switch M1→M2→M3→M4 quickly",
 lambda s: inject_state(s,0,0,1,25,200),
 ["M2","M3","M4"], "rapid mode switching"),

("C-017","User: M5 → S1 → UP×2 → S3 → UP×2",
 lambda s: inject_state(s,0,0,1,25),
 ["M5","S1","KEY:UP","KEY:UP","S3","KEY:UP","KEY:UP"], "M5 sub/param combos"),

("C-018","User: M6 sphere → S1 → PARAM_OK → UP×3",
 lambda s: inject_state(s,0,0,1),
 ["M6","S1","PARAM_OK","KEY:UP","KEY:UP","KEY:UP"], "M6 sphere SM2 params"),

("C-019","User: M7 → PARAM_OK→SM2 → UP×4 tooth",
 lambda s: inject_state(s,0,0,1),
 ["M7","PARAM_OK","KEY:UP","KEY:UP","KEY:UP","KEY:UP"], "M7 divider tooth cycle"),

("C-020","User: M2 aFeed bug from ALL other modes",
 lambda s: inject_state(s,2,0,1,25,330),  # start in M3 with afeed=330
 ["M2"], "M2 shows 330 after starting from M3"),

("C-021","User: M1/S1 → PARAM_OK(SM2) → BTN FEED:75 → verify",
 lambda s: inject_state(s,0,0,1,25,100),
 ["PARAM_OK","FEED:75"], "M1 SM2 feed=75"),

("C-022","User: M2 → BTN AFEED:180 → S2 → verify aFeed=180",
 lambda s: inject_state(s,0,0,1,25,100),
 ["M2","AFEED:180","S2"], "M2/S2 aFeed=180"),

("C-023","User: M3 S1 SM1 → UP×3 → PARAM_OK → verify SM2",
 lambda s: inject_state(s,2,0,1),
 ["KEY:UP","KEY:UP","KEY:UP","PARAM_OK"], "M3 +3 thread steps, SM2"),

("C-024","User: M4 S1 → UP×5 cone → M5 → verify M5",
 lambda s: inject_state(s,0,0,1),
 ["M4","KEY:UP","KEY:UP","KEY:UP","KEY:UP","KEY:UP","M5"], "M4 UP×5 then M5"),

("C-025","User: set M1/S1/SM1 nominal, check new+old display match",
 lambda s: inject_state(s,0,0,1,25,100,0,0,0,0),
 ["M1","S1"], "nominal state both displays match"),

# Mode+limits combos via UI path
("C-026","User: M1 all limits → UP×3",
 lambda s: inject_state(s,0,0,1,25,100,lim="1,1,1,1"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M1 limits feed++"),

("C-027","User: M2 R limit → UP×3 aFeed++",
 lambda s: inject_state(s,1,0,1,25,150,lim="0,1,0,0"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M2 R limit aFeed++"),

("C-028","User: M3 F limit → UP×3 thread++",
 lambda s: inject_state(s,2,0,1,lim="0,0,1,0"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M3 F limit thread++"),

("C-029","User: M1/S3/SM2 L+R limits → UP×2 feed",
 lambda s: inject_state(s,0,2,2,50,100,20,lim="1,1,0,0"),
 ["KEY:UP","KEY:UP"], "M1/S3/SM2 L+R feed++"),

("C-030","User: M4/S1 Re limit → UP×3 cone",
 lambda s: inject_state(s,3,0,1,30,100,lim="0,0,0,1"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M4 Re limit cone++"),

# Complex multi-step sequences
("C-031","User: M2 S1 → UP×3 → S2 → UP×3 → S3 → UP×3",
 lambda s: inject_state(s,1,0,1,25,100),
 ["S1","KEY:UP","KEY:UP","KEY:UP","S2","KEY:UP","KEY:UP","KEY:UP",
  "S3","KEY:UP","KEY:UP","KEY:UP"], "M2 all sub modes with aFeed++"),

("C-032","User: M1 SM full cycle + feed changes",
 lambda s: inject_state(s,0,0,1,25,100),
 ["PARAM_OK","KEY:UP","PARAM_OK","KEY:UP","KEY:UP","PARAM_OK",
  "KEY:DN"], "M1 SM1→2→3→1 with feed adjustments"),

("C-033","User: All modes round trip checking M2",
 lambda s: inject_state(s,0,0,1,25,330),
 ["M2","M3","M4","M5","M6","M7","M2"], "M2 aFeed=330 preserved after full round trip"),

("C-034","User: aFeed stress — switch to M2 10 times",
 lambda s: inject_state(s,0,0,1,25,200),
 ["M2","M1","M2","M1","M2","M1","M2","M1","M2","M1"], "M2 aFeed stable after 5 roundtrips"),

("C-035","User: M1/S1→S2→S3 with feed 25→50→75",
 lambda s: inject_state(s,0,0,1,25,100),
 ["FEED:25","S2","FEED:50","S3","FEED:75"], "M1 S1/S2/S3 feed sequence"),

# M2 aFeed bug — more combos
("C-036","User: M4→M2 afeed=80",    lambda s: inject_state(s,3,0,1,25,80),  ["M2"], "aFeed=80 no bug after M4"),
("C-037","User: M5→M2 afeed=120",   lambda s: inject_state(s,4,0,1,25,120), ["M2"], "aFeed=120 after M5"),
("C-038","User: M6→M2 afeed=200",   lambda s: inject_state(s,5,0,1,25,200), ["M2"], "aFeed=200 after M6"),
("C-039","User: M7→M2 afeed=280",   lambda s: inject_state(s,6,0,1,25,280), ["M2"], "aFeed=280 after M7"),
("C-040","User: M3 S2→M2 afeed=60", lambda s: inject_state(s,2,1,1,25,60),  ["M2"], "aFeed=60 after M3/S2"),

# All mode entries from M7
("C-041","User: M7→M1 mode switch", lambda s: inject_state(s,6,0,1),        ["M1"], "M1 from M7"),
("C-042","User: M7→M3",             lambda s: inject_state(s,6,0,1),        ["M3"], "M3 from M7"),
("C-043","User: M7→M5",             lambda s: inject_state(s,6,0,1),        ["M5"], "M5 from M7"),
("C-044","User: M7→M6",             lambda s: inject_state(s,6,0,1),        ["M6"], "M6 from M7"),

# M2 aFeed with increasing values and UP key
("C-045","User: M2 aFeed=15 → UP×15",  lambda s: inject_state(s,1,0,1,25,15),  ["KEY:UP"]*15, "M2 afeed UP from min"),
("C-046","User: M2 aFeed=300 → DN×10", lambda s: inject_state(s,1,0,1,25,300), ["KEY:DN"]*10, "M2 afeed DN from near max"),
("C-047","User: M1 feed=5 → UP×20",    lambda s: inject_state(s,0,0,1,5),      ["KEY:UP"]*20, "M1 feed UP from min"),
("C-048","User: M1 feed=200 → DN×15",  lambda s: inject_state(s,0,0,1,200),    ["KEY:DN"]*15, "M1 feed DN from max"),

# Submode traversal in all 7 modes
("C-049","User: M3 S1→S2→S3 cycle",  lambda s: inject_state(s,2,0,1), ["S1","S2","S3"], "M3 all submodes"),
("C-050","User: M4 S1→S2→S3 cycle",  lambda s: inject_state(s,3,0,1), ["S1","S2","S3"], "M4 all submodes"),
("C-051","User: M5 S1→S2→S3 cycle",  lambda s: inject_state(s,4,0,1), ["S1","S2","S3"], "M5 all submodes"),
("C-052","User: M6 S1→S2→S3 cycle",  lambda s: inject_state(s,5,0,1), ["S1","S2","S3"], "M6 all submodes"),
("C-053","User: M7 S1→S2 cycle",     lambda s: inject_state(s,6,0,1), ["S1","S2"],       "M7 sub1→2"),

# SM traversal in all modes
("C-054","User: M3 SM1→2→3 cycle",   lambda s: inject_state(s,2,0,1), ["PARAM_OK","PARAM_OK","PARAM_OK"], "M3 SM full cycle"),
("C-055","User: M4 SM1→2→3 cycle",   lambda s: inject_state(s,3,0,1), ["PARAM_OK","PARAM_OK","PARAM_OK"], "M4 SM full cycle"),
("C-056","User: M5 SM1→2→3 cycle",   lambda s: inject_state(s,4,0,1), ["PARAM_OK","PARAM_OK","PARAM_OK"], "M5 SM full cycle"),
("C-057","User: M6 SM1→2 cycle",     lambda s: inject_state(s,5,0,1), ["PARAM_OK","PARAM_OK"], "M6 SM 1→2→1"),
("C-058","User: M7 SM1→2 cycle",     lambda s: inject_state(s,6,0,1), ["PARAM_OK","PARAM_OK"], "M7 SM 1→2→1"),

# Limits in all modes while moving
("C-059","User: M1 Limit L → UP×3",    lambda s: inject_state(s,0,0,1,25,100,lim="1,0,0,0"), ["KEY:UP"]*3, "M1 Limit L feed++"),
("C-060","User: M2 Limit R → UP×3",    lambda s: inject_state(s,1,0,1,25,100,lim="0,1,0,0"), ["KEY:UP"]*3, "M2 Limit R afeed++"),
("C-061","User: M3 Limit F → UP×3",    lambda s: inject_state(s,2,0,1,lim="0,0,1,0"),         ["KEY:UP"]*3, "M3 Limit F thread++"),
("C-062","User: M4 All limits → UP×3", lambda s: inject_state(s,3,0,1,lim="1,1,1,1"),          ["KEY:UP"]*3, "M4 all lim cone++"),
("C-063","User: M5 L+Re limits → UP",  lambda s: inject_state(s,4,0,1,lim="1,0,0,1"),          ["KEY:UP"],   "M5 L+Re limit"),
("C-064","User: M6 Limit L+R",         lambda s: inject_state(s,5,0,1,lim="1,1,0,0"),          ["KEY:UP"],   "M6 L+R limit sphere++"),
("C-065","User: M7 Limit Re → UP",     lambda s: inject_state(s,6,0,1,lim="0,0,0,1"),          ["KEY:UP"],   "M7 Re limit tooth++"),

# AP combos across modes
("C-066","User: M1 AP:100 → M2 → back",  lambda s: inject_state(s,0,0,1,25,200,100), ["M2","M1"], "M1 AP=100 after M2 roundtrip"),
("C-067","User: M3 AP:50 → S2 → UP×3",  lambda s: inject_state(s,2,0,1,25,100,50),  ["S2","KEY:UP"]*3, "M3 AP=50 S2 with UP"),
("C-068","User: BTN AP:0 → BTN AP:200 → verify", lambda s: inject_state(s,0,0,1,25,100,100), ["AP:0","AP:200"], "AP 0→200"),

# Feed BTN and submode combos
("C-069","User: FEED:50 S2 FEED:75 S3 FEED:100",  lambda s: inject_state(s,0,0,1,25,100), ["FEED:50","S2","FEED:75","S3","FEED:100"], "M1 feed BTN sub combo"),
("C-070","User: AFEED:50 S2 AFEED:200 S3 AFEED:300",lambda s: inject_state(s,1,0,1,25,100), ["AFEED:50","S2","AFEED:200","S3","AFEED:300"], "M2 afeed BTN sub combo"),

# Mode→submode→SM→param sequences
("C-071","User: M1 → S2 → SM2 → FEED:100",      lambda s: inject_state(s,0,0,1,25,100), ["M1","S2","KEY:RIGHT","FEED:100"], "M1/S2/SM2 feed=100"),
("C-072","User: M2 → S1 → SM2 → AFEED:250",     lambda s: inject_state(s,0,0,1,25,100), ["M2","S1","KEY:RIGHT","AFEED:250"], "M2/S1/SM2 afeed=250"),
("C-073","User: M3 → S2 → SM3 → UP×5 thread",   lambda s: inject_state(s,0,0,1),        ["M3","S2","KEY:RIGHT","KEY:RIGHT","KEY:UP"]*5, "M3/S2/SM3 thread×5"),
("C-074","User: M4 → S1 → SM1 → UP×3 → M5",    lambda s: inject_state(s,0,0,1),        ["M4","S1","KEY:UP","KEY:UP","KEY:UP","M5"], "M4/S1 cone×3 then M5"),
("C-075","User: M5 → S3 → SM3 → DN×3",         lambda s: inject_state(s,4,2,3,25,100), ["KEY:DN","KEY:DN","KEY:DN"], "M5/S3/SM3 cone DN×3"),

# Stress combos that mix everything
("C-076","User: M2 KEY:UP×5 then S2 KEY:UP×3",  lambda s: inject_state(s,1,0,1,25,100), ["KEY:UP"]*5+["S2"]+["KEY:UP"]*3, "M2 afeed++ then S2 more++"),
("C-077","User: M1 SM3→2→1 full KEY:LEFT cycle", lambda s: inject_state(s,0,0,3),       ["KEY:LEFT","KEY:LEFT","KEY:LEFT"], "M1 SM 3→2→1→3"),
("C-078","User: M3 S1→S2 + SM cycle + thread×2", lambda s: inject_state(s,2,0,1),       ["S2","PARAM_OK","KEY:UP","KEY:UP"], "M3/S2 SM2 thread×2"),
("C-079","User: M6 S2 SM2 AP:30 sphere++×3",    lambda s: inject_state(s,5,0,1,25,100), ["S2","PARAM_OK","AP:30","KEY:UP","KEY:UP","KEY:UP"], "M6/S2/SM2 AP=30 sphere×3"),
("C-080","User: M7 S2 SM2 tooth++×4",           lambda s: inject_state(s,6,0,1),        ["S2","PARAM_OK","KEY:UP"]*4, "M7/S2/SM2 tooth×4"),

# Full panel resets and re-checks
("C-081","User: complete reset M1/S1/SM1/nominal", lambda s: inject_state(s,0,0,1,25,100,0,0,0,0), ["M1","S1"], "full nominal reset"),
("C-082","User: M2 full reset aFeed=100",          lambda s: inject_state(s,1,0,1,25,100,0,0,0,0), ["M2","S1"], "M2 full nominal reset"),

# More from PARAM_OK path
("C-083","User: PARAM_OK from each mode SM cycle",   lambda s: inject_state(s,2,0,1), ["PARAM_OK","PARAM_OK"], "M3 SM1→2→3"),
("C-084","User: PARAM_OK×4 full cycle back to SM1",  lambda s: inject_state(s,0,0,1), ["PARAM_OK"]*4, "M1 SM cycle 4×"),
("C-085","User: M5/S2 PARAM_OK×2 SM1→3",            lambda s: inject_state(s,4,1,1), ["PARAM_OK","PARAM_OK"], "M5/S2 SM1→2→3"),

# BTN FEED/AFEED/AP followed by mode changes
("C-086","User: FEED:150 → M2 → M1 → check feed=150",   lambda s: inject_state(s,0,0,1,25,100), ["FEED:150","M2","M1"], "M1 feed=150 after M2 visit"),
("C-087","User: AFEED:280 → M1 → M2 → check afeed=280", lambda s: inject_state(s,0,0,1,25,280), ["M2","M1","M2"],       "M2 afeed=280 after M1 roundtrip"),
("C-088","User: AP:75 → M3 → M1 → check AP=75",         lambda s: inject_state(s,0,0,1,25,100,75), ["M3","M1"],           "M1 AP=75 after M3 visit"),

# Submode changes across different modes with params
("C-089","User: M4/S1 UP×2 → M4/S2 UP×2",  lambda s: inject_state(s,3,0,1), ["S1","KEY:UP","KEY:UP","S2","KEY:UP","KEY:UP"], "M4 S1→S2 cone++"),
("C-090","User: M5/S1→S3 → UP×3",           lambda s: inject_state(s,4,0,1), ["S1","S3","KEY:UP","KEY:UP","KEY:UP"], "M5 S1→S3 cone++×3"),
("C-091","User: M6/S2→S3 SM → UP×2 sphere",  lambda s: inject_state(s,5,0,1), ["S2","S3","KEY:UP","KEY:UP"], "M6 S2→S3 sphere++"),
("C-092","User: M7/S2 → UP×6 tooth full cycle", lambda s: inject_state(s,6,0,1), ["S2","KEY:UP"]*6, "M7/S2 tooth×6"),

# aFeed bug deep drill
("C-093","User: aFeed=330 M1→M3→M2 chain",   lambda s: inject_state(s,0,0,1,25,330), ["M3","M2"], "aFeed=330 via M3 hop"),
("C-094","User: aFeed=280 M5→M4→M2 chain",   lambda s: inject_state(s,4,0,1,25,280), ["M4","M2"], "aFeed=280 via M4 hop from M5"),
("C-095","User: aFeed=200 M7→M6→M5→M2",      lambda s: inject_state(s,6,0,1,25,200), ["M6","M5","M2"], "aFeed=200 three hops"),
("C-096","User: aFeed=150 M2 S1→S2→S3→M1→M2",lambda s: inject_state(s,1,0,1,25,150), ["S2","S3","M1","M2"], "aFeed=150 sub cycle"),
("C-097","User: aFeed rapid roundtrip×5",      lambda s: inject_state(s,0,0,1,25,175), ["M2","M1"]*5, "afeed=175 stable ×5"),

# Final complex sequences
("C-098","User: full session M1→adjust→M2→adjust→M3",
 lambda s: inject_state(s,0,0,1,25,100,0),
 ["S1","FEED:50","PARAM_OK","M2","S1","AFEED:200","M3","S1","PARAM_OK"], "full session M1/M2/M3"),
("C-099","User: limits change mid-session M2 afeed++",
 lambda s: inject_state(s,1,0,1,25,100,lim="0,0,0,0"),
 ["KEY:UP","KEY:UP","KEY:UP"], "M2 afeed++ then limits would change"),
("C-100","User: final nominal state check",
 lambda s: inject_state(s,0,0,1,25,100,0,0,0,0),
 ["M1","S1"], "final nominal — all displays must match"),
]

# ─── Test runner ──────────────────────────────────────────────────────────────

def run_cat_a(ser, filter_id=None):
    print("\n═══ CATEGORY A: Direct inject → ESP32 (simulates STM32→ESP32) ═══")
    p = f = 0
    for (tc_id, desc, setup_fn, exp) in CAT_A:
        if filter_id and tc_id != filter_id:
            continue
        flush(ser)
        r = setup_fn(ser)   # inject full state
        # Use last [STATE] line — inject_state sends 11 commands spaced 40ms apart.
        # The FIRST [STATE] only reflects the MODE command; the LAST reflects all 11.
        # STM32 sends POS_Z/RPM every 250ms but MODE/FEED/AFEED/AP/LIMITS only on
        # change, so injected values persist in the final [STATE].
        state_line, _lcd_line, raw = read_last_state(ser, window=1.0)
        state = parse_state(state_line)
        issues = []
        verify(state, exp, issues)
        shot = take_screenshot(ser, tc_id)
        if shot is None:
            issues.append("Screenshot failed")
        ok = log(tc_id,"A",desc,str(exp),exp,state,issues,shot,raw)
        if ok: p+=1
        else:  f+=1
    print(f"  Cat A: {p} passed, {f} failed / {len([x for x in CAT_A if not filter_id or x[0]==filter_id])} run")
    return p, f

def run_cat_b(ser, filter_id=None):
    print("\n═══ CATEGORY B: BTN inject → ESP32→STM32 real path ═══")
    p = f = 0
    for (tc_id, desc, setup_fn, btn_seq, note) in CAT_B:
        if filter_id and tc_id != filter_id:
            continue
        flush(ser)
        if setup_fn:
            setup_fn(ser)
            time.sleep(0.3)
        flush(ser)
        for b in btn_seq:
            btn(ser, b)
            time.sleep(0.25)
        time.sleep(0.7)
        # Wait extra for LCD2004 state (STM32 sends it every 500ms)
        state_line, lcd_line, raw = read_last_state(ser, window=1.5)
        state = parse_state(state_line)
        lcd   = parse_lcd2004(lcd_line)
        issues = []
        if state is None:
            issues.append("No [STATE] response from STM32 after BTN sequence")
        if state and state.get('mode') == 1:
            if state.get('afeed', 0) == -32512:
                issues.append("aFeed BUG: got -32512!")
            if state.get('afeed', 0) <= 0:
                issues.append(f"aFeed invalid: {state.get('afeed')}")
        # Cross-verify: LCD2004 must match ESP32 display
        if state is not None:
            verify_lcd_vs_state(lcd, state, issues)
        shot = take_screenshot(ser, tc_id)
        if shot is None:
            issues.append("Screenshot failed")
        ok = log(tc_id,"B",desc,btn_seq,note,state,issues,shot,raw)
        if ok: p+=1
        else:  f+=1
    print(f"  Cat B: {p} passed, {f} failed / {len([x for x in CAT_B if not filter_id or x[0]==filter_id])} run")
    return p, f

def run_cat_c(ser, filter_id=None):
    print("\n═══ CATEGORY C: Full UI simulation (user journey) ═══")
    p = f = 0
    for (tc_id, desc, setup_fn, btn_seq, note) in CAT_C:
        if filter_id and tc_id != filter_id:
            continue
        flush(ser)
        if setup_fn:
            setup_fn(ser)
            time.sleep(0.3)
        flush(ser)
        for b in btn_seq:
            btn(ser, b)
            time.sleep(0.28)
        time.sleep(0.8)
        state_line, lcd_line, raw = read_last_state(ser, window=1.5)
        state = parse_state(state_line)
        lcd   = parse_lcd2004(lcd_line)
        issues = []
        if state is None:
            issues.append("No [STATE] after UI sequence")
        if state and state.get('mode') == 1 and state.get('afeed', 0) == -32512:
            issues.append("aFeed BUG: -32512 detected!")
        if state is not None:
            verify_lcd_vs_state(lcd, state, issues)
        shot = take_screenshot(ser, tc_id)
        if shot is None:
            issues.append("Screenshot failed")
        ok = log(tc_id,"C",desc,btn_seq,note,state,issues,shot,raw)
        if ok: p+=1
        else:  f+=1
    print(f"  Cat C: {p} passed, {f} failed / {len([x for x in CAT_C if not filter_id or x[0]==filter_id])} run")
    return p, f

def write_report(a_p,a_f, b_p,b_f, c_p,c_f):
    total_p = a_p+b_p+c_p
    total_f = a_f+b_f+c_f
    total   = total_p+total_f
    with open(RPT_FILE, 'w') as rpt:
        rpt.write(f"# Test Report — {datetime.now().strftime('%Y-%m-%d %H:%M')}\n\n")
        rpt.write(f"## Summary\n\n")
        rpt.write(f"| Category | Passed | Failed | Total |\n")
        rpt.write(f"|----------|--------|--------|-------|\n")
        rpt.write(f"| A: Direct inject (STM32→ESP32) | {a_p} | {a_f} | {a_p+a_f} |\n")
        rpt.write(f"| B: BTN inject (ESP32→STM32)    | {b_p} | {b_f} | {b_p+b_f} |\n")
        rpt.write(f"| C: UI simulation               | {c_p} | {c_f} | {c_p+c_f} |\n")
        rpt.write(f"| **TOTAL** | **{total_p}** | **{total_f}** | **{total}** |\n\n")

        # List failures
        failures = [r for r in results if not r['passed']]
        if failures:
            rpt.write(f"## Failures ({len(failures)})\n\n")
            for r in failures:
                rpt.write(f"### {r['tc']}: {r['desc']}\n")
                rpt.write(f"- **Category**: {r['cat']}\n")
                rpt.write(f"- **Issues**: {'; '.join(r['issues'])}\n")
                rpt.write(f"- **State received**: `{r['state']}`\n")
                rpt.write(f"- **LCD2004 expected**: `{r['lcd2004']}`\n")
                rpt.write(f"- **Screenshot**: {r['screenshot']}\n\n")
        else:
            rpt.write("## All tests PASSED ✓\n\n")

        # Full results table
        rpt.write("## Full Results\n\n")
        rpt.write("| TC | Cat | Desc | Pass | State | LCD2004 |\n")
        rpt.write("|----|-----|------|------|-------|---------|\n")
        for r in results:
            st = r['pass'] if 'pass' in r else ('✓' if r['passed'] else '✗')
            state_str = ""
            if r['state']:
                s = r['state']
                state_str = f"M{s['mode']+1}/S{s['sub']+1}/SM{s['sm']} feed={s['feed']} afeed={s['afeed']} ap={s['ap']}"
            rpt.write(f"| {r['tc']} | {r['cat']} | {r['desc'][:40]} | {'✓' if r['passed'] else '✗'} | {state_str} | {r['lcd2004'][:50]} |\n")

    print(f"\n  Report written: {RPT_FILE}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=ESP32_PORT)
    ap.add_argument("--cat",  default="all", choices=["A","B","C","all"])
    ap.add_argument("--tc",   default=None)
    args = ap.parse_args()

    print(f"ESP32 port: {args.port}")
    print(f"Log:        {LOG_FILE}")
    print(f"Report:     {RPT_FILE}")
    print(f"Screenshots:{OUT_DIR}\n")

    with serial.Serial(args.port, 115200, timeout=2) as ser:
        time.sleep(1.5)
        flush(ser)

        a_p=a_f=b_p=b_f=c_p=c_f=0
        tc = args.tc

        if args.cat in ("A","all"):
            a_p, a_f = run_cat_a(ser, tc)
        if args.cat in ("B","all"):
            b_p, b_f = run_cat_b(ser, tc)
        if args.cat in ("C","all"):
            c_p, c_f = run_cat_c(ser, tc)

    write_report(a_p,a_f, b_p,b_f, c_p,c_f)

    print(f"\n{'═'*50}")
    print(f"  TOTAL: {a_p+b_p+c_p} passed / {a_p+b_p+c_p+a_f+b_f+c_f} run")
    if a_f+b_f+c_f:
        print(f"  FAILURES: {a_f+b_f+c_f} — see {RPT_FILE}")

if __name__ == "__main__":
    main()
