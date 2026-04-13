/**
 * @file  drv_display.cpp
 * @brief Драйвер ESP32-S3 JC4827W543 — Этап 3
 *
 * Протокол: текстовый, строки вида <CMD:value>\n
 * Аппаратный UART8 (PE0=TX, PE1=RX), 115200/8N1, STM32duino HardwareSerial
 *
 * ЗНАК ПОЗИЦИЙ:
 *   В Arduino: отправляет -(Size_Z_mm * 10L), где Size_Z_mm = -(dro_pos/10)
 *   Итого esp32 получает: dro_pos_y в 0.001мм (исходный знак DRO)
 *   В STM32: els.pos_y = -DRV_DRO_GetY() (уже инвертировано)
 *   Поэтому отправляем -els.pos_y чтобы вернуть исходный знак DRO
 *   Для совместимости с ESP32 firmware: ось Y → POS_Z, ось X → POS_X
 */

#include "drv_display.h"
#include "drv_lcd2004.h"
#include "../Core/els_config.h"
#include "../Core/els_state.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if USE_ESP32_DISPLAY

// ============================================================
// UART8: PE0=TX, PE1=RX  (STM32duino: RX первый, TX второй)
// ============================================================
static HardwareSerial DispSerial(PB11, PB10);  // RX=PB11(USART3_RX), TX=PB10(USART3_TX)

int DRV_Display_TxFree(void) { return DispSerial.availableForWrite(); }

// ============================================================
// RX буфер
// ============================================================
#define DISP_RX_BUF_LEN  64
static char    s_rxbuf[DISP_RX_BUF_LEN];
static uint8_t s_rxlen = 0;

// Callback входящих команд
static DispRxCallback_t s_rx_cb = nullptr;
static volatile uint8_t s_need_sendall = 0; // флаг: ESP32 прислал READY/PONG

void DRV_Display_SetRxCallback(DispRxCallback_t cb) {
    s_rx_cb = cb;
}

// ============================================================
// Парсинг входящей строки (без '\n')
// Формат: <CMD>  или  <CMD:param>  или  <TOUCH:KEY:UP>
// ============================================================
static void _parse_rx(const char* raw) {
    if (raw[0] != '<') return;
    const char* end = strchr(raw, '>');
    if (!end) return;

    // Копируем внутренность без < >
    char inner[DISP_RX_BUF_LEN];
    size_t inner_len = (size_t)(end - raw - 1);
    if (inner_len >= sizeof(inner)) inner_len = sizeof(inner) - 1;
    memcpy(inner, raw + 1, inner_len);
    inner[inner_len] = '\0';

    // Разделяем на cmd и params по первому ':'
    char* colon = strchr(inner, ':');
    char cmd_str[32] = {};
    char prm_str[48] = {};
    if (colon) {
        size_t clen = (size_t)(colon - inner);
        if (clen >= sizeof(cmd_str)) clen = sizeof(cmd_str)-1;
        memcpy(cmd_str, inner, clen);
        strncpy(prm_str, colon + 1, sizeof(prm_str)-1);
    } else {
        strncpy(cmd_str, inner, sizeof(cmd_str)-1);
    }

    DispRxCmd_t rx = {};
    strncpy(rx.cmd, cmd_str, sizeof(rx.cmd)-1);

    // READY / PONG → выставить флаг, SendAll вызовется из loop()
    if (strcmp(cmd_str, "READY") == 0 || strcmp(cmd_str, "PONG") == 0) {
        s_need_sendall = 1;
        return;
    }

    // TOUCH:...
    if (strcmp(cmd_str, "TOUCH") == 0) {
        if      (strcmp(prm_str, "KEY:UP")    == 0) rx.touch = TOUCH_KEY_UP;
        else if (strcmp(prm_str, "KEY:DN")    == 0) rx.touch = TOUCH_KEY_DN;
        else if (strcmp(prm_str, "KEY:LEFT")  == 0) rx.touch = TOUCH_KEY_LEFT;
        else if (strcmp(prm_str, "KEY:RIGHT") == 0) rx.touch = TOUCH_KEY_RIGHT;
        else if (strcmp(prm_str, "PARAM_OK")  == 0) rx.touch = TOUCH_PARAM_OK;
        else if (strcmp(prm_str, "M1")        == 0) rx.touch = TOUCH_M1;
        else if (strcmp(prm_str, "M2")        == 0) rx.touch = TOUCH_M2;
        else if (strcmp(prm_str, "M3")        == 0) rx.touch = TOUCH_M3;
        else if (strcmp(prm_str, "M4")        == 0) rx.touch = TOUCH_M4;
        else if (strcmp(prm_str, "M5")        == 0) rx.touch = TOUCH_M5;
        else if (strcmp(prm_str, "M6")        == 0) rx.touch = TOUCH_M6;
        else if (strcmp(prm_str, "M7")        == 0) rx.touch = TOUCH_M7;
        else if (strcmp(prm_str, "M8")        == 0) rx.touch = TOUCH_M8;
        else if (strcmp(prm_str, "S1")        == 0) rx.touch = TOUCH_S1;
        else if (strcmp(prm_str, "S2")        == 0) rx.touch = TOUCH_S2;
        else if (strcmp(prm_str, "S3")        == 0) rx.touch = TOUCH_S3;
        else if (strcmp(prm_str, "JOY:LEFT")  == 0) rx.touch = TOUCH_JOY_LEFT;
        else if (strcmp(prm_str, "JOY:RIGHT") == 0) rx.touch = TOUCH_JOY_RIGHT;
        else if (strcmp(prm_str, "JOY:UP")    == 0) rx.touch = TOUCH_JOY_UP;
        else if (strcmp(prm_str, "JOY:DOWN")  == 0) rx.touch = TOUCH_JOY_DOWN;
        else if (strcmp(prm_str, "JOY:STOP")  == 0) rx.touch = TOUCH_JOY_STOP;
        else if (strcmp(prm_str, "RAPID_ON")  == 0) rx.touch = TOUCH_RAPID_ON;
        else if (strcmp(prm_str, "RAPID_OFF") == 0) rx.touch = TOUCH_RAPID_OFF;
        else if (strcmp(prm_str, "ALERT_OK")  == 0) rx.touch = TOUCH_ALERT_OK;
        else if (strcmp(prm_str, "THR_CAT")   == 0) rx.touch = TOUCH_THR_CAT;
        else {
            // Параметрические TOUCH: AP:N, FEED:N, AFEED:N, SPHERE:N, ...
            char* sub_colon = strchr(prm_str, ':');
            if (sub_colon) {
                size_t klen = (size_t)(sub_colon - prm_str);
                if (klen < sizeof(rx.cmd)) {
                    memcpy(rx.cmd, prm_str, klen);
                    rx.cmd[klen] = '\0';
                }
                rx.value     = atol(sub_colon + 1);
                rx.has_value = 1;
                rx.touch     = TOUCH_NONE;
            }
        }
    }

    // Числовой параметр для команд не-TOUCH
    if (!rx.has_value && prm_str[0]) {
        rx.value     = atol(prm_str);
        rx.has_value = 1;
    }

    if (s_rx_cb) s_rx_cb(&rx);
}

