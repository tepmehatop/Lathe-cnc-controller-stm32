/**
 * @file  drv_inputs.cpp
 * @brief Драйвер GPIO входов — Этап 5
 *
 * Дебаунс 12мс (4 выборки по 3мс): только устойчивый сигнал принимается.
 *
 * Джойстик    PC4-PC8:    Active LOW, INPUT_PULLUP (внутренняя)
 * Кнопки меню PG0-PG4:    Active LOW, INPUT (внешняя 10кОм)
 * Лимиты BTN  PF0,2,4,6:  Active LOW, INPUT (внешняя 10кОм)
 * Лимиты LED  PF1,3,5,7:  Active HIGH output
 * Submode SW  PG5-PG7:    Active LOW, INPUT (внешняя 1кОм)
 * Mode SW     PG8-PG15:   Active LOW, INPUT (внешняя 1кОм) — байт через IDR
 */

#include "drv_inputs.h"
#include "../Core/els_config.h"
#include <Arduino.h>

// ============================================================
// Дебаунс: 4 выборки по 3мс = 12мс стабилизация
// ============================================================
#define DEBOUNCE_MS         3
#define DEBOUNCE_PRESSED    0x0Fu   // последние 4 выборки все = нажато

// Сдвиговые регистры (младшие 4 бита = 4 последних выборки)
static uint8_t s_joy_raw[5];   // LEFT RIGHT UP DOWN RAPID
static uint8_t s_btn_raw[5];   // LEFT RIGHT UP DOWN SELECT
static uint8_t s_lim_raw[4];   // LEFT RIGHT FRONT REAR
static uint8_t s_sub_raw[3];   // INT MAN EXT

// Дебаунсированные состояния
static JoyState_t  s_joy     = JOY_NONE;
static BtnState_t  s_btn     = BTN_NONE;
static LimState_t  s_lim     = LIM_NONE;
static uint8_t     s_submode = 0;
static uint8_t     s_mode    = 0;

static uint32_t s_last_sample = 0;

static inline uint8_t _pressed(uint32_t pin) {
    return (digitalRead(pin) == LOW) ? 1u : 0u;
}

static inline void _shift(uint8_t* reg, uint8_t bit) {
    *reg = (uint8_t)((*reg << 1) | (bit & 1u));
}

// ============================================================
// Инициализация
// ============================================================
void DRV_Inputs_Init(void) {
    // Джойстик PC4-PC8: внутренняя подтяжка
    pinMode(PC4, INPUT_PULLUP);
    pinMode(PC5, INPUT_PULLUP);
    pinMode(PC6, INPUT_PULLUP);
    pinMode(PC7, INPUT_PULLUP);
    pinMode(PC8, INPUT_PULLUP);

    // Кнопки меню PG0-PG4: внешняя 10кОм
    pinMode(PG_0, INPUT);
    pinMode(PG_1, INPUT);
    pinMode(PG_2, INPUT);
    pinMode(PG_3, INPUT);
    pinMode(PG_4, INPUT);

    // Лимиты (кнопки) PF0,PF2,PF4,PF6: внешняя 10кОм
    pinMode(PF_0, INPUT);
    pinMode(PF_2, INPUT);
    pinMode(PF_4, INPUT);
    pinMode(PF_6, INPUT);

    // Лимиты (LED) PF1,PF3,PF5,PF7: выход, Active HIGH
    pinMode(PF_1, OUTPUT); digitalWrite(PF_1, LOW);
    pinMode(PF_3, OUTPUT); digitalWrite(PF_3, LOW);
    pinMode(PF_5, OUTPUT); digitalWrite(PF_5, LOW);
    pinMode(PF_7, OUTPUT); digitalWrite(PF_7, LOW);

    // Submode PG5-PG7: внешняя 1кОм
    pinMode(PG_5, INPUT);
    pinMode(PG_6, INPUT);
    pinMode(PG_7, INPUT);

    // Mode PG8-PG15: внешняя 1кОм (ОБЯЗАТЕЛЬНА!)
    pinMode(PG_8,  INPUT);
    pinMode(PG_9,  INPUT);
    pinMode(PG_10, INPUT);
    pinMode(PG_11, INPUT);
    pinMode(PG_12, INPUT);
    pinMode(PG_13, INPUT);
    pinMode(PG_14, INPUT);
    pinMode(PG_15, INPUT);

    s_last_sample = millis();
}

