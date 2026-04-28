# TEST_CASES — ELS STM32 ↔ ESP32 Display Integration

> Protocol: STM32→ESP32 `<CMD:value>\n` | ESP32→STM32 `<TOUCH:CMD>`
> STM32 ELS_Mode_t: 0-based (MODE_FEED=0). ESP32 LatheMode: 1-based (MODE_FEED=1). STM32 sends mode+1.
> Units: Feed_mm = мм/об × 100; aFeed_mm = мм/мин (raw integer); THREAD = pitch × 100; positions = 0.001 мм.

---

## MODE_FEED (M1) — Синхронная подача

### TC-001: M1/S1 basic feed display
**Input**: `<MODE:1>` `<SUBMODE:1>` `<FEED:10>` `<POS_Z:56780>` `<POS_X:-8500>`
**Expected**: primary_val = "0.10"; pos_Z = "+56.78"; pos_X = "-8.50"
**Field**: primary_val, row1_val, row2_val

### TC-002: M1/S2 manual submode
**Input**: `<MODE:1>` `<SUBMODE:2>` `<FEED:20>`
**Expected**: submode indicator shows S2; primary_val = "0.20"
**Field**: submode_lbl, primary_val

### TC-003: M1/S3 external submode feed 0.30
**Input**: `<MODE:1>` `<SUBMODE:3>` `<FEED:30>` `<PASS:3,8>` `<AP:15>`
**Expected**: primary_val = "0.30"; pass = "8/3" (green/yellow); row3_val = "0.15"
**Field**: primary_val, row4_val, row3_val

### TC-004: M1/S1 feed 0.50 with high pass count
**Input**: `<MODE:1>` `<SUBMODE:1>` `<FEED:50>` `<PASS:5,10>` `<AP:25>`
**Expected**: primary_val = "0.50"; row4_val shows "10/5"; row3_val = "0.25"
**Field**: primary_val, row4_val, row3_val

### TC-005: M1/S3 negative Z position
**Input**: `<MODE:1>` `<SUBMODE:3>` `<FEED:15>` `<POS_Z:-47800>` `<POS_X:3600>`
**Expected**: row1_val = "-47.80" (red); row2_val = "+3.60" (green)
**Field**: row1_val color, row2_val color

### TC-006: M1 minimum feed value
**Input**: `<MODE:1>` `<FEED:5>`
**Expected**: primary_val = "0.05"
**Field**: primary_val

### TC-007: M1 maximum feed value
**Input**: `<MODE:1>` `<FEED:2500>`
**Expected**: primary_val = "25.00"
**Field**: primary_val

### TC-008: M1/S2 no passes (Manual submode)
**Input**: `<MODE:1>` `<SUBMODE:2>` `<PASS:0,0>`
**Expected**: row4_val = "--/--"
**Field**: row4_val

### TC-009: M1 SM=2 diameter display
**Input**: `<MODE:1>` `<SUBMODE:1>` `<SELECTMENU:2>` `<DIAM_X:85000>` `<POS_X:-8500>` `<POS_Z:12340>`
**Expected**: row1_val shows diameter from diam_x; row2_val = pos_X; row3_val = pos_Z
**Field**: row1_val, row2_val, row3_val

### TC-010: M1 SM=3 rebound/tension display
**Input**: `<MODE:1>` `<SUBMODE:1>` `<SELECTMENU:3>` `<OTSKOK_Z:5000>` `<TENSION_Z:2000>`
**Expected**: row1_val = "5.00"; row2_val = "2.00"; row3_val = "---"
**Field**: row1_val, row2_val, row3_val

### TC-011: M1 SM=1 Ap edit — touch PARAM_UP
**Input**: After `<MODE:1>` `<SUBMODE:1>` `<AP:15>`, ESP32 sends `<TOUCH:KEY:UP>`
**Expected**: STM32 receives `<TOUCH:KEY:UP>`; AP increments; STM32 sends `<AP:20>`
**Field**: row3_val (AP display)

### TC-012: M1 SM=1 pass_total=1 (single pass)
**Input**: `<MODE:1>` `<SUBMODE:1>` `<PASS:1,1>`
**Expected**: row4_val = "1/1"
**Field**: row4_val

### TC-013: M1 large position values
**Input**: `<MODE:1>` `<POS_Z:-999990>` `<POS_X:99990>`
**Expected**: row1_val = "-999.99"; row2_val = "+99.99" (no overflow)
**Field**: row1_val, row2_val

### TC-014: M1 zero position
**Input**: `<MODE:1>` `<POS_Z:0>` `<POS_X:0>`
**Expected**: row1_val = "0.00"; row2_val = "0.00"
**Field**: row1_val, row2_val

### TC-015: M1 → M2 mode switch clears alert
**Input**: Active alert, then `<MODE:2>`
**Expected**: alert dismissed; mode indicator changes to M2
**Field**: alert panel, mode_lbl

---

## MODE_AFEED (M2) — Асинхронная подача

### TC-016: M2/S1 afeed basic display
**Input**: `<MODE:2>` `<SUBMODE:1>` `<AFEED:10>` `<POS_Z:23400>` `<POS_X:-7200>` `<PASS:2,8>` `<AP:10>`
**Expected**: primary_val = "10"; row4_val = "8/2"; row3_val = "0.10"
**Field**: primary_val, row4_val, row3_val

### TC-017: M2/S2 manual no passes
**Input**: `<MODE:2>` `<SUBMODE:2>` `<AFEED:30>` `<PASS:0,0>`
**Expected**: primary_val = "30"; row4_val = "--/--"
**Field**: primary_val, row4_val

### TC-018: M2/S3 afeed=50
**Input**: `<MODE:2>` `<SUBMODE:3>` `<AFEED:50>` `<PASS:7,8>` `<AP:25>`
**Expected**: primary_val = "50"; row4_val = "8/7"; row3_val = "0.25"
**Field**: primary_val, row4_val, row3_val

### TC-019: M2/S1 afeed=20 with positions
**Input**: `<MODE:2>` `<SUBMODE:1>` `<AFEED:20>` `<POS_Z:18900>` `<POS_X:-3500>` `<PASS:4,12>` `<AP:15>`
**Expected**: primary_val = "20"; row1_val = "+18.90"; row2_val = "-3.50"
**Field**: primary_val, row1_val, row2_val

### TC-020: M2/S3 afeed=40 negative Z
**Input**: `<MODE:2>` `<SUBMODE:3>` `<AFEED:40>` `<POS_Z:-22500>` `<POS_X:1800>` `<PASS:0,6>` `<AP:20>`
**Expected**: primary_val = "40"; row1_val = "-22.50"; row2_val = "+1.80"
**Field**: primary_val, row1_val, row2_val

### TC-021: M2 afeed minimum value 15
**Input**: `<MODE:2>` `<AFEED:15>`
**Expected**: primary_val = "15"
**Field**: primary_val

### TC-022: M2 afeed value 150
**Input**: `<MODE:2>` `<AFEED:150>`
**Expected**: primary_val = "150"
**Field**: primary_val

### TC-023: M2 afeed value 300 (max normal)
**Input**: `<MODE:2>` `<AFEED:300>`
**Expected**: primary_val = "300"
**Field**: primary_val

### TC-024: M2 afeed value 330 — int16_t overflow bug check
**Input**: `<MODE:2>` `<AFEED:330>`
**Expected**: primary_val = "330" (NOT "-32512")
**Field**: primary_val
**Note**: BUG — if afeed_mm is int16_t and value > 32767 is sent, overflow occurs. Fix: use int32_t or uint16_t for afeed_mm in LatheData struct.

