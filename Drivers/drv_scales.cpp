/**
 * @file  drv_scales.cpp
 * @brief Цифровые линейки напрямую к STM32 — см. drv_scales.h
 *
 * Квадратура (стекло/магнит): аппаратный энкодер таймера —
 *   ось Z: TIM2 (32 бит), PA15 = A (AF1), PB3 = B (AF1)
 *   ось X: TIM3 (16 бит, расширяется программно до 64), PB4 = A (AF2), PB5 = B (AF2)
 *
 * Протоколы с тактом от линейки (Sylvac 48, BIN6, Digimatic): прерывание по
 * фронту такта (пин A), бит данных — с пина B. Без инверсии в схеме сопряжения
 * данные читаются на спаде такта; с инверсией — на фронте, данные инвертируются.
 * Кадр начинается после паузы такта > SYNC_GAP_US (Digimatic — по запросу REQ).
 *
 * iGaging 21 бит: такт выдаёт STM32 (пин A — выход) из прерывания TIM11,
 * главный цикл не блокируется. Опрос — каждые IGAGING_PERIOD_MS.
 */
#include "drv_scales.h"
#include "scale_protocols.h"
#include "../Core/els_config.h"
#include <Arduino.h>

#if ELS_POS_SOURCE == POS_SOURCE_SCALES

static const uint32_t SYNC_GAP_US        = 2000;  // пауза такта = начало нового кадра
static const uint32_t STALE_MS           = 1000;  // нет кадров дольше — ось «нет связи»
static const uint32_t DIGIMATIC_PERIOD_MS = 50;   // как часто запрашивать Digimatic
static const uint32_t DIGIMATIC_TIMEOUT_MS = 200;
static const uint32_t IGAGING_PERIOD_MS  = 20;    // 50 Гц, как у штатного дисплея iGaging
static const uint32_t IGAGING_TICK_HZ    = 50000; // шаг автомата такта iGaging = 20 мкс

struct ScaleAxisState {
    uint8_t  type;
    int32_t  nm_per_count;
    uint8_t  invert;
    uint8_t  sig_inv;
    uint32_t pin_a, pin_b, pin_req;
    PinName  pn_a, pn_b;
    uint8_t  frame_bits;

    // Сборка кадра (прерывание)
    volatile uint64_t shift;
    volatile uint8_t  nbits;
    volatile uint32_t last_edge_us;
    volatile uint64_t frame;
    volatile uint8_t  frame_ready;

    // Результат
    int32_t  um;
    uint32_t frames_ok, frames_err, last_ok_ms;

    // Квадратура TIM3 (16 бит → 64)
    int64_t  quad_count;
    uint16_t last_cnt16;

    // Digimatic: запрос REQ
    uint8_t  req_active;
    uint32_t req_t;
};

static ScaleAxisState s_ax[2];

// ============================================================
// Квадратура
// ============================================================
static void quad_gpio(GPIO_TypeDef* port, uint16_t pin, uint8_t af) {
    GPIO_InitTypeDef g = {};
    g.Pin       = pin;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;              // открытый коллектор у части линеек
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}

static void quad_timer_init(TIM_TypeDef* tim, uint32_t arr) {
    const uint32_t f = (SCALE_QUAD_FILTER & 0xF);
    tim->CR1   = 0;
    tim->SMCR  = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;                // Encoder mode 3 (×4)
    tim->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0             // IC1→TI1, IC2→TI2
               | (f << TIM_CCMR1_IC1F_Pos) | (f << TIM_CCMR1_IC2F_Pos);
    tim->CCER  = 0;
    tim->PSC   = 0;
    tim->ARR   = arr;
    tim->CNT   = 0;
    tim->EGR   = TIM_EGR_UG;
    tim->CR1   = TIM_CR1_CEN;
}

