#pragma once
#include <stdint.h>

// Джойстик
typedef enum {
    JOY_NONE  = 0,
    JOY_LEFT  = (1<<0),
    JOY_RIGHT = (1<<1),
    JOY_UP    = (1<<2),
    JOY_DOWN  = (1<<3),
    JOY_RAPID = (1<<4),
} JoyState_t;

// Кнопки меню
typedef enum {
    BTN_NONE   = 0,
    BTN_LEFT   = (1<<0),
    BTN_RIGHT  = (1<<1),
    BTN_UP     = (1<<2),
    BTN_DOWN   = (1<<3),
    BTN_SELECT = (1<<4),
} BtnState_t;

// Лимитные выключатели
typedef enum {
    LIM_NONE  = 0,
    LIM_LEFT  = (1<<0),
    LIM_RIGHT = (1<<1),
    LIM_FRONT = (1<<2),
    LIM_REAR  = (1<<3),
} LimState_t;

void        DRV_Inputs_Init(void);
void        DRV_Inputs_Process(void);

JoyState_t  DRV_Inputs_GetJoy(void);
BtnState_t  DRV_Inputs_GetBtn(void);
LimState_t  DRV_Inputs_GetLimits(void);
uint8_t     DRV_Inputs_GetMode(void);      // Байт переключателя mode (PG8-PG15)
uint8_t     DRV_Inputs_GetSubmode(void);   // 0=Internal,1=Manual,2=External

void        DRV_Inputs_SetLimitLED(LimState_t leds); // Включить/выключить LED лимитов
