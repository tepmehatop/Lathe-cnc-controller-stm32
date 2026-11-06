#!/usr/bin/env bash
# Файлы релиза в dist/ — то же делает GitHub Actions по тегу v* (.github/workflows/release.yml).
#   STM32:    els-stm32-<env>.bin            — прошивать с адреса 0x08000000 (ST-Link / STM32CubeProgrammer)
#   Дисплеи:  els-display-<env>.bin          — один файл (загрузчик + разделы + программа), с адреса 0x0
# Пока под станок владельца (BACKLOG-030): STM32 с блоком DRO, механика — значения из Core/els_config.h.
# Другие варианты: STM32_ENVS="stm32f407_discovery stm32f407_dro stm32f407_scales" bash tools/make_release.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
PIO=${PIO:-$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")}
STM32_ENVS=${STM32_ENVS:-stm32f407_dro}
DISPLAY_ENVS=${DISPLAY_ENVS:-"jc4827w543 jc8012p4a1c"}
OUT=dist
rm -rf "$OUT"; mkdir -p "$OUT"

# Wi-Fi дисплея (G-код): ELS_RELEASE — в релиз идёт только шаблон wifi_config.example.h,
# данные из локального wifi_config.h в файлы релиза не попадают
export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS:-} -DELS_RELEASE"

for e in $STM32_ENVS; do
    "$PIO" run -e "$e"
    cp ".pio/build/$e/firmware.bin" "$OUT/els-stm32-${e#stm32f407_}.bin"
done

for e in $DISPLAY_ENVS; do
    "$PIO" run -e "$e"
    # Смещения загрузчика и разделов — из метаданных сборки (у S3 загрузчик с 0x0, у P4 с 0x2000)
    args=$("$PIO" project metadata -e "$e" --json-output | python3 -c '
import json, sys
env = sys.argv[1]
v = json.load(sys.stdin)[env]
chip = {"jc4827w543": "esp32s3", "jc8012p4a1c": "esp32p4"}[env]
parts = ["--chip", chip, "merge_bin", "-o", "OUT"]
for i in v["extra"]["flash_images"]:
    parts += [i["offset"], i["path"]]
parts += [v["extra"].get("application_offset") or "0x10000", ".pio/build/%s/firmware.bin" % env]
print(" ".join(parts))' "$e")
    "$PIO" pkg exec -p tool-esptoolpy -- esptool.py ${args/OUT/$OUT/els-display-$e.bin}
done

( cd "$OUT" && shasum -a 256 *.bin > SHA256SUMS.txt )
ls -l "$OUT"
