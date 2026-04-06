#pragma once
#include <stdint.h>

// LCD2004 через I2C1 (PCF8574, addr 0x27)
// PB6 = SCL, PB7 = SDA, 400кГц
//
// Кастомные символы (совместимо с Arduino Print.ino):
//   \1 = стрелка влево
//   \2 = стрелка вправо
//   \3 = стрелка вверх
//   \4 = стрелка вниз
//   \5 = знак градуса
//   \6 = знак диаметра

void DRV_LCD2004_Init(void);
void DRV_LCD2004_Clear(void);
void DRV_LCD2004_SetCursor(uint8_t col, uint8_t row);
void DRV_LCD2004_Print(uint8_t row, uint8_t col, const char* str);
void DRV_LCD2004_PrintRow(uint8_t row, const char* str20); // Ровно 20 символов, выравнивание
void DRV_LCD2004_UpdatePosition(int32_t pos_y, int32_t pos_x); // 0.001 мм
void DRV_LCD2004_Backlight(uint8_t on);
