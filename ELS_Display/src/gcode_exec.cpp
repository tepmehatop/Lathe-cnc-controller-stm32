/**
 * gcode_exec.cpp
 * Исполнитель GCode: читает файл из LittleFS, отправляет строки в STM32
 * через callback, ждёт ACK (<OK> / <ERR:reason>).
 * Таймаут 5 сек без ACK — продолжить (режим тестирования без STM32).
 */

#include "gcode_exec.h"
#include "gcode_cycles.h"
#include <LittleFS.h>
#include <Arduino.h>

// Программа с циклами G71/G70 — развёрнутая копия: строки «номер исходной строки<TAB>кадр»
#define RUN_FILE      "/.run.nc"
#define CYCLES_MAX_B  65536

// Строка — одно движение на STM32: длинный проход идёт минутами. Без ответа 10 мин — продолжить (без STM32)
#define ACK_TIMEOUT_MS 600000

static GcSendFn  s_send_fn   = nullptr;
static GcExecSt  s_state     = GCE_IDLE;
static File*     s_file      = nullptr;  // указатель: нет глобального конструктора File
static long      s_file_size = 0;
static int       s_line_nr   = 0;
static bool      s_need_start = false;   // перед первой строкой — «%»: STM32 заново снимает опорную точку
static uint32_t  s_ack_t     = 0;
static uint32_t  s_dwell_t   = 0;
static uint32_t  s_dwell_ms  = 0;
static char      s_filename[64] = {};
static char      s_error[200]   = {};   // кириллица — 2 байта на букву: 80 обрезали сообщение STM32
static bool      s_expanded  = false;    // исполняется развёрнутая копия RUN_FILE
static int       s_step      = 0;        // кадров прочитано (у развёрнутой — номер её кадра; веб-рисунок)

// Оценка времени: s_cum[k] — расчётное время первых k кадров, с (GCodeTime_Lines). Пусто — файл больше 64 КБ
static std::vector<float> s_cum;
static double    s_spz = 400, s_spx = 800;   // шагов на мм Z / X — из настроек механики STM32 (CFG)
static int       s_rpm = 0;                  // текущие обороты (подача на оборот); 0 — стоит, расчёт при 600
static uint32_t  s_step_ms   = 0;        // начало текущего кадра
static uint32_t  s_act_ms    = 0;        // фактическое время выполненных кадров (без пауз)
static float     s_est_done  = 0;        // их расчётное время — поправка оценки по факту

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

// Строки программы для разворота циклов (как их считает исполнитель: без комментариев, пустые — выброшены)
static bool load_lines(const char* path, std::vector<std::string>& v) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    if (f.size() > CYCLES_MAX_B) { f.close(); return false; }
    char proc[128];
    while (f.available()) {
        String raw = f.readStringUntil('\n');
        if (preprocess(proc, sizeof(proc), raw.c_str())) v.push_back(proc);
    }
    f.close();
    return true;
}

// ─── Public API ───────────────────────────────────────────────────────────────

// Строки, которые получит STM32 (циклы развёрнуты; exp — с номерами исходных строк, пусто — циклов нет)
static bool prepare(const char* path, std::vector<std::string>& run, std::vector<GcLine>& exp, String& err) {
    run.clear(); exp.clear();
    if (!load_lines(path, run)) { err = "Файл не прочитан (или больше 64 КБ)"; return false; }
    if (!GCodeCycles_Needed(run)) return true;
    std::string e; int en = 0;
    if (!GCodeCycles_Expand(run, exp, e, en)) { err = String("Строка ") + en + ": " + e.c_str(); return false; }
    run.clear();
    for (const GcLine& l : exp) run.push_back(l.g);
    return true;
}

static int est_rpm() { return s_rpm > 0 ? s_rpm : 600; }

static float total_time(const std::vector<std::string>& run, std::vector<float>* cum) {
    std::vector<float> sec;
    GCodeTime_Lines(run, s_spz, s_spx, est_rpm(), sec);
    float t = 0;
    if (cum) { cum->assign(1, 0); cum->reserve(sec.size() + 1); }
    for (float v : sec) { t += v; if (cum) cum->push_back(t); }
    return t;
}

