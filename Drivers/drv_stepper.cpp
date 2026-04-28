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

static volatile uint8_t s_ch1_active = 0; // Y (TIM1_CH1 PWM)
static volatile uint8_t s_ch3_active = 0; // X (TIM1_CH3 PWM, не конус)

// ── Конусный режим: Bresenham X слейвится к Y ──────────────────────────────
// В конусном режиме PE13 переключается в GPIO OUTPUT. STEP-импульсы X
// генерируются вручную в ISR (set HIGH → сброс LOW на следующем тике).
static volatile uint8_t  s_cone_mode    = 0;
static volatile int8_t   s_cone_dir_x   = 0;
static volatile uint8_t  s_cone_cs_div  = 1;
static volatile int16_t  s_cone_cm_div  = 0;
static volatile int16_t  s_cone_cs_cnt  = 0;
static volatile int16_t  s_cone_cm_cnt  = 0;
static volatile uint8_t  s_cone_x_pulse = 0; // 1 = PE13 HIGH, сбросить на следующем тике

// Переключить PE13 между TIM1_CH3 AF и GPIO OUTPUT
static void _pe13_set_gpio_output(void) {
    // MODER[27:26] = 01 (output); preserve AF in AFRL
    GPIOE->MODER = (GPIOE->MODER & ~(3u << 26u)) | (1u << 26u);
    GPIOE->BSRR  = (uint32_t)GPIO_PIN_13 << 16u; // LOW
}
static void _pe13_set_tim1_af(void) {
    // MODER[27:26] = 10 (AF), AFR[1][21:20] = 0001 (AF1 = TIM1)
    GPIOE->AFR[1] = (GPIOE->AFR[1] & ~(0xFu << 20u)) | (1u << 20u);
    GPIOE->MODER  = (GPIOE->MODER  & ~(3u << 26u)) | (2u << 26u);
}

