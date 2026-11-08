#!/usr/bin/env bash
# Разворот циклов G71/G70 (код дисплея) на компьютере: все примеры gcode_sample/, проверка «резец не заходит
# в деталь», затем развёрнутые программы с циклами — через симулятор STM32 (каждая строка — OK).
# Запуск: bash tests/gcode_cycles/run.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
T=${TMPDIR:-/tmp}
c++ -std=c++17 -O1 -Wall -Werror -I ELS_Display/include tests/gcode_cycles/expand.cpp ELS_Display/src/gcode_cycles.cpp \
    -o "$T/gc_expand"
fail=0
for f in gcode_sample/*.nc; do
    echo "== $f"
    "$T/gc_expand" "$f" "$T/gc_$(basename "$f")" || fail=1
done
# контур чистового толще чернового — проверка обязана поймать врезание
printf 'G0 X30 Z2\nG71 U1 R0.5\nG71 P10 Q20 U0 W0 F0.1\nN10 G0 X10\nG1 Z-20\nN20 G1 X30\nG70 P30 Q40 F0.1\nN30 G0 X12\nG1 Z-20\nN40 G1 X30\nM30\n' > "$T/gc_neg.nc"
if "$T/gc_expand" "$T/gc_neg.nc" >/dev/null; then echo "ПРОВЕРКА ВРЕЗАНИЯ НЕ СРАБОТАЛА"; fail=1; else echo "== врезание ловится"; fi
for f in gcode_sample/*.nc; do
    grep -qiE 'G0*7[01]' "$f" || continue
    echo "== симулятор STM32: $f"
    SIM_GCFILE="$T/gc_$(basename "$f")" TMPDIR="$T" bash tools/run_e2_sim.sh | tail -1 || fail=1
done
[ $fail = 0 ] && echo "ИТОГ: OK" || { echo "ИТОГ: FAIL"; exit 1; }
