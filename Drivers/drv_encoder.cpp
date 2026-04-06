/**
 * @file  drv_encoder.cpp
 * @brief Драйверы энкодеров — Этап 6
 *
 * Энкодер шпинделя: TIM5 в режиме квадратурного счётчика (×4)
 *   PA0 = TIM5_CH1 (AF2), PA1 = TIM5_CH2 (AF2)
 *   ENC_LINE_PER_REV = 1800 линий → 7200 импульсов/об
 *   RPM вычисляется по дельте счётчика за 100мс
 *
 * Ручной энкодер (hand wheel): прерывание на PC2 (ChA), PC3 (ChB)
 *   100 линий/об, внешняя подтяжка 4.7кОм
 */

#include "drv_encoder.h"
#include "../Core/els_config.h"
#include <Arduino.h>

// ============================================================
// Шпиндель — TIM5 encoder mode
// ============================================================
#define SPINDLE_PPR     (ENC_LINE_PER_REV * 4)   // импульсов/об (×4 квадратура)

static volatile int32_t s_spindle_count = 0;
static int32_t          s_rpm           = 0;

// Для расчёта RPM
static uint32_t s_rpm_last_ms    = 0;
static uint32_t s_rpm_last_cnt   = 0;
#define RPM_INTERVAL_MS  100   // Период пересчёта RPM

void DRV_Encoder_Spindle_Init(void) {
    // --- GPIO: PA0, PA1 → AF2 (TIM5) ---
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF2_TIM5;
    HAL_GPIO_Init(GPIOA, &gpio);

    // --- TIM5: encoder mode (TI1+TI2, ×4) ---
    __HAL_RCC_TIM5_CLK_ENABLE();

    TIM5->CR1   = 0;                                      // Выкл, сбрасываем
    TIM5->SMCR  = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;       // SMS=011 Encoder mode 3 (TI1+TI2)
    TIM5->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // IC1→TI1, IC2→TI2
    TIM5->CCER  = 0;                                      // нет инверсии
    TIM5->PSC   = 0;
    TIM5->ARR   = 0xFFFFFFFFu;                            // 32-bit максимум
    TIM5->CNT   = 0;
    TIM5->EGR   = TIM_EGR_UG;                            // Сбросить флаги
    TIM5->CR1   = TIM_CR1_CEN;                            // Включить

    s_rpm_last_ms  = millis();
    s_rpm_last_cnt = 0;
}

void DRV_Encoder_Spindle_Update(void) {
    // Читаем 32-bit CNT, приводим к знаковому
    s_spindle_count = (int32_t)TIM5->CNT;

    uint32_t now = millis();
    uint32_t dt  = now - s_rpm_last_ms;
    if (dt >= RPM_INTERVAL_MS) {
        int32_t  cnt   = s_spindle_count;
        int32_t  delta = cnt - (int32_t)s_rpm_last_cnt;
        // RPM = (|delta| / SPINDLE_PPR) / (dt_ms / 60000)
        //     = |delta| * 60000 / (SPINDLE_PPR * dt_ms)
        if (delta < 0) delta = -delta;
        s_rpm = (int32_t)((int64_t)delta * 60000L / ((int64_t)SPINDLE_PPR * (int64_t)dt));

        s_rpm_last_cnt = (uint32_t)cnt;
        s_rpm_last_ms  = now;
    }
}

int32_t DRV_Encoder_Spindle_GetCount(void) {
    return s_spindle_count;
}

int32_t DRV_Encoder_Spindle_GetRPM(void) {
    return s_rpm;
}

// ============================================================
// Ручной энкодер — EXTI на PC2 (ChA)
// ============================================================
static volatile int32_t s_hand_delta = 0;

static void _hand_enc_isr(void) {
    // Читаем ChB (PC3) для определения направления
    if (digitalRead(PC3) == HIGH) {
        s_hand_delta++;
    } else {
        s_hand_delta--;
    }
}

void DRV_Encoder_Hand_Init(void) {
    // PC2 = ChA, PC3 = ChB: внешняя подтяжка 4.7кОм
    pinMode(PC2, INPUT);
    pinMode(PC3, INPUT);
    // Прерывание на фронт ChA
    attachInterrupt(digitalPinToInterrupt(PC2), _hand_enc_isr, RISING);
}

int32_t DRV_Encoder_Hand_GetDelta(void) {
    // Атомарное чтение и сброс
    int32_t d = s_hand_delta;
    s_hand_delta = 0;
    return d;
}
