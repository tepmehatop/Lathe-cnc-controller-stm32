/**
 * @file  e2_hal.cpp
 * @brief Аппаратный слой ядра 7e2 для STM32F407: пины, таймеры, прерывания.
 *
 * См. e2_hal.h — там соответствие таймеров AVR и STM32.
 */
#include "e2_hal.h"
#include "../els_config.h"
#include <HardwareTimer.h>

// Приоритет всех прерываний движения: одинаковый, поэтому они не вытесняют
// друг друга — как на AVR, где обработчики не вложенные.
#define E2_MOTION_IRQ_PRIO   2

// ============================================================================
// Регистры OCR и флаги
// ============================================================================
E2Ocr OCR2A = { &TIM6->ARR };    // Timer2 → TIM6
E2Ocr OCR3A = { &TIM7->ARR };    // Timer3 → TIM7
E2Ocr OCR4A = { &TIM12->ARR };   // Timer4 → TIM12
volatile uint16_t OCR5A = 0;     // Timer5 — программный, см. _spindle_edge()

volatile uint8_t Spindle_Dir, Motor_Z_Dir, Joy_Z_flag, Step_Z_flag;
volatile uint8_t Motor_X_Dir, Joy_X_flag, Step_X_flag, Cone_flag;

volatile uint8_t e2_en_int0, e2_en_int2;
volatile uint8_t e2_en_t2a, e2_en_t2b, e2_en_t3a, e2_en_t3b;
volatile uint8_t e2_en_t4a, e2_en_t4b, e2_en_t5a, e2_en_t5b;

uint32_t e2_step_min_cycles = 0;
volatile uint32_t e2_z_edge = 0, e2_x_edge = 0;

static volatile uint16_t s_t5_cnt = 0;          // TCNT5
static volatile uint32_t s_spindle_edges = 0;   // все фронты канала A — для оборотов

static HardwareTimer* s_tim6  = nullptr;
static HardwareTimer* s_tim7  = nullptr;
static HardwareTimer* s_tim12 = nullptr;

// ============================================================================
// Пины
// ============================================================================
#define IN_PU(...)   pinMode(E2_APIN(__VA_ARGS__), INPUT_PULLUP)
#define OUT_LO(...)  do { pinMode(E2_APIN(__VA_ARGS__), OUTPUT); E2_CLR(__VA_ARGS__); } while (0)

// Motor_Init(): DDRL = B11111011, PORTL = B0000100 — все выходы в 0
void e2_motor_init(void) {
    OUT_LO(E2_PIN_Z_STEP); OUT_LO(E2_PIN_Z_DIR); OUT_LO(E2_PIN_Z_ENA);
    OUT_LO(E2_PIN_X_STEP); OUT_LO(E2_PIN_X_DIR); OUT_LO(E2_PIN_X_ENA);
    OUT_LO(E2_PIN_TACHO);
}

void e2_encoder_init(void) { IN_PU(E2_PIN_ENC_A); IN_PU(E2_PIN_ENC_B); }

void e2_hand_init(void) {
    IN_PU(E2_PIN_HAND_A); IN_PU(E2_PIN_HAND_B);
    IN_PU(E2_PIN_HAND_AXIS_Z); IN_PU(E2_PIN_HAND_AXIS_X);
    IN_PU(E2_PIN_HAND_X1); IN_PU(E2_PIN_HAND_X10);
}

// Limit_Init(): PORTA = B01010101 — светодиоды (нечётные биты) в 0, т.е. горят
void e2_limit_init(void) {
    IN_PU(E2_PIN_LIM_LEFT); IN_PU(E2_PIN_LIM_RIGHT); IN_PU(E2_PIN_LIM_FRONT); IN_PU(E2_PIN_LIM_REAR);
    OUT_LO(E2_PIN_LED_LEFT); OUT_LO(E2_PIN_LED_RIGHT); OUT_LO(E2_PIN_LED_FRONT); OUT_LO(E2_PIN_LED_REAR);
}

void e2_buttons_init(void) {
    IN_PU(E2_PIN_BTN_LEFT); IN_PU(E2_PIN_BTN_RIGHT); IN_PU(E2_PIN_BTN_UP);
    IN_PU(E2_PIN_BTN_DOWN); IN_PU(E2_PIN_BTN_SELECT);
}

void e2_joy_init(void) {
    IN_PU(E2_PIN_JOY_LEFT); IN_PU(E2_PIN_JOY_RIGHT); IN_PU(E2_PIN_JOY_UP);
    IN_PU(E2_PIN_JOY_DOWN); IN_PU(E2_PIN_JOY_RAPID);
    IN_PU(E2_PIN_SUB_INT); IN_PU(E2_PIN_SUB_MAN); IN_PU(E2_PIN_SUB_EXT);
}

void e2_mode_init(void) {
    IN_PU(E2_PIN_MODE_FEED);   IN_PU(E2_PIN_MODE_AFEED);  IN_PU(E2_PIN_MODE_CONE_L);
    IN_PU(E2_PIN_MODE_CONE_R); IN_PU(E2_PIN_MODE_THREAD); IN_PU(E2_PIN_MODE_SPHERE);
    IN_PU(E2_PIN_MODE_DIVIDER); IN_PU(E2_PIN_MODE_RESERVE);
}

