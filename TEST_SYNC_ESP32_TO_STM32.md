# Тесты синхронизации: ESP32 → STM32 (Direction B)

## Назначение
Проверяет что нажатия на тачскрин ESP32 корректно передаются на STM32 и STM32 отвечает корректными данными на оба дисплея.

## Механизм
ПК отправляет `<BTN:name>` на ESP32 через USB CDC.
ESP32 пересылает `<TOUCH:name>` на STM32 через UART.
STM32 обрабатывает команду и отвечает обновлёнными данными на ESP32.
ESP32 обновляет дисплей. Старый LCD обновляется через STM32.

## Примеры BTN команд
```
<BTN:M1>        → ESP32 отправляет <TOUCH:M1>   → STM32 вызывает _set_mode(MODE_FEED)
<BTN:M2>        → ESP32 отправляет <TOUCH:M2>   → STM32 вызывает _set_mode(MODE_AFEED)
<BTN:S1>        → ESP32 отправляет <TOUCH:S1>   → STM32 вызывает _set_submode(SUBMODE_INTERNAL)
<BTN:KEY:UP>    → ESP32 отправляет <TOUCH:KEY:UP>
<BTN:PARAM_OK>  → ESP32 отправляет <TOUCH:PARAM_OK>
<BTN:KEY:LEFT>  → ESP32 отправляет <TOUCH:KEY:LEFT>
<BTN:AFEED:200> → ESP32 отправляет <TOUCH:AFEED:200>
<BTN:FEED:50>   → ESP32 отправляет <TOUCH:FEED:50>
<BTN:AP:25>     → ESP32 отправляет <TOUCH:AP:25>
```

## Запуск
```bash
python3 tools/test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --dir B
```

Одиночный тест:
```bash
python3 tools/test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --tc B-119
```

---

## Тестовые кейсы

