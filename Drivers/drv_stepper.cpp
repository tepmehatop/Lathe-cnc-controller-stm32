/**
 * @file  drv_stepper.cpp
 * @brief Драйвер шаговых двигателей — Этап 7
 *
 * Мотор Y: STEP=PE9 (TIM1_CH1), DIR=PE10, EN=PE11
 * Мотор X: STEP=PE13 (TIM1_CH3), DIR=PE14, EN=PE15
 *
 * Генерация STEP-импульсов: HardwareTimer (TIM1) PWM + overflow callback.
 * Callback на каждый период = 1 шаг. Позиция обновляется в callback.
 *
 * TIM1 разделяется между осями: в любой момент времени обе оси могут
 * работать одновременно с одинаковой частотой (ELS: Y+X для нарезки конуса).
 * Для независимых скоростей — step divider в callback (TODO Этап 8).
 */

#include "drv_stepper.h"
#include "../Core/els_config.h"
#include <Arduino.h>
#include <HardwareTimer.h>

// ============================================================
// Масштаб: шагов на 1000 единиц позиции (0.001мм × 1000 = 1мм)
// steps/mm = (STEP_PER_REV * MICROSTEP * 100) / SCREW
// SCREW_Y = 200 (ед. 0.01мм = 2.0мм шаг)
// SCREW_X = 125 (ед. 0.01мм = 1.25мм шаг)
#define STEPS_PER_1000UNIT_Y  \
    ((uint32_t)(MOTOR_Y_STEP_PER_REV) * MICROSTEP_Y * 100UL / SCREW_Y)
#define STEPS_PER_1000UNIT_X  \
    ((uint32_t)(MOTOR_X_STEP_PER_REV) * MICROSTEP_X * 100UL / SCREW_X)

// Пины
#define PIN_DIR_Y    PE10
#define PIN_EN_Y     PE11
#define PIN_DIR_X    PE14
#define PIN_EN_X     PE15

// ============================================================
// Состояние осей
// ============================================================
struct AxisState {
    volatile int32_t  pos;                // позиция в 0.001 мм
    volatile int32_t  steps_left;         // шагов до конца движения
    volatile int8_t   dir;                // +1 или -1
    volatile uint8_t  moving;
    uint32_t          steps_per_1000unit; // шагов на 1000 единиц позиции
};

static AxisState      s_ax[2];
static HardwareTimer *s_htim1 = nullptr;

static volatile uint8_t s_ch1_active = 0; // Y
static volatile uint8_t s_ch3_active = 0; // X

// ============================================================
// Callback — вызывается на каждом overflow TIM1 (= 1 шаг)
// ============================================================
static void _step_overflow(void) {
    // --- Ось Y ---
    if (s_ch1_active) {
        s_ax[AXIS_Y].pos += s_ax[AXIS_Y].dir;
        if (--s_ax[AXIS_Y].steps_left <= 0) {
            TIM1->CCER &= ~TIM_CCER_CC1E;   // Выключить CH1 output
            s_ch1_active             = 0;
            s_ax[AXIS_Y].moving      = 0;
            s_ax[AXIS_Y].steps_left  = 0;
            GPIOE->BSRR = GPIO_PIN_11;       // EN_Y HIGH = выключить драйвер
        }
    }
    // --- Ось X ---
    if (s_ch3_active) {
        s_ax[AXIS_X].pos += s_ax[AXIS_X].dir;
        if (--s_ax[AXIS_X].steps_left <= 0) {
            TIM1->CCER &= ~TIM_CCER_CC3E;
            s_ch3_active             = 0;
            s_ax[AXIS_X].moving      = 0;
            s_ax[AXIS_X].steps_left  = 0;
            GPIOE->BSRR = GPIO_PIN_15;       // EN_X HIGH = выключить
        }
    }
}

