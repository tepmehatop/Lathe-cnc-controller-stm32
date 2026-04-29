#include <Arduino.h>
#include <Wire.h>
#include "els_config.h"
#include "els_main.h"
#include "els_state.h"
#include "els_control.h"
#include "els_menu.h"
#include "els_serial.h"
#include "els_settings.h"
#include "../Drivers/drv_dro.h"
#include "../Drivers/drv_display.h"
#include "../Drivers/drv_lcd2004.h"
#include "../Drivers/drv_encoder.h"
#include "../Drivers/drv_inputs.h"
#include "../Drivers/drv_stepper.h"
#include "../Drivers/drv_beeper.h"
#include <stdlib.h>

// ============================================================
// АЦП: переменник подачи — Этап 21
// 16-sample скользящее среднее, дедбанд ±4, 12-bit STM32 ADC (0-4095)
// ============================================================
#if USE_ADC_FEED
static int  s_adc_arr[16] = {};
static int  s_adc_sum     = 0;
static int  s_adc_idx     = 0;
static int  s_adc_filt    = 0;  // отфильтрованное значение

static void _adc_feed_process(void) {
    ELS_Mode_t m = els.mode;
    bool feed_mode  = (m == MODE_FEED || m == MODE_CONE_L || m == MODE_CONE_R || m == MODE_SPHERE);
    bool afeed_mode = (m == MODE_AFEED);
    if (!feed_mode && !afeed_mode) return;

    int raw = analogRead(ADC_FEED_PIN);  // 0-4095
    if (raw > s_adc_filt + 4 || raw < s_adc_filt - 4) {
        if (++s_adc_idx > 15) s_adc_idx = 0;
        s_adc_sum -= s_adc_arr[s_adc_idx];
        s_adc_arr[s_adc_idx] = raw;
        s_adc_sum += raw;
        s_adc_filt = s_adc_sum / 16;
    }

    if (feed_mode) {
        uint16_t v = (uint16_t)(MAX_FEED - (long)(MAX_FEED - MIN_FEED + 1) * s_adc_filt / 4096);
        if (v != els.Feed_mm) {
            els.Feed_mm = v;
            els.feed    = v;
            DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
            ELS_Settings_MarkDirty();
        }
    } else {
        uint16_t v = (uint16_t)((MAX_AFEED / 10 - (long)(MAX_AFEED / 10 - MIN_AFEED / 10 + 1) * s_adc_filt / 4096) * 10);
        if (v != els.aFeed_mm) {
            els.aFeed_mm = v;
            els.afeed    = v;
            DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
            ELS_Settings_MarkDirty();
        }
    }
}
#endif  // USE_ADC_FEED

// ============================================================
void ELS_Init(void) {
    ELS_State_Init();
    ELS_Settings_Load();
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
    DRV_Encoder_Hand_Init();
    DRV_Inputs_Init();
#if USE_ADC_FEED
    pinMode(ADC_FEED_PIN, INPUT_ANALOG);
#endif
    ELS_Control_Init();
    ELS_Menu_Init();
    ELS_Serial_Init();
}

// ============================================================
void ELS_Loop(void) {
    static uint32_t s_lcd_t   = 0;
    static uint32_t s_disp_t  = 0;
    static uint32_t s_lcd2004_t = 0;

    ELS_Settings_Process();
#if USE_ADC_FEED
    _adc_feed_process();
#endif
    DRV_DRO_Process();
    DRV_Display_Process();
    DRV_Encoder_Spindle_Update();
    DRV_Inputs_Process();
    ELS_Menu_Process();
    ELS_Control_Update();
    ELS_Serial_Process();

    // ── Обновление позиции из DRO в каждом цикле ──────────────────────────
    // Позиция всегда актуальна: и для LCD, и для ESP32, и для Control.
    // DRO эталон: по нему же будут работать лимиты и остановы мотора.
    els.pos_y      = -DRV_DRO_GetY();
    els.pos_x      =  DRV_DRO_GetX();
    els.Size_Z_mm  = els.pos_y / 10;
    els.Size_X_mm  = els.pos_x / 10;
    els.MSize_X_mm = els.pos_x / 5;

    // RPM из энкодера шпинделя + флаг вращения
    els.spindle_rpm  = DRV_Encoder_Spindle_GetRPM();
    els.spindle_flag = (els.spindle_rpm > 10);

    // Делитель: угол шпинделя и требуемый угол для текущего зуба
    {
        const int32_t SPR = (int32_t)(ENC_LINE_PER_REV * 4);  // шагов/об (×4 квадратура)
        int32_t raw  = DRV_Encoder_Spindle_GetCount() % SPR;
        if (raw < 0) raw += SPR;
        els.Enc_Pos        = raw;
        els.Spindle_Angle  = (uint32_t)((int64_t)raw * 360000L / SPR);
        uint32_t tt        = els.Total_Tooth > 0 ? els.Total_Tooth : 1;
        uint32_t ct        = els.Current_Tooth > 0 ? els.Current_Tooth : 1;
        els.Required_Angle = 360000UL * (ct - 1U) / tt;
    }

    // ── Обновление LCD каждые 200мс ───────────────────────────────────────
    if ((millis() - s_lcd_t) >= 200) {
        s_lcd_t = millis();
        DRV_LCD2004_PrintELS(&els);
    }

    // ── LCD2004→ESP32: дублирование состояния каждые 500мс ───────────────
#if USE_ESP32_DISPLAY
    if ((millis() - s_lcd2004_t) >= 500) {
        s_lcd2004_t = millis();
        DRV_Display_SendLCD2004State();
    }
#endif

    // ── ESP32: отправка позиции и RPM каждые 250мс ────────────────────────
    // Отправляем всегда (как в оригинале Arduino), не только при изменении.
    // Это гарантирует синхронизацию даже после перезагрузки ESP32.
#if USE_ESP32_DISPLAY
    if ((millis() - s_disp_t) >= 250) {
        s_disp_t = millis();
        DRV_Display_SendPosition(els.pos_y, els.pos_x);
        DRV_Display_SendRpm(els.spindle_rpm);
        if (els.mode == MODE_DIVIDER) {
            DRV_Display_SendAngle((int32_t)els.Spindle_Angle);
        }
    }
#endif
}
