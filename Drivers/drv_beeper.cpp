/**
 * @file  drv_beeper.cpp
 * @brief Драйвер зуммера — ЗАГЛУШКА
 *
 * Пин: PD12 (TIM4_CH1 PWM)
 * ВАЖНО: PD12 на STM32F4DISCOVERY занят зелёным LED (LD4).
 *        Необходимо снять перемычку LD4 на плате.
 *
 * TODO: TIM4_CH1 PWM, вычисление ARR/PSC из freq_hz
 */

#include "drv_beeper.h"
#include "../Core/els_config.h"

#if USE_BEEPER

void DRV_Beeper_Init(void) {
    // TODO: GPIO PD12 как AF2 (TIM4_CH1)
    // TODO: TIM4 init: PSC=167 (1МГц тик), ARR=freq_hz/1000000
}

void DRV_Beeper_Tone(uint32_t freq_hz, uint32_t duration_ms) {
    // TODO: ARR = 1000000/freq_hz, CCR1 = ARR/2 (50% duty), запустить TIM4
    // TODO: Таймер остановки через duration_ms (можно SysTick или второй таймер)
    (void)freq_hz; (void)duration_ms;
}

void DRV_Beeper_Off(void) {
    // TODO: Остановить TIM4, PD12 → LOW
}

#else
void DRV_Beeper_Init(void) {}
void DRV_Beeper_Tone(uint32_t f, uint32_t d) { (void)f;(void)d; }
void DRV_Beeper_Off(void) {}
#endif