### TC-025: M2 afeed value 400 (max serial command)
**Input**: `<MODE:2>` `<AFEED:400>`
**Expected**: primary_val = "400"
**Field**: primary_val

### TC-026: M2 SM=2 spindle angle display
**Input**: `<MODE:2>` `<SELECTMENU:2>` `<DIVN:12>` `<DIVM:3>`
**Expected**: primary_val shows spindle angle (degrees); row1_val = "12" (Делений); row2_val = "3" (Метка)
**Field**: primary_val, row1_val, row2_val, primary_unit = "УГОЛ °"

### TC-027: M2 SM=2 sector angle calculation
**Input**: `<MODE:2>` `<SELECTMENU:2>` `<DIVN:12>` `<DIVM:3>` `<ANGLE:600>`
**Expected**: row3_val = sector angle = 360*(3-1)/12 = 60.0°
**Field**: row3_val

### TC-028: M2 SM=3 Z axis display
**Input**: `<MODE:2>` `<SELECTMENU:3>` `<POS_Z:45000>`
**Expected**: row3_val = "+45.00"
**Field**: row3_val

### TC-029: M2 SM=1→SM=2 layout change
**Input**: `<MODE:2>` `<SELECTMENU:1>` then `<SELECTMENU:2>`
**Expected**: Layout switches to divider subscreen (titles change to ДЕЛЕНИЙ/МЕТКА/УГ.СЕКТОР)
**Field**: row1_title, row2_title, row3_title

### TC-030: M2 touch PARAM_UP increases afeed
**Input**: In M2, ESP32 tap on primary_val, press UP; STM32 gets `<TOUCH:AFEED:N>`
**Expected**: STM32 increases aFeed_mm, sends back `<AFEED:N+5>`
**Field**: primary_val updates

---

## MODE_THREAD (M3) — Резьба

### TC-031: M3/S1 thread 1.00mm Ph=1
**Input**: `<MODE:3>` `<SUBMODE:1>` `<THREAD_NAME:1.00mm>` `<THREAD:100>` `<RPM_LIM:800>` `<PH:1>` `<THREAD_CYCL:8>` `<PASS:5,10>` `<POS_Z:45000>`
**Expected**: primary_val = "1.00"; primary_unit = "ШАГ ММ"; row3_title = "ОБ/МИН"; row3_val = "800"; row4_val = "8/5"
**Field**: primary_val, primary_unit, row3_val, row4_val

### TC-032: M3/S2 thread 1.50mm Ph=2 manual
**Input**: `<MODE:3>` `<SUBMODE:2>` `<THREAD_NAME:1.50mm>` `<THREAD:150>` `<PH:2>` `<THREAD_CYCL:16>` `<THREAD_TRAVEL:300>`
**Expected**: primary_val = "1.50"; row3_title = "ХОД ММ"; row3_val = "3.00"; row4_val = "16"
**Field**: primary_val, row3_title, row3_val, row4_val

### TC-033: M3/S3 thread 2.00mm
**Input**: `<MODE:3>` `<SUBMODE:3>` `<THREAD_NAME:2.00mm>` `<THREAD:200>` `<RPM_LIM:400>` `<PH:1>` `<THREAD_CYCL:13>` `<PASS:8,10>`
**Expected**: primary_val = "2.00"; row4_val = "13/8"
**Field**: primary_val, row4_val

### TC-034: M3/S2 8tpi imperial thread Ph=3
**Input**: `<MODE:3>` `<SUBMODE:2>` `<THREAD_NAME: 8tpi >` `<THREAD:318>` `<PH:3>` `<THREAD_CYCL:29>`
**Expected**: primary_val = "8" (filtered: only digits); primary_unit = "ДЮЙМ"; row4_val = "29"
**Field**: primary_val, primary_unit, row4_val

### TC-035: M3/S1 G1/8 pipe thread
**Input**: `<MODE:3>` `<SUBMODE:1>` `<THREAD_NAME:G  1/8>` `<THREAD:91>` `<RPM_LIM:200>` `<THREAD_CYCL:9>` `<PASS:3,6>`
**Expected**: primary_val = "1/8" (filtered); primary_unit = "G-ТРУБ"; row4_val = "9/3"
**Field**: primary_val, primary_unit, row4_val

### TC-036: M3/S3 2.50mm Ph=4
**Input**: `<MODE:3>` `<SUBMODE:3>` `<THREAD_NAME:2.50mm>` `<THREAD:250>` `<PH:4>` `<THREAD_TRAVEL:1000>` `<THREAD_CYCL:8>`
**Expected**: primary_val = "2.50"; row3_title = "ХОД ММ"; row3_val = "10.00"; row4_val = "8/?"
**Field**: primary_val, row3_val

### TC-037: M3 G-ТРУБ category indicator
**Input**: `<MODE:3>` `<THREAD_NAME:G 1/2>`
**Expected**: primary_unit = "G-ТРУБ"
**Field**: primary_unit

### TC-038: M3 K-ТРУБ category indicator
**Input**: `<MODE:3>` `<THREAD_NAME:K 1/8>`
**Expected**: primary_unit = "К-ТРУБ"
**Field**: primary_unit

### TC-039: M3 ДЮЙМ category indicator (tpi)
**Input**: `<MODE:3>` `<THREAD_NAME:64tpi>`
**Expected**: primary_unit = "ДЮЙМ"
**Field**: primary_unit

### TC-040: M3 min thread 0.20mm
**Input**: `<MODE:3>` `<THREAD_NAME:0.20mm>` `<THREAD:20>`
**Expected**: primary_val = "0.20"
**Field**: primary_val

### TC-041: M3 max thread 4.50mm
**Input**: `<MODE:3>` `<THREAD_NAME:4.50mm>` `<THREAD:450>`
**Expected**: primary_val = "4.50"
**Field**: primary_val

### TC-042: M3 80tpi fine thread
**Input**: `<MODE:3>` `<THREAD_NAME: 80tpi>` `<THREAD:32>`
**Expected**: primary_val = "80" (filtered); primary_unit = "ДЮЙМ"
**Field**: primary_val, primary_unit

### TC-043: M3 G 1/8 pipe thread (index 48)
**Input**: `<MODE:3>` `<THREAD_NAME:G  1/8>` `<THREAD:91>`
**Expected**: primary_val = "1/8"
**Field**: primary_val

### TC-044: M3 K 1/8 pipe thread
**Input**: `<MODE:3>` `<THREAD_NAME:K  1/8>` `<THREAD:94>`
**Expected**: primary_val = "1/8"; primary_unit = "K-ТРУБ"
**Field**: primary_val, primary_unit

### TC-045: M3 SM=2 чист.пр + заходов display
**Input**: `<MODE:3>` `<SELECTMENU:2>` `<PASS_FIN:2>` `<PH:3>`
**Expected**: row1_title = "ЧИСТ.ПР"; row1_val = "2"; row2_title = "ЗАХОДОВ"; row2_val = "3"
**Field**: row1_val, row2_val

### TC-046: M3 SM=3 Z axis display
**Input**: `<MODE:3>` `<SELECTMENU:3>` `<POS_Z:32000>`
**Expected**: row3_val = "+32.00"
**Field**: row3_val

### TC-047: M3 THR_CAT double-tap → metric to imperial
**Input**: In M3, ESP32 double-tap on thread type label, sends `<TOUCH:THR_CAT>`
**Expected**: STM32 jumps Thread_Step to first imperial (index ~20); sends new THREAD_NAME with "tpi"
**Field**: primary_unit changes to "ДЮЙМ"

