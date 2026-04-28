# ELS Migration Plan: Arduino Mega → STM32F407

**Источник**: `7e2_dro_integration`  
**Цель**: `Lathe-cnc-controller-stm32`

Полный порт один в один. Все режимы, весь функционал, двухсторонний синк LCD+ESP32.

---

## Статус этапов

| #      | Этап                                                                  | Статус                                         |
| ------ | --------------------------------------------------------------------- | ---------------------------------------------- |
| 1      | Инфраструктура: пины STM32, маппинг GPIO                              | ✅ Готово (els_config.h, drv_inputs.cpp)        |
| 2      | DRO HS800-2 драйвер                                                   | ✅ Готово (drv_dro.cpp)                         |
| 3      | ESP32 дисплей (UART протокол)                                         | ✅ Готово (drv_display.cpp, USART3 PB10/PB11)  |
| 4      | LCD2004 драйвер                                                       | ✅ Готово (drv_lcd2004.cpp)                     |
| 5      | Энкодер шпинделя (TIM5 quadrature)                                    | ✅ Готово (drv_encoder.cpp)                     |
| 6      | Ручной энкодер — EXTI ISR                                             | ✅ Готово (drv_encoder.cpp PC2/PC3)             |
| 7      | Зуммер                                                                | ✅ Готово (drv_beeper.cpp, TIM4_CH1 PD12)      |
| 8      | Глобальное состояние ELS (els_state)                                  | ✅ Готово (els_state.h/cpp)                     |
| 9      | Таблицы резьб и конусов                                               | ✅ Готово (els_tables.h/cpp)                    |
| 10     | GPIO: кнопки M1-M8, подрежимы, навигация, лимиты, LED                | ✅ Готово (drv_inputs.cpp — всё включая LED)    |
| 11     | Ручной энкодер: полный порт HandCoder.ino                             | ✅ Готово (els_control.cpp _update_hand_encoder)|
| 12     | Шаговые двигатели: TIM1 Y+X, Bresenham конус                         | ✅ Готово (drv_stepper.cpp)                     |
| 13     | LCD Print: все 8 режимов × все SelectMenu                             | ✅ Готово (drv_lcd2004.cpp)                     |
| 14     | Menu: режимы/подрежимы, кнопки, джойстик, TOUCH от ESP32             | ✅ Готово (els_menu.cpp)                        |
| 15     | Режим Feed: синхронная подача                                         | ✅ Готово (els_control.cpp MODE_FEED)           |
| 16     | Режим Thread: нарезка резьбы                                          | ✅ Готово (els_control.cpp MODE_THREAD)         |
| 17     | Режим Cone: конус L/R с Bresenham X-slave                            | ✅ Готово (els_control.cpp + drv_stepper.cpp)   |
| 18     | Режим aFeed: асинхронная подача                                       | ✅ Готово (els_control.cpp MODE_AFEED)          |
| 19     | Режим Sphere: шароточка — параметры + мотор-логика                    | ✅ Готово (els_control.cpp _update_sphere, Ext statemachine) |
| 20     | Режим Divider: делилка — параметры + угол шпинделя                    | ✅ Готово (els_main.cpp: Spindle_Angle/Required_Angle/ANGLE на ESP32) |
| 21     | ADC: переменник подачи (PA4)                                          | ✅ Готово (els_main.cpp, USE_ADC_FEED=0 пока нет железа) |
| 22     | EEPROM → STM32 Flash (сохранение настроек)                            | ✅ Готово (els_settings.cpp, sector 11)         |
| 23     | ESP32 синк: DRV_Display_SendAll (все 31 поле)                         | ✅ Готово (drv_display.cpp)                     |
| 24     | ESP32 RX: все TOUCH команды + параметрические (FEED/AP/CONE/…)       | ✅ Готово (els_menu.cpp _on_display_rx)         |
| 25     | Spindle_On: флаг вращения шпинделя                                    | ✅ Готово (els_main.cpp: spindle_flag = rpm>10) |
| 26     | DRO интеграция: Size_X_mm/Size_Z_mm/MSize_X_mm                       | ✅ Готово (els_main.cpp ELS_Loop)               |
| 27     | Limit switches: чтение + LED индикация                                | ✅ Готово (drv_inputs.cpp: LED зеркалят кнопки)|
| 28     | Полная сборка + прошивка + тест всех режимов                          | 🔄 В процессе                                   |

