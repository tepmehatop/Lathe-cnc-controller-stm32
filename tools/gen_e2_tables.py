#!/usr/bin/env python3
"""
Пересчёт таблиц резьб и конусов 7e2 под механику своего станка.

Таблицы в Core/e2/e2_core.cpp (Thread_Info, Cone_Info) — это делители
«фронтов энкодера на шаг мотора», рассчитанные автором 7e2 для энкодера
1800 линий, винта Z 2 мм, винта X 1 мм, 200 шагов × микрошаг 4. Если механика
другая (параметры в Core/els_config.h), делители надо пересчитать — иначе
резьба и конусы получатся не того шага/угла. Прошивка сама не соберётся с
другими параметрами и подскажет запустить этот скрипт.

Запуск:
  python3 tools/gen_e2_tables.py            — проверить: совпадают ли таблицы с конфигом
  python3 tools/gen_e2_tables.py --write    — пересчитать и записать в e2_core.cpp

Формулы (из логики прерываний 7e2):
  резьба Z:  делитель = 2·ENC_LINE_PER_REV / (шаг / винт_Z · шагов_Z_на_оборот)
  резьба X:  то же для винта X
  конус:     делитель = 2·(шагов Z на мм) / (шагов X на мм · уклон по радиусу)
  делитель записывается как целая часть (Ks/Cs) и дробная × 10000 (Km/Cm).
Шаг и уклон берутся из подписей таблицы ("1.50mm", " 8tpi ", "1:10", " 30°", "KM3").
"""
import math, re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONFIG = ROOT / "Core" / "els_config.h"
CORE = ROOT / "Core" / "e2" / "e2_core.cpp"

# Конусы Морзе: конусность (на диаметр) 1 : N
MORSE = {"KM0": 19.212, "KM1": 20.047, "KM2": 20.020, "KM3": 19.922,
         "KM4": 19.254, "KM5": 19.002, "KM6": 19.180}


def config():
    txt = CONFIG.read_text(encoding="utf-8")
    v = {}
    for name in ("ENC_LINE_PER_REV", "MOTOR_Z_STEP_PER_REV", "SCREW_Z", "McSTEP_Z",
                 "MOTOR_X_STEP_PER_REV", "SCREW_X", "McSTEP_X"):
        m = re.search(r"#define\s+" + name + r"\s+(\d+)", txt)
        v[name] = int(m.group(1))
    return v


def split(d):
    ks = int(d)
    km = int(round((d - ks) * 10000))
    if km == 10000:
        ks, km = ks + 1, 0
    return ks, km


# Трубные резьбы: ниток на дюйм по размеру (G — BSPP 55°, K — NPT 60°)
G_TPI = {"1/16": 28, "1/8": 28, "1/4": 19, "3/8": 19, "1/2": 14, "3/4": 14}
K_TPI = {"1/16": 27, "1/8": 27, "1/4": 18, "3/8": 18, "1/2": 14, "3/4": 14}


def thread_pitch(name, step_field):
    """Шаг в мм: метрические — из колонки Step, дюймовые и трубные — точно 25.4 / tpi."""
    t = name.strip('"').strip()
    m = re.match(r"(\d+)tpi", t)
    if m:
        return 25.4 / int(m.group(1))
    if t[0] in "GK":
        size = t[1:].strip()                                  # "1/8", "3/4", "1", "1 1/4" …
        table = G_TPI if t[0] == "G" else K_TPI
        tpi = table.get(size, 11 if t[0] == "G" else 11.5)   # 1" … 2": G — 11, K — 11.5
        return 25.4 / tpi
    return float(step_field)


def cone_slope(label):
    t = label.replace("\\5", "").strip()
    if t in MORSE:
        return 1 / (2 * MORSE[t])
    m = re.match(r"(\d+):(\d+)", t)
    if m:
        return int(m.group(1)) / int(m.group(2)) / 2          # конусность на диаметр → уклон на радиус
    return math.tan(math.radians(int(t)))                    # угол наклона образующей, °


