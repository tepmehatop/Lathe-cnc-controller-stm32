/**
 * @file  els_control.cpp
 * @brief Замкнутый контур ELS — Этап 8
 *
 * Читает шпиндельный RPM и режим из els_state, вычисляет требуемую
 * скорость шагового двигателя и управляет drv_stepper.
 *
 * Режимы:
 *   MODE_FEED    — подача мм/об: step_hz = RPM × feed_mm × steps/mm / 60
 *   MODE_AFEED   — асинхронная подача мм/мин: step_hz = afeed_mm × steps/mm / 60
 *   MODE_THREAD  — нарезка резьбы: step_hz = RPM × pitch_mm × steps/mm / 60
 *   MODE_CONE    — конус (Y+X пропорционально) — TODO Этап 8.2
 *
 * Единицы ELS:
 *   feed        = мм/об × 100   (10 = 0.10 мм/об)
 *   afeed       = мм/мин × 100  (100 = 1.00 мм/мин)
 *   thread_pitch = 0.001 мм     (1000 = 1.000 мм)
 *   pos_y, pos_x = 0.001 мм
 *
 * Масштаб шагового: steps/mm
 *   Y: (MOTOR_Y_STEP_PER_REV × MICROSTEP_Y × 100) / SCREW_Y
 *   X: (MOTOR_X_STEP_PER_REV × MICROSTEP_X × 100) / SCREW_X
 */

#include "els_control.h"
#include "els_state.h"
#include "els_config.h"
#include "../Drivers/drv_stepper.h"
#include "../Drivers/drv_encoder.h"
#include "../Drivers/drv_display.h"
#include <Arduino.h>
#include <stdlib.h>

// ============================================================
// Константы масштабирования
// ============================================================
// steps/mm × 1000 для избежания float
// = (STEP_PER_REV × MICROSTEP × 100) / SCREW
#define STEPS_PER_MM_Y  \
    ((uint32_t)(MOTOR_Y_STEP_PER_REV) * MICROSTEP_Y * 100UL / SCREW_Y)
#define STEPS_PER_MM_X  \
    ((uint32_t)(MOTOR_X_STEP_PER_REV) * MICROSTEP_X * 100UL / SCREW_X)

// Минимальный RPM для начала движения
#define MIN_RPM_FOR_SYNC    10

// Гистерезис: пересчитывать speed_hz если изменилась более чем на N%
#define SPEED_HYSTERESIS_PCT 3

// ============================================================
// Внутреннее состояние
// ============================================================
static uint32_t s_last_speed_y = 0;
static uint32_t s_last_speed_x = 0;
static int8_t   s_last_dir_y   = 0;
static int8_t   s_last_dir_x   = 0;

// ============================================================
// Вспомогательные
// ============================================================

// Обновить скорость оси: запустить если не движется, сменить скорость если движется
static void _apply_speed(Axis_t axis, uint32_t speed_hz, int8_t dir,
                         uint32_t *last_speed, int8_t *last_dir) {
    if (speed_hz == 0) {
        if (DRV_Stepper_IsMoving(axis)) DRV_Stepper_Stop(axis);
        *last_speed = 0;
        return;
    }

    // Смена направления → перезапуск
    if (dir != *last_dir && DRV_Stepper_IsMoving(axis)) {
        DRV_Stepper_Stop(axis);
        *last_speed = 0;
    }

    if (!DRV_Stepper_IsMoving(axis)) {
        DRV_Stepper_SetContinuous(axis, speed_hz, dir);
        *last_speed = speed_hz;
        *last_dir   = dir;
        return;
    }

    // Уже движемся: обновляем скорость если изменилась больше гистерезиса
    if (*last_speed > 0) {
        uint32_t delta = (speed_hz > *last_speed) ?
                          speed_hz - *last_speed : *last_speed - speed_hz;
        uint32_t thresh = *last_speed * SPEED_HYSTERESIS_PCT / 100u;
        if (delta > thresh) {
            DRV_Stepper_SetSpeed(axis, speed_hz);
            *last_speed = speed_hz;
        }
    }
}

// Проверить лимиты: если позиция вышла за лимит → Stop
static void _check_limits(void) {
    if (!els.limits_enabled) return;

    if (els.pos_y <= els.limit_y_left  && DRV_Stepper_IsMoving(AXIS_Y) && s_last_dir_y < 0) {
        ELS_Control_Stop();
    }
    if (els.pos_y >= els.limit_y_right && DRV_Stepper_IsMoving(AXIS_Y) && s_last_dir_y > 0) {
        ELS_Control_Stop();
    }
    if (els.pos_x <= els.limit_x_front && DRV_Stepper_IsMoving(AXIS_X) && s_last_dir_x < 0) {
        ELS_Control_Stop();
    }
    if (els.pos_x >= els.limit_x_rear  && DRV_Stepper_IsMoving(AXIS_X) && s_last_dir_x > 0) {
        ELS_Control_Stop();
    }
}

// ============================================================
// Вычисление step_hz по режиму
// ============================================================