static void quad_init(ScaleAxis axis) {
    if (axis == SCALE_AXIS_Z) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        quad_gpio(GPIOA, GPIO_PIN_15, GPIO_AF1_TIM2);   // PA15 = TIM2_CH1
        quad_gpio(GPIOB, GPIO_PIN_3,  GPIO_AF1_TIM2);   // PB3  = TIM2_CH2
        __HAL_RCC_TIM2_CLK_ENABLE();
        quad_timer_init(TIM2, 0xFFFFFFFFu);
    } else {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        quad_gpio(GPIOB, GPIO_PIN_4, GPIO_AF2_TIM3);    // PB4 = TIM3_CH1
        quad_gpio(GPIOB, GPIO_PIN_5, GPIO_AF2_TIM3);    // PB5 = TIM3_CH2
        __HAL_RCC_TIM3_CLK_ENABLE();
        quad_timer_init(TIM3, 0xFFFFu);
        s_ax[axis].last_cnt16 = 0;
    }
}

static int64_t quad_read(ScaleAxis axis) {
    ScaleAxisState& a = s_ax[axis];
    if (axis == SCALE_AXIS_Z) return (int64_t)(int32_t)TIM2->CNT;
    uint16_t cnt = (uint16_t)TIM3->CNT;
    a.quad_count += (int16_t)(cnt - a.last_cnt16);   // ход между опросами < 32768 имп
    a.last_cnt16 = cnt;
    return a.quad_count;
}

// ============================================================
// Протоколы с тактом от линейки (прерывание по такту)
// ============================================================
static inline void clock_edge(ScaleAxisState& a) {
    uint32_t now = micros();
    if (a.type != SCALE_DIGIMATIC && now - a.last_edge_us > SYNC_GAP_US) {
        a.nbits = 0;
        a.shift = 0;
    }
    a.last_edge_us = now;
    uint32_t bit = (uint32_t)digitalReadFast(a.pn_b) ^ a.sig_inv;
    if (a.nbits < 64) a.shift |= (uint64_t)bit << a.nbits;
    if (++a.nbits == a.frame_bits) {
        a.frame       = a.shift;
        a.frame_ready = 1;
    }
}
static void isr_clock_z() { clock_edge(s_ax[SCALE_AXIS_Z]); }
static void isr_clock_x() { clock_edge(s_ax[SCALE_AXIS_X]); }

// ============================================================
// iGaging: такт от STM32, автомат в прерывании TIM11.
// Такт в покое низкий; бит: 1 тик высокий, 4 тика низкий (≈ 100 мкс на бит);
// данные читаются в момент спада такта. Таймер работает только на время
// кадра (~2 мс из каждых IGAGING_PERIOD_MS), запуск — из loop().
// ============================================================
static HardwareTimer*   s_igaging_tim = nullptr;
static volatile uint8_t s_ig_phase[2];    // 0 = кадр не идёт, 1.. = шаг автомата
static uint32_t         s_ig_last_ms = 0;

static void igaging_tick_axis(ScaleAxisState& a, uint8_t i) {
    if (s_ig_phase[i] == 0) return;
    uint8_t step = (uint8_t)((s_ig_phase[i] - 1) % 5);   // 0 = высокий, 1 = спад, 2..4 = низкий
    if (step == 0) {
        digitalWriteFast(a.pn_a, a.sig_inv ? LOW : HIGH);
    } else if (step == 1) {
        uint32_t bit = (uint32_t)digitalReadFast(a.pn_b) ^ a.sig_inv;
        digitalWriteFast(a.pn_a, a.sig_inv ? HIGH : LOW);
        a.shift |= (uint64_t)bit << a.nbits;
        if (++a.nbits == SCALE_IGAGING21_BITS) {
            a.frame       = a.shift;
            a.frame_ready = 1;
            s_ig_phase[i] = 0;
            return;
        }
    }
    s_ig_phase[i]++;
}

static void igaging_tick() {
    uint8_t busy = 0;
    for (uint8_t i = 0; i < 2; i++) {
        if (s_ax[i].type != SCALE_IGAGING21) continue;
        igaging_tick_axis(s_ax[i], i);
        busy |= s_ig_phase[i];
    }
    if (!busy) s_igaging_tim->pause();
}

