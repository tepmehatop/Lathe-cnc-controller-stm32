/**
 * @file  drv_encoder.cpp
 * @brief Драйверы энкодеров — ЗАГЛУШКИ (Этапы 5, 6)
 *
 * Энкодер шпинделя: TIM5 в режиме счётчика, PA0=CH1, PA1=CH2 (FT пины)
 *   - 1800 линий/об → 7200 импульсов/об (×4)
 *   - RPM вычисляется по периоду между обрывами
 * Ручной энкодер: EXTI на PC2 (ChA), PC3 (ChB)
 *   - 100 линий/об → 400 импульсов/об
 *
 * TODO (Этап 6): TIM5 encoder mode, TIM для RPM измерения
 * TODO (Этап 5): EXTI2/EXTI3 для ручного энкодера
 */

#include "drv_encoder.h"
#include "../Core/els_config.h"

static volatile int32_t s_spindle_count = 0;
static volatile int32_t s_hand_delta    = 0;

// ---- Шпиндель ----
void DRV_Encoder_Spindle_Init(void) {
    // TODO: TIM5 encoder mode (TIM_ENCODERMODE_TI12)
    // PA0 = TIM5_CH1 (AF2), PA1 = TIM5_CH2 (AF2)
    // Предел счётчика: 0xFFFFFFFF (32-bit TIM5)
}

void DRV_Encoder_Spindle_Update(void) {
    // TODO: Читать TIM5->CNT, вычислять RPM по delta/time
}

int32_t DRV_Encoder_Spindle_GetCount(void) {
    return s_spindle_count;
}

int32_t DRV_Encoder_Spindle_GetRPM(void) {
    return 0; // TODO
}

// ---- Ручной энкодер ----
void DRV_Encoder_Hand_Init(void) {
    // TODO: GPIO PC2, PC3 с EXTI2, EXTI3
    // Pullup 4.7кОм внешний (или внутренний PULLUP STM32)
}

int32_t DRV_Encoder_Hand_GetDelta(void) {
    int32_t d = s_hand_delta;
    s_hand_delta = 0;
    return d;
}