// ============================================================
// Низкоуровневые функции отправки
// ============================================================
void DRV_Display_SendCmd(const char* cmd, const char* params) {
    // Не блокировать цикл если TX буфер заполнен
    if (DispSerial.availableForWrite() < 32) return;
    DispSerial.print('<');
    DispSerial.print(cmd);
    if (params && params[0]) {
        DispSerial.print(':');
        DispSerial.print(params);
    }
    DispSerial.println('>');
}

void DRV_Display_SendInt(const char* cmd, int32_t val) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%ld", (long)val);
    DRV_Display_SendCmd(cmd, buf);
}

void DRV_Display_SendInt2(const char* cmd, int32_t a, int32_t b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld,%ld", (long)a, (long)b);
    DRV_Display_SendCmd(cmd, buf);
}

// ============================================================
// Высокоуровневые функции отправки
// ============================================================

// Позиции: els.pos_y/pos_x хранятся с инвертированным знаком
// ESP32 ожидает оригинальный знак DRO → отправляем -pos
// Имена: POS_Z = продольная (Y), POS_X = поперечная (X)
void DRV_Display_SendPosition(int32_t pos_y, int32_t pos_x) {
    DRV_Display_SendInt("POS_Z", -pos_y);
    DRV_Display_SendInt("POS_X", -pos_x);
}

void DRV_Display_SendMode(uint8_t mode, uint8_t submode) {
    // ESP32 firmware ожидает 1-based (Mode_Feed=1), наш enum 0-based → +1
    DRV_Display_SendInt("MODE",    (int32_t)mode + 1);
    DRV_Display_SendInt("SUBMODE", (int32_t)submode + 1);
}

void DRV_Display_SendFeed(int32_t feed, int32_t afeed) {
    DRV_Display_SendInt("FEED",  feed);
    DRV_Display_SendInt("AFEED", afeed);
}

void DRV_Display_SendRpm(int32_t rpm) {
    DRV_Display_SendInt("RPM", rpm);
}

// otskok_mm в 0.001мм → ESP32 ожидает 0.001мм × 10 = 0.0001мм? Нет.
// В Arduino: OTSKOK_Z_mm в 0.01мм, отправляют OTSKOK_Z_mm * 10 → в 0.001мм
// Наш els.otskok_y уже в 0.001мм → умножаем на 10? Нет — проверим шкалу.
// Arduino OTSKOK_Z — в шагах. OTSKOK_Z_mm = OTSKOK_Z / (шаги на мм) → в 0.01мм
// Отправляется: OTSKOK_Z_mm * 10 → в 0.001мм (ESP32 делит на 1000 для мм)
// Наш els.otskok_y хранится в шагах (пока 0), TODO уточнить при Этапе 7
// Для совместимости API: принимаем уже в 0.001мм, отправляем как есть
void DRV_Display_SendOtskok(int32_t otskok_mm) {
    DRV_Display_SendInt("OTSKOK_Z", otskok_mm);
}

