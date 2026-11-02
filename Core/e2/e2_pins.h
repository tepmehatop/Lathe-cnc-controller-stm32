#pragma once
/**
 * @file  e2_pins.h
 * @brief Карта выводов STM32F4DISCOVERY для ядра 7e2.
 *
 * Пины выбраны из свободных на плате Discovery (UM1472, табл. 5 «Free I/O»)
 * либо из подключённых к входам аудио-ЦАП/микрофона/USB, которые не мешают
 * (ЦАП держится в сбросе — PD4). Подключения, которые уже собраны на стенде
 * (дисплей PB10/PB11, LCD2004 PB6/PB7, отладка PA9/PA10, DRO PA3, линейки),
 * сохранены. Не используются: PA5–PA7, PE0, PE1, PE3 (акселерометр), PC3 (микрофон),
 * PA11/PA12 (USB), PA13/PA14 (SWD), PC14/PC15 (кварц 32 кГц), PD12–PD15 (светодиоды платы).
 *
 * Формат: «порт, номер» (например «E, 9» = PE9) — так макросы e2_hal.h дают
 * прямую запись в регистры GPIO без поиска по таблицам (важно в прерываниях).
 *
 * В скобках — вывод Arduino Mega в оригинальной схеме 7e2 (Diagram ELS_V7.jpg).
 * Подробная таблица и схема подключения — docs/wiring/README.md.
 */

// ── Энкодер шпинделя ──────────────────────────────────────────────────────────
#define E2_PIN_ENC_A        A, 1     // (21, INT0 + T5/47) канал A — прерывание по обоим фронтам
#define E2_PIN_ENC_B        A, 8     // (20) канал B

// ── РГИ (ручной энкодер 100 линий) ───────────────────────────────────────────
#define E2_PIN_HAND_A       E, 7     // (19, INT2) канал A — прерывание по обоим фронтам
#define E2_PIN_HAND_B       C, 13    // (18) канал B
#define E2_PIN_HAND_AXIS_Z  C, 10    // (2)  переключатель оси РГИ: Z
#define E2_PIN_HAND_AXIS_X  C, 12    // (3)  переключатель оси РГИ: X
#define E2_PIN_HAND_X1      A, 4     // (14) масштаб x1
#define E2_PIN_HAND_X10     D, 8     // (15) масштаб x10

// ── Драйверы шаговых двигателей ──────────────────────────────────────────────
#define E2_PIN_Z_STEP       E, 9     // (49)
#define E2_PIN_Z_DIR        E, 10    // (43)
#define E2_PIN_Z_ENA        E, 11    // (45)
#define E2_PIN_X_STEP       E, 13    // (48)
#define E2_PIN_X_DIR        E, 14    // (44)
#define E2_PIN_X_ENA        E, 15    // (46)

// ── Выходы ───────────────────────────────────────────────────────────────────
#define E2_PIN_TACHO        E, 8     // (42) импульс на оборот шпинделя
#define E2_PIN_BEEPER       E, 12    // (16) зуммер, активный уровень — низкий

// ── Переключатель режимов (8 положений) ──────────────────────────────────────
#define E2_PIN_MODE_FEED    D, 0     // (30) Синхронная подача
#define E2_PIN_MODE_AFEED   D, 1     // (31) Асинхронная подача
#define E2_PIN_MODE_CONE_L  D, 2     // (32) Конус <
#define E2_PIN_MODE_CONE_R  D, 3     // (33) Конус >
#define E2_PIN_MODE_THREAD  D, 6     // (34) Резьба
#define E2_PIN_MODE_SPHERE  D, 7     // (35) Шар
#define E2_PIN_MODE_DIVIDER D, 10    // (36) Делилка
#define E2_PIN_MODE_RESERVE D, 11    // (37) Резерв

// ── Переключатель подрежима (3 положения) ────────────────────────────────────
#define E2_PIN_SUB_INT      C, 8     // (A13) Внутренний
#define E2_PIN_SUB_MAN      C, 9     // (A14) Ручной
#define E2_PIN_SUB_EXT      C, 11    // (A15) Наружный

// ── Кнопки меню ──────────────────────────────────────────────────────────────
#define E2_PIN_BTN_LEFT     B, 13    // (A0)
#define E2_PIN_BTN_RIGHT    B, 14    // (A1)
#define E2_PIN_BTN_UP       B, 12    // (A2)
#define E2_PIN_BTN_DOWN     D, 9     // (A3)
#define E2_PIN_BTN_SELECT   B, 15    // (A4)

// ── Джойстик ─────────────────────────────────────────────────────────────────
#define E2_PIN_JOY_LEFT     B, 0     // (A8)
#define E2_PIN_JOY_RIGHT    B, 1     // (A9)
#define E2_PIN_JOY_UP       B, 2     // (A10)
#define E2_PIN_JOY_DOWN     B, 9     // (A11) на плате подтяжка 4.7 кОм к 3.3 В — не мешает
#define E2_PIN_JOY_RAPID    B, 8     // (A12) кнопка ускоренного хода

// ── Кнопки упоров (лимитов) и их светодиоды ──────────────────────────────────
#define E2_PIN_LIM_LEFT     C, 5     // (28)
#define E2_PIN_LIM_RIGHT    C, 4     // (26)
#define E2_PIN_LIM_FRONT    C, 2     // (24)
#define E2_PIN_LIM_REAR     A, 0     // (22) на плате — кнопка USER с RC-фильтром: для кнопки упора годится
#define E2_PIN_LED_LEFT     E, 6     // (29) светодиод горит при низком уровне
#define E2_PIN_LED_RIGHT    E, 5     // (27)
#define E2_PIN_LED_FRONT    E, 4     // (25)
#define E2_PIN_LED_REAR     E, 2     // (23)

// ── Переменный резистор подачи (АЦП) ─────────────────────────────────────────
#define E2_PIN_FEED_POT     C, 1     // (A7) ADC123_IN11

// ── Связь ────────────────────────────────────────────────────────────────────
#define E2_PIN_LCD_SCL      B, 6     // I2C1 SCL — LCD2004 по I2C (запасной дисплей)
#define E2_PIN_LCD_SDA      B, 7     // I2C1 SDA
#define E2_PIN_DISP_TX      B, 10    // USART3 TX → дисплей ESP32 (RX)
#define E2_PIN_DISP_RX      B, 11    // USART3 RX ← дисплей ESP32 (TX)
#define E2_PIN_DRO_RX       A, 3     // USART2 RX ← блок DRO HS800-2 (TX); в режиме линеек — REQ оси X
#define E2_PIN_DRO_TX       A, 2     // USART2 TX — не подключается; в режиме линеек — REQ оси Z
#define E2_PIN_DBG_TX       A, 9     // USART1 TX — отладочный порт (Serial)
#define E2_PIN_DBG_RX       A, 10    // USART1 RX
// Линейки напрямую (Core/els_config.h): Z — PA15/PB3 (TIM2), X — PB4/PB5 (TIM3)
#define E2_PIN_AUDIO_RESET  D, 4     // сброс аудио-ЦАП платы: держим 0, чтобы ЦАП не мешал
