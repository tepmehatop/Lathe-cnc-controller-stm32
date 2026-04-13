# ELS Migration Plan: Arduino Mega → STM32F407

**Источник**: `/Users/marknadelman/IdeaProjects/7e2_dro_integration`  
**Цель**: `/Users/marknadelman/IdeaProjects/Lathe-cnc-controller-stm32`

Полный порт один в один. Все режимы, весь функционал, двухсторонний синк LCD+ESP32.

---

## Статус этапов


| #      | Этап                                                                  | Статус                             |
| ------ | --------------------------------------------------------------------- | ---------------------------------- |
| 1      | Инфраструктура: пины STM32, маппинг GPIO                              | ✅ Частично (drv_inputs.cpp)        |
| 2      | DRO HS800-2 драйвер                                                   | ✅ Готово                           |
| 3      | ESP32 дисплей (UART протокол)                                         | ✅ Готово (PB10/PB11, 57600)        |
| 4      | LCD2004 драйвер                                                       | ✅ Готово                           |
| 5      | Энкодер шпинделя (TIM5 quadrature)                                    | ✅ Готово                           |
| 6      | Ручной энкодер                                                        | ✅ Частично (interrupts на PC2/PC3) |
| 7      | Зуммер                                                                | ✅ Готово                           |
| **8**  | **Глобальное состояние ELS (els_state)**                              | ✅ Готово                           |
| **9**  | **Таблицы резьб и конусов**                                           | ✅ Готово                           |
| **10** | **GPIO: кнопки режимов M1-M8, подрежимы, навигация, лимиты, LED**     | ❌ Не готово                        |
| **11** | **Ручной энкодер: полный порт HandCoder.ino**                         | ❌ Не готово                        |
| **12** | **Шаговые двигатели: таймеры STM32 (порт Timer2/3/4/5 AVR)**          | ❌ Не готово                        |
| **13** | **LCD Print: полный порт Print.ino (все 8 режимов × все SelectMenu)** | ✅ Готово                           |
| **14** | **Menu: переключение режимов/подрежимов, кнопки, джойстик**           | ❌ Не готово                        |
| **15** | **Режим Feed: синхронная подача (Feed.ino)**                          | ❌ Не готово                        |
| **16** | **Режим Thread: резьба (Thread.ino)**                                 | ❌ Не готово                        |
| **17** | **Режим Cone: конус L/R (Cone.ino)**                                  | ❌ Не готово                        |
| **18** | **Режим aFeed: асинхронная подача (aFeed.ino)**                       | ❌ Не готово                        |
| **19** | **Режим Sphere: шароточка (Sphere.ino)**                              | ❌ Не готово                        |
| **20** | **Режим Divider: делилка**                                            | ❌ Не готово                        |
| **21** | **ADC: аналоговые входы (ADC.ino)**                                   | ❌ Не готово                        |
| **22** | **EEPROM → STM32 Flash (сохранение настроек)**                        | ❌ Не готово                        |
| **23** | **ESP32 синк: полная отправка состояния (DRV_Display_SendAll)**       | ❌ Частично                         |
| **24** | **ESP32 RX: приём команд с тачскрина → вызов Switch_*/Key_*/**        | ❌ Частично                         |
| **25** | **Spindle_On: тахометр, отслеживание вращения**                       | ❌ Не готово                        |
| **26** | **DRO интеграция: Size_X_mm/Size_Z_mm из DRO**                        | ❌ Частично                         |
| **27** | **Lim switches: подключение и обработка концевиков**                  | ❌ Не готово                        |
| **28** | **Полная сборка + прошивка + тест всех режимов**                      | ❌ Не готово                        |


---

## Архитектура портирования

### Маппинг осей


| Оригинал (Arduino) | STM32                       | Физически          |
| ------------------ | --------------------------- | ------------------ |
| Ось Z (продольная) | Ось Y в els_state (`pos_y`) | Каретка            |
| Ось X (поперечная) | Ось X в els_state (`pos_x`) | Суппорт            |
| DRO Y → Size_Z_mm  | DRO Y → pos_y / Size_Z_mm   | Линейка продольная |
| DRO X → Size_X_mm  | DRO X → pos_x / Size_X_mm   | Линейка поперечная |


### Маппинг таймеров AVR → STM32


| AVR               | Назначение                 | STM32                            |
| ----------------- | -------------------------- | -------------------------------- |
| Timer1 (OCF1A)    | Клавиатура timing          | TIM1                             |
| Timer2 (OCIE2A/B) | Ускоренное перемещение Z/X | TIM2                             |
| Timer3 (OCIE3A/B) | Ручной энкодер Z/X         | TIM3                             |
| Timer4 (OCIE4A/B) | Асинхронная подача Z/X     | TIM4                             |
| Timer5 (OCIE5A/B) | Синхронная подача Z/X      | TIM5 (сейчас encoder) → **TIM6** |
| INT0              | Энкодер шпинделя           | TIM5 quadrature (уже есть)       |
| INT2              | Ручной энкодер             | EXTI                             |


### Маппинг пинов Arduino → STM32F407


| Функция                     | Arduino Mega         | STM32F407          |
| --------------------------- | -------------------- | ------------------ |
| Motor Z STEP                | Pin49 (PL0)          | PA8 (TIM1_CH1)     |
| Motor Z DIR                 | Pin43 (PL6)          | PC0                |
| Motor Z ENA                 | Pin45 (PL4)          | PC1                |
| Motor X STEP                | Pin48 (PL1)          | PA9 (TIM1_CH2)     |
| Motor X DIR                 | Pin44 (PL5)          | PC2                |
| Motor X ENA                 | Pin46 (PL3)          | PC3                |
| Mode Switch M1-M8           | PINC (PC0-PC7)       | PG0-PG7            |
| SubMode Switch              | PINK5-PK7            | PG8-PG10           |
| Joy Left/Right/Up/Down      | PINK0-PK3            | PF0-PF3            |
| Button Rapid                | PINK4                | PF4                |
| Nav Up/Down/Left/Right      | PINF0-PF3            | PF5-PF8            |
| Button Select               | PINF4                | PF9                |
| Limit Left/Right/Front/Rear | PINA0,A2,A4,A6       | PE2-PE5            |
| LED Left/Right/Front/Rear   | PINA1,A3,A5,A7       | PE6-PE9            |
| Hand Enc A/B                | PIND2/D3             | PC2/PC3 (уже есть) |
| Hand Axis Z/X               | PINE4/E5             | PD0/PD1            |
| Hand Scale x1/x10           | PINJ0/J1             | PD2/PD3            |
| Beeper                      | PH1 (Pin16)          | уже есть           |
| LCD I2C SDA                 | Pin11 (PB2 bit-bang) | PB7 (I2C1)         |
| LCD I2C SCL                 | Pin12 (PB3 bit-bang) | PB6 (I2C1)         |
| DRO UART RX                 | Pin17 (Serial2)      | PA3 (USART2)       |
| ESP32 UART TX               | Pin51 (SoftSerial)   | PB10 (USART3)      |
| ESP32 UART RX               | Pin52 (SoftSerial)   | PB11 (USART3)      |
| Debug Serial                | Pin0/1 (Serial)      | PA9/PA10 (USART1)  |


---

## Детализация этапов

### Этап 8: Глобальное состояние (els_state)

Расширить `els_state.h` и `els_state.cpp`:

- Все переменные из Arduino: Mode, Sub_Mode_*, Feed_mm, aFeed_mm, Thread_Step, Cone_Step...
- Motor_Z_Pos, Motor_X_Pos (в микрошагах)
- Size_Z_mm, Size_X_mm (в 0.01мм как в оригинале)
- Limit_Pos_Left/Right/Front/Rear
- Все флаги: Joy_Z_flag, Step_Z_flag, feed_Z_flag и т.д.
- Счётчики резьбы: Ks_Divisor, Km_Divisor, Cs_Divisor, Cm_Divisor

### Этап 9: Таблицы

Перенести из Arduino ino в C++ файлы:

- `Thread_Info[]` — 56 записей (метрика, дюйм, G-труба, K-труба)
- `Cone_Info[]` — 62 записи (градусы, KM0-KM6, 1:N конусы)
- Структуры `thread_info_type`, `cone_info_type`

### Этап 10: GPIO драйвер (полный)

Дополнить `drv_inputs.cpp`:

- Mode switches M1-M8 (PG0-PG7)
- SubMode switches (PG8-PG10)
- Joystick 4 направления + Rapid (PF0-PF4)
- Nav buttons + Select (PF5-PF9)
- Limit switches + LEDs (PE2-PE9)
- Hand axis/scale switches (PD0-PD3)

### Этап 11: Ручной энкодер

Порт HandCoder.ino:

- Interrupt на PC2/PC3 (уже есть INT)
- Масштаб x1/x10 из пинов PD2/PD3
- Выбор оси Z/X из пинов PD0/PD1
- TIM3 для генерации шагов РГИ

### Этап 12: Шаговые двигатели (ключевой этап)

Порт всех прерываний таймеров AVR:

- **ISR(TIMER5_COMPA_vect)** → TIM6 overflow callback (синхронная подача Z)
- **ISR(TIMER5_COMPB_vect)** → TIM6 compare callback (синхронная подача X)  
- **ISR(TIMER4_COMPA_vect)** → TIM4 callback (асинхронная подача Z)
- **ISR(TIMER4_COMPB_vect)** → TIM4 compare B (асинхронная подача X)
- **ISR(TIMER2_COMPA_vect)** → TIM2 callback (ускоренное Z)
- **ISR(TIMER2_COMPB_vect)** → TIM2 compare B (ускоренное X)
- **ISR(INT0_vect)** → уже есть в TIM5 (шпиндель)
- OCR/TCNT → setPeriod()/setCount() STM32duino

### Этап 13: LCD Print (полный порт)

Порт Print.ino в `drv_lcd2004.cpp`:

- Mode_Feed × SM1/SM2/SM3
- Mode_aFeed × SM1/SM2
- Mode_Thread × SM1/SM2/SM3
- Mode_Cone_L × SM1/SM2/SM3
- Mode_Cone_R × SM1/SM2/SM3
- Mode_Sphere × SM1/SM2
- Mode_Divider
- Mode_Reserve
- Err экраны (err_0, err_1, err_2, Complete)

### Этап 14-21: Логика режимов

Порт каждого .ino файла в соответствующий .cpp:

- `els_feed.cpp` ← Feed.ino
- `els_thread.cpp` ← Thread.ino
- `els_cone.cpp` ← Cone.ino
- `els_afeed.cpp` ← aFeed.ino
- `els_sphere.cpp` ← Sphere.ino
- `els_menu.cpp` ← Menu.ino (расширение els_control.cpp)

### Этап 22: EEPROM → Flash

Arduino использует `EEPROM.read/write`. STM32F407 не имеет EEPROM.
Использовать STM32 EEPROM emulation через Flash или библиотеку `EEPROM` в STM32duino.

### Этапы 23-24: ESP32 синк

Полная отправка и приём всего состояния:

- `DRV_Display_SendAll()` — отправить все 21+ параметр
- RX callback — все команды TOUCH:M1-M8, S1-S3, KEY:*, JOY:*

---

## Текущее состояние STM32 проекта

### Уже работает (проверено hardware):

- ✅ DRO читается, пакеты приходят, координаты обновляются
- ✅ LCD2004 отображает данные каждые 300мс
- ✅ ESP32 дисплей получает данные (MODE, POS_Z, POS_X, RPM)
- ✅ ESP32 → STM32 команды приходят (TOUCH:M1 и т.д. видны)

### Не работает / не реализовано:

- ❌ Нет переключения режимов (M1-M8 физически или с ESP32)
- ❌ Нет управления подрежимами
- ❌ Нет кнопок навигации
- ❌ Нет моторов
- ❌ LCD показывает только временную отладку вместо полного меню
- ❌ Нет EEPROM сохранения

