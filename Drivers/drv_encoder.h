#pragma once
#include <stdint.h>

// ---- Энкодер шпинделя (TIM5, PA0/PA1) ----
void    DRV_Encoder_Spindle_Init(void);
void    DRV_Encoder_Spindle_Update(void);
int32_t DRV_Encoder_Spindle_GetCount(void);
int32_t DRV_Encoder_Spindle_GetRPM(void);

// ---- Ручной энкодер (EXTI, PC2/PC3) ----
void    DRV_Encoder_Hand_Init(void);
int32_t DRV_Encoder_Hand_GetDelta(void);  // +/- шаги с последнего вызова
