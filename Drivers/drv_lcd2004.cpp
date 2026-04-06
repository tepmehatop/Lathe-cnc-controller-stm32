/**
 * @file  drv_lcd2004.cpp
 * @brief Драйвер LCD2004 через I2C (PCF8574) — ЗАГЛУШКА (Этап 4)
 *
 * Подключение: I2C1, PB6=SCL, PB7=SDA, addr=0x27, 400кГц
 * TODO (Этап 4): реализовать I2C1 HAL/LL, вывод символов, кастомные символы.
 */

#include "drv_lcd2004.h"
#include "../Core/els_config.h"

#if USE_LCD2004

void DRV_LCD2004_Init(void) {
    // TODO: Инициализация I2C1 (PB6=SCL, PB7=SDA, 400кГц)
    // TODO: Инициализация LCD (8-bit через PCF8574, 4-bit mode)
}

void DRV_LCD2004_Clear(void) {
    // TODO
}

void DRV_LCD2004_Print(uint8_t row, uint8_t col, const char* str) {
    // TODO: Установить курсор row/col, вывести строку
    (void)row; (void)col; (void)str;
}

void DRV_LCD2004_UpdatePosition(int32_t x, int32_t y) {
    // TODO: Форматировать и вывести позиции на строках 0 и 1
    (void)x; (void)y;
}

#else
void DRV_LCD2004_Init(void) {}
void DRV_LCD2004_Clear(void) {}
void DRV_LCD2004_Print(uint8_t r, uint8_t c, const char* s) { (void)r;(void)c;(void)s; }
void DRV_LCD2004_UpdatePosition(int32_t x, int32_t y) { (void)x;(void)y; }
#endif
