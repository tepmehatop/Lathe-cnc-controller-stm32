#pragma once
/**
 * @file  e2_hal.h
 * @brief Аппаратный слой ядра 7e2 для STM32F407.
 *
 * Оригинальный код 7e2 (Arduino Mega) обращается к железу через макросы
 * (Motor_Z_SetPulse(), Joy_Read, Enable_INT_OCR2A() ...) и регистры OCRnA.
 * Здесь те же имена реализованы на STM32, поэтому логика автора переносится
 * без изменений. Уровни на выводах совпадают с оригиналом: схема сопряжения
 * (транзисторные ключи на STEP/DIR/ENA, подтяжки входов) та же, что у автора.
 *
 * Соответствие таймеров AVR → STM32:
 *   INT0  (энкодер шпинделя, оба фронта A)  → EXTI E2_PIN_ENC_A
 *   Timer5 (такт от энкодера, вход T5)       → программный счётчик фронтов A в том же прерывании
 *   Timer2 (500 кГц, ускоренные ходы)        → TIM6
 *   Timer3 (250 кГц, РГИ)                    → TIM7
 *   Timer4 (250 кГц, асинхронная подача)     → TIM12
 *   INT2  (РГИ, оба фронта A)                → EXTI E2_PIN_HAND_A
 *   Timer1 (опрос клавиатуры, 40 мс)         → millis()
 * Все прерывания движения — одного приоритета и не вытесняют друг друга, как на AVR.
 */
#include <Arduino.h>
#include "e2_pins.h"

// ============================================================================
// Быстрый доступ к GPIO по номеру вывода Arduino
// ============================================================================
// Пин задаётся парой «порт, номер» (e2_pins.h); макросы раскрываются в прямую
// работу с регистрами GPIOx — без таблиц, пригодно для прерываний.
#define E2_APIN_(port, n)   P##port##n                       // номер вывода Arduino (pinMode и т.п.)
#define E2_GPIO_(port, n)   GPIO##port
#define E2_BIT_(port, n)    (1u << (n))
#define E2_APIN(...)        E2_APIN_(__VA_ARGS__)
#define E2_GPIO(...)        E2_GPIO_(__VA_ARGS__)
#define E2_BIT(...)         E2_BIT_(__VA_ARGS__)
#define E2_SET(...)         (E2_GPIO(__VA_ARGS__)->BSRR = E2_BIT(__VA_ARGS__))
#define E2_CLR(...)         (E2_GPIO(__VA_ARGS__)->BSRR = E2_BIT(__VA_ARGS__) << 16)
#define E2_TGL(...)         (E2_GPIO(__VA_ARGS__)->ODR ^= E2_BIT(__VA_ARGS__))
#define E2_RD(...)          ((E2_GPIO(__VA_ARGS__)->IDR & E2_BIT(__VA_ARGS__)) ? 1u : 0u)
#define E2_ODR(...)         ((E2_GPIO(__VA_ARGS__)->ODR & E2_BIT(__VA_ARGS__)) ? 1u : 0u)

// _delay_ms из avr/delay.h
#define _delay_ms(ms)       delay(ms)

// ============================================================================
// Регистры сравнения таймеров AVR
//  OCR2A/OCR3A/OCR4A — период аппаратного таймера STM32 (ARR), как TOP в режиме CTC.
//  OCR5A — период программного «Timer5», который считает фронты энкодера.
// ============================================================================
struct E2Ocr {
    volatile uint32_t* reg;
    operator uint16_t() const          { return (uint16_t)*reg; }
    E2Ocr& operator=(uint16_t v)       { *reg = v; return *this; }
    E2Ocr& operator=(const E2Ocr& o)   { *reg = *o.reg; return *this; }
    E2Ocr& operator++()                { *reg = *reg + 1; return *this; }
    E2Ocr& operator--()                { *reg = *reg - 1; return *this; }
    uint16_t operator++(int)           { uint16_t v = (uint16_t)*reg; *reg = v + 1; return v; }
    uint16_t operator--(int)           { uint16_t v = (uint16_t)*reg; *reg = v - 1; return v; }
};
extern E2Ocr OCR2A, OCR3A, OCR4A;
extern volatile uint16_t OCR5A;

