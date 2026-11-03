#!/usr/bin/env bash
# Генерация шрифтов подписей для 4.3" JC4827W543 (480×272) — те же параметры, что у исходных
# файлов ELS_Display/src (строка «Opts» в их заголовке), плюс знак диаметра ⌀ (U+2300).
#
# Требуется: lv_font_conv (npm i -g lv_font_conv), Tahoma Bold (macOS),
#            собранный хотя бы раз env jc4827w543 (для DejaVuSans.ttf из библиотеки LVGL).
# Запуск из корня проекта: bash tools/gen_43_fonts.sh
# Пути относительные — в «Opts» сгенерированных файлов не попадает путь к домашней папке.

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

LVF=.pio/libdeps/jc4827w543/lvgl/scripts/built_in_font
TAHOMA="/System/Library/Fonts/Supplemental/Tahoma Bold.ttf"
CYR="0x20-0x7F,0x400-0x4FF,0xB0"

for s in 12 14 16; do
    echo "  font_dejavu_$s"
    lv_font_conv --bpp 4 --size "$s" --no-compress --font "$LVF/DejaVuSans.ttf" -r "$CYR,0x2300" \
        --format lvgl --output "ELS_Display/src/font_dejavu_$s.c" --force-fast-kern-format
done

echo "  font_tahoma_bold_16 (⌀ из DejaVu)"
lv_font_conv --bpp 4 --size 16 --no-compress --font "$TAHOMA" -r "$CYR" --font "$LVF/DejaVuSans.ttf" -r 0x2300 \
    --format lvgl --output ELS_Display/src/fonts_bold/font_tahoma_bold_16.c --force-fast-kern-format

echo "Готово"