void DRV_Display_SendTension(int32_t tension_mm) {
    DRV_Display_SendInt("TENSION_Z", tension_mm);
}

void DRV_Display_SendLimits(uint8_t l, uint8_t r, uint8_t f, uint8_t b) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d", l, r, f, b);
    DRV_Display_SendCmd("LIMITS", buf);
}

void DRV_Display_SendAlert(int32_t type) {
    DRV_Display_SendInt("ALERT", type);
}

void DRV_Display_SendSelectMenu(uint8_t menu) {
    DRV_Display_SendInt("SELECTMENU", (int32_t)menu);
}

// angle_mdeg: 0.001° → делим на 100 → дециградусы (как в Arduino)
void DRV_Display_SendAngle(int32_t angle_mdeg) {
    DRV_Display_SendInt("ANGLE", angle_mdeg / 100);
}

// ============================================================
// Полное состояние (при READY/PONG от ESP32)
// Отправляем всё что знаем из els_state
// ============================================================
void DRV_Display_SendAll(void) {
    DRV_Display_SendMode(els.mode, els.submode);
    DRV_Display_SendFeed(els.feed, els.afeed);
    DRV_Display_SendPosition(els.pos_y, els.pos_x);
    DRV_Display_SendOtskok(els.otskok_y);
    DRV_Display_SendTension(els.tension_y);
    DRV_Display_SendRpm(els.spindle_rpm);
    DRV_Display_SendSelectMenu(1);
    DRV_Display_SendLimits(0, 0, 0, 0);
    DRV_Display_SendInt("DIAM_X", -(int32_t)els.pos_x);
    // Прочие поля (резьба, конус, сфера, делитель) — TODO при Этапах 7-8
}

// ============================================================
// Инициализация и обработка
// ============================================================
void DRV_Display_Init(void) {
    DispSerial.begin(DISP_UART_BAUD);
    s_rxlen = 0;
    // SendAll убран из Init: ESP32 пришлёт <READY> сам когда загрузится,
    // тогда _parse_rx вызовет DRV_Display_SendAll() через callback.
    // Отправка всего блока здесь блокирует UART если ESP32 ещё не слушает.
}

void DRV_Display_Process(void) {
    // Если ESP32 прислал READY/PONG и TX буфер свободен — отправить всё состояние
    if (s_need_sendall && DispSerial.availableForWrite() > 100) {
        s_need_sendall = 0;
        DRV_Display_SendAll();
    }

    while (DispSerial.available()) {
        char c = (char)DispSerial.read();
        if (c == '\n' || c == '\r') {
            if (s_rxlen > 0) {
                s_rxbuf[s_rxlen] = '\0';
                _parse_rx(s_rxbuf);
                s_rxlen = 0;
            }
        } else if (s_rxlen < DISP_RX_BUF_LEN - 1) {
            s_rxbuf[s_rxlen++] = c;
        }
    }
}

#else  // USE_ESP32_DISPLAY = 0

void DRV_Display_SetRxCallback(DispRxCallback_t cb) { (void)cb; }
void DRV_Display_Init(void) {}
void DRV_Display_Process(void) {}
void DRV_Display_SendCmd(const char* c, const char* p) { (void)c;(void)p; }
void DRV_Display_SendInt(const char* c, int32_t v) { (void)c;(void)v; }
void DRV_Display_SendInt2(const char* c, int32_t a, int32_t b) { (void)c;(void)a;(void)b; }
void DRV_Display_SendPosition(int32_t y, int32_t x) { (void)y;(void)x; }
void DRV_Display_SendMode(uint8_t m, uint8_t sm) { (void)m;(void)sm; }
void DRV_Display_SendFeed(int32_t f, int32_t af) { (void)f;(void)af; }
void DRV_Display_SendRpm(int32_t r) { (void)r; }
void DRV_Display_SendOtskok(int32_t o) { (void)o; }
void DRV_Display_SendTension(int32_t t) { (void)t; }
void DRV_Display_SendLimits(uint8_t l, uint8_t r, uint8_t f, uint8_t b) { (void)l;(void)r;(void)f;(void)b; }
void DRV_Display_SendAlert(int32_t t) { (void)t; }
void DRV_Display_SendSelectMenu(uint8_t m) { (void)m; }
void DRV_Display_SendAngle(int32_t a) { (void)a; }
void DRV_Display_SendAll(void) {}

#endif