static void igaging_start_frame() {
    if (millis() - s_ig_last_ms < IGAGING_PERIOD_MS) return;
    s_ig_last_ms = millis();
    for (uint8_t i = 0; i < 2; i++) {
        if (s_ax[i].type != SCALE_IGAGING21 || s_ig_phase[i]) continue;
        s_ax[i].nbits = 0;
        s_ax[i].shift = 0;
        s_ig_phase[i] = 1;
    }
    s_igaging_tim->resume();
}

// ============================================================
// Инициализация
// ============================================================
static void axis_config(ScaleAxis axis, uint8_t type, int32_t nm, uint8_t inv, uint8_t sinv,
                        uint32_t pa, uint32_t pb, uint32_t preq) {
    ScaleAxisState& a = s_ax[axis];
    a.type = type;  a.nm_per_count = nm;  a.invert = inv;  a.sig_inv = sinv ? 1 : 0;
    a.pin_a = pa;   a.pin_b = pb;         a.pin_req = preq;
    a.pn_a  = digitalPinToPinName(pa);
    a.pn_b  = digitalPinToPinName(pb);
    a.frame_bits = (type == SCALE_SYLVAC48)  ? SCALE_SYLVAC48_BITS
                 : (type == SCALE_BIN6)      ? SCALE_BIN6_BITS
                 : (type == SCALE_IGAGING21) ? SCALE_IGAGING21_BITS
                 : (type == SCALE_DIGIMATIC) ? SCALE_DIGIMATIC_BITS : 0;

    switch (type) {
        case SCALE_QUADRATURE:
            quad_init(axis);
            break;
        case SCALE_SYLVAC48:
        case SCALE_BIN6:
        case SCALE_DIGIMATIC:
            pinMode(pa, INPUT);
            pinMode(pb, INPUT);
            // Без инверсии данные валидны на спаде такта линейки
            attachInterrupt(digitalPinToInterrupt(pa),
                            axis == SCALE_AXIS_Z ? isr_clock_z : isr_clock_x,
                            a.sig_inv ? RISING : FALLING);
            if (type == SCALE_DIGIMATIC) {
                pinMode(preq, OUTPUT);
                digitalWrite(preq, LOW);   // выход на NPN: HIGH = REQ к земле (запрос)
            }
            break;
        case SCALE_IGAGING21:
            pinMode(pa, OUTPUT);
            digitalWrite(pa, a.sig_inv ? HIGH : LOW);
            pinMode(pb, INPUT);
            if (!s_igaging_tim) {
                s_igaging_tim = new HardwareTimer(TIM11);
                s_igaging_tim->setOverflow(IGAGING_TICK_HZ, HERTZ_FORMAT);
                s_igaging_tim->attachInterrupt(igaging_tick);
            }
            break;
        default:
            break;
    }
}

void DRV_Scales_Init(void) {
    axis_config(SCALE_AXIS_Z, SCALE_Z_TYPE, SCALE_Z_NM_PER_COUNT, SCALE_Z_INVERT,
                SCALE_Z_SIGNAL_INVERT, SCALE_Z_PIN_A, SCALE_Z_PIN_B, SCALE_Z_PIN_REQ);
    axis_config(SCALE_AXIS_X, SCALE_X_TYPE, SCALE_X_NM_PER_COUNT, SCALE_X_INVERT,
                SCALE_X_SIGNAL_INVERT, SCALE_X_PIN_A, SCALE_X_PIN_B, SCALE_X_PIN_REQ);
}

// ============================================================
// Обработка (loop)
// ============================================================
static void digimatic_request(ScaleAxisState& a) {
    uint32_t now = millis();
    if (!a.req_active) {
        if (now - a.req_t < DIGIMATIC_PERIOD_MS) return;
        noInterrupts();
        a.nbits = 0;
        a.shift = 0;
        interrupts();
        digitalWrite(a.pin_req, HIGH);   // REQ → 0 через NPN
        a.req_active = 1;
        a.req_t = now;
    } else if (a.nbits > 0 || now - a.req_t > DIGIMATIC_TIMEOUT_MS) {
        digitalWrite(a.pin_req, LOW);    // прибор начал передачу (или не ответил)
        a.req_active = 0;
        if (a.nbits == 0) a.frames_err++;
    }
}

