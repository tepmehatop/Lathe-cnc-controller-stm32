/**
 * @file    els_config.h
 * @brief   ELS — Электронная Гитара Токарного Станка
 *          ЕДИНЫЙ КОНФИГУРАЦИОННЫЙ ФАЙЛ
 *
 * Все параметры железа, пинов и флагов — ТОЛЬКО ЗДЕСЬ.
 * В остальном коде используются только макросы из этого файла.
 * Для переназначения пинов (например, под новый шилд) — менять только этот файл.
 *
 * Полное описание каждого параметра — в README.md, раздел "Конфигурация".
 */

#pragma once

// ============================================================================
// ФЛАГИ ВКЛЮЧЕНИЯ МОДУЛЕЙ
// Установите 1 чтобы включить, 0 чтобы выключить.
// ============================================================================

/** Работа через DRO HS800-2 (замкнутый контур).
 *  1 = позиция с цифровых линеек (исключает люфты)
 *  0 = позиция по шагам шагового двигателя (для станков без DRO) */
#define USE_DRO_HS800           1

/** Эмулятор DRO — Arduino Mega отправляет тестовые пакеты вместо HS800-2.
 *  1 = режим эмуляции (для разработки без реального станка)
 *  0 = реальный HS800-2 */
#define USE_DRO_SIMULATOR       0

/** Старый дисплей LCD2004 через I2C.
 *  1 = включён, 0 = отключён */
#define USE_LCD2004             1

/** Новый дисплей ESP32-S3 JC4827W543 через UART.
 *  1 = включён, 0 = отключён */
#define USE_ESP32_DISPLAY       1

/** Зуммер.
 *  1 = включён, 0 = отключён */
#define USE_BEEPER              1

// ============================================================================
// ПАРАМЕТРЫ СТАНКА (механика)
// ============================================================================

/** Количество линий энкодера шпинделя на 1 оборот.
 *  Типовые значения: 600, 1000, 1800, 2500 */
#define ENC_LINE_PER_REV        1800

/** Шагов двигателя на 1 оборот (без микрошага).
 *  200 для стандартного ШД с шагом 1.8°, 400 для 0.9° */
#define MOTOR_Y_STEP_PER_REV    200     // ось Y (продольная)
#define MOTOR_X_STEP_PER_REV    200     // ось X (поперечная)

/** Шаг ходового винта в сотых мм (0.01мм).
 *  200 = 2.00мм, 500 = 5.00мм */
#define SCREW_Y                 200     // шаг винта Y, продольная
#define SCREW_X                 125     // шаг винта X, поперечная

/** Микрошаг драйвера (1, 2, 4, 8, 16, 32...).
 *  Должен совпадать с переключателями на драйвере. */
#define MICROSTEP_Y             4
#define MICROSTEP_X             4

/** Линейный масштаб (для линейки на X-оси).
 *  Количество импульсов на мм линейки. */
#define LIN_X                   5

// ============================================================================
// ПИНЫ UART
// ВАЖНО: все UART используют DMA — CPU не блокируется при передаче/приёме!
// ============================================================================

/** DRO HS800-2: только приём (TX DRO → RX STM32).
 *  USART2 RX. Скорость: 57600 бод, 8N1. */
#define DRO_UART                USART2
#define DRO_UART_PIN_RX         GPIO_PIN_3      // PA3
#define DRO_UART_PORT_RX        GPIOA
#define DRO_UART_BAUD           57600

/** ESP32-S3 JC4827W543: двусторонний обмен.
 *  USART3. PB10=TX → ESP32 GPIO44(RX), PB11=RX ← ESP32 GPIO43(TX). */
#define DISP_UART               USART3
#define DISP_UART_PIN_TX        GPIO_PIN_10     // PB10
#define DISP_UART_PORT_TX       GPIOB
#define DISP_UART_PIN_RX        GPIO_PIN_11     // PB11
#define DISP_UART_PORT_RX       GPIOB
#define DISP_UART_BAUD          57600

