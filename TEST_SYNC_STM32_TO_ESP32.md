# Тесты синхронизации: STM32 → ESP32 (Direction A)

## Назначение
Проверяет что данные отправленные из STM32 (через USART1 VCP) корректно отображаются на новом дисплее ESP32.

## Подключение
- **ESP32 USB CDC**: `/dev/cu.usbmodem14801` (уже подключен)
- **STM32 USART1 VCP**: Подключите кабель USB от ПК к **Discovery board USB** (не ST-Link V2 донгл!).
  Discovery board имеет встроенный ST-Link с VCP подключённым к USART1 (PA9=TX, PA10=RX).
  На macOS появится новый порт вида `/dev/cu.usbmodemXXXX`.
- **ST-Link V2 standalone dongle**: НЕЛЬЗЯ использовать для UART команд — только для SWD программирования.

## Запуск
```bash
python3 tools/test_bidir.py --esp32 /dev/cu.usbmodem14801 --stm32 /dev/cu.usbmodemXXXX --dir A
```

## Формат команд STM32
Команды отправляются в USART1 в формате `CMD:VALUE\r\n`:
- `MODE:0` — режим (0=Feed, 1=aFeed, 2=Thread, 3=ConeL, 4=ConeR, 5=Sphere, 6=Divider)
- `SUB:0` — подрежим (0=Internal, 1=Manual, 2=External)
- `FEED:25` — подача мм/об × 100 (25 = 0.25 мм/об)
- `AFEED:330` — асинхронная подача мм/мин
- `AP:50` — съём за проход мм × 100 (50 = 0.50 мм)
- `PASS:3,10` — проход N/Total
- `RPM:1500` — обороты шпинделя
- `POS_Z:123450` — позиция Z в мкм (0.001мм)
- `POS_X:-67890` — позиция X в мкм
- `LIMITS:1,0,0,1` — концевики L,R,F,Re
- `SELECTMENU:1` — SelectMenu (1/2/3)
- `SENDALL` — отправить всё состояние на ESP32

---

## Тестовые кейсы

