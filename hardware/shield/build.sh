#!/usr/bin/env bash
# Сборка шилда: схема → размещение → (трассировка) → проверки → файлы для заказа.
#
#   bash hardware/shield/build.sh            — схема, плата без дорожек, BOM, проверки
#   bash hardware/shield/build.sh route      — то же + автотрассировка Freerouting
#
# Для трассировки нужен Freerouting (https://github.com/freerouting/freerouting/releases)
# и Java 17+. Путь к jar — в переменной FREEROUTING_JAR.
set -euo pipefail
cd "$(dirname "$0")"
KICAD=/Applications/KiCad/KiCad.app/Contents
KPY=$KICAD/Frameworks/Python.framework/Versions/3.9/bin/python3
KCLI=$KICAD/MacOS/kicad-cli
mkdir -p build

python3 gen_sch.py
python3 gen_bom.py
"$KCLI" sch erc --severity-all -o build/erc.rpt els_shield.kicad_sch | tail -1
"$KPY" gen_pcb.py place 2>&1 | grep -v "wxApp\|assert" || true

if [ "${1:-}" = "route" ]; then
  : "${FREEROUTING_JAR:?укажите путь: FREEROUTING_JAR=/путь/freerouting.jar}"
  java -jar "$FREEROUTING_JAR" -de build/els_shield.dsn -do build/els_shield.ses -mp 40
  "$KPY" gen_pcb.py import build/els_shield.ses 2>&1 | grep -v "wxApp\|assert" || true
fi

"$KCLI" pcb drc --severity-all -o build/drc.rpt els_shield.kicad_pcb | tail -2
"$KCLI" sch export pdf -o els_shield_schematic.pdf els_shield.kicad_sch >/dev/null

if [ "${1:-}" = "route" ]; then
  mkdir -p gerbers
  "$KCLI" pcb export gerbers -o gerbers/ --layers F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts els_shield.kicad_pcb >/dev/null
  "$KCLI" pcb export drill -o gerbers/ els_shield.kicad_pcb >/dev/null
  (cd gerbers && zip -q ../els_shield_gerbers.zip ./*)
  echo "Файлы для заказа: hardware/shield/els_shield_gerbers.zip"
fi
