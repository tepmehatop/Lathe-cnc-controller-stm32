/**
 * @file  els_serial.cpp
 * @brief Serial Monitor команды ELS — Этап 9
 *
 * Парсит ASCII команды из USB CDC (ST-Link VCP, USART1, 115200/8N1).
 * Позволяет управлять станком и менять параметры без перепрошивки.
 */

#include "els_serial.h"
#include "els_state.h"
#include "els_config.h"
#include "els_control.h"
#include "../Drivers/drv_stepper.h"
#include "../Drivers/drv_encoder.h"
#include "../Drivers/drv_display.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Буфер приёма
// ============================================================
#define SERIAL_BUF_LEN  64
static char    s_buf[SERIAL_BUF_LEN];
static uint8_t s_len = 0;

// ============================================================
// Вспомогательные функции вывода
// ============================================================
static void _println(const char* s) { Serial.println(s); }

static void _print_mm(const char* label, int32_t val_001mm) {
    // Вывести значение в мм с тремя знаками после запятой
    char buf[24];
    int32_t abs_val = (val_001mm < 0) ? -val_001mm : val_001mm;
    snprintf(buf, sizeof(buf), "%s%s%ld.%03ld mm",
        label,
        (val_001mm < 0) ? "-" : "",
        (long)(abs_val / 1000L),
        (long)(abs_val % 1000L));
    _println(buf);
}

// ============================================================
// Вывод HELP
// ============================================================
static void _cmd_help(void) {
    _println("--- ELS STM32 Commands ---");
    _println("  HELP              List commands");
    _println("  INFO              Show current state");
    _println("  START             Start movement");
    _println("  STOP              Stop movement");
    _println("  MODE:N            Set mode (0=FEED 1=AFEED 2=THREAD 3=CONE)");
    _println("  FEED:N            Set feed mm/rev x100 (5..200)");
    _println("  AFEED:N           Set async feed mm/min x100 (5..300)");
    _println("  PITCH:N           Set thread pitch 0.001mm (100..5000)");
    _println("  STARTS:N          Set thread starts (1..8)");
    _println("  ZERO:Y            Zero Y (longitudinal) position");
    _println("  ZERO:X            Zero X (cross) position");
    _println("  LIMIT:ON          Enable limits");
    _println("  LIMIT:OFF         Disable limits");
    _println("  55                Test preset: MODE_FEED, 300RPM, 0.20mm/rev");
    _println("--------------------------");
}

// ============================================================
// Вывод INFO
// ============================================================
static void _cmd_info(void) {
    char buf[48];
    const char* mode_str[] = {"FEED","AFEED","THREAD","CONE","SPHERE","DIVIDER","--","--"};
    const char* sub_str[]  = {"Internal","Manual","External"};

    _println("--- ELS State ---");
    snprintf(buf, sizeof(buf), "  Mode:    %s (%d)", mode_str[els.mode & 7], els.mode);
    _println(buf);
    snprintf(buf, sizeof(buf), "  Submode: %s", sub_str[els.submode < 3 ? els.submode : 0]);
    _println(buf);
    snprintf(buf, sizeof(buf), "  Running: %s", els.running ? "YES" : "NO");
    _println(buf);
    snprintf(buf, sizeof(buf), "  RPM:     %ld", (long)els.spindle_rpm);
    _println(buf);
    snprintf(buf, sizeof(buf), "  Feed:    %ld.%02ld mm/rev",
        (long)(els.feed / 100L), (long)(abs((int)(els.feed % 100))));
    _println(buf);
    snprintf(buf, sizeof(buf), "  AFeed:   %ld.%02ld mm/min",
        (long)(els.afeed / 100L), (long)(abs((int)(els.afeed % 100))));
    _println(buf);
    snprintf(buf, sizeof(buf), "  Pitch:   %ld.%03ld mm  Starts: %d",
        (long)(els.thread_pitch / 1000L),
        (long)(els.thread_pitch % 1000L),
        (int)els.thread_starts);
    _println(buf);
    _print_mm("  Pos Y:   ", els.pos_y);
    _print_mm("  Pos X:   ", els.pos_x);
    snprintf(buf, sizeof(buf), "  Limits:  %s", els.limits_enabled ? "ON" : "OFF");
    _println(buf);
    _println("-----------------");
}

