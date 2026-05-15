# Поддержка дисплеев ELS Controller

Проект поддерживает два дисплея. Оба используют **одинаковый UI и функционал**, только аппаратная часть и масштабирование отличаются.

## Дисплеи

| Параметр          | JC4827W543          | JC8012P4A1C          |
|-------------------|---------------------|----------------------|
| Размер            | 4.3"                | 10.1"                |
| Разрешение        | 480×272 (ландшафт)  | 1280×800 (ландшафт*) |
| Чип               | ESP32-S3            | ESP32-P4             |
| LCD интерфейс     | NV3041A QSPI        | JD9365 MIPI-DSI      |
| Тач               | GT911 I2C           | GSL3680 I2C          |
| WiFi/BT           | Встроен в ESP32-S3  | ESP32-C6 (доп. чип)  |
| Папка firmware    | `ELS_Display/`      | `ELS_Display_P4/`    |
| PlatformIO env    | `jc4827w543`        | `jc8012p4a1c`        |

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
    font_*.c           ← шрифты ← SHARED

ELS_Display_P4/        ← jc8012p4a1c (ESP32-P4) только драйверы
  include/
    display_config_p4.h  ← GPIO пины P4, физ. разрешение
    lv_conf.h
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
| UART RX (STM32)  | 16   | **TODO: уточнить по схеме модуля**   |
| UART TX (STM32)  | 17   | **TODO: уточнить по схеме модуля**   |

⚠️ **UART пины (GPIO16/17) требуют проверки по схеме** `docs/JC8012P4A1C_I_W_Y/5-Schematic/`.
После уточнения обновить `ELS_Display_P4/include/display_config_p4.h`.

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
- [ ] Аппаратное ускорение поворота через PPA (ESP32-P4 DMA2D)
- [ ] Ландшафтный режим без LVGL-поворота (прямая инициализация DSI в ландшафте)
