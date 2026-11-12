# Поддержка дисплеев ELS Controller

Проект поддерживает три дисплея. Все используют **одинаковый UI и функционал**, только аппаратная часть и масштабирование отличаются.

## Дисплеи

| Параметр          | JC4827W543          | JC8012P4A1C          | JC1060P470 (не проверен на железе) |
|-------------------|---------------------|----------------------|------------------------------------|
| Размер            | 4.3"                | 10.1"                | 7"                                 |
| Разрешение        | 480×272 (ландшафт)  | 1280×800 (ландшафт*) | 1024×600 (ландшафт, переворот 180°) |
| Чип               | ESP32-S3            | ESP32-P4             | ESP32-P4                           |
| LCD интерфейс     | NV3041A QSPI        | JD9365 MIPI-DSI      | JD9165 MIPI-DSI                    |
| Тач               | GT911 I2C           | GSL3680 I2C          | GT911 I2C                          |
| WiFi/BT           | Встроен в ESP32-S3  | ESP32-C6 (доп. чип)  | ESP32-C6 (доп. чип)                |
| Папка firmware    | `ELS_Display/`      | `ELS_Display_P4/`    | `ELS_Display_P7/`                  |
| PlatformIO env    | `jc4827w543`        | `jc8012p4a1c`        | `jc1060p470`, `jc1060p470_v2`, `jc1060p470_v3` |

*Физически 800×1280 (портрет), LVGL поворот 90° → ландшафт 1280×800

---

## Как собрать прошивку

### JC4827W543 (4.3", ESP32-S3)
```bash
# Сборка
pio run -e jc4827w543

# Сборка + прошивка
pio run -e jc4827w543 --target upload

# Серийный монитор
pio device monitor -e jc4827w543
```

### JC8012P4A1C (10.1", ESP32-P4)
```bash
# Сборка
pio run -e jc8012p4a1c

# Сборка + прошивка
pio run -e jc8012p4a1c --target upload

# Серийный монитор
pio device monitor -e jc8012p4a1c
```

---

### JC1060P470 (7", ESP32-P4) — три варианта

Под одним названием продаются две панели и две ревизии чипа ESP32-P4, прошивки разные:

| env | Метка «V2» на корпусе | Ревизия ESP32-P4 | Файл релиза |
|---|---|---|---|
| `jc1060p470` | нет | 1.x | `els-display-jc1060p470.bin` |
| `jc1060p470_v2` | есть | 1.x | `els-display-jc1060p470_v2.bin` |
| `jc1060p470_v3` | есть | 3.x | `els-display-jc1060p470_v3.bin` |

Ревизию чипа показывает esptool при подключении («Chip is ESP32-P4 (revision v1.3)» или «v3.1»).
Не та панель — белый экран с полосой шума, не та ревизия чипа — экран не включается вовсе; тогда
шить другой файл, плате это не вредит.

```bash
pio run -e jc1060p470 --target upload     # или jc1060p470_v2 / jc1060p470_v3
```