// ============================================================================
// Флаги GPIOR0 оригинала (битовое поле в регистре) — обычные volatile-переменные
// ============================================================================
extern volatile uint8_t Spindle_Dir, Motor_Z_Dir, Joy_Z_flag, Step_Z_flag;
extern volatile uint8_t Motor_X_Dir, Joy_X_flag, Step_X_flag, Cone_flag;

// ============================================================================
// Зуммер
// ============================================================================
#define Beeper_Init()          do { pinMode(E2_APIN(E2_PIN_BEEPER), OUTPUT); } while (0)
#define Beeper_On()            E2_CLR(E2_PIN_BEEPER)     // Pin16 0
#define Beeper_Off()           E2_SET(E2_PIN_BEEPER)     // Pin16 1

// ============================================================================
// Шаговые двигатели (уровни как у оригинала: импульс — низкий уровень)
// ============================================================================
void e2_motor_init(void);
#define Motor_Init()           e2_motor_init()

#define Motor_Z_SetPulse()     E2_CLR(E2_PIN_Z_STEP)
#define Motor_Z_RemovePulse()  E2_SET(E2_PIN_Z_STEP)
#define Motor_Z_InvertPulse()  E2_TGL(E2_PIN_Z_STEP)
#define Read_Z_State           E2_ODR(E2_PIN_Z_STEP)

#define Motor_X_SetPulse()     E2_CLR(E2_PIN_X_STEP)
#define Motor_X_RemovePulse()  E2_SET(E2_PIN_X_STEP)
#define Motor_X_InvertPulse()  E2_TGL(E2_PIN_X_STEP)
#define Read_X_State           E2_ODR(E2_PIN_X_STEP)

#define Motor_Z_CW()           E2_CLR(E2_PIN_Z_DIR)
#define Motor_Z_CCW()          E2_SET(E2_PIN_Z_DIR)
#define Motor_X_CW()           E2_CLR(E2_PIN_X_DIR)
#define Motor_X_CCW()          E2_SET(E2_PIN_X_DIR)

#define Motor_Z_Enable()       do { E2_SET(E2_PIN_Z_ENA); _delay_ms(100); } while (0)
#define Motor_Z_Disable()      { E2_CLR(E2_PIN_Z_ENA); _delay_ms(100); }
#define Read_Z_Ena_State       E2_ODR(E2_PIN_Z_ENA)
#define Motor_X_Enable()       do { E2_SET(E2_PIN_X_ENA); _delay_ms(100); } while (0)
#define Motor_X_Disable()      { E2_CLR(E2_PIN_X_ENA); _delay_ms(100); }
#define Read_X_Ena_State       E2_ODR(E2_PIN_X_ENA)

// ============================================================================
// Тахометр
// ============================================================================
#define TachoSetPulse()        E2_SET(E2_PIN_TACHO)
#define TachoRemovePulse()     E2_CLR(E2_PIN_TACHO)

// ============================================================================
// Энкодер шпинделя
// ============================================================================
#define Encoder_Init()         e2_encoder_init()
#define Enc_Ch_A               E2_RD(E2_PIN_ENC_A)
#define Enc_Ch_B               E2_RD(E2_PIN_ENC_B)
#define Enc_Read               Enc_Ch_B

// ============================================================================
// РГИ: каналы, переключатель оси и масштаба.
// Битовые шаблоны те же, что у оригинала (порты E и J Arduino Mega):
//   Hand_Axis_Read  — бит4 = вход «Z», бит5 = вход «X»   (0 = выбрано)
//   Hand_Scale_Read — бит0 = вход «x10», бит1 = вход «x1» (0 = выбрано)
// ============================================================================
#define Hand_Init()            e2_hand_init()
#define Hand_Ch_A              E2_RD(E2_PIN_HAND_A)
#define Hand_Ch_B              E2_RD(E2_PIN_HAND_B)
#define Hand_Axis_Read         ((uint8_t)((E2_RD(E2_PIN_HAND_AXIS_Z) << 4) | (E2_RD(E2_PIN_HAND_AXIS_X) << 5)))
#define Hand_Scale_Read        ((uint8_t)((E2_RD(E2_PIN_HAND_X10) << 0) | (E2_RD(E2_PIN_HAND_X1) << 1)))

