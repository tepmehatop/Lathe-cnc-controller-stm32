#!/usr/bin/env bash
# Проверка ядра 7e2 и аппаратного слоя STM32 на компьютере (без платы):
# резьбы, подачи, ускоренный ход, РГИ, упоры, режимы с дисплея.
# Запуск: bash tools/run_e2_sim.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
OUT=${TMPDIR:-/tmp}/e2_sim
c++ -std=c++17 -O1 -w -fsigned-char -fsingle-precision-constant \
    -Itests/e2_host/mock -ICore -ICore/e2 -IDrivers \
    tests/e2_host/sim.cpp tests/e2_host/stubs.cpp \
    Core/e2/e2_hal.cpp Core/e2/e2_lcd.cpp \
    -o "$OUT"
"$OUT"
