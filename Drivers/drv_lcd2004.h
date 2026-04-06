#pragma once
#include <stdint.h>

void DRV_LCD2004_Init(void);
void DRV_LCD2004_Clear(void);
void DRV_LCD2004_Print(uint8_t row, uint8_t col, const char* str);
void DRV_LCD2004_UpdatePosition(int32_t x, int32_t y);
