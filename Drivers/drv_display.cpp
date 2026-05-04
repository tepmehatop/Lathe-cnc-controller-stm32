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
#include "../Core/els_tables.h"
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
#define DISP_RX_BUF_LEN  128   // увеличен для GCode строк до ~80 символов
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
    char cmd_str[32]  = {};
    char prm_str[96]  = {};
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

    // GCODE: строка GCode — передаём в callback для исполнения
    if (strcmp(cmd_str, "GCODE") == 0) {
        DispRxCmd_t rx = {};
        rx.touch = TOUCH_GCODE;
        strncpy(rx.gcode_line, prm_str, sizeof(rx.gcode_line) - 1);
        if (s_rx_cb) s_rx_cb(&rx);
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
// Отправка с дропом если буфер занят (для частых позиций)
void DRV_Display_SendCmd(const char* cmd, const char* params) {
    // DBG: считаем drops для POS_Z
    static uint32_t dbg_posz_ok = 0, dbg_posz_drop = 0, dbg_t = 0;
    bool is_posz = (strcmp(cmd, "POS_Z") == 0);
    if (is_posz) {
        uint32_t now = millis();
        if (now - dbg_t >= 3000) {
            Serial.printf("[STM32] POS_Z ok=%lu drop=%lu buf=%d\n",
                          dbg_posz_ok, dbg_posz_drop, DispSerial.availableForWrite());
            dbg_posz_ok = 0; dbg_posz_drop = 0; dbg_t = now;
        }
    }
    if (is_posz) dbg_posz_ok++;
    // Все отправки приоритетные — ждём освобождения буфера (до 10мс)
    {
        uint32_t t = millis();
        while (DispSerial.availableForWrite() < 32 && (millis() - t) < 10) {}
    }
    DispSerial.print('<');
    DispSerial.print(cmd);
    if (params && params[0]) {
        DispSerial.print(':');
        DispSerial.print(params);
    }
    DispSerial.println('>');
}

// Приоритетная отправка — ждёт освобождения буфера (для режима/субменю/touch-ответов)
// Ожидание ограничено 10мс чтобы не блокировать loop надолго
static void _SendCmdPriority(const char* cmd, const char* params) {
    uint32_t t = millis();
    while (DispSerial.availableForWrite() < 32 && (millis() - t) < 10) { /* wait */ }
    DispSerial.print('<');
    DispSerial.print(cmd);
    if (params && params[0]) {
        DispSerial.print(':');
        DispSerial.print(params);
    }
    DispSerial.println('>');
}

static void _SendIntPriority(const char* cmd, int32_t val) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%ld", (long)val);
    _SendCmdPriority(cmd, buf);
}

void DRV_Display_SendInt(const char* cmd, int32_t val) {
    _SendIntPriority(cmd, val);
}

void DRV_Display_SendInt2(const char* cmd, int32_t a, int32_t b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld,%ld", (long)a, (long)b);
    _SendCmdPriority(cmd, buf);
}

// ============================================================
// Высокоуровневые функции отправки
// ============================================================

// Позиции: els.pos_y/pos_x хранятся с инвертированным знаком
// ESP32 ожидает оригинальный знак DRO → отправляем -pos
// Имена: POS_Z = продольная (Y), POS_X = поперечная (X)
void DRV_Display_SendPosition(int32_t pos_y, int32_t pos_x) {
    _SendIntPriority("POS_Z", -pos_y);
    _SendIntPriority("POS_X", -pos_x);
}

void DRV_Display_SendMode(uint8_t mode, uint8_t submode) {
    // ESP32 firmware ожидает 1-based (Mode_Feed=1), наш enum 0-based → +1
    // Приоритетная отправка — не дропать при занятом буфере
    _SendIntPriority("MODE",    (int32_t)mode + 1);
    _SendIntPriority("SUBMODE", (int32_t)submode + 1);
}

void DRV_Display_SendFeed(int32_t feed, int32_t afeed) {
    DRV_Display_SendInt("FEED",  feed);
    DRV_Display_SendInt("AFEED", afeed);
}

void DRV_Display_SendRpm(int32_t rpm) {
    _SendIntPriority("RPM", rpm);
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
    _SendIntPriority("SELECTMENU", (int32_t)menu);
}

// angle_mdeg: 0.001° → делим на 100 → дециградусы (как в Arduino)
void DRV_Display_SendAngle(int32_t angle_mdeg) {
    DRV_Display_SendInt("ANGLE", angle_mdeg / 100);
}