bool GCodeExec_Plan(const char* filepath, String& out, String& err) {
    std::vector<std::string> run;
    std::vector<GcLine> exp;
    out = ""; err = "";
    if (!prepare(filepath, run, exp, err)) return false;
    out.reserve(exp.size() * 24);
    for (const GcLine& l : exp) { out += l.n; out += '\t'; out += l.g.c_str(); out += '\n'; }
    return true;
}

bool GCodeExec_Estimate(const char* filepath, float& sec, int& rpm, String& err) {
    std::vector<std::string> run;
    std::vector<GcLine> exp;
    sec = 0; rpm = est_rpm(); err = "";
    if (!prepare(filepath, run, exp, err)) return false;
    sec = total_time(run, nullptr);
    return true;
}

void GCodeExec_SetMachine(double spz, double spx) { if (spz > 0) s_spz = spz; if (spx > 0) s_spx = spx; }
void GCodeExec_SetRpm(int rpm) { s_rpm = rpm > 0 ? rpm : 0; }   // не «последние»: при остановке бывает выброс

void GCodeExec_Init(GcSendFn fn) {
    s_send_fn = fn;
    s_state   = GCE_IDLE;
}

bool GCodeExec_Run(const char* filepath) {
    close_file();
    s_state    = GCE_IDLE;
    s_line_nr  = 0;
    s_step     = 0;
    s_error[0] = '\0';

    strncpy(s_filename, filepath, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';

    // циклы G71/G70 — развернуть заранее (контур P…Q нужен целиком) в RUN_FILE; расчётное время кадров
    s_expanded = false;
    s_cum.clear(); s_act_ms = 0; s_est_done = 0;
    {
        std::vector<std::string> run;
        std::vector<GcLine> exp;
        String perr;
        if (!prepare(filepath, run, exp, perr)) {
            if (LittleFS.exists(filepath) && perr.startsWith("Файл")) run.clear();   // большой файл — без оценки
            else {
                strncpy(s_error, perr.c_str(), sizeof(s_error) - 1);
                s_error[sizeof(s_error) - 1] = '\0';
                s_state = GCE_ERROR;
                Serial.printf("[gce] ERROR: %s\n", s_error);
                return false;
            }
        } else {
            float t = total_time(run, &s_cum);
            Serial.printf("[gce] расчётное время %.0f с (%d об/мин)\n", t, est_rpm());
        }
        if (!exp.empty()) {
            String plan;
            plan.reserve(exp.size() * 24);
            for (const GcLine& l : exp) { plan += l.n; plan += '\t'; plan += l.g.c_str(); plan += '\n'; }
            File o = LittleFS.open(RUN_FILE, "w");
            bool ok = o && o.print(plan) == plan.length();
            if (o) o.close();
            if (!ok) {
                snprintf(s_error, sizeof(s_error), "Нет места для развёрнутой программы (G71/G70)");
                s_state = GCE_ERROR;
                return false;
            }
            s_expanded = true;
            Serial.printf("[gce] G71/G70 развёрнуты: %u B\n", (unsigned)plan.length());
        }
    }
    s_file = new File(LittleFS.open(s_expanded ? RUN_FILE : filepath, "r"));
    if (!s_file || !(*s_file)) {
        delete s_file;
        s_file = nullptr;
        snprintf(s_error, sizeof(s_error), "Нет файла: %s", filepath);
        s_state = GCE_ERROR;
        Serial.printf("[gce] ERROR: %s\n", s_error);
        return false;
    }
    s_file_size = s_file->size();
    s_state = GCE_RUNNING;
    s_need_start = true;
    Serial.printf("[gce] Start: %s (%ld B)\n", filepath, s_file_size);
    return true;
}

void GCodeExec_Stop() {
    // STM32 исполняет строку — остановить движение (он ищет «STOP>» во входящих байтах)
    if (s_send_fn && (s_state == GCE_WAITING_ACK || s_state == GCE_RUNNING || s_state == GCE_PAUSED)) s_send_fn("STOP");
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

// кадр выполнен — его фактическое и расчётное время в поправку оценки
static void step_done() {
    if (s_step <= 0 || s_step >= (int)s_cum.size()) return;
    s_act_ms   += millis() - s_step_ms;
    s_est_done += s_cum[s_step] - s_cum[s_step - 1];
}

void GCodeExec_OnAck(bool ok, const char* err) {
    if (s_state != GCE_WAITING_ACK) return;
    if (ok) {
        step_done();
        s_state = GCE_RUNNING;
    } else {
        snprintf(s_error, sizeof(s_error), "Строка %d: %s", s_line_nr, err ? err : "?");
        close_file();
        s_state = GCE_ERROR;
        Serial.printf("[gce] ERR: %s\n", s_error);
    }
}

void GCodeExec_Process() {
    switch (s_state) {

        case GCE_RUNNING: {
            if (s_need_start) {                  // начало программы: опорная точка — с текущего положения
                s_need_start = false;
                if (s_send_fn) s_send_fn("%");
                s_ack_t = millis();
                s_state = GCE_WAITING_ACK;
                return;
            }
            if (!s_file || !s_file->available()) {
                close_file();
                s_state = GCE_DONE;
                Serial.printf("[gce] Done: %d lines\n", s_line_nr);
                return;
            }

            String raw = s_file->readStringUntil('\n');
            char proc[128];
            const char* src = raw.c_str();
            int orig = 0;
            if (s_expanded) {                    // «номер<TAB>кадр»
                orig = atoi(src);
                const char* t = strchr(src, '\t');
                src = t ? t + 1 : src;
            }
            if (!preprocess(proc, sizeof(proc), src)) return;

            s_step++;
            s_step_ms = millis();
            if (s_expanded) s_line_nr = orig; else s_line_nr++;
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
            if (millis() - s_dwell_t >= s_dwell_ms) { step_done(); s_state = GCE_RUNNING; }
            break;
        }

        default: break;
    }
}

GcExecSt GCodeExec_GetState() { return s_state; }

// Осталось по расчёту (без поправки), с: кадры после текущего + недоделанная часть текущего
static float remain_raw() {
    int n = (int)s_cum.size() - 1, k = s_step < n ? s_step : n;
    if (k < 0) k = 0;
    float rem = s_cum[n] - s_cum[k];
    if (k > 0 && (s_state == GCE_WAITING_ACK || s_state == GCE_DWELL)) {
        float cur = s_cum[k] - s_cum[k - 1], in = (millis() - s_step_ms) / 1000.0f;
        rem += cur > in ? cur - in : 0;
    }
    return rem;
}

int GCodeExec_GetProgress() {
    if (s_state == GCE_DONE) return 100;
    if (s_cum.size() > 1 && s_cum.back() > 0) {
        if (s_state == GCE_IDLE || s_state == GCE_ERROR) return 0;
        return (int)(100 * (1 - remain_raw() / s_cum.back()));
    }
    if (!s_file || s_file_size == 0) return 0;
    return (int)((long)s_file->position() * 100L / s_file_size);
}

int GCodeExec_GetEta() {
    if (s_state == GCE_DONE) return 0;
    if (s_cum.size() < 2 || s_state == GCE_IDLE || s_state == GCE_ERROR) return -1;
    float k = 1;                                         // поправка по факту: станок идёт медленнее/быстрее расчёта
    if (s_est_done > 20) { k = s_act_ms / 1000.0f / s_est_done; k = k < 0.5f ? 0.5f : (k > 3 ? 3 : k); }
    return (int)(remain_raw() * k + 0.5f);
}

int GCodeExec_GetTotal() { return s_cum.size() > 1 ? (int)(s_cum.back() + 0.5f) : -1; }

int         GCodeExec_GetLine()     { return s_line_nr; }
int         GCodeExec_GetStep()     { return s_step; }
const char* GCodeExec_GetFilename() { return s_filename; }
const char* GCodeExec_GetError()    { return s_error; }
