#pragma once
#include <stdint.h>

void    DRV_DRO_Init(void);
void    DRV_DRO_Process(void);
int32_t DRV_DRO_GetX(void);
int32_t DRV_DRO_GetY(void);
uint8_t DRV_DRO_GetBtn(void);
uint8_t DRV_DRO_IsNewPacket(void);
