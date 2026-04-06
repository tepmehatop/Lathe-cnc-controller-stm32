/**
 * @file  drv_display.cpp
 * @brief Драйвер ESP32-S3 JC4827W543 — ЗАГЛУШКА (Этап 3)
 *
 * Протокол: UART8 115200/8N1 с DMA
 * Пины: PE0 (TX), PE1 (RX)
 * Формат пакетов: текстовый, как в Arduino-версии
 *   <POS_X:value> <POS_Y:value> <RPM:value> <MODE:m,sm> ...
 *
 * TODO (Этап 3): реализовать UART8 + DMA TX/RX, формировать и отправлять пакеты.
 */

#include "drv_display.h"
#include "../Core/els_config.h"

#if USE_ESP32_DISPLAY

void DRV_Display_Init(void) {
    // TODO: Инициализация UART8 + DMA (115200/8N1)
    // TX: PE0 (UART8_TX), RX: PE1 (UART8_RX)
}

void DRV_Display_SendPosition(int32_t x, int32_t y) {
    // TODO: Сформировать и отправить <POS_X:x><POS_Y:y>
    (void)x; (void)y;
}

void DRV_Display_SendMode(uint8_t mode, uint8_t submode) {
    // TODO
    (void)mode; (void)submode;
}

void DRV_Display_SendFeed(int32_t feed) {
    // TODO
    (void)feed;
}

void DRV_Display_SendRpm(int32_t rpm) {
    // TODO
    (void)rpm;
}

void DRV_Display_SendOtskok(int32_t otskok_y) {
    // TODO
    (void)otskok_y;
}

void DRV_Display_SendTension(int32_t tension_y) {
    // TODO
    (void)tension_y;
}

void DRV_Display_Process(void) {
    // TODO: Обработка входящих команд от ESP32 (нажатия на дисплее)
}

#else
void DRV_Display_Init(void) {}
void DRV_Display_SendPosition(int32_t x, int32_t y) { (void)x;(void)y; }
void DRV_Display_SendMode(uint8_t m, uint8_t sm) { (void)m;(void)sm; }
void DRV_Display_SendFeed(int32_t f) { (void)f; }
void DRV_Display_SendRpm(int32_t r) { (void)r; }
void DRV_Display_SendOtskok(int32_t o) { (void)o; }
void DRV_Display_SendTension(int32_t t) { (void)t; }
void DRV_Display_Process(void) {}
#endif