/** Debug / Serial Monitor.
 *  USART1 (через ST-Link VCP на DISCOVERY). Скорость: 115200. */
#define DEBUG_UART              USART1
#define DEBUG_UART_PIN_TX       GPIO_PIN_9      // PA9
#define DEBUG_UART_PORT_TX      GPIOA
#define DEBUG_UART_PIN_RX       GPIO_PIN_10     // PA10
#define DEBUG_UART_PORT_RX      GPIOA
#define DEBUG_UART_BAUD         115200

// ============================================================================
// ПИНЫ I2C — LCD2004
// ============================================================================

/** I2C1 для LCD2004.
 *  Адрес PCF8574: 0x27 (по умолчанию). Частота: 400кГц. */
#define LCD_I2C                 I2C1
#define LCD_I2C_PIN_SCL         GPIO_PIN_6      // PB6
#define LCD_I2C_PORT_SCL        GPIOB
#define LCD_I2C_PIN_SDA         GPIO_PIN_7      // PB7
#define LCD_I2C_PORT_SDA        GPIOB
#define LCD_I2C_ADDR            0x27
#define LCD_COLS                20
#define LCD_ROWS                4

// ============================================================================
// ПИНЫ ЭНКОДЕР ШПИНДЕЛЯ
// TIM5 в режиме энкодера — счёт без прерываний, точное определение позиции.
// ============================================================================

/** Ch A и Ch B энкодера шпинделя.
 *  TIM5_CH1 и TIM5_CH2 (аппаратный счётчик). */
#define SPINDLE_ENC_TIMER       TIM5
#define SPINDLE_ENC_PIN_A       GPIO_PIN_0      // PA0 (TIM5_CH1)
#define SPINDLE_ENC_PORT_A      GPIOA
#define SPINDLE_ENC_PIN_B       GPIO_PIN_1      // PA1 (TIM5_CH2)
#define SPINDLE_ENC_PORT_B      GPIOA

// ВНИМАНИЕ: PA0 на STM32F4DISCOVERY — USER Button. При использовании
// энкодера шпинделя кнопку отключить (снять перемычку SB9 на плате).

// ============================================================================
// ПИНЫ РУЧНОЙ ЭНКОДЕР (Hand Coder 100 линий)
// ============================================================================

/** Ch A и Ch B ручного энкодера — через EXTI (прерывания). */
#define HAND_ENC_PIN_A          GPIO_PIN_2      // PC2
#define HAND_ENC_PORT_A         GPIOC
#define HAND_ENC_PIN_B          GPIO_PIN_3      // PC3
#define HAND_ENC_PORT_B         GPIOC

/** Переключатель оси (Z=продольная / X=поперечная). */
#define HAND_AXIS_PIN_Z         GPIO_PIN_0      // PD0
#define HAND_AXIS_PORT_Z        GPIOD
#define HAND_AXIS_PIN_X         GPIO_PIN_1      // PD1
#define HAND_AXIS_PORT_X        GPIOD

/** Переключатель шага (×1 / ×10). */
#define HAND_SCALE_PIN_1        GPIO_PIN_2      // PD2
#define HAND_SCALE_PORT_1       GPIOD
#define HAND_SCALE_PIN_10       GPIO_PIN_3      // PD3
#define HAND_SCALE_PORT_10      GPIOD

// ============================================================================
// ПИНЫ ШАГОВЫЕ ДВИГАТЕЛИ
// TIM1 (расширенный таймер) — генерирует STEP-импульсы через DMA.
// ============================================================================