### TC-048: M3 THR_CAT: imperial → G-pipe
**Input**: ESP32 sends `<TOUCH:THR_CAT>` when in ДЮЙМ category
**Expected**: STM32 jumps to G1/16; sends `<THREAD_NAME:G 1/16>`; ESP32 shows primary_unit = "G-ТРУБ"
**Field**: primary_unit

### TC-049: M3 THR_CAT: G-pipe → K-pipe
**Input**: ESP32 sends `<TOUCH:THR_CAT>` when in G-ТРУБ category
**Expected**: STM32 jumps to K1/16; primary_unit = "K-ТРУБ"
**Field**: primary_unit

### TC-050: M3 THR_CAT: K-pipe → back to metric
**Input**: ESP32 sends `<TOUCH:THR_CAT>` when in K-ТРУБ category
**Expected**: STM32 jumps to 0.20mm; primary_unit = "ШАГ ММ"
**Field**: primary_unit

### TC-051: M3 cycles display with manual submode (no pass_nr)
**Input**: `<MODE:3>` `<SUBMODE:2>` `<THREAD_CYCL:16>` `<PASS:0,0>`
**Expected**: row4_val = "16" (no slash, just total cycles for manual)
**Field**: row4_val

### TC-052: M3/S1 cycles display with pass_nr
**Input**: `<MODE:3>` `<SUBMODE:1>` `<THREAD_CYCL:8>` `<PASS:5,10>`
**Expected**: row4_val = "8/5" (cycles green, pass_nr yellow)
**Field**: row4_val

### TC-053: M3 BUG-04 edit mode sync by thread_name (64tpi)
**Input**: ESP32 in test mode, scenario with thread_mm=40, thread_name="64tpi"; tap primary_val to enter edit mode
**Expected**: local_step = 44 (64tpi index), not index for 0.40mm
**Field**: edit mode local_step

### TC-054: M3 BUG-06 cycles formula +1 check
**Input**: `<MODE:3>` `<SUBMODE:1>` `<THREAD_CYCL:4>` `<PASS:3,6>`
**Expected**: row4_val = "4/3"
**Field**: row4_val

### TC-055: M3 thread_name empty → fallback to numeric
**Input**: `<MODE:3>` `<THREAD:150>` and thread_name=""
**Expected**: primary_val = "1.50" (numeric fallback)
**Field**: primary_val

---

## MODE_CONE_L (M4) — Конус влево

### TC-056: M4/S1 basic cone display 45°
**Input**: `<MODE:4>` `<SUBMODE:1>` `<FEED:15>` `<CONE:0>` `<PASS:2,4>` `<AP:15>` `<POS_Z:-18300>` `<POS_X:4200>`
**Expected**: primary_val = "0.15"; row3_val = "45°"; row4_val = "4/2"; row1_val = "-18.30"
**Field**: primary_val, row3_val, row4_val, row1_val

### TC-057: M4/S2 manual cone 1°
**Input**: `<MODE:4>` `<SUBMODE:2>` `<FEED:25>` `<CONE:2>` `<PASS:0,0>`
**Expected**: primary_val = "0.25"; row3_val = "1°"; row4_val = "--/--"
**Field**: primary_val, row3_val, row4_val

### TC-058: M4/S3 cone 7°
**Input**: `<MODE:4>` `<SUBMODE:3>` `<FEED:30>` `<CONE:8>` `<PASS:1,3>` `<AP:15>`
**Expected**: primary_val = "0.30"; row3_val = "7°"; row4_val = "3/1"
**Field**: primary_val, row3_val, row4_val

### TC-059: M4/S1 cone 3°
**Input**: `<MODE:4>` `<SUBMODE:1>` `<FEED:20>` `<CONE:4>` `<PASS:3,6>`
**Expected**: primary_val = "0.20"; row3_val = "3°"; row4_val = "6/3"
**Field**: primary_val, row3_val, row4_val

### TC-060: M4/S3 KM0 Morse taper
**Input**: `<MODE:4>` `<SUBMODE:3>` `<FEED:15>` `<CONE:45>` `<PASS:1,3>`
**Expected**: row3_val = "KM0"
**Field**: row3_val

### TC-061: M4/S2 KM5 Morse taper
**Input**: `<MODE:4>` `<SUBMODE:2>` `<FEED:25>` `<CONE:50>` `<PASS:0,0>`
**Expected**: row3_val = "KM5"
**Field**: row3_val

### TC-062: M4/S1 3:25 ratio taper
**Input**: `<MODE:4>` `<SUBMODE:1>` `<FEED:15>` `<CONE:62>` `<PASS:3,6>`
**Expected**: row3_val = "3:25"
**Field**: row3_val

### TC-063: M4 SM=2 cone type + conical thread display
**Input**: `<MODE:4>` `<SELECTMENU:2>` `<CONE:45>` `<CONE_THR:1>`
**Expected**: row1_val = "KM0"; row2_val = "ВКЛ"
**Field**: row1_val, row2_val

### TC-064: M4 SM=2 cone thr disabled
**Input**: `<MODE:4>` `<SELECTMENU:2>` `<CONE_THR:0>`
**Expected**: row2_val = "ВЫКЛ"
**Field**: row2_val

### TC-065: M4 SM=3 Z axis
**Input**: `<MODE:4>` `<SELECTMENU:3>` `<POS_Z:-12000>`
**Expected**: row3_val = "-12.00"
**Field**: row3_val

---

## MODE_CONE_R (M5) — Конус вправо

### TC-066: M5/S1 45° cone positive Z
**Input**: `<MODE:5>` `<SUBMODE:1>` `<FEED:15>` `<CONE:0>` `<PASS:2,4>` `<POS_Z:18300>` `<POS_X:4200>`
**Expected**: primary_val = "0.15"; row1_val = "+18.30" (green); row3_val = "45°"
**Field**: primary_val, row1_val, row3_val

### TC-067: M5/S2 2° manual
**Input**: `<MODE:5>` `<SUBMODE:2>` `<FEED:20>` `<CONE:3>` `<PASS:0,0>`
**Expected**: primary_val = "0.20"; row3_val = "2°"; row4_val = "--/--"
**Field**: primary_val, row3_val, row4_val

### TC-068: M5/S3 6° external
**Input**: `<MODE:5>` `<SUBMODE:3>` `<FEED:30>` `<CONE:7>` `<PASS:1,3>`
**Expected**: primary_val = "0.30"; row3_val = "6°"
**Field**: primary_val, row3_val

### TC-069: M5/S3 4° pass counting
**Input**: `<MODE:5>` `<SUBMODE:3>` `<FEED:25>` `<CONE:5>` `<PASS:4,6>`
**Expected**: row4_val = "6/4"
**Field**: row4_val

### TC-070: M5/S1 KM3 Morse taper
**Input**: `<MODE:5>` `<SUBMODE:1>` `<FEED:20>` `<CONE:48>` `<PASS:2,5>`
**Expected**: row3_val = "KM3"
**Field**: row3_val

### TC-071: M5/S3 1:20 ratio taper
**Input**: `<MODE:5>` `<SUBMODE:3>` `<FEED:10>` `<CONE:57>` `<PASS:0,4>`
**Expected**: row3_val = "1:20"
**Field**: row3_val

### TC-072: M5/S3 3:25 ratio
**Input**: `<MODE:5>` `<SUBMODE:3>` `<FEED:10>` `<CONE:62>` `<PASS:1,4>`
**Expected**: row3_val = "3:25"
**Field**: row3_val

