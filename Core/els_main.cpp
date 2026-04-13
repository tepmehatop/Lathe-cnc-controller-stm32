#include <Arduino.h>
#include <Wire.h>
#include "els_config.h"
#include "els_main.h"
#include "els_state.h"
#include "els_control.h"
#include "els_menu.h"
#include "els_serial.h"
#include "../Drivers/drv_dro.h"
#include "../Drivers/drv_display.h"
#include "../Drivers/drv_lcd2004.h"
#include "../Drivers/drv_encoder.h"
#include "../Drivers/drv_inputs.h"
#include "../Drivers/drv_stepper.h"
#include "../Drivers/drv_beeper.h"
#include <stdlib.h>

// ============================================================
void ELS_Init(void) {
    ELS_State_Init();
    Serial.begin(DEBUG_UART_BAUD);

#if USE_LCD2004
    DRV_LCD2004_Init();
    DRV_LCD2004_PrintWelcome();
    DRV_LCD2004_Clear();
#endif

#if USE_DRO_HS800
    DRV_DRO_Init();
#endif

#if USE_ESP32_DISPLAY
    DRV_Display_Init();
#endif

    DRV_Encoder_Spindle_Init();
    DRV_Inputs_Init();
    ELS_Control_Init();
    ELS_Menu_Init();
    ELS_Serial_Init();
}

// ============================================================
void ELS_Loop(void) {
    static uint32_t s_lcd_t  = 0;
    static uint32_t s_disp_t = 0;

    DRV_DRO_Process();
    DRV_Display_Process();
    DRV_Encoder_Spindle_Update();
    DRV_Inputs_Process();
    ELS_Menu_Process();
    ELS_Control_Update();
    ELS_Serial_Process();

    // Обновление LCD каждые 200мс
    if ((millis() - s_lcd_t) >= 200) {
        s_lcd_t = millis();

        // Обновить позицию из DRO
        els.pos_y = -DRV_DRO_GetY();
        els.pos_x =  DRV_DRO_GetX();
        els.Size_Z_mm = els.pos_y / 10;
        els.Size_X_mm = els.pos_x / 10;
        els.MSize_X_mm = els.pos_x / 5;

        // RPM из энкодера
        els.spindle_rpm = DRV_Encoder_Spindle_GetRPM();

        // Основной экран
        DRV_LCD2004_PrintELS(&els);
    }

    // ESP32: обновление позиции и RPM раз в 250мс (как в оригинале Arduino),
    // только при реальном изменении значений.
#if USE_ESP32_DISPLAY
    if ((millis() - s_disp_t) >= 250) {
        s_disp_t = millis();

        static int32_t s_last_pos_y = 0x7FFFFFFF;
        static int32_t s_last_pos_x = 0x7FFFFFFF;
        static int32_t s_last_rpm   = 0x7FFFFFFF;

        if (els.pos_y != s_last_pos_y || els.pos_x != s_last_pos_x) {
            DRV_Display_SendPosition(els.pos_y, els.pos_x);
            s_last_pos_y = els.pos_y;
            s_last_pos_x = els.pos_x;
        }
        if (els.spindle_rpm != s_last_rpm) {
            DRV_Display_SendRpm(els.spindle_rpm);
            s_last_rpm = els.spindle_rpm;
        }
    }
#endif
}