/** Мотор Y — продольная подача (каретка). */
#define MOTOR_Y_STEP_TIMER      TIM1
#define MOTOR_Y_STEP_PIN        GPIO_PIN_9      // PE9 (TIM1_CH1)
#define MOTOR_Y_STEP_PORT       GPIOE
#define MOTOR_Y_DIR_PIN         GPIO_PIN_10     // PE10
#define MOTOR_Y_DIR_PORT        GPIOE
#define MOTOR_Y_EN_PIN          GPIO_PIN_11     // PE11
#define MOTOR_Y_EN_PORT         GPIOE
/** Активный уровень ENABLE (LOW=включён для большинства драйверов A4988/DRV8825/TMC2208). */
#define MOTOR_Y_EN_ACTIVE       GPIO_PIN_RESET  // 0 = включён

/** Мотор X — поперечная подача (суппорт). */
#define MOTOR_X_STEP_PIN        GPIO_PIN_13     // PE13 (TIM1_CH3)
#define MOTOR_X_STEP_PORT       GPIOE
#define MOTOR_X_DIR_PIN         GPIO_PIN_14     // PE14
#define MOTOR_X_DIR_PORT        GPIOE
#define MOTOR_X_EN_PIN          GPIO_PIN_15     // PE15
#define MOTOR_X_EN_PORT         GPIOE
#define MOTOR_X_EN_ACTIVE       GPIO_PIN_RESET  // 0 = включён

// ============================================================================
// ПИНЫ ДЖОЙСТИК И КНОПКА RAPID
// Активный уровень: LOW (внутренняя подтяжка PULLUP).
// ============================================================================
#define JOY_LEFT_PIN            GPIO_PIN_4      // PC4
#define JOY_LEFT_PORT           GPIOC
#define JOY_RIGHT_PIN           GPIO_PIN_5      // PC5
#define JOY_RIGHT_PORT          GPIOC
#define JOY_UP_PIN              GPIO_PIN_6      // PC6
#define JOY_UP_PORT             GPIOC
#define JOY_DOWN_PIN            GPIO_PIN_7      // PC7
#define JOY_DOWN_PORT           GPIOC
#define JOY_RAPID_PIN           GPIO_PIN_8      // PC8
#define JOY_RAPID_PORT          GPIOC

// ============================================================================
// ПИНЫ КНОПКИ МЕНЮ
// ============================================================================
#define BTN_LEFT_PIN            GPIO_PIN_0      // PG0
#define BTN_LEFT_PORT           GPIOG
#define BTN_RIGHT_PIN           GPIO_PIN_1      // PG1
#define BTN_RIGHT_PORT          GPIOG
#define BTN_UP_PIN              GPIO_PIN_2      // PG2
#define BTN_UP_PORT             GPIOG
#define BTN_DOWN_PIN            GPIO_PIN_3      // PG3
#define BTN_DOWN_PORT           GPIOG
#define BTN_SELECT_PIN          GPIO_PIN_4      // PG4
#define BTN_SELECT_PORT         GPIOG

// ============================================================================
// ПИНЫ ПЕРЕКЛЮЧАТЕЛИ РЕЖИМА И ПОДРЕЖИМА
// Активный уровень: LOW (внешняя подтяжка 1кОм к +3.3В обязательна!).
// ============================================================================

/** Submode (3 позиции: Внутренний / Ручной / Наружный). */
#define SUBMODE_INT_PIN         GPIO_PIN_5      // PG5
#define SUBMODE_INT_PORT        GPIOG
#define SUBMODE_MAN_PIN         GPIO_PIN_6      // PG6
#define SUBMODE_MAN_PORT        GPIOG
#define SUBMODE_EXT_PIN         GPIO_PIN_7      // PG7
#define SUBMODE_EXT_PORT        GPIOG

/** Mode (8 позиций: M1-M8 через бинарный код).
 *  Порт PG8-PG15 читается как байт: mode = (~GPIOG->IDR >> 8) & 0xFF */
