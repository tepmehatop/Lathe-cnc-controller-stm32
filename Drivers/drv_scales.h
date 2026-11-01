#pragma once
/**
 * @file  drv_scales.h
 * @brief Цифровые линейки, подключённые к STM32 напрямую (без блока DRO).
 *
 * Включается при ELS_POS_SOURCE == POS_SOURCE_SCALES. Тип линейки и параметры
 * каждой оси — Core/els_config.h (SCALE_Z_* / SCALE_X_*), подробности —
 * docs/scales/README.md.
 */
#include <stdint.h>

enum ScaleAxis { SCALE_AXIS_Z = 0, SCALE_AXIS_X = 1 };

void     DRV_Scales_Init(void);
void     DRV_Scales_Process(void);              // вызывать из loop()

/** Есть ли на оси линейка (тип != SCALE_NONE) */
uint8_t  DRV_Scales_Present(ScaleAxis axis);
/** Позиция, 0.001 мм, со знаком по SCALE_*_INVERT */
int32_t  DRV_Scales_GetUm(ScaleAxis axis);
/** 1 = данные свежие (квадратура — всегда; протоколы — кадр не старше 1 с) */
uint8_t  DRV_Scales_IsOk(ScaleAxis axis);
/** Диагностика: принятые и отбракованные кадры (для квадратуры — 0) */
uint32_t DRV_Scales_GetFrames(ScaleAxis axis);
uint32_t DRV_Scales_GetErrors(ScaleAxis axis);
/** Название типа линейки оси (для отладочного вывода) */
const char* DRV_Scales_TypeName(ScaleAxis axis);