def main():
    c = config()
    z_spm = c["MOTOR_Z_STEP_PER_REV"] * c["McSTEP_Z"] / (c["SCREW_Z"] / 100)   # шагов Z на мм
    x_spm = c["MOTOR_X_STEP_PER_REV"] * c["McSTEP_X"] / (c["SCREW_X"] / 100)
    tick = 2 * c["ENC_LINE_PER_REV"]
    src = CORE.read_text(encoding="utf-8")
    out = src
    diffs = []

    # Резьбы: { Ks_Z, Km_Z, Ks_X, Km_X, "имя", шаг, проходы, обороты },
    thr_re = re.compile(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*("[^"]*"),\s*([\d.]+),(.*?)\}')
    def thr(m):
        p = thread_pitch(m.group(5), m.group(6))
        zk = split(tick / (p * z_spm))
        xk = split(tick / (p * x_spm))
        old = tuple(int(m.group(i)) for i in range(1, 5))
        new = zk + xk
        if old != new:
            diffs.append(f"резьба {m.group(5)}: {old} → {new}")
        return f"{{ {new[0]}, {new[1]}, {new[2]}, {new[3]}, {m.group(5)}, {m.group(6)},{m.group(7)}}}"
    a, b = out.index("const thread_info_type Thread_Info[]"), out.index("#define TOTAL_THREADS")
    out = out[:a] + thr_re.sub(thr, out[a:b]) + out[b:]

    # Конусы: { Cs, Cm, "подпись" },
    cone_re = re.compile(r'\{\s*(\d+),\s*(\d+),\s*("[^"]*")\s*\}')
    def cone(m):
        d = 2 * z_spm / (x_spm * cone_slope(m.group(3).strip('"')))
        new = split(d)
        old = (int(m.group(1)), int(m.group(2)))
        if abs((old[0] + old[1] / 1e4) - d) > 0.002 * d:      # таблица автора округлена — сравниваем с допуском 0.2 %
            diffs.append(f"конус {m.group(3)}: {old} → {new}")
            return f"{{ {new[0]}, {new[1]}, {m.group(3)} }}"
        return m.group(0)
    a, b = out.index("const cone_info_type Cone_Info[]"), out.index("#define TOTAL_CONE")
    out = out[:a] + cone_re.sub(cone, out[a:b]) + out[b:]

    # Делитель резьбы хранится в byte, и к нему прибавляется THRD_ACCEL (разгон) — сумма < 256
    accel = int(re.search(r"#define\s+THRD_ACCEL\s+(\d+)", CONFIG.read_text(encoding="utf-8")).group(1))
    for m in thr_re.finditer(out[out.index("const thread_info_type Thread_Info[]"):out.index("#define TOTAL_THREADS")]):
        if int(m.group(1)) + accel > 255 or int(m.group(3)) + accel > 255:
            print(f"ВНИМАНИЕ: резьба {m.group(5)} — делитель + THRD_ACCEL > 255: такой шаг на этой механике не нарезать")

    # Отметка механики в e2_core.cpp (её сверяет static_assert при сборке)
    stamp = {"E2_TABLES_ENC": c["ENC_LINE_PER_REV"],
             "E2_TABLES_Z_STEPS": c["MOTOR_Z_STEP_PER_REV"] * c["McSTEP_Z"], "E2_TABLES_SCREW_Z": c["SCREW_Z"],
             "E2_TABLES_X_STEPS": c["MOTOR_X_STEP_PER_REV"] * c["McSTEP_X"], "E2_TABLES_SCREW_X": c["SCREW_X"]}
    for k, val in stamp.items():
        out2 = re.sub(r"(#define\s+" + k + r"\s+)\d+", lambda m: m.group(1) + str(val), out)
        if out2 != out:
            out = out2
            if not diffs:
                diffs.append(f"отметка механики {k} → {val}")

    print(f"Механика: энкодер {c['ENC_LINE_PER_REV']} линий, Z {z_spm:g} шаг/мм, X {x_spm:g} шаг/мм")
    if not diffs:
        print("Таблицы соответствуют конфигу — пересчёт не нужен.")
        return 0
    print(f"Расхождений: {len(diffs)}")
    for d in diffs[:20]:
        print("  " + d)
    if "--write" in sys.argv:
        CORE.write_text(out, encoding="utf-8")
        print("Записано в Core/e2/e2_core.cpp — можно собирать прошивку.")
        return 0
    print("Запустите с --write, чтобы записать пересчитанные таблицы.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
