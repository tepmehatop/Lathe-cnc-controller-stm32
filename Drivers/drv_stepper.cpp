/**
 * @file  drv_stepper.cpp
 * @brief Драйвер шаговых двигателей — ЗАГЛУШКА (Этап 7)
 *
 * Мотор Y: STEP=PE9 (TIM1_CH1), DIR=PE10, EN=PE11
 * Мотор X: STEP=PE13 (TIM1_CH3), DIR=PE14, EN=PE15
 *
 * Генерация STEP-импульсов: TIM1 в режиме PWM, DMA для burst-последовательностей
 * Минимальный STEP pulse: 1 мкс (согласно datasheet A4988/DRV8825)
 *
 * TODO (Этап 7): TIM1 PWM + DMA, расчёт частоты из feed/spindle_rpm
 */

#include "drv_stepper.h"
#include "../Core/els_config.h"

static int32_t s_pos[2] = {0, 0};
static uint8_t s_moving[2] = {0, 0};

void DRV_Stepper_Init(void) {
    // TODO: GPIO PE9-PE15 init
    // TODO: TIM1 channel 1 (PE9) и channel 3 (PE13) в PWM mode
    // TODO: EN пины (PE11, PE15) → HIGH (отключены по умолчанию)
}

void DRV_Stepper_MoveTo(Axis_t axis, int32_t target_pos, uint32_t speed_hz) {
    // TODO: Рассчитать кол-во шагов, направление, запустить TIM1
    (void)axis; (void)target_pos; (void)speed_hz;
}

void DRV_Stepper_Stop(Axis_t axis) {
    s_moving[axis] = 0;
    // TODO: Остановить TIM1 соответствующего канала
}

void DRV_Stepper_StopAll(void) {
    DRV_Stepper_Stop(AXIS_Y);
    DRV_Stepper_Stop(AXIS_X);
}

void DRV_Stepper_Enable(Axis_t axis, uint8_t en) {
    // TODO: Управлять EN пином (PE11 для Y, PE15 для X)
    // Active LOW: en=1 → LOW, en=0 → HIGH
    (void)axis; (void)en;
}

int32_t DRV_Stepper_GetPos(Axis_t axis) {
    return s_pos[axis];
}

uint8_t DRV_Stepper_IsMoving(Axis_t axis) {
    return s_moving[axis];
}