// ============================================================
// Инициализация
// ============================================================
void DRV_Stepper_Init(void) {
    s_ax[AXIS_Y].steps_per_1000unit = STEPS_PER_1000UNIT_Y;
    s_ax[AXIS_X].steps_per_1000unit = STEPS_PER_1000UNIT_X;

    // DIR, EN как обычные выходы
    pinMode(PIN_DIR_Y, OUTPUT); digitalWrite(PIN_DIR_Y, LOW);
    pinMode(PIN_EN_Y,  OUTPUT); digitalWrite(PIN_EN_Y,  HIGH); // Active LOW — выключен
    pinMode(PIN_DIR_X, OUTPUT); digitalWrite(PIN_DIR_X, LOW);
    pinMode(PIN_EN_X,  OUTPUT); digitalWrite(PIN_EN_X,  HIGH);

    // TIM1: CH1 = PE9 (STEP_Y), CH3 = PE13 (STEP_X)
    s_htim1 = new HardwareTimer(TIM1);

    s_htim1->setMode(1, TIMER_OUTPUT_COMPARE_PWM1, PE9);
    s_htim1->setMode(3, TIMER_OUTPUT_COMPARE_PWM1, PE13);

    // Дефолтная частота 1 кГц, CCR = 1% ≈ 10 мкс pulse (достаточно для A4988/DRV8825)
    s_htim1->setOverflow(1000, HERTZ_FORMAT);
    s_htim1->setCaptureCompare(1, 1, PERCENT_COMPARE_FORMAT);
    s_htim1->setCaptureCompare(3, 1, PERCENT_COMPARE_FORMAT);

    // Выходы пока выключены (MOE устанавливает HardwareTimer)
    TIM1->CCER &= ~(TIM_CCER_CC1E | TIM_CCER_CC3E);

    // Callback на overflow
    s_htim1->attachInterrupt(_step_overflow);

    // Таймер запущен, но CCER выходы выключены — шагов не будет
    s_htim1->resume();
}

// ============================================================
// target_pos в 0.001 мм, speed_hz в шагах/сек
// ============================================================
void DRV_Stepper_MoveTo(Axis_t axis, int32_t target_pos, uint32_t speed_hz) {
    if (speed_hz == 0) return;

    AxisState* ax = &s_ax[axis];

    int32_t delta_unit = target_pos - ax->pos;
    if (delta_unit == 0) return;

    int32_t abs_delta = (delta_unit > 0) ? delta_unit : -delta_unit;
    int32_t steps = (int32_t)(((int64_t)abs_delta * ax->steps_per_1000unit) / 1000LL);
    if (steps == 0) return;

    ax->dir        = (delta_unit > 0) ? +1 : -1;
    ax->steps_left = steps;
    ax->moving     = 1;

    // DIR и EN
    if (axis == AXIS_Y) {
        digitalWrite(PIN_DIR_Y, (ax->dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_11 << 16u; // LOW = включить
    } else {
        digitalWrite(PIN_DIR_X, (ax->dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_15 << 16u;
    }

    // Перенастроить частоту только если нет другого активного движения
    // (смена частоты при активном движении нарушила бы скорость второй оси)
    if (!s_ch1_active && !s_ch3_active) {
        s_htim1->setOverflow(speed_hz, HERTZ_FORMAT);
        // Pulse width ~5мкс: 5мкс / period_us × 100%
        // При speed_hz < 200кГц период ≥ 5мкс → 1% достаточно для ≥ 100Гц
        // Для высоких скоростей вычислим точнее:
        uint32_t period_us = 1000000UL / speed_hz;
        uint32_t duty_pct  = (period_us >= 20u) ? 1u : 5u; // 5% если период < 20мкс
        s_htim1->setCaptureCompare(1, duty_pct, PERCENT_COMPARE_FORMAT);
        s_htim1->setCaptureCompare(3, duty_pct, PERCENT_COMPARE_FORMAT);
    }

    // Включить output канала
    if (axis == AXIS_Y) {
        s_ch1_active = 1;
        TIM1->CCER |= TIM_CCER_CC1E;
    } else {
        s_ch3_active = 1;
        TIM1->CCER |= TIM_CCER_CC3E;
    }
}

// ============================================================
// Остановка
// ============================================================
void DRV_Stepper_Stop(Axis_t axis) {
    if (axis == AXIS_Y) {
        TIM1->CCER &= ~TIM_CCER_CC1E;
        s_ch1_active = 0;
        digitalWrite(PIN_EN_Y, HIGH);
    } else {
        TIM1->CCER &= ~TIM_CCER_CC3E;
        s_ch3_active = 0;
        digitalWrite(PIN_EN_X, HIGH);
    }
    s_ax[axis].moving     = 0;
    s_ax[axis].steps_left = 0;
}

void DRV_Stepper_StopAll(void) {
    DRV_Stepper_Stop(AXIS_Y);
    DRV_Stepper_Stop(AXIS_X);
}

void DRV_Stepper_Enable(Axis_t axis, uint8_t en) {
    if (axis == AXIS_Y) {
        digitalWrite(PIN_EN_Y, en ? LOW : HIGH);
    } else {
        digitalWrite(PIN_EN_X, en ? LOW : HIGH);
    }
}

int32_t DRV_Stepper_GetPos(Axis_t axis) {
    return s_ax[axis].pos;
}

uint8_t DRV_Stepper_IsMoving(Axis_t axis) {
    return s_ax[axis].moving;
}

// Вызывать из loop() — в аппаратной реализации не требуется
void DRV_Stepper_Update(void) {
    // Всё обрабатывается в ISR (HardwareTimer callback)
}