---

## Что осталось (приоритет)

### [MEDIUM] Этап 19: Sphere мотор-логика
Сейчас параметры шара (Sph_R_mm, Bar_R_mm) сохраняются и отображаются,
но фактическое движение по профилю шара не реализовано.
Порт `Sphere.ino` — требует два синхронных мотора.

### [MEDIUM] Этап 20: Divider мотор-логика
Параметры делилки (Total_Tooth/Current_Tooth) работают,
но фиксация шпинделя и индексирование — не реализованы.
Порт нужен когда подключим шпиндельный тормоз.

---

## Архитектура портирования

### Маппинг осей

| Оригинал (Arduino) | STM32                       | Физически          |
| ------------------ | --------------------------- | ------------------ |
| Ось Z (продольная) | Ось Y в els_state (`pos_y`) | Каретка            |
| Ось X (поперечная) | Ось X в els_state (`pos_x`) | Суппорт            |
| DRO Y → Size_Z_mm  | DRO Y → pos_y / Size_Z_mm   | Линейка продольная |
| DRO X → Size_X_mm  | DRO X → pos_x / Size_X_mm   | Линейка поперечная |

### Маппинг пинов Arduino → STM32F407

| Функция                     | Arduino Mega         | STM32F407               |
| --------------------------- | -------------------- | ----------------------- |
| Motor Y STEP                | Pin49 (PL0)          | PE9  (TIM1_CH1)         |
| Motor Y DIR                 | Pin43 (PL6)          | PE10                    |
| Motor Y ENA                 | Pin45 (PL4)          | PE11                    |
| Motor X STEP                | Pin48 (PL1)          | PE13 (TIM1_CH3 / GPIO)  |
| Motor X DIR                 | Pin44 (PL5)          | PE14                    |
| Motor X ENA                 | Pin46 (PL3)          | PE15                    |
| Mode Switch M1-M8           | PINC (PC0-PC7)       | PG8-PG15                |
| SubMode Switch              | PINK5-PK7            | PG5-PG7                 |
| Joy Left/Right/Up/Down      | PINK0-PK3            | PC4-PC7                 |
| Button Rapid                | PINK4                | PC8                     |
| Nav Up/Down/Left/Right/Sel  | PINF0-PF4            | PG0-PG4                 |
| Limit Left/Right/Front/Rear | PINA0,A2,A4,A6       | PF0,PF2,PF4,PF6         |
| LED Left/Right/Front/Rear   | PINA1,A3,A5,A7       | PF1,PF3,PF5,PF7         |
| Hand Enc A/B                | PIND2/D3 (INT0/INT1) | PC2/PC3 (EXTI)          |
| Hand Axis Z/X               | PINE4/E5             | PD0/PD1                 |
| Hand Scale x1/x10           | PINJ0/J1             | PD2/PD3                 |
| Beeper                      | PH1                  | PD12 (TIM4_CH1)         |
| Spindle Enc A/B             | INT0/INT1 (PE4/PE5)  | PA0/PA1 (TIM5_CH1/CH2)  |
| LCD I2C SDA/SCL             | PB2/PB3 (bit-bang)   | PB7/PB6 (I2C1)          |
| DRO UART RX                 | Pin17 (Serial2 RX)   | PA3 (USART2 RX)         |
| ESP32 UART TX/RX            | Pin51/52 (SoftSerial)| PB10/PB11 (USART3)      |
| Debug Serial                | Pin0/1 (Serial)      | PA9/PA10 (USART1)       |