---

## MODE_SPHERE (M6) — Шар

### TC-073: M6/S2 sphere ø40mm
**Input**: `<MODE:6>` `<SUBMODE:2>` `<SPHERE:2000>` `<PASS:3,8>` `<BAR:500>` `<PASS_SPHR:80>` `<POS_Z:-12500>` `<POS_X:8000>`
**Expected**: primary_val = sphere diameter display; row3_val = "10.00" (bar_r*2/100); row4_val = "82/3" (80+2=82)
**Field**: primary_val, row3_val, row4_val

### TC-074: M6/S3 sphere ø100mm
**Input**: `<MODE:6>` `<SUBMODE:3>` `<SPHERE:5000>` `<PASS:0,15>` `<BAR:1000>` `<PASS_SPHR:200>`
**Expected**: row3_val = "20.00"; row4_val = "202/0"
**Field**: row3_val, row4_val

### TC-075: M6/S1 INTERNAL → alert "Режим невозможен!"
**Input**: `<MODE:6>` `<SUBMODE:1>`
**Expected**: Alert overlay shown with "Режим невозможен!"
**Field**: alert panel visible

### TC-076: M6/S1 alert dismiss on mode change
**Input**: `<MODE:6>` `<SUBMODE:1>` → alert shown, then `<MODE:1>`
**Expected**: Alert dismissed automatically on mode change
**Field**: alert panel hidden

### TC-077: M6/S1 alert → OK button
**Input**: Alert showing, ESP32 taps OK → sends `<TOUCH:ALERT_OK>`
**Expected**: STM32 receives ALERT_OK; alert dismissed
**Field**: alert dismissed; STM32 serial log

### TC-078: M6/S2 (manual) no alert
**Input**: `<MODE:6>` `<SUBMODE:2>`
**Expected**: No alert shown
**Field**: alert panel hidden

### TC-079: M6 sphere ø10mm minimal
**Input**: `<MODE:6>` `<SUBMODE:3>` `<SPHERE:500>` `<BAR:200>` `<PASS_SPHR:20>` `<PASS:2,5>`
**Expected**: row3_val = "4.00"; row4_val = "22/2"
**Field**: row3_val, row4_val

### TC-080: M6 sphere ø200mm maximum
**Input**: `<MODE:6>` `<SUBMODE:3>` `<SPHERE:10000>` `<BAR:1500>` `<PASS_SPHR:300>` `<PASS:0,20>`
**Expected**: row3_val = "30.00"; row4_val = "302/0"
**Field**: row3_val, row4_val

### TC-081: M6 SM=2 cutter width + cutting step
**Input**: `<MODE:6>` `<SELECTMENU:2>` `<CUTTER_W:25>` `<CUTTING_W:50>`
**Expected**: row1_val = "0.25" (cutter width); row2_val = "0.50" (cutting step)
**Field**: row1_val, row2_val

### TC-082: M6 SM=3 Z axis display
**Input**: `<MODE:6>` `<SELECTMENU:3>` `<POS_Z:-30000>`
**Expected**: row3_val = "-30.00"
**Field**: row3_val

---

## MODE_DIVIDER (M7) — Делитель

### TC-083: M7 6 divisions, label 1, angle 60°
**Input**: `<MODE:7>` `<DIVN:6>` `<DIVM:1>` `<ANGLE:600>`
**Expected**: primary_val = "60.0"; row1_val = "6"; row2_val = "1"; row3_val = "60.0°" (360/6)
**Field**: primary_val, row1_val, row2_val, row3_val

### TC-084: M7 12 divisions, label 7, angle 210°
**Input**: `<MODE:7>` `<DIVN:12>` `<DIVM:7>` `<ANGLE:2100>`
**Expected**: primary_val = "210.0"; row3_val = "30.0°" (360/12)
**Field**: primary_val, row3_val

### TC-085: M7 36 divisions, label 25, angle 250°
**Input**: `<MODE:7>` `<DIVN:36>` `<DIVM:25>` `<ANGLE:2500>`
**Expected**: primary_val = "250.0"; row3_val = "10.0°" (360/36)
**Field**: primary_val, row3_val

### TC-086: M7 200 divisions limit (byte max)
**Input**: `<MODE:7>` `<DIVN:200>` `<DIVM:100>` `<ANGLE:1800>`
**Expected**: primary_val = "180.0"; row1_val = "200"
**Field**: primary_val, row1_val

### TC-087: M7 4 divisions, label 2
**Input**: `<MODE:7>` `<DIVN:4>` `<DIVM:2>` `<ANGLE:900>`
**Expected**: primary_val = "90.0"; row3_val = "90.0°"
**Field**: primary_val, row3_val

### TC-088: M7 100 divisions, label 37
**Input**: `<MODE:7>` `<DIVN:100>` `<DIVM:37>` `<ANGLE:1296>`
**Expected**: primary_val = "129.6"; row3_val = "3.6°"
**Field**: primary_val, row3_val

### TC-089: M7 3 divisions (minimum practical)
**Input**: `<MODE:7>` `<DIVN:3>` `<DIVM:1>` `<ANGLE:1200>`
**Expected**: primary_val = "120.0"; row3_val = "120.0°"
**Field**: primary_val, row3_val

### TC-090: M7 total_tooth = 0 edge case
**Input**: `<MODE:7>` `<DIVN:0>`
**Expected**: row3_val = "---" (division by zero guard)
**Field**: row3_val

### TC-091: M7 SUBSEL:DIVN — edit total divisions
**Input**: In M7, tap row1 (Делений) → ESP32 sends `<TOUCH:SUBSEL:DIVN>`
**Expected**: STM32 receives SUBSEL:DIVN; row1 highlighted with yellow border
**Field**: row1 edit highlight

### TC-092: M7 SUBSEL:DIVM — edit current mark
**Input**: In M7, tap row2 (Метка) → ESP32 sends `<TOUCH:SUBSEL:DIVM>`
**Expected**: STM32 receives SUBSEL:DIVM; row2 highlighted
**Field**: row2 edit highlight

---

## MODE_RESERVE (M8) — Резерв

### TC-093: M8 zero positions
**Input**: `<MODE:8>` `<POS_Z:0>` `<POS_X:0>`
**Expected**: mode_lbl = "M8"; row1_val = "0.00"; row2_val = "0.00"; primary_val = "---"
**Field**: mode_lbl, row1_val, row2_val, primary_val

### TC-094: M8 with positions
**Input**: `<MODE:8>` `<POS_Z:-95000>` `<POS_X:3000>`
**Expected**: row1_val = "-95.00"; row2_val = "+3.00"
**Field**: row1_val, row2_val

---

## Limit Switch Tests

### TC-095: L-01 all limits OFF
**Input**: `<LIMITS:0,0,0,0>`
**Expected**: All 4 arrows dim (← → dim cyan; ↑ ↓ dim green)
**Field**: limit indicators

### TC-096: L-02 Z-left limit SET
**Input**: `<LIMITS:1,0,0,0>`
**Expected**: ← bright cyan; → ↑ ↓ dim
**Field**: limit_left indicator

### TC-097: L-03 Z-right limit SET
**Input**: `<LIMITS:0,1,0,0>`
**Expected**: → bright cyan; ← ↑ ↓ dim
**Field**: limit_right indicator

### TC-098: L-04 both Z limits
**Input**: `<LIMITS:1,1,0,0>`
**Expected**: ← → bright cyan; ↑ ↓ dim
**Field**: limit_left, limit_right indicators

