#!/usr/bin/env bash
# Юнит-тесты логики, не зависящей от железа (запуск на ПК, нужен c++).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
OUT=$(mktemp -d)
c++ -std=c++11 -Wall -Wextra -Werror -IDrivers \
    tests/test_scale_protocols.cpp Drivers/scale_protocols.cpp -o "$OUT/test_scale_protocols"
"$OUT/test_scale_protocols"
rm -rf "$OUT"
