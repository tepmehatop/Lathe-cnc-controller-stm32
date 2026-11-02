#!/usr/bin/env bash
# Сборка прошивки STM32 во всех вариантах конфига: источники координат, типы линеек,
# инверсия сигналов, типы дисплея. Проверяет, что каждое сочетание компилируется.
# Запуск: bash tools/build_matrix.sh
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}

CONFIGS=(
  "-DELS_POS_SOURCE=POS_SOURCE_DRO_UART"
  "-DELS_POS_SOURCE=POS_SOURCE_STEPS"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_QUADRATURE -DSCALE_X_TYPE=SCALE_QUADRATURE"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_SYLVAC48 -DSCALE_X_TYPE=SCALE_SYLVAC48"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_BIN6 -DSCALE_X_TYPE=SCALE_BIN6 -DSCALE_Z_SIGNAL_INVERT=1 -DSCALE_X_SIGNAL_INVERT=1"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_IGAGING21 -DSCALE_X_TYPE=SCALE_IGAGING21"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_DIGIMATIC -DSCALE_X_TYPE=SCALE_DIGIMATIC"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_QUADRATURE -DSCALE_X_TYPE=SCALE_NONE"
  "-DELS_POS_SOURCE=POS_SOURCE_SCALES -DSCALE_Z_TYPE=SCALE_SYLVAC48 -DSCALE_X_TYPE=SCALE_IGAGING21"
  "-DELS_DISPLAY_TYPE=DISPLAY_TYPE_NONE"
  "-DELS_DISPLAY_TYPE=DISPLAY_TYPE_4_3"
)

fail=0
for cfg in "${CONFIGS[@]}"; do
  out=$(PLATFORMIO_BUILD_FLAGS="$cfg" "$PIO" run -e stm32f407_discovery 2>&1)
  if echo "$out" | grep -q "\[SUCCESS\]"; then
    warn=$(echo "$out" | grep -E "Core/e2/e2_|drv_scales|drv_dro|scale_protocols" | grep -c "warning:")
    echo "OK    ($warn предупр.)  $cfg"
  else
    echo "FAIL  $cfg"; echo "$out" | grep -E "error" | head -5; fail=1
  fi
done
# вернуть сборку конфига по умолчанию
"$PIO" run -e stm32f407_discovery >/dev/null 2>&1
exit $fail