### TC-099: L-05 X-forward limit SET
**Input**: `<LIMITS:0,0,1,0>`
**Expected**: ↑ bright green; ← → ↓ dim
**Field**: limit_front indicator

### TC-100: L-06 X-rear limit SET
**Input**: `<LIMITS:0,0,0,1>`
**Expected**: ↓ bright green; ← → ↑ dim
**Field**: limit_rear indicator

### TC-101: L-07 both X limits
**Input**: `<LIMITS:0,0,1,1>`
**Expected**: ↑ ↓ bright green; ← → dim
**Field**: limit_front, limit_rear indicators

### TC-102: L-08 all limits SET
**Input**: `<LIMITS:1,1,1,1>`
**Expected**: ← → bright cyan (#00d4ff); ↑ ↓ bright green (#00ff88)
**Field**: all limit indicators

### TC-103: L-09 diagonal Z-left + X-rear
**Input**: `<LIMITS:1,0,0,1>`
**Expected**: ← cyan; ↓ green; → and ↑ dim
**Field**: limit_left, limit_rear

### TC-104: L-10 diagonal Z-right + X-forward
**Input**: `<LIMITS:0,1,1,0>`
**Expected**: → cyan; ↑ green; ← and ↓ dim
**Field**: limit_right, limit_front

### TC-105: LM-01 M1 + all limits
**Input**: PT:1 equivalent + `<LIMITS:1,1,1,1>`
**Expected**: M1 mode with all 4 arrows bright
**Field**: mode_lbl, all limit indicators

### TC-106: LM-02 M3 + Z limits
**Input**: M3 with 2.00mm + `<LIMITS:1,1,0,0>`
**Expected**: M3 thread display + ← → bright
**Field**: mode_lbl, limit indicators

### TC-107: LM-03 M4 + X limits
**Input**: M4 cone 45° + `<LIMITS:0,0,1,1>`
**Expected**: M4 cone display + ↑ ↓ bright green
**Field**: mode_lbl, limit indicators

### TC-108: LM-04 M6 + all limits
**Input**: M6 sphere + `<LIMITS:1,1,1,1>`
**Expected**: All limits bright; sphere display normal
**Field**: all indicators

### TC-109: LM-05 M7 + Z-left
**Input**: M7 divider + `<LIMITS:1,0,0,0>`
**Expected**: M7 display + ← cyan
**Field**: mode_lbl, limit_left

---

## Alert Tests

### TC-110: A-01 ALERT:1 "УСТАНОВИТЕ УПОРЫ"
**Input**: `<ALERT:1>`
**Expected**: Alert overlay visible with "УСТАНОВИТЕ УПОРЫ!" text
**Field**: alert panel

### TC-111: A-02 ALERT:2 "УСТАНОВИТЕ СУППОРТ"
**Input**: `<ALERT:2>`
**Expected**: Alert overlay with "УСТАНОВИТЕ СУППОРТ В ИСХОДНУЮ ПОЗИЦИЮ!"
**Field**: alert panel

### TC-112: A-03 ALERT:3 "ОПЕРАЦИЯ ЗАВЕРШЕНА"
**Input**: `<ALERT:3>`
**Expected**: Alert overlay with "ОПЕРАЦИЯ ЗАВЕРШЕНА!"
**Field**: alert panel

### TC-113: A-04 ALERT:4 joystick neutral
**Input**: `<ALERT:4>`
**Expected**: Alert overlay with joystick neutral message
**Field**: alert panel

### TC-114: A-05 ALERT:0 dismiss alert
**Input**: `<ALERT:1>` then `<ALERT:0>`
**Expected**: Alert dismissed
**Field**: alert panel hidden

### TC-115: A-06 M6/S1 auto-alert
**Input**: `<MODE:6>` `<SUBMODE:1>`
**Expected**: "Режим невозможен!" alert shown automatically
**Field**: alert panel

### TC-116: A-07 alert + limits coexist
**Input**: `<ALERT:1>` + `<LIMITS:1,0,0,0>`
**Expected**: Alert shown + ← limit indicator visible in status bar
**Field**: alert panel, limit_left

### TC-117: A-08 alert dismissed on mode change (M6/S1 → M5/S1)
**Input**: `<MODE:6>` `<SUBMODE:1>` → alert, then `<MODE:5>` `<SUBMODE:1>`
**Expected**: Alert disappears; M5 display without alert
**Field**: alert panel hidden, mode_lbl = M5

### TC-118: A-09 alert OK touch response
**Input**: Alert showing, user taps OK → `<TOUCH:ALERT_OK>` sent to STM32
**Expected**: STM32 receives ALERT_OK in drv_display.cpp rx handler
**Field**: STM32 serial output

---

## Touch Events Tests

### TC-119: T-01 mode button M1
**Input**: ESP32 taps M1 button → sends `<TOUCH:M1>`
**Expected**: STM32 switches to MODE_FEED (mode=0); sends back `<MODE:1>`
**Field**: mode display

### TC-120: T-02 mode button M2
**Input**: ESP32 sends `<TOUCH:M2>`
**Expected**: STM32 switches to MODE_AFEED; sends `<MODE:2>`
**Field**: mode display

### TC-121: T-03 mode button M3
**Input**: ESP32 sends `<TOUCH:M3>`
**Expected**: STM32 switches to MODE_THREAD; sends `<MODE:3>`
**Field**: mode display

### TC-122: T-04 mode button M4
**Input**: ESP32 sends `<TOUCH:M4>`
**Expected**: STM32 switches to MODE_CONE_L; sends `<MODE:4>`
**Field**: mode display

### TC-123: T-05 mode button M5
**Input**: ESP32 sends `<TOUCH:M5>`
**Expected**: STM32 switches to MODE_CONE_R; sends `<MODE:5>`
**Field**: mode display

### TC-124: T-06 mode button M6
**Input**: ESP32 sends `<TOUCH:M6>`
**Expected**: STM32 switches to MODE_SPHERE; sends `<MODE:6>`
**Field**: mode display

### TC-125: T-07 mode button M7
**Input**: ESP32 sends `<TOUCH:M7>`
**Expected**: STM32 switches to MODE_DIVIDER; sends `<MODE:7>`
**Field**: mode display

### TC-126: T-08 mode button M8
**Input**: ESP32 sends `<TOUCH:M8>`
**Expected**: STM32 switches to MODE_RESERVE; sends `<MODE:8>`
**Field**: mode display

### TC-127: T-09 submode S1 (Internal)
**Input**: ESP32 sends `<TOUCH:S1>`
**Expected**: STM32 sets submode=Internal; sends `<SUBMODE:1>` back
**Field**: submode display

### TC-128: T-10 submode S2 (Manual)
**Input**: ESP32 sends `<TOUCH:S2>`
**Expected**: STM32 sets submode=Manual; sends `<SUBMODE:2>` back
**Field**: submode display

### TC-129: T-11 submode S3 (External)
**Input**: ESP32 sends `<TOUCH:S3>`
**Expected**: STM32 sets submode=External; sends `<SUBMODE:3>` back
**Field**: submode display

### TC-130: T-12 KEY:UP in edit mode increases parameter
**Input**: In M1 edit mode (feed), ESP32 sends `<TOUCH:KEY:UP>`
**Expected**: STM32 increments Feed_mm; sends new `<FEED:N>`
**Field**: primary_val

### TC-131: T-13 KEY:DN in edit mode decreases parameter
**Input**: In M1 edit mode, ESP32 sends `<TOUCH:KEY:DN>`
**Expected**: STM32 decrements Feed_mm
**Field**: primary_val

### TC-132: T-14 KEY:LEFT navigation
**Input**: ESP32 sends `<TOUCH:KEY:LEFT>`
**Expected**: STM32 receives TOUCH_KEY_LEFT; handles navigation
**Field**: STM32 rx handler

### TC-133: T-15 KEY:RIGHT navigation
**Input**: ESP32 sends `<TOUCH:KEY:RIGHT>`
**Expected**: STM32 receives TOUCH_KEY_RIGHT; handles navigation
**Field**: STM32 rx handler

### TC-134: T-16 PARAM_OK confirm edit
**Input**: In edit mode, ESP32 sends `<TOUCH:PARAM_OK>`
**Expected**: STM32 confirms parameter, exits edit mode; sends SelectMenu update
**Field**: edit state, selectmenu

### TC-135: T-17 JOY:LEFT (carriage left)
**Input**: ESP32 sends `<TOUCH:JOY:LEFT>`
**Expected**: STM32 receives TOUCH_JOY_LEFT; joy_y = -1 (or +1 depending on mapping)
**Field**: STM32 joy_y state

### TC-136: T-18 JOY:RIGHT (carriage right)
**Input**: ESP32 sends `<TOUCH:JOY:RIGHT>`
**Expected**: STM32 activates carriage movement
**Field**: STM32 joy_y state

### TC-137: T-19 JOY:UP (X axis forward)
**Input**: ESP32 sends `<TOUCH:JOY:UP>`
**Expected**: STM32 activates X-axis movement
**Field**: STM32 joy_x state

### TC-138: T-20 JOY:DOWN (X axis rear)
**Input**: ESP32 sends `<TOUCH:JOY:DOWN>`
**Expected**: STM32 activates X-axis rear movement
**Field**: STM32 joy_x state

### TC-139: T-21 JOY:STOP
**Input**: ESP32 sends `<TOUCH:JOY:STOP>`
**Expected**: STM32 stops all movement; joy_y=0, joy_x=0
**Field**: STM32 joy state

### TC-140: T-22 RAPID_ON
**Input**: ESP32 sends `<TOUCH:RAPID_ON>`
**Expected**: STM32 sets joy_rapid=0 (rapid mode active)
**Field**: STM32 joy_rapid

### TC-141: T-23 RAPID_OFF
**Input**: ESP32 sends `<TOUCH:RAPID_OFF>`
**Expected**: STM32 sets joy_rapid=1 (slow/feed mode)
**Field**: STM32 joy_rapid

### TC-142: T-24 parametric FEED touch from ESP32
**Input**: ESP32 sends `<TOUCH:FEED:25>`
**Expected**: STM32 rx parses cmd="FEED", value=25; Feed_mm=25
**Field**: STM32 els.Feed_mm

### TC-143: T-25 parametric AFEED touch from ESP32
**Input**: ESP32 sends `<TOUCH:AFEED:50>`
**Expected**: STM32 rx parses cmd="AFEED", value=50; aFeed_mm=50
**Field**: STM32 els.aFeed_mm

### TC-144: T-26 parametric AP touch from ESP32
**Input**: ESP32 sends `<TOUCH:AP:15>`
**Expected**: STM32 Ap=15
**Field**: STM32 els.Ap

---

## SelectMenu Tests

### TC-145: SM-01 SELECTMENU:1 (main screen)
**Input**: `<SELECTMENU:1>`
**Expected**: Main screen layout (Z/X positions in row1/row2, Ap in row3)
**Field**: row titles, row values

### TC-146: SM-02 SELECTMENU:2 for M1
**Input**: `<MODE:1>` `<SELECTMENU:2>`
**Expected**: Diameter/X/Z subscreen
**Field**: row1_title = "ДИАМЕТР", row2_title = "ОСЬ X"

### TC-147: SM-03 SELECTMENU:3 for M1
**Input**: `<MODE:1>` `<SELECTMENU:3>`
**Expected**: Rebound/tension subscreen
**Field**: row1_title = "ОТСКОК Z", row2_title = "НАТЯГ Z"

### TC-148: SM-04 SELECTMENU:2 for M2
**Input**: `<MODE:2>` `<SELECTMENU:2>`
**Expected**: Divider subscreen: Делений/Метка/УГ.СЕКТОР
**Field**: row1_title, row2_title, row3_title

### TC-149: SM-05 SELECTMENU:3 for M2
**Input**: `<MODE:2>` `<SELECTMENU:3>` `<POS_Z:5000>`
**Expected**: row3_val shows Z position "+5.00"
**Field**: row3_val

### TC-150: SM-06 SELECTMENU:2 for M3
**Input**: `<MODE:3>` `<SELECTMENU:2>`
**Expected**: Thread params subscreen: ЧИСТ.ПР/ЗАХОДОВ/ХОД ММ
**Field**: row1_title, row2_title, row3_title

### TC-151: SM-07 SELECTMENU:3 for M3
**Input**: `<MODE:3>` `<SELECTMENU:3>` `<POS_Z:78500>`
**Expected**: row3_val = "+78.50"
**Field**: row3_val

### TC-152: SM-08 SELECTMENU:2 for M4
**Input**: `<MODE:4>` `<SELECTMENU:2>`
**Expected**: Cone params subscreen: КОНУС/К.РЕЗЬБА/---
**Field**: row1_title, row2_title

### TC-153: SM-09 SELECTMENU:2 for M6
**Input**: `<MODE:6>` `<SELECTMENU:2>`
**Expected**: Sphere params: ШИРИНА РЕЗЦА/ШАГ ОСИ
**Field**: row1_title, row2_title

---

## RPM and Motor State Tests

### TC-154: R-01 RPM display in secondary_val
**Input**: `<RPM:1500>`
**Expected**: secondary_val = "1500"
**Field**: secondary_val

### TC-155: R-02 RPM=0 (spindle stopped)
**Input**: `<RPM:0>`
**Expected**: secondary_val = "0"; rpm_bar = 0
**Field**: secondary_val, rpm_bar

### TC-156: R-03 RPM=3000 max spindle
**Input**: `<RPM:3000>`
**Expected**: secondary_val = "3000"; rpm_bar high
**Field**: secondary_val, rpm_bar

### TC-157: R-04 motor running state
**Input**: `<STATE:1,1,1>` (or `<STATE:run>`)
**Expected**: motor indicator active (green)
**Field**: pwr_bg indicator

### TC-158: R-05 motor stopped state
**Input**: `<STATE:0,1,1>` (or `<STATE:stop>`)
**Expected**: motor indicator inactive (dark)
**Field**: pwr_bg indicator

### TC-159: R-06 M3 RPM_LIM display with Ph=1
**Input**: `<MODE:3>` `<PH:1>` `<RPM_LIM:800>`
**Expected**: row3_title = "ОБ/МИН"; row3_val = "800"
**Field**: row3_title, row3_val

### TC-160: R-07 M3 THREAD_TRAVEL display with Ph=2
**Input**: `<MODE:3>` `<PH:2>` `<THREAD_TRAVEL:300>`
**Expected**: row3_title = "ХОД ММ"; row3_val = "3.00"
**Field**: row3_title, row3_val

---

## Mode Sequence Tests

### TC-161: S-01 full mode cycle M1→M8
**Input**: `<MODE:1>` → `<MODE:2>` → `<MODE:3>` → `<MODE:4>` → `<MODE:5>` → `<MODE:6>` → `<MODE:7>` → `<MODE:8>`
**Expected**: mode_lbl changes correctly at each step: M1→M2→...→M8
**Field**: mode_lbl

### TC-162: S-02 mode change clears previous values
**Input**: `<MODE:1>` `<FEED:25>`, then `<MODE:3>` `<THREAD:150>`
**Expected**: M3 display shows thread; M1 feed not visible
**Field**: primary_val, primary_unit

### TC-163: S-03 submode persists per mode
**Input**: `<MODE:1>` `<SUBMODE:3>`, then `<MODE:2>`, then back `<MODE:1>`
**Expected**: M1 returns to S3 (STM32 stores sub_feed separately)
**Field**: submode_lbl

### TC-164: S-04 alert dismisses on any mode change
**Input**: M6/S1 alert active, then `<MODE:3>`
**Expected**: Alert dismissed; M3 displays normally
**Field**: alert panel

### TC-165: S-05 READY/PONG triggers SendAll
**Input**: ESP32 boots and sends `<READY>`
**Expected**: STM32 receives READY, calls DRV_Display_SendAll() — sends all state fields
**Field**: all display fields update simultaneously

### TC-166: S-06 PING → PONG roundtrip
**Input**: STM32 sends `<PING>`
**Expected**: ESP32 sends `<READY>` back; STM32 calls SendAll
**Field**: all fields

---

## Edge Cases and Boundary Tests

### TC-167: E-01 pass_total = 0 → "--/--"
**Input**: `<MODE:1>` `<PASS:0,0>`
**Expected**: row4_val = "--/--"
**Field**: row4_val

### TC-168: E-02 pass_nr > pass_total (anomalous)
**Input**: `<MODE:1>` `<PASS:12,8>` (nr>total)
**Expected**: Display shows values as received: "8/12" — no crash
**Field**: row4_val

### TC-169: E-03 maximum positions Z=-999.99mm
**Input**: `<MODE:1>` `<POS_Z:-999990>` `<POS_X:99990>`
**Expected**: row1_val = "-999.99"; row2_val = "+99.99" (no field overflow)
**Field**: row1_val, row2_val

### TC-170: E-04 feed_mm=0 edge case
**Input**: `<MODE:1>` `<FEED:0>`
**Expected**: primary_val = "0.00" — no crash
**Field**: primary_val

### TC-171: E-05 very large pass_total
**Input**: `<MODE:1>` `<PASS:5,255>` (uint16_t max practical)
**Expected**: row4_val = "255/5" — no overflow
**Field**: row4_val

### TC-172: E-06 cone_idx out of range (bounds check)
**Input**: `<CONE:100>` (beyond CONE_COUNT)
**Expected**: display shows last valid cone name (constrain applied) — no crash
**Field**: row3_val

### TC-173: E-07 AFEED=10 (minimum) display
**Input**: `<MODE:2>` `<AFEED:10>`
**Expected**: primary_val = "10"
**Field**: primary_val

### TC-174: E-08 AFEED=15 (MIN_AFEED when unit=mm/min×100)
**Input**: `<MODE:2>` `<AFEED:15>`
**Expected**: primary_val = "15"
**Field**: primary_val

### TC-175: E-09 AFEED overflow boundary int16_t (32767)
**Input**: `<MODE:2>` `<AFEED:32767>`
**Expected**: primary_val = "32767" (max safe int16_t)
**Field**: primary_val

### TC-176: E-10 AFEED=32768 causes int16_t overflow to -32768
**Input**: `<MODE:2>` `<AFEED:32768>` — value beyond int16_t max
**Expected**: BUG: primary_val shows -32768 (NOT expected); FIX: change afeed_mm to int32_t
**Field**: primary_val

### TC-177: E-11 AFEED=33024 shows -32512 (reported bug)
**Input**: `<MODE:2>` `<AFEED:33024>`
**Expected**: BUG: primary_val = "-32512"; correct behavior = "33024"
**Field**: primary_val
**Note**: Root cause — LatheData.afeed_mm is int16_t; 33024 stored as 0x8100 = -32512. Fix: change field type to int32_t.

### TC-178: E-12 total_tooth=1 divider (minimum)
**Input**: `<MODE:7>` `<DIVN:1>` `<DIVM:1>`
**Expected**: row3_val = "360.0°"
**Field**: row3_val

### TC-179: E-13 spindle_angle=3600 (360.0°)
**Input**: `<MODE:7>` `<ANGLE:3600>`
**Expected**: primary_val = "360.0"
**Field**: primary_val

### TC-180: E-14 thread_name buffer overflow check (7 chars + null)
**Input**: `<THREAD_NAME:1.50mm>` (7 chars "1.50mm\0" — fits in char[8])
**Expected**: thread_name stored safely; no overflow
**Field**: primary_val = "1.50"

### TC-181: E-15 sphere_radius=0 edge case
**Input**: `<MODE:6>` `<SUBMODE:3>` `<SPHERE:0>`
**Expected**: primary_val = "0.00" — no crash
**Field**: primary_val

---

## Full Cycle Tests (STM32 → ESP32 → STM32)

### TC-182: FC-01 M1 touch M2 → STM32 switches mode → ESP32 updates
**Input**: STM32 in M1, ESP32 user taps M2 → sends `<TOUCH:M2>` → STM32 sets mode=AFEED, sends `<MODE:2>` `<SUBMODE:N>` `<AFEED:N>` → ESP32 updates
**Expected**: ESP32 mode_lbl changes to "M2"
**Field**: mode_lbl

### TC-183: FC-02 M3 thread UP → step increments
**Input**: In M3 edit mode, ESP32 sends KEY:UP → STM32 increments Thread_Step → sends back THREAD_NAME/THREAD/RPM_LIM/THREAD_TRAVEL/THREAD_CYCL
**Expected**: ESP32 primary_val updates to next thread step
**Field**: primary_val, rpm_limit

### TC-184: FC-03 M1 Ap DOWN → decrements
**Input**: In M1 SM=1 sub-edit of row3 (Ap), ESP32 sends KEY:DN → STM32 decrements Ap → sends `<AP:N-step>`
**Expected**: row3_val decreases
**Field**: row3_val

### TC-185: FC-04 M7 division edit UP → total_tooth increments
**Input**: M7 SUBSEL:DIVN active, KEY:UP → STM32 increments Total_Tooth → sends `<DIVN:N>`
**Expected**: row1_val increases by 1
**Field**: row1_val

### TC-186: FC-05 mode + submode + all params cycle (PT:1 equivalent)
**Input**: `<MODE:1>` `<SUBMODE:1>` `<FEED:10>` `<POS_Z:56780>` `<POS_X:-8500>` `<PASS:1,6>` `<AP:10>` `<RPM:800>`
**Expected**: Full M1/S1 display: primary_val=0.10, Z=+56.78, X=-8.50, passes=6/1, ap=0.10, rpm=800
**Field**: all display elements

### TC-187: FC-06 M3 full state (PT:11 equivalent)
**Input**: `<MODE:3>` `<SUBMODE:1>` `<THREAD_NAME:1.00mm>` `<THREAD:100>` `<PH:1>` `<RPM_LIM:800>` `<THREAD_CYCL:8>` `<THREAD_TRAVEL:100>` `<PASS:5,10>` `<POS_Z:45000>` `<POS_X:-5000>`
**Expected**: primary_val=1.00; primary_unit=ШАГ ММ; row3_title=ОБ/МИН; row3_val=800; row4_val=8/5
**Field**: all M3 fields

### TC-188: FC-07 M7 full state (PT:30 equivalent)
**Input**: `<MODE:7>` `<DIVN:6>` `<DIVM:1>` `<ANGLE:600>`
**Expected**: primary_val=60.0; row1_val=6; row2_val=1; row3_val=60.0°
**Field**: all M7 fields

### TC-189: FC-08 alert OK clears and STM32 gets notification
**Input**: Alert:1 shown, ESP32 taps OK → `<TOUCH:ALERT_OK>` → STM32 calls Switch_Man() or similar
**Expected**: STM32 receives ALERT_OK; submode may switch to Manual; sends `<SUBMODE:2>`
**Field**: STM32 behavior after ALERT_OK

### TC-190: FC-09 M2 → M7 → back to M2 (state retention)
**Input**: `<MODE:2>` `<AFEED:50>`, then `<MODE:7>` `<DIVN:12>`, then `<MODE:2>`
**Expected**: M2 returns with afeed from STM32 state (SendAll sends correct afeed)
**Field**: primary_val on return to M2

### TC-191: FC-10 limits change while in M6
**Input**: M6/S3 active, then `<LIMITS:1,0,1,0>`
**Expected**: ← cyan + ↑ green visible; M6 display unaffected
**Field**: limit indicators, M6 display

---

## PT Preset Tests (ESP32 internal test mode: 5× fast DN)

### TC-192: PT-01 M1/S1 scenario
**Input**: ESP32 test mode, select scenario "M1/S1 Подача 0.10 Пр1/6"
**Expected**: primary_val="0.10"; row4_val="6/1"; row3_val="0.10"; row1_val="+56.78"; row2_val="-8.50"
**Field**: all M1 fields

### TC-193: PT-02 M2/S2 Manual scenario
**Input**: ESP32 test mode, select scenario "M2/S2 аПодача 0.30 Ручной"
**Expected**: primary_val="30"; row4_val="--/--"; submode=S2
**Field**: primary_val, row4_val

### TC-194: PT-03 M3/S1 1.00mm scenario
**Input**: ESP32 test mode, select M3 scenario
**Expected**: primary_unit="ШАГ ММ"; primary_val="1.00"; row3_val="800" (ОБ/МИН)
**Field**: primary_unit, primary_val, row3_val

### TC-195: PT-04 M6/S1 alert scenario
**Input**: ESP32 test mode, select M6 SUBMODE_INTERNAL scenario
**Expected**: Alert "Режим невозможен!" shown
**Field**: alert panel

### TC-196: PT-05 M7 divider scenario
**Input**: ESP32 test mode, select M7 scenario (6 divisions)
**Expected**: row3_val="60.0°"; row1_val="6"; row2_val="1"
**Field**: all M7 fields

### TC-197: PT-06 M4 cone scenario KM0
**Input**: ESP32 test mode, M4 with CONE:45
**Expected**: row3_val="KM0"
**Field**: row3_val

### TC-198: PT-07 M5 cone scenario 1:20
**Input**: ESP32 test mode, M5 with CONE:57
**Expected**: row3_val="1:20"
**Field**: row3_val

---

## Screenshot Mechanism Tests

### TC-199: SCR-01 USB screenshot trigger
**Input**: Run `python3 tools/screenshot_usb.py /dev/cu.usbmodem14801`
**Expected**: Script sends byte 'S', receives magic 0xDEADBEEF + uint32 size + JPEG data; saves to screenshot_latest.jpg
**Field**: File created in tools/ directory

### TC-200: SCR-02 screenshot during M3 display
**Input**: Set M3 mode with thread display active, then trigger screenshot
**Expected**: JPEG captures M3 UI with thread name, ОБ/МИН, cycles visible
**Field**: screenshot content

### TC-201: SCR-03 screenshot during alert
**Input**: Trigger ALERT:1, then take screenshot
**Expected**: Alert overlay visible in JPEG
**Field**: screenshot content

---

## AFEED Bug Fix Verification Tests

### TC-202: BUG-AFEED-01 value 330 correct display (after fix)
**Input**: `<MODE:2>` `<AFEED:330>`
**Expected**: primary_val = "330" (requires LatheData.afeed_mm changed from int16_t to int32_t)
**Field**: primary_val

### TC-203: BUG-AFEED-02 value 400 correct display (after fix)
**Input**: `<MODE:2>` `<AFEED:400>`
**Expected**: primary_val = "400"
**Field**: primary_val

### TC-204: BUG-AFEED-03 value 3300 (мм/мин × 100 = 33 мм/мин) (after fix)
**Input**: `<MODE:2>` `<AFEED:3300>`
**Expected**: primary_val = "3300" (if units are мм/мин×100 internally)
**Field**: primary_val

### TC-205: BUG-AFEED-04 setAfeedOptimistic with large value
**Input**: ESP32 local optimistic update with afeed=33000 via setAfeedOptimistic
**Expected**: afeed_mm stores 33000 without truncation (requires int32_t)
**Field**: primary_val

---

## Additional State Commands

### TC-206: M3 THREAD_TRAVEL with Ph=4 and 2.50mm
**Input**: `<MODE:3>` `<THREAD:250>` `<PH:4>` `<THREAD_TRAVEL:1000>`
**Expected**: row3_val = "10.00" (1000/100=10.00 mm)
**Field**: row3_val

### TC-207: M3 thread_cycles=0 → "--"
**Input**: `<MODE:3>` `<THREAD_CYCL:0>`
**Expected**: row4_val = "--"
**Field**: row4_val

### TC-208: M6 pass_total_sphr=0 → "--"
**Input**: `<MODE:6>` `<PASS_SPHR:0>`
**Expected**: row4_val = "--"
**Field**: row4_val

### TC-209: STATE:run display
**Input**: `<STATE:run>`
**Expected**: pwr_bg indicator green/active
**Field**: motor state indicator

### TC-210: STATE:stop display
**Input**: `<STATE:stop>`
**Expected**: pwr_bg indicator dark/inactive
**Field**: motor state indicator

### TC-211: PH:1 → M3 shows ОБ/МИН label
**Input**: `<MODE:3>` `<PH:1>` `<RPM_LIM:600>`
**Expected**: row3_title = "ОБ/МИН"; row3_val = "600"
**Field**: row3_title, row3_val

### TC-212: PH:2 → M3 shows ХОД ММ label
**Input**: `<MODE:3>` `<PH:2>` `<THREAD_TRAVEL:300>`
**Expected**: row3_title = "ХОД ММ"; row3_val = "3.00"
**Field**: row3_title, row3_val

### TC-213: M4/M5 SM=1 sub-edit row3 CONE selection
**Input**: In M4 SM=1, tap row3 → ESP32 sends `<TOUCH:SUBSEL:CONE>`
**Expected**: row3 highlighted; STM32 receives SUBSEL:CONE
**Field**: row3 border, STM32 rx

### TC-214: M1/M2 SM=1 sub-edit row3 AP selection
**Input**: In M1 SM=1, tap row3 → ESP32 sends `<TOUCH:SUBSEL:AP>`
**Expected**: row3 highlighted yellow border; STM32 receives SUBSEL:AP
**Field**: row3 border

### TC-215: Sub-edit timeout (5 seconds auto-exit)
**Input**: Enter sub-edit mode (SUBSEL:AP), wait 5 seconds without input
**Expected**: Yellow border disappears; sub-edit exits automatically
**Field**: row3 border disappears

---

*Total: 215 test cases covering all modes, submodes, SelectMenu screens, parameter variations, limit states, touch events, edge cases, and full UART cycles.*