| ID | Описание | Начальное состояние STM32 | BTN команды | Ожидаемое на ESP32 | Ожидаемое на LCD | Скриншот |
|----|----------|--------------------------|------------|-------------------|-----------------|----------|
| B-001 | Переключить на M1 | MODE:1 | BTN:M1 | Режим M1 | LCD M1 | B-001.jpg |
| B-002 | Переключить на M2 | MODE:0 | BTN:M2 | Режим M2 | LCD M2 | B-002.jpg |
| B-003 | Переключить на M3 | MODE:0 | BTN:M3 | Режим M3 | LCD M3 | B-003.jpg |
| B-004 | Переключить на M4 | MODE:0 | BTN:M4 | Режим M4 | LCD M4 | B-004.jpg |
| B-005 | Переключить на M5 | MODE:0 | BTN:M5 | Режим M5 | LCD M5 | B-005.jpg |
| B-006 | Переключить на M6 | MODE:0 | BTN:M6 | Режим M6 | LCD M6 | B-006.jpg |
| B-007 | Переключить на M7 | MODE:0 | BTN:M7 | Режим M7 | LCD M7 | B-007.jpg |
| B-008 | M2 → M1 назад | MODE:1 | BTN:M1 | Режим M1 | LCD M1 | B-008.jpg |
| B-009 | M3 → M1 назад | MODE:2 | BTN:M1 | Режим M1 | LCD M1 | B-009.jpg |
| B-010 | M4 → M2 | MODE:3 | BTN:M2 | Режим M2 | LCD M2 | B-010.jpg |
| B-011 | M1 → подрежим S1 | MODE:0, SUB:1 | BTN:S1 | M1/S1 | LCD S1 | B-011.jpg |
| B-012 | M1 → подрежим S2 | MODE:0, SUB:0 | BTN:S2 | M1/S2 | LCD S2 | B-012.jpg |
| B-013 | M1 → подрежим S3 | MODE:0, SUB:0 | BTN:S3 | M1/S3 | LCD S3 | B-013.jpg |
| B-014 | M2 → S1 | MODE:1, SUB:2 | BTN:S1 | M2/S1 | LCD M2/S1 | B-014.jpg |
| B-015 | M2 → S2 | MODE:1, SUB:0 | BTN:S2 | M2/S2 | LCD M2/S2 | B-015.jpg |
| B-016 | M2 → S3 | MODE:1, SUB:1 | BTN:S3 | M2/S3 | LCD M2/S3 | B-016.jpg |
| B-017 | M3 → S1 | MODE:2, SUB:1 | BTN:S1 | M3/S1 | LCD M3/S1 | B-017.jpg |
| B-018 | M3 → S2 | MODE:2, SUB:0 | BTN:S2 | M3/S2 | LCD M3/S2 | B-018.jpg |
| B-019 | M3 → S3 | MODE:2, SUB:0 | BTN:S3 | M3/S3 | LCD M3/S3 | B-019.jpg |
| B-020 | M4 → S2 | MODE:3, SUB:0 | BTN:S2 | M4/S2 | LCD M4/S2 | B-020.jpg |
| B-021 | M1 KEY:UP подача++ | MODE:0, FEED:25 | BTN:KEY:UP | Feed > 0.25 | LCD feed++ | B-021.jpg |
| B-022 | M1 KEY:UP × 5 | MODE:0, FEED:25 | BTN:KEY:UP ×5 | Feed +5 шагов | LCD feed+5 | B-022.jpg |
| B-023 | M2 KEY:UP aFeed++ | MODE:1, AFEED:100 | BTN:KEY:UP | aFeed > 100 | LCD afeed++ | B-023.jpg |
| B-024 | M2 KEY:UP × 5 | MODE:1, AFEED:100 | BTN:KEY:UP ×5 | aFeed +5 | LCD afeed+5 | B-024.jpg |
| B-025 | M3 KEY:UP шаг++ | MODE:2 | BTN:KEY:UP | Thread шаг++ | LCD thread++ | B-025.jpg |
| B-026 | M3 KEY:UP × 3 | MODE:2 | BTN:KEY:UP ×3 | Thread +3 шага | LCD thread+3 | B-026.jpg |
| B-027 | M4 KEY:UP конус++ | MODE:3 | BTN:KEY:UP | Конус шаг++ | LCD cone++ | B-027.jpg |
| B-028 | M5 KEY:UP | MODE:4 | BTN:KEY:UP | M5 шаг++ | LCD M5++ | B-028.jpg |
| B-029 | M7 KEY:UP метка++ | MODE:6, DIVN:12, DIVM:1 | BTN:KEY:UP | Метка 2 | LCD metka=2 | B-029.jpg |
| B-030 | M7 KEY:UP × 3 | MODE:6, DIVN:12, DIVM:1 | BTN:KEY:UP ×3 | Метка 4 | LCD metka=4 | B-030.jpg |
| B-031 | M1 KEY:DN подача-- | MODE:0, FEED:50 | BTN:KEY:DN | Feed < 0.50 | LCD feed-- | B-031.jpg |
| B-032 | M2 KEY:DN aFeed-- | MODE:1, AFEED:200 | BTN:KEY:DN | aFeed < 200 | LCD afeed-- | B-032.jpg |
| B-033 | M3 KEY:DN шаг-- | MODE:2 | BTN:KEY:DN | Thread шаг-- | LCD thread-- | B-033.jpg |
| B-034 | M4 KEY:DN конус-- | MODE:3 | BTN:KEY:DN | Конус шаг-- | LCD cone-- | B-034.jpg |
| B-035 | M7 KEY:DN метка-- | MODE:6, DIVN:12, DIVM:6 | BTN:KEY:DN | Метка 5 | LCD metka=5 | B-035.jpg |
| B-036 | M1 PARAM_OK SM1→SM2 | MODE:0, SELECTMENU:1 | BTN:PARAM_OK | SelectMenu 2 | LCD SM=2 | B-036.jpg |
| B-037 | M1 PARAM_OK SM2→SM3 | MODE:0, SELECTMENU:2 | BTN:PARAM_OK | SelectMenu 3 | LCD SM=3 | B-037.jpg |
| B-038 | M1 PARAM_OK SM3→SM1 | MODE:0, SELECTMENU:3 | BTN:PARAM_OK | SelectMenu 1 | LCD SM=1 | B-038.jpg |
| B-039 | M2 PARAM_OK SM1→SM2 | MODE:1, SELECTMENU:1 | BTN:PARAM_OK | M2 SM=2 | LCD M2 SM=2 | B-039.jpg |
| B-040 | M3 PARAM_OK SM1→SM2 | MODE:2, SELECTMENU:1 | BTN:PARAM_OK | M3 SM=2 | LCD M3 SM=2 | B-040.jpg |
| B-041 | M1 KEY:RIGHT SM→SM+1 | MODE:0, SELECTMENU:1 | BTN:KEY:RIGHT | SM=2 | LCD SM=2 | B-041.jpg |
| B-042 | M1 KEY:RIGHT SM2→SM3 | MODE:0, SELECTMENU:2 | BTN:KEY:RIGHT | SM=3 | LCD SM=3 | B-042.jpg |
| B-043 | M1 KEY:LEFT SM3→SM2 | MODE:0, SELECTMENU:3 | BTN:KEY:LEFT | SM=2 | LCD SM=2 | B-043.jpg |
| B-044 | M1 KEY:LEFT SM2→SM1 | MODE:0, SELECTMENU:2 | BTN:KEY:LEFT | SM=1 | LCD SM=1 | B-044.jpg |
| B-045 | M2 KEY:RIGHT SM1→SM2 | MODE:1, SELECTMENU:1 | BTN:KEY:RIGHT | M2 SM=2 | LCD M2 SM=2 | B-045.jpg |
| B-046 | M1→M2→M1 туда-обратно | MODE:0 | BTN:M2, BTN:M1 | Вернулся в M1 | LCD M1 | B-046.jpg |
| B-047 | M1→M3→M1 туда-обратно | MODE:0 | BTN:M3, BTN:M1 | Вернулся в M1 | LCD M1 | B-047.jpg |
| B-048 | M2→M3→M2 туда-обратно | MODE:1 | BTN:M3, BTN:M2 | Вернулся в M2 | LCD M2 | B-048.jpg |
| B-049 | M1→M2→M3→M4→M1 цикл | MODE:0 | M2,M3,M4,M1 | Вернулся в M1 | LCD M1 | B-049.jpg |
| B-050 | M7→M6→M5→M4→M3 | MODE:6 | M6,M5,M4,M3 | M3 активен | LCD M3 | B-050.jpg |
| B-051 | M2 AFEED:100 через BTN | MODE:1 | BTN:AFEED:100 | aFeed=100 | LCD aFeed=100 | B-051.jpg |
| B-052 | M2 AFEED:200 через BTN | MODE:1 | BTN:AFEED:200 | aFeed=200 | LCD aFeed=200 | B-052.jpg |
| B-053 | M2 AFEED:300 через BTN | MODE:1 | BTN:AFEED:300 | aFeed=300 | LCD aFeed=300 | B-053.jpg |
| B-054 | M1 FEED:25 через BTN | MODE:0 | BTN:FEED:25 | Feed=0.25 | LCD Feed=0.25 | B-054.jpg |
| B-055 | M1 FEED:50 через BTN | MODE:0 | BTN:FEED:50 | Feed=0.50 | LCD Feed=0.50 | B-055.jpg |
| B-056 | M1 FEED:100 через BTN | MODE:0 | BTN:FEED:100 | Feed=1.00 | LCD Feed=1.00 | B-056.jpg |
| B-057 | M1 AP:25 через BTN | MODE:0 | BTN:AP:25 | AP=0.25 | LCD AP=0.25 | B-057.jpg |
| B-058 | M1 AP:50 через BTN | MODE:0 | BTN:AP:50 | AP=0.50 | LCD AP=0.50 | B-058.jpg |
| **B-119** | **aFeed баг: M1→M2 afeed=330** | **MODE:0, AFEED:330** | **BTN:M2** | **ESP32 показывает 330** (не -32512!) | **LCD aFeed=330** | B-119.jpg |
| **B-120** | **aFeed баг: M3→M2 afeed=100** | **MODE:2, AFEED:100** | **BTN:M2** | **ESP32 показывает 100** | **LCD aFeed=100** | B-120.jpg |
| **B-121** | **aFeed баг: M4→M2 afeed=200** | **MODE:3, AFEED:200** | **BTN:M2** | **ESP32 показывает 200** | **LCD aFeed=200** | B-121.jpg |
| **B-122** | **aFeed баг: M1 SM2→M2** | **MODE:0, SM:2, AFEED:150** | **BTN:M2** | **ESP32 показывает 150** | **LCD aFeed=150** | B-122.jpg |
| **B-123** | **aFeed баг: M2→M1→M2 цикл** | **MODE:1, AFEED:250** | **BTN:M1, BTN:M2** | **ESP32 показывает 250** | **LCD aFeed=250** | B-123.jpg |
| B-059–B-200 | (см. test_bidir.py) | ... | ... | ... | ... | ... |