// ============================================================
// Callback — вызывается на каждом overflow TIM1
// ============================================================
static void _step_overflow(void) {
    // Сброс STEP-импульса X (конус): LOW через один тик после HIGH
    if (s_cone_x_pulse) {
        GPIOE->BSRR  = (uint32_t)GPIO_PIN_13 << 16u; // PE13 LOW
        s_cone_x_pulse = 0;
    }

    // --- Ось Y ---
    if (s_ch1_active) {
        s_ax[AXIS_Y].pos += s_ax[AXIS_Y].dir;
        if (--s_ax[AXIS_Y].steps_left <= 0) {
            TIM1->CCER  &= ~TIM_CCER_CC1E;
            s_ch1_active             = 0;
            s_ax[AXIS_Y].moving      = 0;
            s_ax[AXIS_Y].steps_left  = 0;
            GPIOE->BSRR = GPIO_PIN_11; // EN_Y HIGH = выключить
            if (s_cone_mode) {
                s_ax[AXIS_X].moving = 0;
                GPIOE->BSRR = GPIO_PIN_15; // EN_X HIGH
                s_cone_mode = 0;
            }
        }

        // Bresenham: за каждый шаг Y → генерируем STEP X если нужно
        if (s_cone_mode) {
            if (++s_cone_cs_cnt > (int16_t)s_cone_cs_div) {
                // Физический STEP-импульс: PE13 HIGH (сбросится на след. тике)
                GPIOE->BSRR  = GPIO_PIN_13;
                s_cone_x_pulse = 1;
                s_ax[AXIS_X].pos += s_cone_dir_x;
                s_cone_cm_cnt += s_cone_cm_div;
                if (s_cone_cm_cnt > s_cone_cm_div) {
                    s_cone_cm_cnt -= 10000;
                    s_cone_cs_cnt  = 0;
                } else {
                    s_cone_cs_cnt  = 1;
                }
            }
        }
    }

    // --- Ось X (независимое движение, не конус) ---
    if (s_ch3_active) {
        s_ax[AXIS_X].pos += s_ax[AXIS_X].dir;
        if (--s_ax[AXIS_X].steps_left <= 0) {
            TIM1->CCER  &= ~TIM_CCER_CC3E;
            s_ch3_active             = 0;
            s_ax[AXIS_X].moving      = 0;
            s_ax[AXIS_X].steps_left  = 0;
            GPIOE->BSRR = GPIO_PIN_15; // EN_X HIGH = выключить
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

// ============================================================
// Непрерывное движение — steps_left = INT32_MAX
// ============================================================
void DRV_Stepper_SetContinuous(Axis_t axis, uint32_t speed_hz, int8_t dir) {
    if (speed_hz == 0) { DRV_Stepper_Stop(axis); return; }

    AxisState* ax = &s_ax[axis];
    ax->dir        = dir;
    ax->steps_left = 0x7FFFFFFF; // "бесконечно"
    ax->moving     = 1;

    if (axis == AXIS_Y) {
        digitalWrite(PIN_DIR_Y, (dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_11 << 16u; // EN LOW = включить
    } else {
        digitalWrite(PIN_DIR_X, (dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_15 << 16u;
    }

    if (!s_ch1_active && !s_ch3_active) {
        s_htim1->setOverflow(speed_hz, HERTZ_FORMAT);
        uint32_t period_us = 1000000UL / speed_hz;
        uint32_t duty = (period_us >= 20u) ? 1u : 5u;
        s_htim1->setCaptureCompare(1, duty, PERCENT_COMPARE_FORMAT);
        s_htim1->setCaptureCompare(3, duty, PERCENT_COMPARE_FORMAT);
    }

    if (axis == AXIS_Y) { s_ch1_active = 1; TIM1->CCER |= TIM_CCER_CC1E; }
    else                { s_ch3_active = 1; TIM1->CCER |= TIM_CCER_CC3E; }
}

// ============================================================
// Конус: включить Bresenham-слейв X к Y
// cs_div/cm_div из Cone_Info[Cone_Step], dir_x = +1 или -1
// PE13 переключается из TIM1_CH3 AF в GPIO OUTPUT;
// STEP-импульсы X генерируются вручную в ISR.
// ============================================================
void DRV_Stepper_SetConeRatio(uint8_t cs_div, int16_t cm_div, int8_t dir_x) {
    if (cs_div == 0) cs_div = 1;

    // DIR и EN для X
    digitalWrite(PIN_DIR_X, (dir_x > 0) ? HIGH : LOW);
    GPIOE->BSRR = (uint32_t)GPIO_PIN_15 << 16u; // EN_X LOW = включить

    // PE13 → GPIO OUTPUT (не TIM1_CH3 AF)
    TIM1->CCER &= ~TIM_CCER_CC3E; // выключить CH3 hardware output
    _pe13_set_gpio_output();

    s_cone_dir_x  = dir_x;
    s_cone_cs_div = cs_div;
    s_cone_cm_div = cm_div;
    s_cone_cs_cnt = 0;
    s_cone_cm_cnt = 0;
    s_cone_x_pulse = 0;
    s_ax[AXIS_X].dir    = dir_x;
    s_ax[AXIS_X].moving = 1;
    s_ch3_active  = 0;
    s_cone_mode   = 1;
}

void DRV_Stepper_ClearCone(void) {
    s_cone_mode = 0;
    GPIOE->BSRR = (uint32_t)GPIO_PIN_13 << 16u; // PE13 LOW
    _pe13_set_tim1_af();                          // восстановить TIM1_CH3 AF
    s_ax[AXIS_X].moving = 0;
    GPIOE->BSRR = GPIO_PIN_15;                   // EN_X HIGH = выключить
}

// ============================================================
// Движение ровно N шагов — для ручного энкодера
// ============================================================
void DRV_Stepper_MoveSteps(Axis_t axis, int32_t steps, uint32_t hz, int8_t dir) {
    if (steps <= 0 || hz == 0) return;

    AxisState* ax = &s_ax[axis];
    ax->dir        = dir;
    ax->steps_left = steps;
    ax->moving     = 1;

    if (axis == AXIS_Y) {
        digitalWrite(PIN_DIR_Y, (dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_11 << 16u;
    } else {
        digitalWrite(PIN_DIR_X, (dir > 0) ? HIGH : LOW);
        GPIOE->BSRR = (uint32_t)GPIO_PIN_15 << 16u;
    }

    if (!s_ch1_active && !s_ch3_active) {
        s_htim1->setOverflow(hz, HERTZ_FORMAT);
        uint32_t period_us = 1000000UL / hz;
        uint32_t duty = (period_us >= 20u) ? 1u : 5u;
        s_htim1->setCaptureCompare(1, duty, PERCENT_COMPARE_FORMAT);
        s_htim1->setCaptureCompare(3, duty, PERCENT_COMPARE_FORMAT);
    }

    if (axis == AXIS_Y) { s_ch1_active = 1; TIM1->CCER |= TIM_CCER_CC1E; }
    else                { s_ch3_active = 1; TIM1->CCER |= TIM_CCER_CC3E; }
}

uint8_t DRV_Stepper_IsContinuous(Axis_t axis) {
    return (s_ax[axis].steps_left == 0x7FFFFFFF) ? 1u : 0u;
}

// ============================================================
// Изменить частоту текущего движения (ISR-safe: только ARR)
// ============================================================
void DRV_Stepper_SetSpeed(Axis_t axis, uint32_t speed_hz) {
    (void)axis;
    if (speed_hz == 0) return;
    // Меняем ARR напрямую (обе оси на одном таймере, меняем сразу)
    uint32_t clk = s_htim1->getTimerClkFreq();
    uint32_t psc = TIM1->PSC + 1u;
    uint32_t arr = clk / psc / speed_hz;
    if (arr < 20u)     arr = 20u;
    if (arr > 0xFFFFu) arr = 0xFFFFu;
    TIM1->ARR = arr - 1u;
}
