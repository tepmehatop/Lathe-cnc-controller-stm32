#pragma once
#include <stdint.h>

void DRV_Display_Init(void);
void DRV_Display_SendPosition(int32_t x, int32_t y);
void DRV_Display_SendMode(uint8_t mode, uint8_t submode);
void DRV_Display_SendFeed(int32_t feed);
void DRV_Display_SendRpm(int32_t rpm);
void DRV_Display_SendOtskok(int32_t otskok_y);
void DRV_Display_SendTension(int32_t tension_y);
void DRV_Display_Process(void);