---

## Важные замечания

### Баг -32512 (B-119 — B-123)
Эти тесты специально проверяют сценарий баги. При переключении на M2 через тачскрин ESP32 
должен отображать корректное значение aFeed (например 330), а не -32512.

Ожидаемый результат:
- `[DBG] AFEED raw='330' int=330 int16=330` в USB CDC логе
- `[DBG] AFEED display: 330` в USB CDC логе
- Скриншот показывает "330" в поле подачи

Если в USB CDC выводится:
- `[DBG] mode tap M2 afeed_mm_before=XXX` — значение до переключения
- `[DBG] AFEED raw='YYY' int=YYY int16=ZZZ` — что реально прислал STM32

### Старый LCD (LCD2004)
Автоматически верифицировать старый LCD нельзя — только визуально.
При каждом тесте Direction B старый LCD должен:
1. Обновить режим (M1-M7)
2. Обновить подрежим (S1-S3)
3. Обновить значения подачи/параметров
4. Обновить позиции Z/X при движении

### SelectMenu
STM32 использует `SELECTMENU` (1/2/3) который выбирает подэкран.
При переключении через PARAM_OK или KEY:LEFT/RIGHT STM32 обновляет `els.select_menu` 
и рассылает `<SELECTMENU:N>` на ESP32 — ESP32 перерисовывает правую панель.