// ============================================================
// Обработка — вызывать из loop()
// ============================================================
void DRV_Inputs_Process(void) {
    uint32_t now = millis();
    if ((now - s_last_sample) < DEBOUNCE_MS) return;
    s_last_sample = now;

    // --- Джойстик ---
    _shift(&s_joy_raw[0], _pressed(PC4));
    _shift(&s_joy_raw[1], _pressed(PC5));
    _shift(&s_joy_raw[2], _pressed(PC6));
    _shift(&s_joy_raw[3], _pressed(PC7));
    _shift(&s_joy_raw[4], _pressed(PC8));

    JoyState_t joy = JOY_NONE;
    if ((s_joy_raw[0] & 0x0Fu) == DEBOUNCE_PRESSED) joy = (JoyState_t)(joy | JOY_LEFT);
    if ((s_joy_raw[1] & 0x0Fu) == DEBOUNCE_PRESSED) joy = (JoyState_t)(joy | JOY_RIGHT);
    if ((s_joy_raw[2] & 0x0Fu) == DEBOUNCE_PRESSED) joy = (JoyState_t)(joy | JOY_UP);
    if ((s_joy_raw[3] & 0x0Fu) == DEBOUNCE_PRESSED) joy = (JoyState_t)(joy | JOY_DOWN);
    if ((s_joy_raw[4] & 0x0Fu) == DEBOUNCE_PRESSED) joy = (JoyState_t)(joy | JOY_RAPID);
    s_joy = joy;

    // --- Кнопки меню ---
    _shift(&s_btn_raw[0], _pressed(PG_0));
    _shift(&s_btn_raw[1], _pressed(PG_1));
    _shift(&s_btn_raw[2], _pressed(PG_2));
    _shift(&s_btn_raw[3], _pressed(PG_3));
    _shift(&s_btn_raw[4], _pressed(PG_4));

    BtnState_t btn = BTN_NONE;
    if ((s_btn_raw[0] & 0x0Fu) == DEBOUNCE_PRESSED) btn = (BtnState_t)(btn | BTN_LEFT);
    if ((s_btn_raw[1] & 0x0Fu) == DEBOUNCE_PRESSED) btn = (BtnState_t)(btn | BTN_RIGHT);
    if ((s_btn_raw[2] & 0x0Fu) == DEBOUNCE_PRESSED) btn = (BtnState_t)(btn | BTN_UP);
    if ((s_btn_raw[3] & 0x0Fu) == DEBOUNCE_PRESSED) btn = (BtnState_t)(btn | BTN_DOWN);
    if ((s_btn_raw[4] & 0x0Fu) == DEBOUNCE_PRESSED) btn = (BtnState_t)(btn | BTN_SELECT);
    s_btn = btn;

    // --- Лимитные выключатели ---
    _shift(&s_lim_raw[0], _pressed(PF_0));
    _shift(&s_lim_raw[1], _pressed(PF_2));
    _shift(&s_lim_raw[2], _pressed(PF_4));
    _shift(&s_lim_raw[3], _pressed(PF_6));

    LimState_t lim = LIM_NONE;
    if ((s_lim_raw[0] & 0x0Fu) == DEBOUNCE_PRESSED) lim = (LimState_t)(lim | LIM_LEFT);
    if ((s_lim_raw[1] & 0x0Fu) == DEBOUNCE_PRESSED) lim = (LimState_t)(lim | LIM_RIGHT);
    if ((s_lim_raw[2] & 0x0Fu) == DEBOUNCE_PRESSED) lim = (LimState_t)(lim | LIM_FRONT);
    if ((s_lim_raw[3] & 0x0Fu) == DEBOUNCE_PRESSED) lim = (LimState_t)(lim | LIM_REAR);
    s_lim = lim;

    // --- Submode (3-позиционный переключатель) ---
    _shift(&s_sub_raw[0], _pressed(PG_5));
    _shift(&s_sub_raw[1], _pressed(PG_6));
    _shift(&s_sub_raw[2], _pressed(PG_7));

    if      ((s_sub_raw[0] & 0x0Fu) == DEBOUNCE_PRESSED) s_submode = 0; // Internal
    else if ((s_sub_raw[1] & 0x0Fu) == DEBOUNCE_PRESSED) s_submode = 1; // Manual
    else if ((s_sub_raw[2] & 0x0Fu) == DEBOUNCE_PRESSED) s_submode = 2; // External

    // --- Mode PG8-PG15: читаем IDR напрямую ---
    // Active LOW: нажатый контакт = 0 в IDR → бит = 1 в mode
    s_mode = (uint8_t)(~(GPIOG->IDR >> 8) & 0xFFu);
}

// ============================================================
// Геттеры
// ============================================================
JoyState_t  DRV_Inputs_GetJoy(void)     { return s_joy; }
BtnState_t  DRV_Inputs_GetBtn(void)     { return s_btn; }
LimState_t  DRV_Inputs_GetLimits(void)  { return s_lim; }
uint8_t     DRV_Inputs_GetMode(void)    { return s_mode; }
uint8_t     DRV_Inputs_GetSubmode(void) { return s_submode; }

// ============================================================
// Управление LED лимитов (Active HIGH)
// ============================================================
void DRV_Inputs_SetLimitLED(LimState_t leds) {
    digitalWrite(PF_1, (leds & LIM_LEFT)  ? HIGH : LOW);
    digitalWrite(PF_3, (leds & LIM_RIGHT) ? HIGH : LOW);
    digitalWrite(PF_5, (leds & LIM_FRONT) ? HIGH : LOW);
    digitalWrite(PF_7, (leds & LIM_REAR)  ? HIGH : LOW);
}