// ============================================================
// Полное состояние (при READY/PONG от ESP32)
// Полный аналог Arduino Display_Send_All() — все 31 поле.
// ============================================================
void DRV_Display_SendAll(void) {
    // 1. Режим и подрежим
    DRV_Display_SendMode(els.mode, els.submode);

    // 2. Подачи
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);

    // 3. Данные резьбы (имя из таблицы, шаг, RPM лимит, ход, циклов)
    {
        uint8_t ts = els.Thread_Step;
        if (ts >= TOTAL_THREADS) ts = 0;
        DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
        DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
        DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
        int16_t ph = (els.Ph > 0) ? els.Ph : 1;
        DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
        int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
        DRV_Display_SendInt("THREAD_CYCL",  cycl);
    }

    // 4. Позиции (знак инвертирован как в Arduino)
    DRV_Display_SendPosition(els.pos_y, els.pos_x);

    // 5. Шар: радиус и заходы
    DRV_Display_SendInt("SPHERE",    els.Sph_R_mm);
    DRV_Display_SendInt2("PASS",     els.Pass_Nr, els.Pass_Total);

    // 6. Съём за проход, заходы резьбы
    DRV_Display_SendInt("AP",   els.Ap);
    DRV_Display_SendInt("PH",   (els.Ph > 0) ? els.Ph : 1);

    // 7. Угол шпинделя (делитель)
    DRV_Display_SendAngle((int32_t)els.Spindle_Angle);

    // 8. Конус: индекс и угол
    {
        uint8_t cs = els.Cone_Step;
        if (cs >= TOTAL_CONE_ANGLES) cs = TOTAL_CONE_ANGLES - 1;
        DRV_Display_SendInt("CONE",       cs);
        DRV_Display_SendInt("CONE_ANGLE", Cone_Angle_x10[cs]);
    }

    // 9. Делитель
    DRV_Display_SendInt("DIVN", (int32_t)els.Total_Tooth);
    DRV_Display_SendInt("DIVM", (int32_t)els.Current_Tooth);

    // 10. Шар доп.
    DRV_Display_SendInt("BAR",       els.Bar_R_mm);
    DRV_Display_SendInt("PASS_SPHR", els.Pass_Total_Sphr);

    // 11. Активный подэкран
    DRV_Display_SendSelectMenu(els.select_menu);

    // 12. Отскок/натяг Z (в 0.001мм × 10 как в Arduino)
    DRV_Display_SendOtskok((int32_t)els.OTSKOK_Z_mm * 10L);
    DRV_Display_SendTension((int32_t)els.TENSION_Z_mm * 10L);

    // 13. Диаметр заготовки (совпадает с Arduino: -(MSize_X_mm * 10))
    DRV_Display_SendInt("DIAM_X", -(int32_t)els.MSize_X_mm * 10L);

    // 14. Чистовых проходов
    DRV_Display_SendInt("PASS_FIN", (int32_t)PASS_FINISH + els.Pass_Fin);

    // 15. Коническая резьба флаг (M4/M5 SM=2)
    DRV_Display_SendInt("CONE_THR", (els.ConL_Thr_flag || els.ConR_Thr_flag) ? 1 : 0);

    // 16. Ширина резца и шаг оси Z (M6 SM=2)
    DRV_Display_SendInt("CUTTER_W",  els.Cutter_Width);
    DRV_Display_SendInt("CUTTING_W", els.Cutting_Width);

    // 17. Лимиты
    DRV_Display_SendLimits(0, 0, 0, 0);

    // 18. RPM
    DRV_Display_SendRpm(els.spindle_rpm);
}

// ============================================================
// Дублирование состояния LCD2004 через ESP32 Serial
// ============================================================
void DRV_Display_SendLCD2004State(void) {
    char buf[80];
    int16_t ph = (els.Ph > 0) ? els.Ph : 1;
    uint8_t ts = (els.Thread_Step < TOTAL_THREADS) ? els.Thread_Step : 0;
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
        (int)els.mode,
        (int)els.submode,
        (int)els.select_menu,
        (int)els.Feed_mm,
        (int)els.aFeed_mm,
        (int)els.Ap,
        (int)els.Pass_Nr,
        (int)els.Pass_Total,
        (int)ph,
        (int)((int32_t)PASS_FINISH + els.Pass_Fin),
        (int)ts,
        (int)els.Total_Tooth,
        (int)els.Current_Tooth
    );
    DRV_Display_SendCmd("LCD2004", buf);
}

// ============================================================
// GCode ACK: отправить <OK> или <ERR:reason> в ESP32
// ============================================================
void DRV_Display_SendGCodeAck(bool ok, const char* err) {
    if (ok) {
        _SendCmdPriority("OK", nullptr);
    } else {
        _SendCmdPriority("ERR", err ? err : "error");
    }
}

// ============================================================
// Инициализация и обработка
// ============================================================
void DRV_Display_Init(void) {
    DispSerial.begin(DISP_UART_BAUD);
    s_rxlen = 0;
    // При перезагрузке STM32 ESP32 может быть уже запущен и не пришлёт READY повторно.
    // Флаг s_need_sendall гарантирует отправку полного состояния при старте STM32
    // вне зависимости от того, перезагружался ли ESP32.
    s_need_sendall = 1;
}

void DRV_Display_Process(void) {
    // Если ESP32 прислал READY/PONG — отправить полное состояние.
    // Порог 32 (не 100): при буфере 64-256 байт порог 100 мог не достигаться.
    // SendAll содержит ~28 сообщений × ~20 байт; неприоритетные сообщения
    // отправятся быстро т.к. UART 57600 baud = ~5.76 байт/мс.
    if (s_need_sendall && DispSerial.availableForWrite() > 32) {
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
void DRV_Display_SendLCD2004State(void) {}

#endif
