/**
 * @file  drv_inputs.cpp
 * @brief Драйвер входов (кнопки, джойстик, лимиты, переключатели) — ЗАГЛУШКА (Этап 5)
 *
 * Джойстик:    PC4-PC8, Active LOW, внутренняя PULLUP STM32
 * Кнопки меню: PG0-PG4, Active LOW, внешняя PULLUP 10кОм
 * Лимиты BTN:  PF0,PF2,PF4,PF6, Active LOW, внешняя PULLUP 10кОм
 * Лимиты LED:  PF1,PF3,PF5,PF7, Active LOW output
 * Submode SW:  PG5-PG7, Active LOW, внешняя PULLUP 1кОм
 * Mode SW:     PG8-PG15, Active LOW, внешняя PULLUP 1кОм (ОБЯЗАТЕЛЬНА!)
 *
 * TODO (Этап 5): GPIO init, debounce, mode read
 */

#include "drv_inputs.h"
#include "../Core/els_config.h"

void DRV_Inputs_Init(void) {
    // TODO: GPIO init для всех входов
    // Джойстик PC4-PC8: INPUT_PULLUP (внутренняя)
    // Кнопки PG0-PG4:  INPUT (внешняя 10кОм)
    // Лимиты PF0-PF7:  INPUT (внешняя 10кОм) / OUTPUT (LED)
    // Submode PG5-PG7: INPUT (внешняя 1кОм)
    // Mode PG8-PG15:   INPUT (внешняя 1кОм)
}

void DRV_Inputs_Process(void) {
    // TODO: Дебаунс, обработка автоповтора кнопок меню
}

JoyState_t DRV_Inputs_GetJoy(void) {
    // TODO: Читать PC4-PC8, вернуть битовую маску
    return JOY_NONE;
}

BtnState_t DRV_Inputs_GetBtn(void) {
    // TODO: Читать PG0-PG4
    return BTN_NONE;
}

LimState_t DRV_Inputs_GetLimits(void) {
    // TODO: Читать PF0,PF2,PF4,PF6
    return LIM_NONE;
}

uint8_t DRV_Inputs_GetMode(void) {
    // TODO: Читать (~GPIOG->IDR >> 8) & 0xFF
    return 0;
}

uint8_t DRV_Inputs_GetSubmode(void) {
    // TODO: Читать PG5,PG6,PG7
    return 0;
}

void DRV_Inputs_SetLimitLED(LimState_t leds) {
    // TODO: Управлять PF1,PF3,PF5,PF7
    (void)leds;
}