// ============================================================================
// Кнопки упоров и светодиоды (порт A Mega: входы — чётные биты, LED — нечётные)
//   бит0 = Rear, бит2 = Front, бит4 = Right, бит6 = Left (0 = нажата)
// ============================================================================
#define Limit_Init()           e2_limit_init()
#define Limit_Buttons_Read     ((uint8_t)((E2_RD(E2_PIN_LIM_REAR) << 0) | (E2_RD(E2_PIN_LIM_FRONT) << 2) | \
                                          (E2_RD(E2_PIN_LIM_RIGHT) << 4) | (E2_RD(E2_PIN_LIM_LEFT) << 6)))
#define Limit_Rear_LED_On()    E2_CLR(E2_PIN_LED_REAR)
#define Limit_Rear_LED_Off()   E2_SET(E2_PIN_LED_REAR)
#define Limit_Front_LED_On()   E2_CLR(E2_PIN_LED_FRONT)
#define Limit_Front_LED_Off()  E2_SET(E2_PIN_LED_FRONT)
#define Limit_Right_LED_On()   E2_CLR(E2_PIN_LED_RIGHT)
#define Limit_Right_LED_Off()  E2_SET(E2_PIN_LED_RIGHT)
#define Limit_Left_LED_On()    E2_CLR(E2_PIN_LED_LEFT)
#define Limit_Left_LED_Off()   E2_SET(E2_PIN_LED_LEFT)

// ============================================================================
// Кнопки меню (порт F Mega): бит0 Left, бит1 Right, бит2 Up, бит3 Down, бит4 Select
// ============================================================================
#define Menu_Buttons_Init()    e2_buttons_init()
#define Buttons_Read           ((uint8_t)((E2_RD(E2_PIN_BTN_LEFT) << 0) | (E2_RD(E2_PIN_BTN_RIGHT) << 1) | \
                                          (E2_RD(E2_PIN_BTN_UP) << 2)   | (E2_RD(E2_PIN_BTN_DOWN) << 3)))
#define Button_Sel_Read        ((uint8_t)(E2_RD(E2_PIN_BTN_SELECT) << 4))

// ============================================================================
// Джойстик (порт K Mega): бит0 Left, бит1 Right, бит2 Up, бит3 Down, бит4 Rapid;
// подрежим: бит5 Int, бит6 Man, бит7 Ext
// ============================================================================
#define Joy_Init()             e2_joy_init()
#define Joy_Read               ((uint8_t)((E2_RD(E2_PIN_JOY_LEFT) << 0) | (E2_RD(E2_PIN_JOY_RIGHT) << 1) | \
                                          (E2_RD(E2_PIN_JOY_UP) << 2)   | (E2_RD(E2_PIN_JOY_DOWN) << 3)))
#define Button_Rapid           (g_emul_rapid ? 0 : (E2_RD(E2_PIN_JOY_RAPID) << 4))
#define Submode_Read           ((uint8_t)((E2_RD(E2_PIN_SUB_INT) << 5) | (E2_RD(E2_PIN_SUB_MAN) << 6) | \
                                          (E2_RD(E2_PIN_SUB_EXT) << 7)))

// ============================================================================
// Переключатель режимов (порт C Mega): бит7 Feed ... бит0 Reserve (0 = выбран)
// ============================================================================
#define Mode_Switch_Init()     e2_mode_init()
#define Mode_Read              ((uint8_t)((E2_RD(E2_PIN_MODE_FEED) << 7)    | (E2_RD(E2_PIN_MODE_AFEED) << 6) | \
                                          (E2_RD(E2_PIN_MODE_CONE_L) << 5)  | (E2_RD(E2_PIN_MODE_CONE_R) << 4) | \
                                          (E2_RD(E2_PIN_MODE_THREAD) << 3)  | (E2_RD(E2_PIN_MODE_SPHERE) << 2) | \
                                          (E2_RD(E2_PIN_MODE_DIVIDER) << 1) | (E2_RD(E2_PIN_MODE_RESERVE) << 0)))