// ============================================================================
// Энкодер шпинделя: INT0 (оба фронта A) + Timer5 (такт — передний фронт A)
// ============================================================================
static void _spindle_edge(void) {
    s_spindle_edges++;
    if (e2_en_int0) e2_isr_int0();

    // Timer5 в режиме CTC с внешним тактом: TCNT5 считает 0..OCR5A, на переходе
    // TOP → 0 срабатывает COMPA; COMPB (OCR5B = 0) — с тем же периодом.
    if (Enc_Ch_A) {
        if (s_t5_cnt >= OCR5A) {
            s_t5_cnt = 0;
            if (e2_en_t5a) e2_isr_t5a();
            if (e2_en_t5b) e2_isr_t5b();
        } else {
            s_t5_cnt++;
        }
    }
}

static void _hand_edge(void) {
    if (e2_en_int2) e2_isr_int2();
}

uint32_t e2_spindle_edges(void) { return s_spindle_edges; }

// ============================================================================
// Аппаратные таймеры TIM6/TIM7/TIM12 вместо Timer2/3/4 (режим CTC = перезагрузка по ARR)
// ============================================================================
static void _tim6_cb(void)  { if (e2_en_t2a) e2_isr_t2a(); if (e2_en_t2b) e2_isr_t2b(); }
static void _tim7_cb(void)  { if (e2_en_t3a) e2_isr_t3a(); if (e2_en_t3b) e2_isr_t3b(); }
static void _tim12_cb(void) { if (e2_en_t4a) e2_isr_t4a(); if (e2_en_t4b) e2_isr_t4b(); }

static HardwareTimer* _timer_start(TIM_TypeDef* tim, uint32_t hz, uint16_t period, void (*cb)(void)) {
    HardwareTimer* t = new HardwareTimer(tim);
    t->setPrescaleFactor(t->getTimerClkFreq() / hz);
    t->setOverflow(period + 1, TICK_FORMAT);       // ARR = period, как OCRnA в CTC
    t->attachInterrupt(cb);
    t->setInterruptPriority(E2_MOTION_IRQ_PRIO, 0);
    t->resume();
    tim->CR1 &= ~TIM_CR1_ARPE;                     // запись ARR действует сразу, как OCRnA на AVR
    return t;
}

void e2_timers_init(void) {
    // Счётчик тактов ядра — для минимальной длительности импульса STEP
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    e2_step_min_cycles = (uint32_t)((uint64_t)SystemCoreClock * STEP_PULSE_MIN_US / 1000000u);

    s_tim6  = _timer_start(TIM6,  500000, 240, _tim6_cb);    // Timer2: 16 МГц / 32
    s_tim7  = _timer_start(TIM7,  250000, 250, _tim7_cb);    // Timer3: 16 МГц / 64
    s_tim12 = _timer_start(TIM12, 250000, 1000, _tim12_cb);  // Timer4: 16 МГц / 64

    attachInterrupt(digitalPinToInterrupt(E2_APIN(E2_PIN_ENC_A)), _spindle_edge, CHANGE);
    attachInterrupt(digitalPinToInterrupt(E2_APIN(E2_PIN_HAND_A)), _hand_edge, CHANGE);
    HAL_NVIC_SetPriority(EXTI1_IRQn,   E2_MOTION_IRQ_PRIO, 0);   // PA1
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, E2_MOTION_IRQ_PRIO, 0);   // PE7
}

// Enable_INT_OCRnX(): TCNTn = 0; сброс флага; TIMSKn = (1<<OCIEnX) — второй канал выключается
static TIM_TypeDef* _tim_of(uint8_t timer) {
    return timer == 2 ? TIM6 : timer == 3 ? TIM7 : TIM12;
}

void e2_timer_enable(uint8_t timer, uint8_t ch) {
    if (timer == 5) {
        e2_en_t5a = 0; e2_en_t5b = 0;
        s_t5_cnt = 0;
        if (ch == 0) e2_en_t5a = 1; else e2_en_t5b = 1;
        return;
    }
    volatile uint8_t* a = timer == 2 ? &e2_en_t2a : timer == 3 ? &e2_en_t3a : &e2_en_t4a;
    volatile uint8_t* b = timer == 2 ? &e2_en_t2b : timer == 3 ? &e2_en_t3b : &e2_en_t4b;
    TIM_TypeDef* tim = _tim_of(timer);
    *a = 0; *b = 0;
    tim->CNT = 0;
    tim->SR = ~TIM_SR_UIF;
    if (ch == 0) *a = 1; else *b = 1;
}

void e2_timer_disable(uint8_t timer, uint8_t ch) {
    switch (timer) {
        case 2: if (ch == 0) e2_en_t2a = 0; else e2_en_t2b = 0; break;
        case 3: if (ch == 0) e2_en_t3a = 0; else e2_en_t3b = 0; break;
        case 4: if (ch == 0) e2_en_t4a = 0; else e2_en_t4b = 0; break;
        case 5: if (ch == 0) e2_en_t5a = 0; else e2_en_t5b = 0; break;
    }
}

// ============================================================================
// Timer1: флаг опроса клавиатуры. 16 МГц / 1024 / (625 + 1) = 40.06 мс
// ============================================================================
#define E2_KEYB_PERIOD_US  40064u
static uint32_t s_keyb_t0 = 0;

uint8_t e2_keyb_timer_flag(void) { return (micros() - s_keyb_t0) >= E2_KEYB_PERIOD_US; }
void    e2_keyb_timer_clear(void) { s_keyb_t0 = micros(); }
