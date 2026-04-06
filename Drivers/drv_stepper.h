#pragma once
#include <stdint.h>

// Оси
typedef enum {
    AXIS_Y = 0,  // Продольная (каретка)
    AXIS_X = 1,  // Поперечная (суппорт)
} Axis_t;

void DRV_Stepper_Init(void);

// Запустить движение к target_pos (0.001 мм) с заданной скоростью (шаги/сек)
void DRV_Stepper_MoveTo(Axis_t axis, int32_t target_pos, uint32_t speed_hz);

// Остановить немедленно
void DRV_Stepper_Stop(Axis_t axis);

// Остановить оба
void DRV_Stepper_StopAll(void);

// Включить/выключить драйвер (EN пин)
void DRV_Stepper_Enable(Axis_t axis, uint8_t en);

// Текущая позиция в шагах
int32_t DRV_Stepper_GetPos(Axis_t axis);

// В движении?
uint8_t DRV_Stepper_IsMoving(Axis_t axis);

// Вызывать из loop() — обслуживает шаговые импульсы
void DRV_Stepper_Update(void);
