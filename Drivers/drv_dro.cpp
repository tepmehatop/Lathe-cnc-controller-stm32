/**
 * @file  drv_dro.cpp
 * @brief Драйвер DRO HS800-2 — ЗАГЛУШКА (Этап 2)
 *
 * Протокол: UART 57600/8N1, пакет 29 байт
 *   SOF  = 0xFE (byte[0])
 *   EOF  = 0xEF (byte[28])
 *   TYPE = 0x33 (byte[2])
 *   X    = int32 LE bytes[6..9]   (0.001 мм)
 *   Y    = int32 LE bytes[10..13] (0.001 мм)
 *   BTN  = byte[18], byte[19]
 *   CRC  = CRC16/MODBUS bytes[1..25] (bytes[26..27])
 *
 * TODO (Этап 2): реализовать USART2 + DMA приём, парсер пакетов, CRC-проверку.
 */

#include "drv_dro.h"
#include "../Core/els_config.h"

#if USE_DRO_HS800

static int32_t s_pos_x = 0;
static int32_t s_pos_y = 0;
static uint8_t s_btn   = 0;
static uint8_t s_new   = 0;

void DRV_DRO_Init(void) {
    // TODO: Инициализация USART2 + DMA RX (57600/8N1)
    // Пин: PA3 (USART2_RX)
}

void DRV_DRO_Process(void) {
    // TODO: Обработка DMA-буфера, поиск SOF/EOF, проверка CRC, парсинг X/Y
    s_new = 0;
}

int32_t DRV_DRO_GetX(void) { return s_pos_x; }
int32_t DRV_DRO_GetY(void) { return s_pos_y; }
uint8_t DRV_DRO_GetBtn(void) { return s_btn; }
uint8_t DRV_DRO_IsNewPacket(void) { return s_new; }

#else
// Stub когда USE_DRO_HS800=0
void    DRV_DRO_Init(void) {}
void    DRV_DRO_Process(void) {}
int32_t DRV_DRO_GetX(void) { return 0; }
int32_t DRV_DRO_GetY(void) { return 0; }
uint8_t DRV_DRO_GetBtn(void) { return 0; }
uint8_t DRV_DRO_IsNewPacket(void) { return 0; }
#endif