| ID | Описание | Команды STM32 | Ожидаемое на ESP32 | Ожидаемое на старом LCD | Скриншот |
|----|----------|---------------|-------------------|------------------------|----------|
| A-001 | M1/S1 Feed 0.25 | MODE:0, SUB:0, FEED:25, SENDALL | Режим M1, Подача 0.25 мм/об | M1 Sub=Int Feed=0.25 | A-001.jpg |
| A-002 | M1/S2 Feed 0.50 | MODE:0, SUB:1, FEED:50, SENDALL | M1 Ручной, 0.50 | M1 Sub=Man Feed=0.50 | A-002.jpg |
| A-003 | M1/S3 Feed 1.00 | MODE:0, SUB:2, FEED:100, SENDALL | M1 Наружный, 1.00 | M1 Sub=Ext Feed=1.00 | A-003.jpg |
| A-004 | M2/S1 aFeed 100 | MODE:1, SUB:0, AFEED:100, SENDALL | Режим M2, aFeed 100 мм/мин | M2 AFeed=100 | A-004.jpg |
| A-005 | M2/S2 aFeed 200 | MODE:1, SUB:1, AFEED:200, SENDALL | M2 Ручной, 200 | M2 Man AFeed=200 | A-005.jpg |
| A-006 | M2/S3 aFeed 300 | MODE:1, SUB:2, AFEED:300, SENDALL | M2 Наружный, 300 | M2 Ext AFeed=300 | A-006.jpg |
| A-007 | M3/S1 Thread Int | MODE:2, SUB:0, SENDALL | Режим M3 Внутренний | M3 Thread Int | A-007.jpg |
| A-008 | M3/S2 Thread Man | MODE:2, SUB:1, SENDALL | M3 Ручной | M3 Thread Man | A-008.jpg |
| A-009 | M3/S3 Thread Ext | MODE:2, SUB:2, SENDALL | M3 Наружный | M3 Thread Ext | A-009.jpg |
| A-010 | M4/S1 ConeL Int | MODE:3, SUB:0, SENDALL | M4 Конус L Внутренний | M4 ConeL Int | A-010.jpg |
| A-011 | M4/S2 ConeL Man | MODE:3, SUB:1, SENDALL | M4 Конус L Ручной | M4 ConeL Man | A-011.jpg |
| A-012 | M4/S3 ConeL Ext | MODE:3, SUB:2, SENDALL | M4 Конус L Наружный | M4 ConeL Ext | A-012.jpg |
| A-013 | M5/S1 ConeR Int | MODE:4, SUB:0, SENDALL | M5 Конус R Внутренний | M5 ConeR Int | A-013.jpg |
| A-014 | M5/S2 ConeR Man | MODE:4, SUB:1, SENDALL | M5 Конус R Ручной | M5 ConeR Man | A-014.jpg |
| A-015 | M5/S3 ConeR Ext | MODE:4, SUB:2, SENDALL | M5 Конус R Наружный | M5 ConeR Ext | A-015.jpg |
| A-016 | M6/S1 Sphere Int | MODE:5, SUB:0, SENDALL | M6 Шар Внутренний | M6 Sphere Int | A-016.jpg |
| A-017 | M6/S2 Sphere Man | MODE:5, SUB:1, SENDALL | M6 Шар Ручной | M6 Sphere Man | A-017.jpg |
| A-018 | M6/S3 Sphere Ext | MODE:5, SUB:2, SENDALL | M6 Шар Наружный | M6 Sphere Ext | A-018.jpg |
| A-019 | M7/S1 Divider Int | MODE:6, SUB:0, SENDALL | M7 Делитель Внутренний | M7 Divider Int | A-019.jpg |
| A-020 | M7/S2 Divider Man | MODE:6, SUB:1, SENDALL | M7 Делитель Ручной | M7 Divider Man | A-020.jpg |
| A-021 | Feed MIN=0.05 | MODE:0, FEED:5, SENDALL | 0.05 мм/об | Feed=0.05 | A-021.jpg |
| A-022 | Feed 0.10 | MODE:0, FEED:10, SENDALL | 0.10 мм/об | Feed=0.10 | A-022.jpg |
| A-023 | Feed 0.25 | MODE:0, FEED:25, SENDALL | 0.25 мм/об | Feed=0.25 | A-023.jpg |
| A-024 | Feed 0.50 | MODE:0, FEED:50, SENDALL | 0.50 мм/об | Feed=0.50 | A-024.jpg |
| A-025 | Feed 0.75 | MODE:0, FEED:75, SENDALL | 0.75 мм/об | Feed=0.75 | A-025.jpg |
| A-026 | Feed 1.00 | MODE:0, FEED:100, SENDALL | 1.00 мм/об | Feed=1.00 | A-026.jpg |
| A-027 | Feed 1.50 | MODE:0, FEED:150, SENDALL | 1.50 мм/об | Feed=1.50 | A-027.jpg |
| A-028 | Feed MAX=2.00 | MODE:0, FEED:200, SENDALL | 2.00 мм/об | Feed=2.00 | A-028.jpg |
| A-029 | aFeed MIN=15 | MODE:1, AFEED:15, SENDALL | 15 мм/мин | AFeed=15 | A-029.jpg |
| A-030 | aFeed 50 | MODE:1, AFEED:50, SENDALL | 50 мм/мин | AFeed=50 | A-030.jpg |
| A-031 | aFeed 100 | MODE:1, AFEED:100, SENDALL | 100 мм/мин | AFeed=100 | A-031.jpg |
| A-032 | aFeed 150 | MODE:1, AFEED:150, SENDALL | 150 мм/мин | AFeed=150 | A-032.jpg |
| A-033 | aFeed 200 | MODE:1, AFEED:200, SENDALL | 200 мм/мин | AFeed=200 | A-033.jpg |
| A-034 | aFeed 250 | MODE:1, AFEED:250, SENDALL | 250 мм/мин | AFeed=250 | A-034.jpg |
| A-035 | aFeed 300 | MODE:1, AFEED:300, SENDALL | 300 мм/мин | AFeed=300 | A-035.jpg |
| A-036 | aFeed 330 | MODE:1, AFEED:330, SENDALL | **330 мм/мин** (не -32512!) | AFeed=330 | A-036.jpg |
| A-037 | AP=0.00 | MODE:0, AP:0, SENDALL | Съём 0.00 | AP=0.00 | A-037.jpg |
| A-038 | AP=0.10 | MODE:0, AP:10, SENDALL | Съём 0.10 | AP=0.10 | A-038.jpg |
| A-039 | AP=0.25 | MODE:0, AP:25, SENDALL | Съём 0.25 | AP=0.25 | A-039.jpg |
| A-040 | AP=0.50 | MODE:0, AP:50, SENDALL | Съём 0.50 | AP=0.50 | A-040.jpg |
| A-041 | AP=1.00 | MODE:0, AP:100, SENDALL | Съём 1.00 | AP=1.00 | A-041.jpg |
| A-042 | AP=2.00 | MODE:0, AP:200, SENDALL | Съём 2.00 | AP=2.00 | A-042.jpg |
| A-043 | Pass 0/0 | MODE:0, PASS:0,0, SENDALL | --/-- | Pass empty | A-043.jpg |
| A-044 | Pass 1/8 | MODE:0, PASS:1,8, SENDALL | 1/8 | Pass 1/8 | A-044.jpg |
| A-045 | Pass 4/8 | MODE:0, PASS:4,8, SENDALL | 4/8 | Pass 4/8 | A-045.jpg |
| A-046 | Pass 8/8 | MODE:0, PASS:8,8, SENDALL | 8/8 | Pass 8/8 | A-046.jpg |
| A-047 | M2 Pass 0/5 | MODE:1, PASS:0,5, SENDALL | 0/5 или --/5 | M2 Pass 0/5 | A-047.jpg |
| A-048 | M2 Pass 3/5 | MODE:1, PASS:3,5, SENDALL | 3/5 | M2 Pass 3/5 | A-048.jpg |
| A-049 | RPM 0 | MODE:0, RPM:0, SENDALL | RPM 0 | RPM=0 | A-049.jpg |
| A-050 | RPM 500 | MODE:0, RPM:500, SENDALL | RPM 500 | RPM=500 | A-050.jpg |
| A-051 | RPM 1000 | MODE:0, RPM:1000, SENDALL | RPM 1000 | RPM=1000 | A-051.jpg |
| A-052 | RPM 1500 | MODE:0, RPM:1500, SENDALL | RPM 1500 | RPM=1500 | A-052.jpg |
| A-053 | RPM 2000 | MODE:0, RPM:2000, SENDALL | RPM 2000 | RPM=2000 | A-053.jpg |
| A-054 | M1 SelectMenu=1 | MODE:0, SELECTMENU:1, SENDALL | Основной вид | SM=1 | A-054.jpg |
| A-055 | M1 SelectMenu=2 | MODE:0, SELECTMENU:2, SENDALL | Параметры | SM=2 | A-055.jpg |
| A-056 | M1 SelectMenu=3 | MODE:0, SELECTMENU:3, SENDALL | Оси | SM=3 | A-056.jpg |
| A-057 | M2 SelectMenu=1 | MODE:1, SELECTMENU:1, SENDALL | M2 SM=1 | M2 SM=1 | A-057.jpg |
| A-058 | M2 SelectMenu=2 | MODE:1, SELECTMENU:2, SENDALL | M2 SM=2 делитель | M2 SM=2 | A-058.jpg |
| A-059 | M3 SelectMenu=1 | MODE:2, SELECTMENU:1, SENDALL | M3 SM=1 | M3 SM=1 | A-059.jpg |
| A-060 | M3 SelectMenu=2 | MODE:2, SELECTMENU:2, SENDALL | M3 SM=2 параметры | M3 SM=2 | A-060.jpg |
| A-061 | Нет концевиков | MODE:0, LIMITS:0,0,0,0, SENDALL | Нет иконок лимитов | LCD нет лим | A-061.jpg |
| A-062 | Лимит Левый | MODE:0, LIMITS:1,0,0,0, SENDALL | Иконка L | LCD Lim L | A-062.jpg |
| A-063 | Лимит Правый | MODE:0, LIMITS:0,1,0,0, SENDALL | Иконка R | LCD Lim R | A-063.jpg |
| A-064 | Лимит Перед | MODE:0, LIMITS:0,0,1,0, SENDALL | Иконка F | LCD Lim F | A-064.jpg |
| A-065 | Лимит Зад | MODE:0, LIMITS:0,0,0,1, SENDALL | Иконка Re | LCD Lim Re | A-065.jpg |
| A-066 | Лимит L+R | MODE:0, LIMITS:1,1,0,0, SENDALL | Иконки L+R | LCD L+R | A-066.jpg |
| A-067 | Лимит F+Re | MODE:0, LIMITS:0,0,1,1, SENDALL | Иконки F+Re | LCD F+Re | A-067.jpg |
| A-068 | Все лимиты ON | MODE:0, LIMITS:1,1,1,1, SENDALL | Все 4 иконки | LCD all lim | A-068.jpg |
| A-069 | Pos Z=0 X=0 | POS_Z:0, POS_X:0, SENDALL | Z 0.000 X 0.000 | Z=0.000 X=0.000 | A-069.jpg |
| A-070 | Pos Z+123.450 | POS_Z:123450, SENDALL | Z 123.450 | Z=123.450 | A-070.jpg |
| A-071 | Pos Z-67.890 | POS_Z:-67890, SENDALL | Z -67.890 | Z=-67.890 | A-071.jpg |
| A-072 | Pos X+45.678 | POS_X:45678, SENDALL | X 45.678 | X=45.678 | A-072.jpg |
| A-073 | Pos X-12.345 | POS_X:-12345, SENDALL | X -12.345 | X=-12.345 | A-073.jpg |
| A-074 | Pos Z +999.999 | POS_Z:999999, SENDALL | Z 999.999 | Z=999.999 | A-074.jpg |
| A-075 | Pos Z -999.999 | POS_Z:-999999, SENDALL | Z -999.999 | Z=-999.999 | A-075.jpg |
| A-076 | M3 PH=1 | MODE:2, PH:1, SENDALL | Заходов 1 | M3 PH=1 | A-076.jpg |
| A-077 | M3 PH=2 | MODE:2, PH:2, SENDALL | Заходов 2 | M3 PH=2 | A-077.jpg |
| A-078 | M3 PH=4 | MODE:2, PH:4, SENDALL | Заходов 4 | M3 PH=4 | A-078.jpg |
| A-079 | M3 PH=8 | MODE:2, PH:8, SENDALL | Заходов 8 | M3 PH=8 | A-079.jpg |
| A-080 | M3 CYCL=10 | MODE:2, THREAD_CYCL:10, SENDALL | Циклов 10 | M3 Cycl=10 | A-080.jpg |
| A-081 | M3 CYCL=36 | MODE:2, THREAD_CYCL:36, SENDALL | Циклов 36 | M3 Cycl=36 | A-081.jpg |
| A-082 | M3 RPM_LIM=80 | MODE:2, RPM_LIM:80, SENDALL | ОБ/МИН 80 | M3 RPM_LIM=80 | A-082.jpg |
| A-083 | M3 RPM_LIM=300 | MODE:2, RPM_LIM:300, SENDALL | ОБ/МИН 300 | M3 RPM_LIM=300 | A-083.jpg |
| A-084 | M4 Feed 0.25 | MODE:3, FEED:25, SENDALL | M4 Подача 0.25 | M4 Feed=0.25 | A-084.jpg |
| A-085 | M5 Feed 0.50 | MODE:4, FEED:50, SENDALL | M5 Подача 0.50 | M5 Feed=0.50 | A-085.jpg |
| A-086 | M6 Sphere R=10.00 | MODE:5, SPHERE:1000, SENDALL | R=10.00 мм | M6 R=10 | A-086.jpg |
| A-087 | M6 Sphere R=20.00 | MODE:5, SPHERE:2000, SENDALL | R=20.00 мм | M6 R=20 | A-087.jpg |
| A-088 | M6 BAR=2.50 | MODE:5, BAR:250, SENDALL | Ножка 2.50 | M6 BAR=2.50 | A-088.jpg |
| A-089 | M6 PASS_SPHR=8 | MODE:5, PASS_SPHR:8, SENDALL | Заходов 10 | M6 Pass=8 | A-089.jpg |
| A-090 | M7 Делений=12 | MODE:6, DIVN:12, SENDALL | Делений 12 | M7 DIVN=12 | A-090.jpg |
| A-091 | M7 Делений=24 | MODE:6, DIVN:24, SENDALL | Делений 24 | M7 DIVN=24 | A-091.jpg |
| A-092 | M7 Метка 1/12 | MODE:6, DIVN:12, DIVM:1, SENDALL | Метка 1 | M7 M=1 | A-092.jpg |
| A-093 | M7 Метка 6/12 | MODE:6, DIVN:12, DIVM:6, SENDALL | Метка 6 | M7 M=6 | A-093.jpg |
| A-094 | STATE run | MODE:0, STATE:run, SENDALL | Индикатор RUN | LCD run | A-094.jpg |
| A-095 | STATE stop | MODE:0, STATE:stop, SENDALL | Индикатор STOP | LCD stop | A-095.jpg |
| A-096–A-200 | (см. test_bidir.py) | ... | ... | ... | ... |

---

## Ожидаемые результаты

**Критичный кейс A-036** (aFeed 330): должен показывать **330**, НЕ **-32512**.

После запуска тестов файл `tools/test_bidir_results.jsonl` содержит результаты.
Скриншоты сохраняются в `tools/screenshots_bidir/`.
