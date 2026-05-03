/**
 * gcode_exec.cpp
 * Исполнитель GCode: читает файл из LittleFS, отправляет строки в STM32
 * через callback, ждёт ACK (<OK> / <ERR:reason>).
 * Таймаут 5 сек без ACK — продолжить (режим тестирования без STM32).
 */

#include "gcode_exec.h"
#include <LittleFS.h>
#include <Arduino.h>

#define ACK_TIMEOUT_MS 5000

static GcSendFn  s_send_fn   = nullptr;
static GcExecSt  s_state     = GCE_IDLE;
static File*     s_file      = nullptr;  // указатель: нет глобального конструктора File
static long      s_file_size = 0;
static int       s_line_nr   = 0;
static uint32_t  s_ack_t     = 0;
static uint32_t  s_dwell_t   = 0;
static uint32_t  s_dwell_ms  = 0;
static char      s_filename[64] = {};
static char      s_error[80]    = {};

// ─── Утилиты ──────────────────────────────────────────────────────────────────

static void close_file() {
    if (s_file) {
        s_file->close();
        delete s_file;
        s_file = nullptr;
    }
}

// Убирает комментарии (;... и (...)), переводит в верхний регистр, триммирует.
// Возвращает true если строка непустая.
static bool preprocess(char* buf, size_t maxlen, const char* src) {
    size_t j = 0;
    bool in_paren = false;
    for (size_t i = 0; src[i] && j < maxlen - 1; i++) {
        char c = src[i];
        if (c == '(')              { in_paren = true;  continue; }
        if (c == ')')              { in_paren = false; continue; }
        if (in_paren)                continue;
        if (c == ';')                break;
        if (c == '\r' || c == '\n')  break;
        buf[j++] = (char)toupper((unsigned char)c);
    }
    buf[j] = '\0';

    // trim leading
    char* s = buf;
    while (*s == ' ' || *s == '\t') s++;
    if (s != buf) memmove(buf, s, strlen(s) + 1);

    // trim trailing
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\t')) buf[--len] = '\0';

    return len > 0;
}

// 0 = отправить в STM32, 1 = dwell G4, 2 = M0 пауза, 3 = M2/M30 конец
static int classify(const char* line, uint32_t* dwell_out) {
    const char* p = line;
    while (*p && *p != 'G' && *p != 'M') p++;
    if (!*p) return 0;

    if (*p == 'G') {
        int n = atoi(p + 1);
        if (n == 4) {
            const char* pp = strchr(p, 'P');
            float sec = pp ? atof(pp + 1) : 1.0f;
            *dwell_out = (uint32_t)(sec * 1000.0f);
            if (*dwell_out == 0) *dwell_out = 1000;
            return 1;
        }
    } else {
        int n = atoi(p + 1);
        if (n == 0)            return 2;
        if (n == 2 || n == 30) return 3;
    }
    return 0;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void GCodeExec_Init(GcSendFn fn) {
    s_send_fn = fn;
    s_state   = GCE_IDLE;
}

bool GCodeExec_Run(const char* filepath) {
    close_file();
    s_state    = GCE_IDLE;
    s_line_nr  = 0;
    s_error[0] = '\0';

    strncpy(s_filename, filepath, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';

    s_file = new File(LittleFS.open(filepath, "r"));
    if (!s_file || !(*s_file)) {
        delete s_file;
        s_file = nullptr;
        snprintf(s_error, sizeof(s_error), "File not found: %s", filepath);
        s_state = GCE_ERROR;
        Serial.printf("[gce] ERROR: %s\n", s_error);
        return false;
    }
    s_file_size = s_file->size();
    s_state = GCE_RUNNING;
    Serial.printf("[gce] Start: %s (%ld B)\n", filepath, s_file_size);
    return true;
}

void GCodeExec_Stop() {
    close_file();
    s_state = GCE_IDLE;
    Serial.println("[gce] Stopped");
}

void GCodeExec_TogglePause() {
    if (s_state == GCE_PAUSED) {
        s_state = GCE_RUNNING;
        Serial.println("[gce] Resumed");
    } else if (s_state == GCE_RUNNING || s_state == GCE_WAITING_ACK) {
        s_state = GCE_PAUSED;
        Serial.println("[gce] Paused");
    }
}

void GCodeExec_OnAck(bool ok, const char* err) {
    if (s_state != GCE_WAITING_ACK) return;
    if (ok) {
        s_state = GCE_RUNNING;
    } else {
        snprintf(s_error, sizeof(s_error), "STM32 ERR L%d: %s", s_line_nr, err ? err : "?");
        close_file();
        s_state = GCE_ERROR;
        Serial.printf("[gce] ERR: %s\n", s_error);
    }
}

void GCodeExec_Process() {
    switch (s_state) {

        case GCE_RUNNING: {
            if (!s_file || !s_file->available()) {
                close_file();
                s_state = GCE_DONE;
                Serial.printf("[gce] Done: %d lines\n", s_line_nr);
                return;
            }

            String raw = s_file->readStringUntil('\n');
            char proc[128];
            if (!preprocess(proc, sizeof(proc), raw.c_str())) return;

            s_line_nr++;
            uint32_t dwell_ms = 0;
            int cls = classify(proc, &dwell_ms);

            if (cls == 1) {
                s_dwell_ms = dwell_ms;
                s_dwell_t  = millis();
                s_state    = GCE_DWELL;
                Serial.printf("[gce] L%d: dwell %lu ms\n", s_line_nr, dwell_ms);
            } else if (cls == 2) {
                s_state = GCE_PAUSED;
                Serial.printf("[gce] L%d: M0 pause\n", s_line_nr);
            } else if (cls == 3) {
                close_file();
                s_state = GCE_DONE;
                Serial.printf("[gce] L%d: end program\n", s_line_nr);
            } else {
                if (s_send_fn) s_send_fn(proc);
                Serial.printf("[gce] L%d → %s\n", s_line_nr, proc);
                s_ack_t = millis();
                s_state = GCE_WAITING_ACK;
            }
            break;
        }

        case GCE_WAITING_ACK: {
            if (millis() - s_ack_t > ACK_TIMEOUT_MS) {
                Serial.printf("[gce] ACK timeout L%d → continue\n", s_line_nr);
                s_state = GCE_RUNNING;
            }
            break;
        }

        case GCE_DWELL: {
            if (millis() - s_dwell_t >= s_dwell_ms) s_state = GCE_RUNNING;
            break;
        }

        default: break;
    }
}

GcExecSt GCodeExec_GetState() { return s_state; }

int GCodeExec_GetProgress() {
    if (s_state == GCE_DONE) return 100;
    if (!s_file || s_file_size == 0) return 0;
    return (int)((long)s_file->position() * 100L / s_file_size);
}

int         GCodeExec_GetLine()     { return s_line_nr; }
const char* GCodeExec_GetFilename() { return s_filename; }
const char* GCodeExec_GetError()    { return s_error; }