Код — как у 10.1": общий UI из `ELS_Display/src`, драйверы в `ELS_Display_P7/src` (JD9165 от Espressif
`esp_lcd_jd9165`, таблицы инициализации панелей — из ESPHome, тач GT911 — свой опрос по I2C), шрифты
×32/15 в `ELS_Display_P7/fonts` (`OUT=ELS_Display_P7/fonts NUM=32 DEN=15 bash tools/gen_p4_fonts.sh`).
Ноги (`ELS_Display_P7/include/display_config_p7.h`): сброс панели GPIO0, подсветка GPIO23, тач SDA 7 / SCL 8 /
RST 22 / INT 21, UART до STM32 — TX GPIO37 / RX GPIO38 (UART0, как у 10.1"). У владельца проекта этого
дисплея нет: всё взято из открытых описаний платы, замечания с реального экрана — BACKLOG-037.

## Архитектура кода

```
ELS_Display/           ← jc4827w543 (ESP32-S3) + общий код UI
  include/
    display_config.h   ← выбор дисплея + масштабирование
    uart_protocol.h
    gcode_wifi.h
    gcode_exec.h
    wifi_config.h
    lv_conf.h
  src/
    main.cpp           ← setup/loop + UI (#ifdef по дисплею)
    uart_protocol.cpp  ← UART протокол ← SHARED
    gcode_exec.cpp     ← GCode исполнитель ← SHARED
    gcode_wifi.cpp     ← WiFi сервер ← SHARED
    ui_design_*.cpp    ← UI дизайн ← SHARED
    font_*.c           ← шрифты 4.3" (на 10.1" заменяются ELS_Display_P4/fonts/)

ELS_Display_P4/        ← jc8012p4a1c (ESP32-P4): драйверы + шрифты ×8/3
  fonts/               ← шрифты 10.1" (генерируются tools/gen_p4_fonts.sh)
  include/
    display_config_p4.h  ← GPIO пины P4, физ. разрешение, буфер LVGL
    lv_conf.h            ← свой: Montserrat заменены на ×8/3 версии
  src/
    lcd/               ← JD9365 MIPI-DSI драйвер
      esp_lcd_jd9365.c/h
      jd9365_lcd.cpp/h
    touch/             ← GSL3680 I2C touch драйвер
      esp_lcd_gsl3680.c/h
      esp_lcd_touch.c/h
      gsl3680_touch.cpp/h
      gsl_point_id.c/h
```

Общий код (uart, gcode, wifi, UI) компилируется в обоих сборках через `build_src_filter`.
Изменения в shared-файлах автоматически применяются к обоим дисплеям.

---

## Масштабирование UI

UI рисуется в координатах базового экрана **4.3" (480×272)** — это эталон дизайна.
На других разрешениях размеры пересчитываются макросами из `display_config.h`:

| Макрос | Что масштабирует | 4.3" | 10.1" |
|--------|------------------|------|-------|
| `UX(v)` | x, ширина | v | v × 1280/480 |
| `UY(v)` | y, высота | v | v × 800/272 |
| `US(v)` | рамки, радиусы, отступы, метрики шрифтов | v | v × 1280/480 |

**Правило:** любой пиксельный литерал в UI оборачивать в `UX/UY/US` (кроме 0 и множителей/делителей).

**Шрифты:** для 10.1" те же шрифты сгенерированы под теми же именами, но ×8/3 по размеру
(`font_tahoma_bold_22` → 59 px) в `ELS_Display_P4/fonts/` — `bash tools/gen_p4_fonts.sh`.
Встроенные `lv_font_montserrat_14/20/28` (стрелки, иконки) тоже заменены в `ELS_Display_P4/include/lv_conf.h`.
Если в UI появляется новый шрифт — добавить его в `tools/gen_p4_fonts.sh`.

## Отрисовка на 10.1"

LVGL работает с частичной перерисовкой: буфер 1280×64 во внутренней SRAM, flush
поворачивает область сразу во framebuffer DPI-панели (без промежуточных копий).
Время перерисовки: ~10–40 мс (было ~860 мс при full_refresh).

## Тесты и скриншоты

Дисплей принимает по USB CDC: `S` — скриншот, `<BTN:x>` — нажатие (уходит в STM32 как `<TOUCH:x>`),
`<TAP:x,y>` — касание в координатах 480×272, `<CMD:v>` — команда как от STM32.

```bash
# Протокольные тесты A/B/C (~326 кейсов) со скриншотами
python3 tools/run_all_tests.py --port /dev/cu.usbmodemXXXX --out <папка>
# UI-сценарии через касания (меню, барабаны, оверлеи)
python3 tools/ui_tap_tests.py  --port /dev/cu.usbmodemXXXX --out <папка>
```

## Подключение к STM32

UART 57600 8N1, уровни 3.3 В, три провода:

| STM32F4DISCOVERY | 4.3" JC4827W543 (разъём P1) | 10.1" JC8012P4A1C (разъём CN2) |
|---|---|---|
| PB10 (TX, штырёк P1-34) | GPIO44 (RX) | GPIO38 (RX) |
| PB11 (RX, штырёк P1-35) | GPIO43 (TX) | GPIO37 (TX) |
| GND | GND | GND |

Питание дисплея — от его USB-C или отдельных 5 В, не от платы Discovery.
Схема — [docs/wiring/links.svg](docs/wiring/links.svg).

## Выбор дисплея на STM32

В `Core/els_config.h`: `ELS_DISPLAY_TYPE` = `DISPLAY_TYPE_4_3` / `DISPLAY_TYPE_10_1` /
`DISPLAY_TYPE_NONE` (только LCD2004). Дисплей при старте (и в ответ на `<PING>` от STM32)
сообщает свой тип `<DISP:43>` / `<DISP:101>`; при несовпадении с конфигом STM32 выводит
алерт «ДИСПЛЕЙ НЕ СОВПАДАЕТ С КОНФИГОМ STM32».

## Как добавить фичу/фикс для обоих дисплеев

1. Изменяешь файл в `ELS_Display/src/` (uart_protocol, gcode, UI)
2. Оба env (`jc4827w543` и `jc8012p4a1c`) подхватывают изменение автоматически
3. Если нужна дисплей-специфичная логика — добавляй `#ifdef DISPLAY_JC8012P4A1C` в main.cpp

---

## Пины JC8012P4A1C

| Назначение       | GPIO | Примечание                           |
|------------------|------|--------------------------------------|
| LCD Reset        | 27   |                                      |
| LCD Backlight    | 23   | ON=HIGH                              |
| Touch SDA        | 7    | I2C0                                 |
| Touch SCL        | 8    | I2C0                                 |
| Touch Reset      | 22   | Active LOW                           |
| Touch INT        | 21   | Active LOW                           |
| UART RX (STM32)  | 38   | разъём CN2 "INPUT" (UART0_RXD)      |
| UART TX (STM32)  | 37   | разъём CN2 "INPUT" (UART0_TXD)      |

UART пины сверены со схемой `docs/JC8012P4A1C_I_W_Y/5-Schematic/` (3_ESP32-P4, 4_CONN).
GPIO14..19 заняты SDIO до ESP32-C6 (WiFi) — для UART их использовать нельзя.

---

## Требования к платформе для JC8012P4A1C

- Arduino-ESP32 >= 3.1.0 (поддержка ESP32-P4 chip)
- pioarduino platform >= 55.03.10
- Board: `esp32p4dev`

Если сборка падает с ошибкой платформы, обновить URL в `platformio.ini`:
```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.10/platform-espressif32.zip
```

---

## Беклог (не сейчас)

- [ ] Использовать дополнительное место на 10.1" — вывести цифровые кнопки на экране
- [ ] Добавить отображение дополнительных параметров (больше строк в правой панели)
- [ ] Аппаратное ускорение поворота через PPA (ESP32-P4 DMA2D) — сейчас программный поворот области, достаточно быстро
- [ ] Ландшафтный режим без LVGL-поворота (прямая инициализация DSI в ландшафте)