static void process_axis(ScaleAxis axis) {
    ScaleAxisState& a = s_ax[axis];
    int32_t um = a.um;
    bool got = false;

    if (a.type == SCALE_QUADRATURE) {
        um = ScaleProto_QuadToUm(quad_read(axis), a.nm_per_count);
        got = true;
    } else if (a.type != SCALE_NONE) {
        if (a.type == SCALE_DIGIMATIC) digimatic_request(a);
        if (!a.frame_ready) return;
        noInterrupts();
        uint64_t frame = a.frame;
        a.frame_ready = 0;
        interrupts();

        bool ok = true;
        uint8_t nib[13];
        switch (a.type) {
            case SCALE_SYLVAC48:  um = ScaleProto_Sylvac48ToUm(frame);                break;
            case SCALE_BIN6:      ok = ScaleProto_Bin6ToUm((uint32_t)frame, &um);     break;
            case SCALE_IGAGING21: um = ScaleProto_IGaging21ToUm((uint32_t)frame);     break;
            case SCALE_DIGIMATIC:
                ScaleProto_SplitNibbles(frame, nib);
                ok = ScaleProto_DigimaticToUm(nib, &um);
                break;
        }
        if (!ok) { a.frames_err++; return; }
        a.frames_ok++;
        a.last_ok_ms = millis();
        got = true;
    }
    if (got) a.um = a.invert ? -um : um;
}

void DRV_Scales_Process(void) {
    if (s_igaging_tim) igaging_start_frame();
    process_axis(SCALE_AXIS_Z);
    process_axis(SCALE_AXIS_X);
}

// ============================================================
// API
// ============================================================
uint8_t  DRV_Scales_Present(ScaleAxis axis)  { return s_ax[axis].type != SCALE_NONE; }
int32_t  DRV_Scales_GetUm(ScaleAxis axis)    { return s_ax[axis].um; }
uint32_t DRV_Scales_GetFrames(ScaleAxis axis){ return s_ax[axis].frames_ok; }
uint32_t DRV_Scales_GetErrors(ScaleAxis axis){ return s_ax[axis].frames_err; }

uint8_t DRV_Scales_IsOk(ScaleAxis axis) {
    const ScaleAxisState& a = s_ax[axis];
    if (a.type == SCALE_NONE || a.type == SCALE_QUADRATURE) return 1;
    return a.frames_ok > 0 && (millis() - a.last_ok_ms) < STALE_MS;
}

const char* DRV_Scales_TypeName(ScaleAxis axis) {
    switch (s_ax[axis].type) {
        case SCALE_QUADRATURE: return "QUADRATURE";
        case SCALE_SYLVAC48:   return "SYLVAC48";
        case SCALE_BIN6:       return "BIN6";
        case SCALE_IGAGING21:  return "IGAGING21";
        case SCALE_DIGIMATIC:  return "DIGIMATIC";
        default:               return "NONE";
    }
}

#else  // ELS_POS_SOURCE != POS_SOURCE_SCALES — заглушки

void     DRV_Scales_Init(void)                  {}
void     DRV_Scales_Process(void)               {}
uint8_t  DRV_Scales_Present(ScaleAxis)          { return 0; }
int32_t  DRV_Scales_GetUm(ScaleAxis)            { return 0; }
uint8_t  DRV_Scales_IsOk(ScaleAxis)             { return 0; }
uint32_t DRV_Scales_GetFrames(ScaleAxis)        { return 0; }
uint32_t DRV_Scales_GetErrors(ScaleAxis)        { return 0; }
const char* DRV_Scales_TypeName(ScaleAxis)      { return "NONE"; }

#endif
