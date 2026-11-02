#pragma once
// Имитация HardwareTimer STM32duino: запоминает обработчик, период пишет в ARR.
#include "Arduino.h"

struct SimTimer { TIM_TypeDef* tim; uint32_t tick_ns; void (*cb)(void); };
SimTimer* sim_timer_slot(TIM_TypeDef* tim);

class HardwareTimer {
public:
    explicit HardwareTimer(TIM_TypeDef* t) : _t(t), _psc(1) {}
    uint32_t getTimerClkFreq() { return 84000000u; }
    void setPrescaleFactor(uint32_t f) { _psc = f; sim_timer_slot(_t)->tick_ns = (uint32_t)(1000000000ull * f / 84000000u); }
    void setOverflow(uint32_t v, int) { _t->ARR = v - 1; }
    void attachInterrupt(void (*cb)(void)) { sim_timer_slot(_t)->cb = cb; }
    void setInterruptPriority(uint32_t, uint32_t) {}
    void resume() {}
private:
    TIM_TypeDef* _t;
    uint32_t _psc;
};
