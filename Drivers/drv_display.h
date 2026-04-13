#pragma once
#include <stdint.h>

// ============================================================
// Протокол ESP32 ↔ STM32  (UART8, 115200/8N1)
//
// TX: <CMD>\n  или  <CMD:value>\n  или  <CMD:v1,v2>\n
// RX: <TOUCH:KEY:UP>, <TOUCH:M1>..<TOUCH:M8>,
//     <TOUCH:JOY:LEFT>, <READY>, <PONG>, и т.д.
//
// Пины: PE0 = UART8_TX → ESP32 GPIO44 (RX)
//        PE1 = UART8_RX ← ESP32 GPIO43 (TX)
// ============================================================

// Callback: вызывается при получении команды от дисплея
// Тип касания
typedef enum {
    TOUCH_NONE      = 0,
    TOUCH_KEY_UP,
    TOUCH_KEY_DN,
    TOUCH_KEY_LEFT,
    TOUCH_KEY_RIGHT,
    TOUCH_PARAM_OK,
    TOUCH_M1, TOUCH_M2, TOUCH_M3, TOUCH_M4,
    TOUCH_M5, TOUCH_M6, TOUCH_M7, TOUCH_M8,
    TOUCH_S1, TOUCH_S2, TOUCH_S3,
    TOUCH_JOY_LEFT, TOUCH_JOY_RIGHT, TOUCH_JOY_UP, TOUCH_JOY_DOWN, TOUCH_JOY_STOP,
    TOUCH_RAPID_ON, TOUCH_RAPID_OFF,
    TOUCH_ALERT_OK,
    TOUCH_THR_CAT,
    TOUCH_READY,
} DispTouch_t;

// Структура входящей команды с числовым параметром
typedef struct {
    DispTouch_t touch;
    char        cmd[16];   // Имя команды (AP, FEED, AFEED, SPHERE, ...)
    int32_t     value;     // Числовой параметр (если есть)
    uint8_t     has_value;
} DispRxCmd_t;

// Регистрация callback для входящих команд
typedef void (*DispRxCallback_t)(const DispRxCmd_t* cmd);
void DRV_Display_SetRxCallback(DispRxCallback_t cb);

// ---- Инициализация и обработка ----
void DRV_Display_Init(void);
void DRV_Display_Process(void);      // Вызывать из loop()

// ---- Отправка одиночных значений ----
void DRV_Display_SendPosition(int32_t pos_y, int32_t pos_x); // в 0.001 мм
void DRV_Display_SendMode(uint8_t mode, uint8_t submode);
void DRV_Display_SendFeed(int32_t feed, int32_t afeed);
void DRV_Display_SendRpm(int32_t rpm);
void DRV_Display_SendOtskok(int32_t otskok_mm);   // 0.001 мм
void DRV_Display_SendTension(int32_t tension_mm); // 0.001 мм
void DRV_Display_SendLimits(uint8_t l, uint8_t r, uint8_t f, uint8_t b);
void DRV_Display_SendAlert(int32_t type);
void DRV_Display_SendSelectMenu(uint8_t menu);
void DRV_Display_SendAngle(int32_t angle_mdeg); // угол в 0.001°, отправляет /100 → дециградусы

// ---- Низкоуровневые хелперы (для отправки произвольных команд) ----
void DRV_Display_SendCmd(const char* cmd, const char* params);
void DRV_Display_SendInt(const char* cmd, int32_t val);
void DRV_Display_SendInt2(const char* cmd, int32_t a, int32_t b);

// ---- Полное обновление состояния (при READY/PONG от ESP32) ----
// Заполняется снаружи через колбэк или вручную из els_main
void DRV_Display_SendAll(void);
int  DRV_Display_TxFree(void);
