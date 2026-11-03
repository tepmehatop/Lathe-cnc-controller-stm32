#!/usr/bin/env bash
# Проверка ядра 7e2 и аппаратного слоя STM32 на компьютере (без платы):
# резьбы, подачи, ускоренный ход, РГИ, упоры, режимы с дисплея.
# Запуск: bash tools/run_e2_sim.sh
# Матрица автоциклов (≈40 мин виртуального времени, несколько минут реального):
#   SIM_CYCLES=1 bash tools/run_e2_sim.sh            — все 29 циклов
#   SIM_CYCLES=1 SIM_CYC_ONLY="резьба 1.50" bash ...  — только случаи с подстрокой в названии
# Люфт поперечного винта (модель механики) и реальная глубина резания: SIM_BACKLASH=1 bash tools/run_e2_sim.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
OUT=${TMPDIR:-/tmp}/e2_sim
c++ -std=c++17 -O1 -w -fsigned-char -fsingle-precision-constant ${SIM_FLAGS:-} \
    -Itests/e2_host/mock -ICore -ICore/e2 -IDrivers \
    tests/e2_host/sim.cpp tests/e2_host/stubs.cpp \
    Core/e2/e2_hal.cpp Core/e2/e2_lcd.cpp \
    -o "$OUT"
"$OUT"
