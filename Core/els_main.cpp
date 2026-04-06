#include <Arduino.h>
#include "els_config.h"
#include "els_main.h"
#include "els_state.h"
#include "els_control.h"
#include "els_serial.h"
#include "../Drivers/drv_dro.h"
#include "../Drivers/drv_display.h"
#include "../Drivers/drv_lcd2004.h"
#include "../Drivers/drv_encoder.h"
#include "../Drivers/drv_inputs.h"
#include "../Drivers/drv_stepper.h"
#include "../Drivers/drv_beeper.h"
#include <stdlib.h>  // abs()

// ============================================================
void ELS_Init(void) {
    // Инициализация состояния
    ELS_State_Init();

    // Debug UART (через ST-Link VCP)
    Serial.begin(DEBUG_UART_BAUD);
    Serial.println("[ELS] Boot v0.1");
    Serial.println("[ELS] STM32F4DISCOVERY ELS starting...");

#if USE_BEEPER
    DRV_Beeper_Init();
    DRV_Beeper_Tone(1000, 100); // Короткий бип при старте
#endif

#if USE_LCD2004
    DRV_LCD2004_Init();
    DRV_LCD2004_Print(0, 0, "ELS STM32 v0.1  ");
    DRV_LCD2004_Print(1, 0, "Initializing... ");
#endif

#if USE_DRO_HS800
    DRV_DRO_Init();
    Serial.println("[ELS] DRO HS800-2 initialized");
#endif

#if USE_ESP32_DISPLAY
    DRV_Display_Init();
    Serial.println("[ELS] ESP32 display initialized");
#endif

    DRV_Encoder_Spindle_Init();
    DRV_Inputs_Init();
    DRV_Stepper_Init();

    ELS_Control_Init();
    ELS_Serial_Init();

    Serial.println("[ELS] Init complete");
    Serial.println("[ELS] Type HELP for commands");

#if USE_LCD2004
    DRV_LCD2004_Print(1, 0, "Ready           ");
#endif
}

// ============================================================
void ELS_Loop(void) {
#if USE_DRO_HS800
    DRV_DRO_Process();

    // Обновляем позицию из DRO
    // Знак: DRO отдаёт положительные значения при движении "от нуля",
    // но на дисплее логика инвертирована (как в оригинальном Arduino-коде)
    int32_t new_x = -DRV_DRO_GetX();
    int32_t new_y = -DRV_DRO_GetY();
    if (new_x != els.pos_x || new_y != els.pos_y) {
        els.pos_x = new_x;
        els.pos_y = new_y;
#if USE_ESP32_DISPLAY
        DRV_Display_SendPosition(els.pos_y, els.pos_x);
#endif
#if USE_LCD2004
        DRV_LCD2004_UpdatePosition(els.pos_x, els.pos_y);
#endif
        // Debug: выводим позицию в Serial Monitor
        Serial.print("[DRO] X=");
        Serial.print(els.pos_x / 1000L);
        Serial.print(".");
        Serial.print(abs(els.pos_x % 1000L));
        Serial.print("mm  Y=");
        Serial.print(els.pos_y / 1000L);
        Serial.print(".");
        Serial.print(abs(els.pos_y % 1000L));
        Serial.print("mm  pkts=");
        Serial.print(DRV_DRO_GetPacketCount());
        Serial.print("  errs=");
        Serial.println(DRV_DRO_GetErrorCount());
    }
#endif

    // Обработка входящих команд от ESP32
#if USE_ESP32_DISPLAY
    DRV_Display_Process();
#endif

    // Обработка энкодера шпинделя
    DRV_Encoder_Spindle_Update();

    // Обработка входов (кнопки, джойстик)
    DRV_Inputs_Process();

    // Этап 7+8: управление двигателями
    DRV_Stepper_Update();
    ELS_Control_Update();

    // Этап 9: Serial Monitor команды
    ELS_Serial_Process();
}