// MODE_FEED / MODE_THREAD: step_hz = RPM × pitch_mm × steps/mm / 60
// feed в мм/об × 100 → pitch_mm = feed / 100.0
// thread_pitch в 0.001мм → pitch_mm = thread_pitch / 1000.0
// Используем integer math: × 1000 / 1000
static uint32_t _calc_sync_hz(int32_t rpm, int32_t pitch_001mm, uint32_t steps_per_mm) {
    if (rpm <= 0 || pitch_001mm <= 0) return 0;
    // step_hz = rpm × pitch_mm × steps_per_mm / 60
    //         = rpm × pitch_001mm × steps_per_mm / (1000 × 60)
    // Используем int64 чтобы не переполниться
    int64_t hz = (int64_t)rpm * (int64_t)pitch_001mm * (int64_t)steps_per_mm / 60000LL;
    if (hz < 1)      return 0;
    if (hz > 50000)  return 50000; // Аппаратный предел: 50 кГц
    return (uint32_t)hz;
}

// MODE_AFEED: step_hz = afeed_mm_per_min × steps_per_mm / 60
// afeed в мм/мин × 100
static uint32_t _calc_afeed_hz(int32_t afeed_100, uint32_t steps_per_mm) {
    if (afeed_100 <= 0) return 0;
    // step_hz = (afeed_100 / 100) × steps_per_mm / 60
    //         = afeed_100 × steps_per_mm / 6000
    int64_t hz = (int64_t)afeed_100 * (int64_t)steps_per_mm / 6000LL;
    if (hz < 1)      return 0;
    if (hz > 50000)  return 50000;
    return (uint32_t)hz;
}

// ============================================================
// Публичный API
// ============================================================
void ELS_Control_Init(void) {
    s_last_speed_y = 0;
    s_last_speed_x = 0;
    s_last_dir_y   = 0;
    s_last_dir_x   = 0;
}

void ELS_Control_Start(void) {
    els.running = 1;
}

void ELS_Control_Stop(void) {
    els.running = 0;
    DRV_Stepper_StopAll();
    s_last_speed_y = 0;
    s_last_speed_x = 0;
}

void ELS_Control_Update(void) {
    // Обновляем RPM из энкодера шпинделя
    int32_t rpm = DRV_Encoder_Spindle_GetRPM();
    if (rpm < 0) rpm = -rpm;  // Используем абсолютное значение RPM
    els.spindle_rpm = rpm;

    // Проверить лимиты
    _check_limits();

    if (!els.running) {
        // Остановить моторы если были активны
        if (DRV_Stepper_IsMoving(AXIS_Y) || DRV_Stepper_IsMoving(AXIS_X)) {
            DRV_Stepper_StopAll();
            s_last_speed_y = 0;
            s_last_speed_x = 0;
        }
        return;
    }

    // Направление движения: +1 = к задней бабке (стандарт), -1 = обратно
    // Управляется переключателем submode или джойстиком (TODO: джойстик)
    int8_t dir_y = (els.submode == SUBMODE_EXTERNAL) ? +1 : -1;
    int8_t dir_x = +1;

    uint32_t hz_y = 0;
    uint32_t hz_x = 0;

    switch (els.mode) {

    case MODE_FEED:
        // Синхронная подача мм/об (ось Y)
        if (rpm < MIN_RPM_FOR_SYNC) {
            hz_y = 0;
        } else {
            // feed = мм/об × 100 → pitch_001mm = feed × 10
            hz_y = _calc_sync_hz(rpm, els.feed * 10L, STEPS_PER_MM_Y);
        }
        _apply_speed(AXIS_Y, hz_y, dir_y, &s_last_speed_y, &s_last_dir_y);
        break;

    case MODE_AFEED:
        // Асинхронная подача мм/мин (ось Y), не зависит от шпинделя
        hz_y = _calc_afeed_hz(els.afeed, STEPS_PER_MM_Y);
        _apply_speed(AXIS_Y, hz_y, dir_y, &s_last_speed_y, &s_last_dir_y);
        break;

    case MODE_THREAD:
        // Нарезка резьбы (ось Y)
        if (rpm < MIN_RPM_FOR_SYNC) {
            hz_y = 0;
        } else {
            hz_y = _calc_sync_hz(rpm, els.thread_pitch, STEPS_PER_MM_Y);
        }
        _apply_speed(AXIS_Y, hz_y, dir_y, &s_last_speed_y, &s_last_dir_y);
        break;

    case MODE_CONE_L:
    case MODE_CONE_R:
        // Конус: Y + X одновременно
        // TODO Этап 12: правильный расчёт угла конуса из Cone_Info[els.Cone_Step]
        if (rpm >= MIN_RPM_FOR_SYNC) {
            hz_y = _calc_sync_hz(rpm, els.feed * 10L, STEPS_PER_MM_Y);
            hz_x = hz_y / 2; // TODO: реальное соотношение Cs_Div/Cm_Div
        }
        _apply_speed(AXIS_Y, hz_y, dir_y, &s_last_speed_y, &s_last_dir_y);
        _apply_speed(AXIS_X, hz_x, dir_x, &s_last_speed_x, &s_last_dir_x);
        break;

    case MODE_DIVIDER:
    case MODE_SPHERE:
    case MODE_RESERVE:
    default:
        // Нет движения мотора
        break;
    }

    // Отправить RPM на дисплей при изменении
    static int32_t s_last_rpm = -1;
    if (rpm != s_last_rpm) {
        s_last_rpm = rpm;
#if USE_ESP32_DISPLAY
        DRV_Display_SendRpm(rpm);
#endif
    }
}
