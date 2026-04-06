#pragma once
#include <stdint.h>

void DRV_Beeper_Init(void);
void DRV_Beeper_Tone(uint32_t freq_hz, uint32_t duration_ms);
void DRV_Beeper_Off(void);