// ============================================================
// Обработчик команды
// ============================================================
static void _dispatch(char* line) {
    // Убираем пробелы в конце
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    // Разделить на CMD и param по ':'
    char* colon = strchr(line, ':');
    char  cmd[32] = {};
    char  prm[32] = {};
    if (colon) {
        int clen = (int)(colon - line);
        if (clen >= (int)sizeof(cmd)) clen = (int)sizeof(cmd) - 1;
        memcpy(cmd, line, (size_t)clen);
        strncpy(prm, colon + 1, sizeof(prm) - 1);
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
    }

    // Привести к верхнему регистру
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] -= 32;
    }

    // ---- Обработка команд ----
    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        _cmd_help();

    } else if (strcmp(cmd, "INFO") == 0) {
        _cmd_info();

    } else if (strcmp(cmd, "START") == 0) {
        ELS_Control_Start();
        _println("[ELS] Started");

    } else if (strcmp(cmd, "STOP") == 0) {
        ELS_Control_Stop();
        _println("[ELS] Stopped");

    } else if (strcmp(cmd, "MODE") == 0) {
        int n = atoi(prm);
        if (n >= 0 && n <= 7) {
            ELS_Control_Stop();
            els.mode = (ELS_Mode_t)n;
#if USE_ESP32_DISPLAY
            DRV_Display_SendMode(els.mode, els.submode);
#endif
            char buf[32];
            snprintf(buf, sizeof(buf), "[ELS] Mode = %d", n);
            _println(buf);
        } else {
            _println("[ERR] Mode 0..7");
        }

    } else if (strcmp(cmd, "FEED") == 0) {
        int32_t v = atol(prm);
        if (v >= MIN_FEED && v <= MAX_FEED) {
            els.feed = v;
#if USE_ESP32_DISPLAY
            DRV_Display_SendFeed(els.feed, els.afeed);
#endif
            char buf[32];
            snprintf(buf, sizeof(buf), "[ELS] Feed = %ld.%02ld mm/rev",
                (long)(v/100), (long)(v%100));
            _println(buf);
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "[ERR] Feed %d..%d", MIN_FEED, MAX_FEED);
            _println(buf);
        }

    } else if (strcmp(cmd, "AFEED") == 0) {
        int32_t v = atol(prm);
        if (v >= MIN_AFEED && v <= MAX_AFEED) {
            els.afeed = v;
#if USE_ESP32_DISPLAY
            DRV_Display_SendFeed(els.feed, els.afeed);
#endif
            char buf[32];
            snprintf(buf, sizeof(buf), "[ELS] AFeed = %ld.%02ld mm/min",
                (long)(v/100), (long)(v%100));
            _println(buf);
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "[ERR] AFeed %d..%d", MIN_AFEED, MAX_AFEED);
            _println(buf);
        }

    } else if (strcmp(cmd, "PITCH") == 0) {
        int32_t v = atol(prm);
        if (v >= 100 && v <= 5000) {
            els.thread_pitch = v;
            char buf[40];
            snprintf(buf, sizeof(buf), "[ELS] Pitch = %ld.%03ld mm",
                (long)(v/1000), (long)(v%1000));
            _println(buf);
        } else {
            _println("[ERR] Pitch 100..5000 (0.001mm)");
        }

    } else if (strcmp(cmd, "STARTS") == 0) {
        int n = atoi(prm);
        if (n >= 1 && n <= (int)MAX_STARTS) {
            els.thread_starts = (uint8_t)n;
            char buf[32];
            snprintf(buf, sizeof(buf), "[ELS] Starts = %d", n);
            _println(buf);
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "[ERR] Starts 1..%d", MAX_STARTS);
            _println(buf);
        }

    } else if (strcmp(cmd, "ZERO") == 0) {
        if (prm[0] == 'Y' || prm[0] == 'y') {
            els.pos_y = 0;
            _println("[ELS] Zero Y");
        } else if (prm[0] == 'X' || prm[0] == 'x') {
            els.pos_x = 0;
            _println("[ELS] Zero X");
        } else {
            _println("[ERR] ZERO:Y or ZERO:X");
        }
#if USE_ESP32_DISPLAY
        DRV_Display_SendPosition(els.pos_y, els.pos_x);
#endif

    } else if (strcmp(cmd, "LIMIT") == 0) {
        if (prm[0] == 'O' && prm[1] == 'N') {
            els.limits_enabled = 1;
            _println("[ELS] Limits ON");
        } else {
            els.limits_enabled = 0;
            _println("[ELS] Limits OFF");
        }

    } else if (strcmp(cmd, "55") == 0 || strcmp(line, "55") == 0) {
        // Тестовый пресет
        ELS_Control_Stop();
        els.mode  = MODE_FEED;
        els.feed  = 20; // 0.20 мм/об
        // Шпиндель не меняем — используем реальный RPM из энкодера
        _println("[ELS] Test preset: FEED 0.20mm/rev, START when spindle turns");
        ELS_Control_Start();

    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "[ERR] Unknown: %s", cmd);
        _println(buf);
    }
}

// ============================================================
// Публичный API
// ============================================================
void ELS_Serial_Init(void) {
    s_len = 0;
    // Serial.begin уже вызван в ELS_Init
}

void ELS_Serial_Process(void) {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
                _dispatch(s_buf);
                s_len = 0;
            }
        } else if (c >= 0x20 && s_len < SERIAL_BUF_LEN - 1) {
            s_buf[s_len++] = c;
        }
    }
}