// ============================================================================
// Таймеры и прерывания
// ============================================================================
void e2_timers_init(void);
void e2_encoder_init(void);
void e2_hand_init(void);
void e2_limit_init(void);
void e2_buttons_init(void);
void e2_joy_init(void);
void e2_mode_init(void);

// Флаги разрешения «прерываний» (аналоги битов TIMSKn/EIMSK)
extern volatile uint8_t e2_en_int0, e2_en_int2;
extern volatile uint8_t e2_en_t2a, e2_en_t2b, e2_en_t3a, e2_en_t3b;
extern volatile uint8_t e2_en_t4a, e2_en_t4b, e2_en_t5a, e2_en_t5b;
void e2_timer_enable(uint8_t timer, uint8_t ch);    // timer 2..5, ch 0 = A, 1 = B
void e2_timer_disable(uint8_t timer, uint8_t ch);

#define Timer2_Init()          // настраиваются в e2_timers_init()
#define Timer3_Init()
#define Timer4_Init()
#define Timer5_Init()
#define INT0_Init()
#define INT2_Init()

#define Enable_INT0()          (e2_en_int0 = 1)
#define Disable_INT0()         (e2_en_int0 = 0)
#define Ena_INT_Hcoder()       (e2_en_int2 = 1)
#define Disa_INT_Hcoder()      (e2_en_int2 = 0)

#define Enable_INT_OCR2A()     e2_timer_enable(2, 0)
#define Disable_INT_OCR2A()    e2_timer_disable(2, 0)
#define Enable_INT_OCR2B()     e2_timer_enable(2, 1)
#define Disable_INT_OCR2B()    e2_timer_disable(2, 1)
#define Enable_INT_OCR3A()     e2_timer_enable(3, 0)
#define Disable_INT_OCR3A()    e2_timer_disable(3, 0)
#define Enable_INT_OCR3B()     e2_timer_enable(3, 1)
#define Disable_INT_OCR3B()    e2_timer_disable(3, 1)
#define Enable_INT_OCR4A()     e2_timer_enable(4, 0)
#define Disable_INT_OCR4A()    e2_timer_disable(4, 0)
#define Enable_INT_OCR4B()     e2_timer_enable(4, 1)
#define Disable_INT_OCR4B()    e2_timer_disable(4, 1)
#define Enable_INT_OCR5A()     e2_timer_enable(5, 0)
#define Disable_INT_OCR5A()    e2_timer_disable(5, 0)
#define Enable_INT_OCR5B()     e2_timer_enable(5, 1)
#define Disable_INT_OCR5B()    e2_timer_disable(5, 1)

// Опрос клавиатуры: на AVR — флаг Timer1 каждые 40 мс
uint8_t e2_keyb_timer_flag(void);
void    e2_keyb_timer_clear(void);
#define KEYB_TIMER_FLAG        e2_keyb_timer_flag()
#define CLEAR_KEYB_TIMER       e2_keyb_timer_clear()

// Тела прерываний оригинала (определены в ядре)
void e2_isr_int0(void);
void e2_isr_int2(void);
void e2_isr_t2a(void);
void e2_isr_t2b(void);
void e2_isr_t3a(void);
void e2_isr_t3b(void);
void e2_isr_t4a(void);
void e2_isr_t4b(void);
void e2_isr_t5a(void);
void e2_isr_t5b(void);

// Счётчик фронтов канала A энкодера шпинделя (2 × ENC_LINE_PER_REV на оборот) —
// для расчёта оборотов, которые показывает дисплей ESP32 (в оригинале не было)
uint32_t e2_spindle_edges(void);

extern bool g_emul_rapid;
