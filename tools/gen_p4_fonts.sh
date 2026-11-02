#!/usr/bin/env bash
# Генерация шрифтов для 10.1" JC8012P4A1C (1280×800).
#
# UI рассчитан в пикселях 4.3" (480×272). На 10.1" всё масштабируется ×8/3 (1280/480),
# поэтому каждый шрифт генерируется с тем же ИМЕНЕМ символа, что и на 4.3", но ×8/3 по
# размеру: font_tahoma_bold_22 (4.3", 22px) → font_tahoma_bold_22 (P4, 59px).
# Код UI ссылается на шрифты по «дизайнерскому» размеру и одинаков для обоих экранов.
#
# Требуется: lv_font_conv (npm i -g lv_font_conv), Tahoma Bold (macOS),
#            собранный хотя бы раз env jc8012p4a1c (для TTF из библиотеки LVGL).
# Запуск из корня проекта: bash tools/gen_p4_fonts.sh

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

OUT=ELS_Display_P4/fonts
LVF=.pio/libdeps/jc8012p4a1c/lvgl/scripts/built_in_font
TAHOMA="/System/Library/Fonts/Supplemental/Tahoma Bold.ttf"
mkdir -p "$OUT"

scale() { echo $(( ($1 * 8 + 1) / 3 )); }   # округление 4.3"-размера ×8/3

gen() {  # gen <имя> <4.3-размер> <ttf> <диапазоны> [доп. аргументы lv_font_conv]
    local name=$1 base=$2 size; size=$(scale "$2"); local ttf=$3 ranges=$4; shift 4
    echo "  $name: ${base}px → ${size}px"
    lv_font_conv --bpp 4 --size "$size" --no-compress --font "$ttf" -r "$ranges" "$@" \
        --format lvgl --force-fast-kern-format --lv-font-name "$name" -o "$OUT/$name.c"
}

CYR_DEJAVU="0x20-0x7F,0x400-0x45F,0xB0"
CYR_TAHOMA="0x20-0x7E,0x400-0x44F,0xB0"

gen font_dejavu_12      12 "$LVF/DejaVuSans.ttf" "$CYR_DEJAVU"
gen font_dejavu_14      14 "$LVF/DejaVuSans.ttf" "$CYR_DEJAVU"
gen font_dejavu_16      16 "$LVF/DejaVuSans.ttf" "$CYR_DEJAVU"
gen font_tahoma_bold_16 16 "$TAHOMA" "0x20-0x7F,0x400-0x4FF,0xB0"
gen font_tahoma_bold_22 22 "$TAHOMA" "$CYR_TAHOMA"
gen font_tahoma_bold_28 28 "$TAHOMA" "$CYR_TAHOMA"
gen font_tahoma_bold_40 40 "$TAHOMA" "0x20-0x3A,0x47,0x4B,0x69,0x70,0x74"

# Montserrat + символы LVGL (LV_SYMBOL_*) — замена встроенных lv_font_montserrat_NN
SYMBOLS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
for s in 14 20 28; do
    gen "lv_font_montserrat_$s" "$s" "$LVF/Montserrat-Medium.ttf" "0x20-0x7F,0xB0,0x2022" \
        --font "$LVF/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYMBOLS"
done

# lv_font_conv подставляет #include "lvgl/lvgl.h" — в проекте LVGL подключается как "lvgl.h"
sed -i '' -e 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' "$OUT"/*.c
# Файл обёрнут в #if LV_FONT_MONTSERRAT_NN, а в lv_conf P4 встроенные Montserrat
# выключены (=0) — переименовываем защитный макрос, чтобы замена компилировалась
sed -i '' -E 's/LV_FONT_MONTSERRAT_([0-9]+)/LV_FONT_P4_MONTSERRAT_\1/g' "$OUT"/lv_font_montserrat_*.c
echo "Готово: $OUT"
