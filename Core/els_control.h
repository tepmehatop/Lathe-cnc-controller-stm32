#pragma once
#include <stdint.h>

// ============================================================
// Модуль управления движением ELS — Этап 8
//
// ELS_Control_Update() вызывать из loop().
// Читает els.mode, els.running, spindle_rpm →
// вычисляет step_hz → управляет drv_stepper.
// ============================================================

void ELS_Control_Init(void);
void ELS_Control_Update(void);

// Запустить / остановить движение (устанавливает els.running)
void ELS_Control_Start(void);
void ELS_Control_Stop(void);