#define MODE_PORT               GPIOG
#define MODE_PIN_0              GPIO_PIN_8      // PG8  — бит 0
#define MODE_PIN_1              GPIO_PIN_9      // PG9  — бит 1
#define MODE_PIN_2              GPIO_PIN_10     // PG10 — бит 2
#define MODE_PIN_3              GPIO_PIN_11     // PG11 — бит 3
#define MODE_PIN_4              GPIO_PIN_12     // PG12 — бит 4
#define MODE_PIN_5              GPIO_PIN_13     // PG13 — бит 5
#define MODE_PIN_6              GPIO_PIN_14     // PG14 — бит 6
#define MODE_PIN_7              GPIO_PIN_15     // PG15 — бит 7

// ============================================================================
// ПИНЫ ЛИМИТНЫЕ ВЫКЛЮЧАТЕЛИ И СВЕТОДИОДЫ
// Кнопки: активный уровень LOW (PULLUP).
// LED: активный уровень LOW (открытый коллектор, резистор 470Ом).
// ============================================================================
#define LIMIT_BTN_LEFT_PIN      GPIO_PIN_0      // PF0
#define LIMIT_BTN_LEFT_PORT     GPIOF
#define LIMIT_LED_LEFT_PIN      GPIO_PIN_1      // PF1
#define LIMIT_LED_LEFT_PORT     GPIOF

#define LIMIT_BTN_RIGHT_PIN     GPIO_PIN_2      // PF2
#define LIMIT_BTN_RIGHT_PORT    GPIOF
#define LIMIT_LED_RIGHT_PIN     GPIO_PIN_3      // PF3
#define LIMIT_LED_RIGHT_PORT    GPIOF

#define LIMIT_BTN_FRONT_PIN     GPIO_PIN_4      // PF4
#define LIMIT_BTN_FRONT_PORT    GPIOF
#define LIMIT_LED_FRONT_PIN     GPIO_PIN_5      // PF5
#define LIMIT_LED_FRONT_PORT    GPIOF

#define LIMIT_BTN_REAR_PIN      GPIO_PIN_6      // PF6
#define LIMIT_BTN_REAR_PORT     GPIOF
#define LIMIT_LED_REAR_PIN      GPIO_PIN_7      // PF7
#define LIMIT_LED_REAR_PORT     GPIOF

// ============================================================================
// ПРОЧИЕ ВЫХОДЫ
// ============================================================================

/** Тахометр — выходной импульс на каждый оборот шпинделя. */
#define TACHO_PIN               GPIO_PIN_8      // PE8
#define TACHO_PORT              GPIOE

/** Зуммер — PWM для генерации тона (TIM4_CH1).
 *  Примечание: PD12 на DISCOVERY занято зелёным LED. Снять перемычку LD4. */
#define BEEPER_PIN              GPIO_PIN_12     // PD12 (TIM4_CH1)
#define BEEPER_PORT             GPIOD
#define BEEPER_TIMER            TIM4

// ============================================================================
// ПАРАМЕТРЫ ЛОГИКИ ELS
// ============================================================================

/** Задержка входа в режим автоповтора кнопок меню (количество циклов). */
#define DELAY_ENTER_KEYCYCLE    2
/** Интервал автоповтора кнопок меню. */
#define DELAY_INTO_KEYCYCLE     0

/** Максимальная и минимальная позиции лимитов (внутренние единицы). */
#define LIMIT_POS_MAX           1073741824L
#define LIMIT_POS_MIN          -1073741824L

/** Диапазон подач (мм/об × 100).
 *  MIN_FEED = 0.05мм/об, MAX_FEED = 2.00мм/об */
#define MIN_FEED                5
#define MAX_FEED                200

/** Диапазон асинхронных подач (мм/мин × 100). */
#define MIN_AFEED               5
#define MAX_AFEED               300

/** Количество заходов резьбы максимум. */
#define MAX_STARTS              8

/** Количество чистовых проходов по умолчанию (PASS_FINISH в Arduino). */
#define PASS_FINISH             2

/** Отскок Z по умолчанию в микрошагах (REBOUND_Z в Arduino). */
#define REBOUND_Z               200

/** Ускорение на подачах. */
#define FEED_ACCEL              3
