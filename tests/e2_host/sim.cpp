/**
 * Проверка ядра 7e2 + аппаратного слоя STM32 (Core/e2) на компьютере.
 *
 * Собираются настоящие e2_core.cpp (скетч 7e2), e2_hal.cpp (таймеры, прерывания)
 * и e2_lcd.cpp. Регистры GPIO/таймеров — в памяти (mock/Arduino.h), время —
 * виртуальное. Симулятор крутит энкодер шпинделя и РГИ, отсчитывает периоды
 * TIM6/TIM7/TIM12 и вызывает те же обработчики, что и прерывания на плате.
 * Шаги считаются по фронтам на выводах STEP — как их увидел бы драйвер.
 *
 * Запуск: bash tools/run_e2_sim.sh
 */
#include "Arduino.h"
#include "HardwareTimer.h"
#include "Wire.h"
#include "../../Core/els_config.h"
#include "../../Core/e2/e2_pins.h"
#include "../../Core/e2/e2_hal.h"
#include "../../Core/e2/e2_lcd.h"
#include <vector>
#include <functional>

// ── Глобальные объекты имитации ──────────────────────────────────────────────
uint64_t sim_now_us = 0;
GPIO_TypeDef g_gpio[5];
TIM_TypeDef g_tim6, g_tim7, g_tim12;
HardwareSerial Serial;
TwoWire Wire;
DWT_Type g_dwt; CoreDebug_Type g_coredebug;
static uint64_t t_ns_for_cyc();
static uint64_t s_cyc_extra = 0;
uint32_t sim_cyccnt_read() { s_cyc_extra += 4; return (uint32_t)(t_ns_for_cyc() * 168 / 1000 + s_cyc_extra); }
// Длительности уровней на STEP в тактах ядра (по фактическим записям в регистры)
struct EdgeLog { uint64_t last = 0; uint64_t min_cyc = ~0ull; long n = 0; };
static EdgeLog s_ez, s_ex;
static long s_tacho_rises = 0;
void sim_gpio_changed(GPIO_TypeDef* g, uint32_t o, uint32_t n) {
    uint64_t c = t_ns_for_cyc() * 168 / 1000 + s_cyc_extra;
    auto chk = [&](EdgeLog& l, uint32_t bit) {
        if (((o ^ n) & bit) == 0) return;
        if (l.last && c - l.last < l.min_cyc) l.min_cyc = c - l.last;
        l.last = c; l.n++;
    };
    if (g == E2_GPIO(E2_PIN_Z_STEP)) chk(s_ez, E2_BIT(E2_PIN_Z_STEP));
    if (g == E2_GPIO(E2_PIN_X_STEP)) chk(s_ex, E2_BIT(E2_PIN_X_STEP));
    if (g == E2_GPIO(E2_PIN_TACHO) && (n & E2_BIT(E2_PIN_TACHO)) && !(o & E2_BIT(E2_PIN_TACHO))) s_tacho_rises++;
}
static SimTimer s_timers[3] = {{&g_tim6, 0, nullptr}, {&g_tim7, 0, nullptr}, {&g_tim12, 0, nullptr}};
SimTimer* sim_timer_slot(TIM_TypeDef* t) { for (auto& s : s_timers) if (s.tim == t) return &s; return nullptr; }
static void (*s_exti[80])(void);
void attachInterrupt(int pin, void (*cb)(void), int) { s_exti[pin] = cb; }
void pinMode(int, int) {}
static int s_adc = 512;
int analogRead(int) { return s_adc; }

// Скетч 7e2 целиком — одна единица трансляции, как на плате
#include "../../Core/e2/e2_core.cpp"
static double Thread_Info_Ks(int i) { return Thread_Info[i].Ks_Div_Z; }
static double Thread_Info_Km(int i) { return Thread_Info[i].Km_Div_Z; }

// ── Движок событий (время в нс) ──────────────────────────────────────────────
static uint64_t t_ns = 0;
static uint64_t t_ns_for_cyc() { return t_ns; }
static const uint32_t CNT_SENTINEL = 0xFFFFFFFFu;
struct TimState { uint64_t t_cnt0; };
static TimState s_ts[3];

static double   spindle_rpm = 0;      // > 0 — CW, < 0 — CCW
static uint64_t spindle_next = 0;
static int      spindle_phase = 0;    // 0: A↓, 1: B↑, 2: A↑, 3: B↓ (для CW)
static long     spindle_lines = 0;    // пройдено линий (со знаком)

static uint64_t loop_next = 0;
static const uint64_t LOOP_PERIOD_NS = 20000;   // loop() раз в 20 мкс
static int      in_loop = 0;
static int      s_cur_ev = -1;

// Фронты STEP
struct StepLog { long count = 0; uint32_t prev = 0; uint64_t last_edge = 0; uint64_t min_level_ns = ~0ull; };
static StepLog s_z, s_x;
static std::vector<long> s_z_at_rev;   // счётчик шагов Z на каждом обороте шпинделя
static long spindle_lines_now();
extern int McStep_Lag;
struct MoveStart { long line; long x; int dir; long z; long pass_nr; std::string lcd_row2; std::string disp_pass; double cx, cz; };
static std::vector<MoveStart> s_z_starts;   // начала движений Z (после паузы ≥ 50 мс)
static uint64_t s_z_last_step = 0;
struct XTurn { long z; long x; int lag; };
static std::vector<XTurn> s_x_turns;   // точки, где X сменил направление с «вперёд» на «назад»
static int s_x_last_dir = -1;

#define PIN_BIT(...) E2_BIT(__VA_ARGS__)
static void set_in(GPIO_TypeDef* g, uint32_t bit, int level) { if (level) g->IDR |= bit; else g->IDR &= ~bit; }
#define SET_IN(pin, lvl) set_in(E2_GPIO(pin), E2_BIT(pin), (lvl))

// Огибающая движения за автоцикл: крайние положения кареток на каждом шаге (проверка упоров)
static bool s_env_on = false;
static long s_env_zmin, s_env_zmax, s_env_xmin, s_env_xmax;
static double s_env_ox = 0, s_env_oz = 0;          // каретка (модель люфта) → координаты прошивки
static double s_car_x, s_car_z;
static double car_x() { return s_car_x + s_env_ox; }
static double car_z() { return s_car_z + s_env_oz; }
static std::string lcd_row(int r);
static std::string last_disp_pass();

// Механика с люфтом (SIM_BACKLASH): каретка отстаёт от гайки винта на люфт при смене направления.
// Гайка — счёт импульсов STEP на выводе драйвера (столько повернулся винт), каретка — где на самом
// деле резец. Motor_*_Pos — то, что думает прошивка (с компенсацией люфта — положение каретки).
static long s_bl_x = 0, s_bl_z = 0;              // люфт, шаги (0 — без люфта)
static long s_lost_x = 0;                        // пропущенные шаги X: мотор не провернулся (каретка отстала)
extern int32_t g_stub_dro_x, g_stub_dro_y;
// положение каретки, шаги (s_car_x/s_car_z объявлены выше)
static double s_car_x_max = -1e18, s_car_x_min = 1e18;
static int s_nut_dir_x = 0, s_nut_dir_z = 0;     // направление последнего движения гайки
static void backlash_follow() {
    static long px = 0, pz = 0;
    if (s_x.count != px) {
        int nd = s_x.count > px ? 1 : -1;
        if (nd != s_nut_dir_x && getenv("SIM_BLTRACE"))
            printf("      [X разворот] t=%.3f имп=%ld Motor_X=%ld каретка=%.0f loaded=%d Null_X=%ld\n",
                   t_ns / 1e9, s_x.count, Motor_X_Pos, s_car_x + s_env_ox, e2_bl_loaded_x, Null_X_Pos);
        s_nut_dir_x = nd; px = s_x.count;
    }
    if (s_z.count != pz) { s_nut_dir_z = s_z.count > pz ? 1 : -1; pz = s_z.count; }
    auto f = [](double& c, long m, long b) {
        if (m > c + b / 2.0) c = m - b / 2.0;
        else if (m < c - b / 2.0) c = m + b / 2.0;
    };
    f(s_car_x, s_x.count, s_bl_x); f(s_car_z, s_z.count, s_bl_z);
    if (s_car_x > s_car_x_max) s_car_x_max = s_car_x;
    if (s_car_x < s_car_x_min) s_car_x_min = s_car_x;
    // Блок DRO показывает каретку (не гайку): люфт и пропущенные шаги в нём видны; пакеты — 50 Гц
    static uint64_t s_dro_t = 0;
    if (t_ns - s_dro_t >= 20000000ULL) {
        s_dro_t = t_ns;
        g_stub_dro_x = lround((s_car_x - s_lost_x) * (SCREW_X * 10.0 / (MOTOR_X_STEP_PER_REV * McSTEP_X)));
        g_stub_dro_y = lround(s_car_z * (SCREW_Z * 10.0 / (MOTOR_Z_STEP_PER_REV * McSTEP_Z)));
    }
}

long e2_dbg_pulses_x(void);
static void sample_steps() {
    backlash_follow();
    auto chk = [](StepLog& l, uint32_t lvl, uint32_t dir_ccw) {
        if (lvl != l.prev) {
            uint64_t w = t_ns - l.last_edge;
            if (l.last_edge && w < l.min_level_ns) l.min_level_ns = w;
            l.last_edge = t_ns;
            if (!lvl) l.count += dir_ccw ? -1 : 1;     // шаг — по спаду (Motor_*_SetPulse = 0)
            l.prev = lvl;
        }
    };
    long before = s_z.count;
    chk(s_z, E2_DRV_RD(E2_PIN_Z_STEP), E2_DRV_RD(E2_PIN_Z_DIR));
    if (s_z.count != before) {
        if (t_ns - s_z_last_step > 50000000ull)
            s_z_starts.push_back({spindle_lines_now(), Motor_X_Pos, (int)E2_DRV_RD(E2_PIN_Z_DIR), Motor_Z_Pos,
                                  Pass_Nr, lcd_row(2), last_disp_pass(), car_x(), car_z()});
        s_z_last_step = t_ns;
    }
    long xb = s_x.count;
    chk(s_x, E2_DRV_RD(E2_PIN_X_STEP), E2_DRV_RD(E2_PIN_X_DIR));
    static long s_xpos_prev = 0;
    if (s_x.count != xb && Motor_X_Pos == s_xpos_prev && getenv("SIM_XTRACE"))
        printf("      X: спад без счёта t=%.6f en5b=%d en2b=%d en4b=%d en5a=%d jz=%d jx=%d stepX=%d\n", t_ns / 1e9, e2_en_t5b, e2_en_t2b, e2_en_t4b, e2_en_t5a, Joy_Z_flag, Joy_X_flag, Step_X_flag);
    if (s_x.count == xb && Motor_X_Pos != s_xpos_prev && getenv("SIM_XTRACE"))
        printf("      X: счёт без импульса t=%.6f %ld→%ld in_loop=%d ev=%d en3b=%d en4b=%d pin=%u ARR12=%u\n", t_ns / 1e9, s_xpos_prev, Motor_X_Pos, in_loop, s_cur_ev, e2_en_t3b, e2_en_t4b, (unsigned)E2_ODR(E2_PIN_X_STEP), (unsigned)TIM12->ARR);
    s_xpos_prev = Motor_X_Pos;
    if (s_env_on) {                                  // по реальной каретке (модель люфта)
        long cz = lround(car_z()), cx = lround(car_x());
        if (cz < s_env_zmin) s_env_zmin = cz;
        if (cz > s_env_zmax) s_env_zmax = cz;
        if (cx < s_env_xmin) s_env_xmin = cx;
        if (cx > s_env_xmax) s_env_xmax = cx;
    }
    if (s_x.count != xb) {
        int d = (int)E2_DRV_RD(E2_PIN_X_DIR);
        if (s_x_last_dir == 0 && d == 1) s_x_turns.push_back({Motor_Z_Pos, Motor_X_Pos, McStep_Lag});
        s_x_last_dir = d;
    }
}

static void check_cnt_resets() {
    for (int i = 0; i < 3; i++) {
        if (s_timers[i].tim->CNT == 0) s_ts[i].t_cnt0 = t_ns;
        s_timers[i].tim->CNT = CNT_SENTINEL;
    }
}

static uint64_t tim_next(int i) {
    uint64_t tick = s_timers[i].tick_ns;
    uint64_t n = s_ts[i].t_cnt0 + (uint64_t)(s_timers[i].tim->ARR + 1) * tick;
    if (n < t_ns) { n = s_ts[i].t_cnt0 + 65536ull * tick;   // ARR уменьшили ниже CNT — счёт до переполнения
    }
    return n;
}

static long spindle_lines_now() { return ((spindle_lines % ENC_LINE_PER_REV) + ENC_LINE_PER_REV) % ENC_LINE_PER_REV; }

static void spindle_edge() {
    bool cw = spindle_rpm > 0;
    int ph = spindle_phase;
    // CW: A↓ B↑ A↑ B↓ ; CCW — обратный порядок
    static const int seq_cw[4][2] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}};   // {канал(0=A,1=B), уровень}
    int ch, lvl;
    if (cw) { ch = seq_cw[ph][0]; lvl = seq_cw[ph][1]; spindle_phase = (ph + 1) & 3; }
    else    { int p = (ph + 3) & 3; ch = seq_cw[p][0]; lvl = !seq_cw[p][1]; spindle_phase = p; }
    if (ch == 0) SET_IN(E2_PIN_ENC_A, lvl); else SET_IN(E2_PIN_ENC_B, lvl);
    if (ch == 0 && s_exti[E2_APIN(E2_PIN_ENC_A)]) s_exti[E2_APIN(E2_PIN_ENC_A)]();
    if (cw && ph == 3) { spindle_lines++; if (spindle_lines % ENC_LINE_PER_REV == 0) s_z_at_rev.push_back(s_z.count); }
    if (!cw && spindle_phase == 0) spindle_lines--;
}

// Отложенная строка с дисплея: придёт в UART в момент s_later_t (посреди блокирующего движения)
static std::string s_later;
static uint64_t s_later_t = 0;
static void inject_later(const std::string& str, double ms) { s_later = str; s_later_t = t_ns + (uint64_t)(ms * 1e6); }

// Продвинуть время до t_end, обрабатывая события
static void run_until(uint64_t t_end, bool allow_loop) {
    for (;;) {
        if (!s_later.empty() && t_ns >= s_later_t) { if (getenv("SIM_LATERDBG")) printf("      [later] t=%.3f %s", t_ns / 1e9, s_later.c_str()); DISPLAY_UART.inject(s_later); s_later.clear(); }
        uint64_t nxt = t_end; int ev = -1;
        for (int i = 0; i < 3; i++) if (s_timers[i].cb) {
            if (!s_ts[i].t_cnt0) s_ts[i].t_cnt0 = t_ns;       // таймер только что запущен
            uint64_t n = tim_next(i); if (n <= nxt) { nxt = n; ev = i; }
        }
        if (spindle_rpm != 0 && spindle_next <= nxt) { nxt = spindle_next; ev = 3; }
        if (allow_loop && loop_next <= nxt) { nxt = loop_next; ev = 4; }
        if (ev < 0) { if (t_end > t_ns) t_ns = t_end; sim_now_us = t_ns / 1000; sample_steps(); return; }
        if (nxt > t_ns) t_ns = nxt;            // событие могло «опоздать» из-за delay() внутри loop()
        sim_now_us = t_ns / 1000;
        s_cur_ev = ev;
        if (ev < 3) { s_ts[ev].t_cnt0 = t_ns; s_timers[ev].cb(); }
        else if (ev == 3) {
            spindle_edge();
            double q_ns = 60e9 / (fabs(spindle_rpm) * ENC_LINE_PER_REV * 4);
            spindle_next = t_ns + (uint64_t)q_ns;
        } else {
            int lag0 = McStep_Lag;
            in_loop++; loop(); in_loop--;
            loop_next = t_ns + LOOP_PERIOD_NS;
            if (getenv("SIM_LAG") && McStep_Lag != lag0 && McStep_Lag < 40)
                printf("      t=%.3f lag=%d Z=%ld X=%ld rear=%ld right=%ld stepZ=%d stepX=%d cyc=%d pass=%ld LP=%ld LPL=%ld OCR5A=%u en5a=%d fz=%d jz=%d dir=%d\n", t_ns / 1e9, McStep_Lag, Motor_Z_Pos, Motor_X_Pos, Limit_Pos_Rear, Limit_Pos_Right, Step_Z_flag, Step_X_flag, cycle_flag, Pass_Nr, (long)Limit_Pos, Limit_Pos_Left, (unsigned)OCR5A, e2_en_t5a, feed_Z_flag, Joy_Z_flag, Motor_Z_Dir);
        }
        check_cnt_resets();
        sample_steps();
    }
}

void sim_delay_us(uint64_t us) { run_until(t_ns + us * 1000, false); }
static void run_ms(double ms) { run_until(t_ns + (uint64_t)(ms * 1e6), true); }
static void spindle(double rpm) { spindle_rpm = rpm; spindle_next = t_ns + 1000; }

// РГИ: n щелчков (знак — направление), по period_ms на щелчок
static void hand_turn(int n, double period_ms) {
    int dir = n > 0 ? 1 : -1;
    for (int k = 0; k < abs(n); k++) {
        // +: A↑ при B=0 → ++ ; −: A↓ при B=0 → −−
        auto edgeA = [](int lvl) { SET_IN(E2_PIN_HAND_A, lvl); if (s_exti[E2_APIN(E2_PIN_HAND_A)]) s_exti[E2_APIN(E2_PIN_HAND_A)](); };
        if (dir > 0) { edgeA(1); run_ms(period_ms / 4); SET_IN(E2_PIN_HAND_B, 1); run_ms(period_ms / 4); edgeA(0); run_ms(period_ms / 4); SET_IN(E2_PIN_HAND_B, 0); run_ms(period_ms / 4); }
        else         { edgeA(0); run_ms(period_ms / 4); SET_IN(E2_PIN_HAND_B, 1); run_ms(period_ms / 4); edgeA(1); run_ms(period_ms / 4); SET_IN(E2_PIN_HAND_B, 0); run_ms(period_ms / 4); }
    }
}

static void touch(const char* cmd) { DISPLAY_UART.inject(std::string("<TOUCH:") + cmd + ">\n"); run_ms(5); }

// ── Мини-фреймворк ───────────────────────────────────────────────────────────
static int s_pass = 0, s_fail = 0;
#define CHECK(cond, ...) do { if (cond) { s_pass++; } else { s_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::string lcd_row(int r) {
    std::string t;
    for (int c = 0; c < 20; c++) { uint8_t ch = lcd.row(r)[c]; t += (ch >= 32 && ch < 127) ? (char)ch : '.'; }
    return t;
}
// Последний счётчик проходов, отправленный новому дисплею: <PASS:nr,total> или <THREAD_CYCL:n>
static std::string last_disp_pass() {
    const std::string& o = DISPLAY_UART.out;
    size_t p = o.rfind("<PASS:");
    if (p == std::string::npos) return "";
    size_t e = o.find('>', p);
    return o.substr(p + 6, e == std::string::npos ? std::string::npos : e - p - 6);
}

static std::string lcd_text() {
    std::string t;
    for (int r = 0; r < 4; r++) { for (int c = 0; c < 20; c++) { uint8_t ch = lcd.row(r)[c]; t += (ch >= 32 && ch < 127) ? (char)ch : '.'; } t += '\n'; }
    return t;
}

// Положение органов управления «по умолчанию»: всё отпущено, подрежим «Ручной»
static void controls_neutral() {
    for (auto& g : g_gpio) g.IDR = 0xFFFF;
    SET_IN(E2_PIN_ENC_A, 1); SET_IN(E2_PIN_ENC_B, 0);
    SET_IN(E2_PIN_HAND_A, 0); SET_IN(E2_PIN_HAND_B, 0);
    SET_IN(E2_PIN_SUB_MAN, 0);
}

static void select_mode(int m) {   // m: 1..8 как Mode в 7e2 (Feed, aFeed, Thread, Cone_L, Cone_R, Sphere, Divider, Reserve)
    SET_IN(E2_PIN_MODE_FEED, 1); SET_IN(E2_PIN_MODE_AFEED, 1); SET_IN(E2_PIN_MODE_THREAD, 1);
    SET_IN(E2_PIN_MODE_CONE_L, 1); SET_IN(E2_PIN_MODE_CONE_R, 1); SET_IN(E2_PIN_MODE_SPHERE, 1);
    SET_IN(E2_PIN_MODE_DIVIDER, 1); SET_IN(E2_PIN_MODE_RESERVE, 1);
    switch (m) {
        case 1: SET_IN(E2_PIN_MODE_FEED, 0); break;
        case 2: SET_IN(E2_PIN_MODE_AFEED, 0); break;
        case 3: SET_IN(E2_PIN_MODE_THREAD, 0); break;
        case 4: SET_IN(E2_PIN_MODE_CONE_L, 0); break;
        case 5: SET_IN(E2_PIN_MODE_CONE_R, 0); break;
        case 6: SET_IN(E2_PIN_MODE_SPHERE, 0); break;
        case 7: SET_IN(E2_PIN_MODE_DIVIDER, 0); break;
        case 8: SET_IN(E2_PIN_MODE_RESERVE, 0); break;
    }
    run_ms(100);
}

// Средние шаги Z на оборот за последние n оборотов
static double steps_per_rev(int n) {
    size_t k = s_z_at_rev.size();
    if (k < (size_t)n + 1) return -1;
    return (double)(s_z_at_rev[k - 1] - s_z_at_rev[k - 1 - n]) / n;
}

// ============================================================================
static void test_boot() {
    printf("[%7.1f с] [boot] ", t_ns / 1e9); printf("старт, заставка LCD, обмен с дисплеем\n");
    std::string t = lcd_text();
    CHECK(DISPLAY_UART.out.find("<PING>") != std::string::npos, "нет PING дисплею");
    CHECK(DISPLAY_UART.out.find("<MODE:1>") != std::string::npos, "нет MODE:1 в начальном состоянии");
    CHECK(Serial.out.find("SETUP() ЗАВЕРШЕН") != std::string::npos, "setup() не завершился");
    CHECK(Mode == 1, "режим после старта %d, ожидался 1 (Подача)", Mode);
}

static void test_thread(int idx, double pitch_mm, double rpm) {
    printf("[%7.1f с] [thread] ", t_ns / 1e9); printf("резьба #%d шаг %.3f мм, %g об/мин\n", idx, pitch_mm, rpm);
    select_mode(3);
    char b[32]; snprintf(b, sizeof b, "THREAD_STEP:%d", idx); touch(b);
    CHECK(Thread_Step == idx, "Thread_Step=%d", Thread_Step);
    spindle(rpm); run_ms(300);
    s_z_at_rev.clear();
    long z0 = Motor_Z_Pos;
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(60000.0 / rpm * 12);   // 12 оборотов
    double spr = steps_per_rev(6);
    // Шагов на оборот по делителю из таблицы резьб: ENC_TICK / (Ks + Km/10000)
    double expect = 2.0 * ENC_LINE_PER_REV / (Thread_Info_Ks(idx) + Thread_Info_Km(idx) / 10000.0);
    double by_pitch = pitch_mm / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z;
    CHECK(fabs(expect - by_pitch) / by_pitch < 0.001, "делитель таблицы %.3f шагов/об не соответствует шагу %.3f", expect, by_pitch);
    CHECK(fabs(spr - expect) < 0.2, "шагов/об %.3f, ожидалось %.3f", spr, expect);
    CHECK(s_z.count - (long)0 != 0 && Motor_Z_Pos != z0, "мотор Z не двигался");
    long pin_steps = s_z.count;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(500);
    CHECK(s_z.min_level_ns >= 2000, "импульс STEP короче 2 мкс: %llu нс", (unsigned long long)s_z.min_level_ns);
    printf("    шагов/об %.3f (расчёт %.3f), шагов по выводу %ld, Motor_Z_Pos %ld\n", spr, expect, pin_steps, Motor_Z_Pos - z0);
    spindle(0); run_ms(50);
}

// Синхронизация заходов: два прохода резьбы должны начинаться с одного угла шпинделя
static void test_thread_sync() {
    printf("[%7.1f с] [thread-sync] ", t_ns / 1e9); printf("повторный проход попадает в ту же нитку\n");
    select_mode(3); touch("THREAD_STEP:13");     // 1.50 мм
    spindle(200); run_ms(200);
    long phase[2];
    for (int pass = 0; pass < 2; pass++) {
        long start_steps = s_z.count;
        SET_IN(E2_PIN_JOY_LEFT, 0);
        // ждём первый шаг и запоминаем линию энкодера
        while (s_z.count == start_steps) run_ms(0.05);
        phase[pass] = ((spindle_lines % ENC_LINE_PER_REV) + ENC_LINE_PER_REV) % ENC_LINE_PER_REV;
        run_ms(1500);
        SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1200);
        // отъезд обратно ускоренно не моделируем: важен угол старта
        run_ms(400);
    }
    CHECK(labs(phase[0] - phase[1]) <= 1, "первый шаг на линиях %ld и %ld", phase[0], phase[1]);
    printf("    первый шаг: линия %ld и %ld\n", phase[0], phase[1]);
    spindle(0); run_ms(50);
}

static void test_feed(int feed) {
    printf("[%7.1f с] [feed] ", t_ns / 1e9); printf("синхронная подача %.2f мм/об\n", feed / 100.0);
    select_mode(1);
    char b[32]; snprintf(b, sizeof b, "FEED:%d", feed); touch(b);
    CHECK(Feed_mm == feed, "Feed_mm=%d", Feed_mm);
    spindle(500); run_ms(200);
    s_z_at_rev.clear();
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(60000.0 / 500 * 14);
    double spr = steps_per_rev(6);
    // 7e2: Timer5 срабатывает раз в (OCR5A+1) линий, каждое срабатывание — полшага
    double expect_avr = (double)ENC_LINE_PER_REV / (2.0 * (Feed_Divisor + 1));
    double ideal = feed / 100.0 / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z;
    CHECK(fabs(spr - expect_avr) < 0.002 * expect_avr, "шагов/об %.3f, по формуле 7e2 %.3f", spr, expect_avr);
#if FEED_EXACT
    // BACKLOG-020: подача равна заданной с точностью до округления целого периода таймера
    // период таймера — целое число линий: ошибка не больше половины линии на период
    double period = ENC_LINE_PER_REV / (2.0 * ideal);
    CHECK(fabs(spr - ideal) / ideal < 0.5 / period + 0.005, "подача %.2f: шагов/об %.3f, задано %.3f (FEED_EXACT)", feed / 100.0, spr, ideal);
#endif
    printf("    шагов/об %.3f (по формуле: %.3f; задано %.3f)\n", spr, expect_avr, ideal);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(800);
    spindle(0); run_ms(50);
}

static void test_afeed(int afeed) {
    printf("[%7.1f с] [afeed] ", t_ns / 1e9); printf("асинхронная подача %d мм/мин\n", afeed);
    select_mode(2);
    char b[32]; snprintf(b, sizeof b, "AFEED:%d", afeed); touch(b);
    SET_IN(E2_PIN_JOY_LEFT, 0);
    run_ms(1500);
    long c0 = s_z.count; run_ms(2000); long c1 = s_z.count;
    double sps = (c1 - c0) / 2.0;
    double expect_avr = 250000.0 / (2.0 * (aFeed_Divisor + 1));
    double ideal = afeed / 60.0 / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z;
    CHECK(fabs(sps - expect_avr) / expect_avr < 0.01, "шагов/с %.1f, по формуле 7e2 %.1f", sps, expect_avr);
    printf("    шагов/с %.1f (7e2: %.1f; идеально %.1f)\n", sps, expect_avr, ideal);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
}

static void test_rapid() {
    printf("[%7.1f с] [rapid] ", t_ns / 1e9); printf("ускоренный ход Z с кнопкой\n");
    select_mode(1);
    spindle(0);
    SET_IN(E2_PIN_JOY_RAPID, 0); SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(1500);
    long c0 = s_z.count; run_ms(500); long c1 = s_z.count;
    double sps = (c1 - c0) / 0.5;
    double expect = 500000.0 / (2.0 * (MAX_RAPID_MOTION + 1));
    CHECK(fabs(sps - expect) / expect < 0.02, "шагов/с %.0f, ожидалось %.0f", sps, expect);
    printf("    шагов/с %.0f (расчёт %.0f = %.0f мм/мин)\n", sps, expect, expect / (MOTOR_Z_STEP_PER_REV * McSTEP_Z) * SCREW_Z / 100.0 * 60);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(2000);
    long after = s_z.count; run_ms(300);
    CHECK(s_z.count == after, "мотор не остановился после отпускания");
    SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(100);
}

static void test_limit() {
    printf("[%7.1f с] [limit] ", t_ns / 1e9); printf("упор слева останавливает подачу\n");
    select_mode(1); touch("FEED:20");
    long z = Motor_Z_Pos;
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(200); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    CHECK(limit_Left_flag, "упор не установился");
    CHECK(Limit_Pos_Left != 1073741824L, "Limit_Pos_Left не задан");
    // отъехать вправо на 2 мм ускоренно
    SET_IN(E2_PIN_JOY_RAPID, 0); SET_IN(E2_PIN_JOY_RIGHT, 0); run_ms(800); SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(1500); SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(100);
    printf("    после отъезда вправо Z=%ld\n", Motor_Z_Pos);
    // подача влево до упора
    spindle(800);
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(15000);
    long stop = Motor_Z_Pos;
    run_ms(500);
    CHECK(Motor_Z_Pos == stop, "мотор не стоит у упора");
    CHECK(labs(stop - Limit_Pos_Left) <= 4, "остановка %ld, упор %ld", stop, Limit_Pos_Left);
    printf("    упор %ld, стоп %ld (старт %ld)\n", Limit_Pos_Left, stop, z);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(300);
    spindle(0);
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(200); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    CHECK(!limit_Left_flag, "упор не снялся");
}

// BACKLOG-022: упоры без физического переключателя подрежима — «Ручной» выбран на экране
static void test_limit_no_switch() {
    printf("[%7.1f с] [limit-touch] ", t_ns / 1e9); printf("упор без переключателя подрежима (подрежим с экрана)\n");
    controls_neutral(); select_mode(2);
    SET_IN(E2_PIN_SUB_MAN, 1); run_ms(200);                 // переключателя нет: все три провода «1»
    touch("S2"); run_ms(200);
    if (limit_Left_flag) { SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(150); }
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(150);
    CHECK(limit_Left_flag == (LIMITS_BY_SELECTED_SUBMODE ? 1 : 0),
          "упор с «Ручным» на экране: limit_Left_flag=%d (LIMITS_BY_SELECTED_SUBMODE=%d)", limit_Left_flag, LIMITS_BY_SELECTED_SUBMODE);
    // в автоцикле (подрежим не «Ручной») упор переставить нельзя — как у автора
    bool before = limit_Left_flag;
    Sub_Mode_aFeed = Sub_Mode_aFeed_Ext;
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(150);
    CHECK(limit_Left_flag == before, "упор переставился в подрежиме «Наружный»");
    Sub_Mode_aFeed = Sub_Mode_aFeed_Man;
    if (limit_Left_flag) { SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(150); }
    controls_neutral(); run_ms(100);
}

// Ввод и сброс осей: с экрана (AXIS:) и кнопкой «Сброс» автора. Показания — как уходят на экран, мкм.
extern uint8_t g_stub_scale_present[2];
extern int32_t g_stub_scale_um[2];
static long shown_z() { return -(long)Size_Z_mm * 10; }
static long shown_x() { return -(long)Size_X_mm * 10; }
static long shown_d() { return -(long)MSize_X_mm * 10; }
static void test_axis_set() {
    printf("[%7.1f с] [axis] ", t_ns / 1e9); printf("ввод и сброс осей (экран AXIS:, кнопка «Сброс»), источник %d\n", ELS_POS_SOURCE);
    controls_neutral(); select_mode(1); run_ms(100);
#if ELS_POS_SOURCE == POS_SOURCE_SCALES
    g_stub_scale_present[0] = g_stub_scale_present[1] = 1;
    g_stub_scale_um[0] = 123456; g_stub_scale_um[1] = -7890; run_ms(50);
#endif
    long z0 = shown_z(), x0 = shown_x(), d0 = shown_d();
    touch("AXIS:Z:-12345"); touch("AXIS:X:2500"); touch("AXIS:D:40000"); run_ms(50);
#if ELS_POS_SOURCE == POS_SOURCE_DRO_UART
    CHECK(shown_z() == z0 && shown_x() == x0 && shown_d() == d0, "с блоком DRO оси с экрана меняться не должны");
#else
    (void)z0; (void)x0; (void)d0;
    CHECK(shown_z() == -12340 && shown_x() == 2500 && shown_d() == 40000,
          "после ввода Z=%ld X=%ld ⌀=%ld, ожидалось -12340 2500 40000", shown_z(), shown_x(), shown_d());
    // ось после ввода продолжает считать движение
  #if ELS_POS_SOURCE == POS_SOURCE_SCALES
    g_stub_scale_um[0] += 1000; g_stub_scale_um[1] -= 500; run_ms(50);
    CHECK(shown_z() == -11340 && shown_x() == 2000 && shown_d() == 39000,
          "после движения Z=%ld X=%ld ⌀=%ld, ожидалось -11340 2000 39000", shown_z(), shown_x(), shown_d());
  #else
    long zs = shown_z();
    SET_IN(E2_PIN_HAND_X1, 0); SET_IN(E2_PIN_HAND_X10, 1); SET_IN(E2_PIN_HAND_AXIS_Z, 0); run_ms(100);
    hand_turn(25, 20); run_ms(1500);
    long dz = shown_z() - zs;
    CHECK(dz == 250 || dz == -250, "после ввода РГИ 25 щелчков сдвинул Z на %ld мкм (ожидалось ±250)", dz);
    hand_turn(-25, 20); run_ms(1500);
    SET_IN(E2_PIN_HAND_AXIS_Z, 1); run_ms(100);
  #endif
    // «Сброс» автора: подрежим «Ввод * Сброс осей» (SelectMenu 2 в синхронной подаче), ▲ — X, ▼ — Z
    SelectMenu = 2;
    touch("KEY:UP"); touch("KEY:DN"); run_ms(50);
    CHECK(shown_z() == 0 && shown_x() == 0, "после «Сброс» Z=%ld X=%ld, ожидалось 0 0", shown_z(), shown_x());
    SelectMenu = 1;
#endif
#if ELS_POS_SOURCE == POS_SOURCE_SCALES
    g_stub_scale_present[0] = g_stub_scale_present[1] = 0;
#endif
    printf("    Z=%ld X=%ld ⌀=%ld мкм\n", shown_z(), shown_x(), shown_d());
}

// Механика из настроек (BACKLOG-028): пересчёт таблиц по формулам gen_e2_tables.py, проверки, флеш
extern uint8_t g_stub_flash[256];
static void test_mech() {
    printf("[%7.1f с] [mech] ", t_ns / 1e9); printf("механика из настроек: таблицы, проверки, флеш\n");
    // 1. Пересчёт под механику автора совпадает с его таблицами (резьбы — точно, конусы — 0.2 %)
    static thread_info_type thr[TOTAL_THREADS]; static cone_info_type cone[TOTAL_CONE];
    memcpy(thr, Thread_Info, sizeof thr); memcpy(cone, Cone_Info, sizeof cone);
    CHECK(e2_mech_tables(E2_MECH_CONFIG, true) == nullptr, "таблицы автора не считаются");
    int bad_thr = 0, bad_cone = 0;
    for (unsigned i = 0; i < TOTAL_THREADS; i++)
        if (thr[i].Ks_Div_Z != Thread_Info[i].Ks_Div_Z || thr[i].Km_Div_Z != Thread_Info[i].Km_Div_Z ||
            thr[i].Ks_Div_X != Thread_Info[i].Ks_Div_X || thr[i].Km_Div_X != Thread_Info[i].Km_Div_X) {
            if (!bad_thr++) printf("    резьба %s: %d.%04d → %d.%04d\n", thr[i].Thread_Print, thr[i].Ks_Div_Z,
                                   thr[i].Km_Div_Z, Thread_Info[i].Ks_Div_Z, Thread_Info[i].Km_Div_Z);
        }
    for (unsigned i = 0; i < TOTAL_CONE; i++) {
        double a = cone[i].Cs_Div + cone[i].Cm_Div / 1e4, b = Cone_Info[i].Cs_Div + Cone_Info[i].Cm_Div / 1e4;
        if (fabs(a - b) > 0.002 * a) { if (!bad_cone++) printf("    конус %s: %.4f → %.4f\n", cone[i].Cone_Print, a, b); }
    }
    CHECK(bad_thr == 0 && bad_cone == 0, "пересчёт расходится с таблицами автора: резьб %d, конусов %d", bad_thr, bad_cone);
    CHECK(e2_mech_apply(E2_MECH_CONFIG) == nullptr && !memcmp(thr, Thread_Info, sizeof thr) && !memcmp(cone, Cone_Info, sizeof cone),
          "механика автора — не его таблицы");
    // 2. Неверные значения отклоняются, текущие не меняются
    E2Mech m = E2_MECH_CONFIG; m.z_mc = 3;
    CHECK(e2_mech_apply(m) != nullptr && e2_mech.z_mc == E2_MECH_CONFIG.z_mc, "микрошаг 3 принят");
    m = E2_MECH_CONFIG; m.enc_lines = 10000; m.z_steps = 50; m.z_mc = 1; m.z_screw = 1000;
    const char* err = e2_mech_check(m);
    CHECK(err != nullptr, "механика с делителем резьбы > 255 принята");
    printf("    отказ: %s\n", err ? err : "-");
    m = E2_MECH_CONFIG; m.z_mc = 16;                 // 1600 шагов/мм: на 4.5 мм мотор не успевает за шпинделем
    err = e2_mech_check(m);
    CHECK(err && strstr(err, "РЕЗЬБА"), "механика, где мотор не успевает в резьбе, принята (%s)", err ? err : "-");
    printf("    отказ: %s\n", err ? err : "-");
    // 3. Флеш: сохранить другую механику, затем загрузка при старте её возвращает; испорченная запись — из конфига
    m = E2_MECH_CONFIG; m.z_screw = 150; m.bl_x_um = 80;
    CHECK(e2_mech_apply(m) == nullptr && e2_mech_save() == nullptr, "винт Z 1.5 мм не применился/не сохранился");
    e2_mech_apply(E2_MECH_CONFIG); e2_mech_load();
    CHECK(e2_mech.z_screw == 150 && e2_mech.bl_x_um == 80, "из флеша: винт Z %d, люфт X %d", e2_mech.z_screw, e2_mech.bl_x_um);
    g_stub_flash[10] ^= 1; e2_mech_load();
    CHECK(e2_mech.z_screw == E2_MECH_CONFIG.z_screw, "испорченная запись принята");
    memset(g_stub_flash, 0xFF, sizeof g_stub_flash);
    e2_mech_apply(E2_MECH_CONFIG);
    // 4. С экрана: при вращающемся шпинделе — отказ; на стоящем станке — применить, сохранить, ответить
    controls_neutral(); select_mode(1);
    spindle(300); run_ms(600);
    size_t o = DISPLAY_UART.out.size();
    touch("CFG_SET:1800,200,4,150,200,4,125,0,80"); run_ms(20);
    CHECK(e2_mech.z_screw == E2_MECH_CONFIG.z_screw && DISPLAY_UART.out.find("<CFG_RES:OK>", o) == std::string::npos,
          "настройки приняты при вращающемся шпинделе");
    spindle(0); run_ms(600);
    o = DISPLAY_UART.out.size();
    touch("CFG_SET:1800,200,4,150,200,4,125,0,80"); run_ms(20);
    CHECK(e2_mech.z_screw == 150 && e2_mech.x_screw == 125 && e2_mech.bl_x_um == 80, "CFG_SET не применился");
    CHECK(DISPLAY_UART.out.find("<CFG_RES:OK>", o) != std::string::npos &&
          DISPLAY_UART.out.find("<CFG:1800,200,4,150,200,4,125,0,80>", o) != std::string::npos, "нет ответа CFG_RES/CFG");
    e2_mech_apply(E2_MECH_CONFIG); e2_mech_load();
    CHECK(e2_mech.z_screw == 150, "CFG_SET не сохранился во флеш");
    o = DISPLAY_UART.out.size();
    touch("CFG_SET:1800,200,3,200,200,4,100,0,0"); run_ms(20);
    CHECK(e2_mech.z_mc == 4 && DISPLAY_UART.out.find("<CFG_RES:МИКРОШАГ", o) != std::string::npos, "микрошаг 3 с экрана не отклонён");
    touch("CFG_DEFAULT"); run_ms(20);
    CHECK(!memcmp(&e2_mech, &E2_MECH_CONFIG, sizeof e2_mech), "CFG_DEFAULT не вернул els_config.h");
    // 5. Источник координат и линейки с экрана: сохранить и перезапуск; при загрузке — из флеша
    extern bool g_stub_rebooted;
    g_stub_rebooted = false;
    touch("POS_SET:2,4,10000,1,0,1,5000,0,1"); run_ms(20);
    CHECK(g_stub_rebooted && e2_pos.source == POS_SOURCE_SCALES && e2_pos.ax[0].type == SCALE_IGAGING21 &&
          e2_pos.ax[1].sig_inv == 1, "POS_SET не применился (перезапуск %d, источник %d)", g_stub_rebooted, e2_pos.source);
    e2_pos = E2_POS_CONFIG; e2_mech_load();
    CHECK(e2_pos.source == POS_SOURCE_SCALES && e2_pos.ax[0].nm_per_count == 10000, "POS не сохранился во флеш");
    g_stub_rebooted = false;
    touch("POS_SET:2,9,10000,1,0,1,5000,0,1"); run_ms(20);
    CHECK(!g_stub_rebooted, "неверный тип линейки принят");
    memset(g_stub_flash, 0xFF, sizeof g_stub_flash);
    e2_pos = E2_POS_CONFIG;
}

// Калибровка по индикатору (BACKLOG-029): фазы CAL_STEP — люфт, ход, возврат; счётчики не меняются
static void test_cal_step() {
    printf("[%7.1f с] [cal-step] ", t_ns / 1e9); printf("калибровка по индикатору: фазы движения\n");
    controls_neutral(); select_mode(2); run_ms(100);
    long m0 = Motor_X_Pos, p0 = s_x.count;
    long steps10 = lround(10.0 / (SCREW_X / 100.0) * MOTOR_X_STEP_PER_REV * McSTEP_X);
    long pmax = p0;
    for (int ph = 1; ph <= 3; ph++) {
        size_t o = DISPLAY_UART.out.size();
        char b[40]; snprintf(b, sizeof b, "CAL_STEP:X:%d:10000", ph); touch(b); run_ms(50);
        char want[16]; snprintf(want, sizeof want, "<CAL_STEP:%d>", ph);
        CHECK(DISPLAY_UART.out.find(want, o) != std::string::npos, "нет ответа %s", want);
        if (ph == 2) pmax = s_x.count;
    }
    CHECK(pmax - p0 == steps10, "фаза 2: %ld импульсов, ожидалось %ld (10 мм)", pmax - p0, steps10);
    CHECK(s_x.count == p0 && Motor_X_Pos == m0, "после фаз ось не на месте: импульсов %+ld, счётчик %+ld", s_x.count - p0, Motor_X_Pos - m0);
}

// BACKLOG-015: физические дубли экранных функций
static void test_physical_dupes() {
    printf("[%7.1f с] [buttons] ", t_ns / 1e9); printf("физические кнопки: категория резьбы, диаметр при линейках\n");
    controls_neutral(); select_mode(3); touch("THREAD_STEP:13"); run_ms(100);
    SET_IN(E2_PIN_JOY_RAPID, 0); run_ms(20);                          // кнопка ускоренного хода нажата
    touch("KEY:UP");  int a = Thread_Step; run_ms(700);              // повтор удержанной кнопки — не чаще 0.6 с
    touch("KEY:UP");  int b = Thread_Step; touch("KEY:UP");            // повтор сразу — без перехода
    CHECK(Thread_Step == b, "повтор кнопки сразу перешёл категорию (%d → %d)", b, Thread_Step); run_ms(700);
    touch("KEY:DN");  int c = Thread_Step; run_ms(700);
    touch("KEY:DN");  int d = Thread_Step; run_ms(700);
    SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(20);
    touch("KEY:UP");  int e = Thread_Step;
    CHECK(a == THR_IDX_INCH_START && b == THR_IDX_GPIPE_START && c == THR_IDX_INCH_START && d == 0 && e == 1,
          "категории: %d %d %d %d, без ускоренного %d", a, b, c, d, e);
    printf("    ▲▲▼▼ с ускоренным: %d → %d → %d → %d; без него ▲: %d\n", a, b, c, d, e);
#if ELS_POS_SOURCE == POS_SOURCE_SCALES
    g_stub_scale_present[1] = 1; g_stub_scale_um[1] = 5000; run_ms(50);
    select_mode(1); SelectMenu = 2;
    SET_IN(E2_PIN_HAND_X1, 0); SET_IN(E2_PIN_HAND_X10, 1); run_ms(50);
    long d0 = -(long)MSize_X_mm * 10, mx0 = MX_pos;
    touch("KEY:LEFT"); run_ms(50);
    long dd = -(long)MSize_X_mm * 10 - d0, dmx = MX_pos - mx0;
    float spu = (float)MOTOR_X_STEP_PER_REV / SCREW_X * McSTEP_X;
    long want = -lroundf(dmx * 20.0f / spu);
    CHECK(dmx != 0 && labs(dd - want) <= 10, "диаметр кнопкой при линейках: %+ld мкм, ожидалось %+ld (MX %+ld)", dd, want, dmx);
    printf("    ◀ в «Ввод * Сброс»: диаметр %+ld мкм (по шагам было бы %+ld)\n", dd, want);
    SelectMenu = 1; SET_IN(E2_PIN_HAND_X1, 1); g_stub_scale_present[1] = 0; run_ms(50);
#endif
    controls_neutral(); run_ms(50);
}

// G-код (BACKLOG-001): строки <GCODE:…> в режиме M8, ответ <OK> / <ERR:…>
static std::string gcode(const char* line, double ms = 200) {
    size_t o = DISPLAY_UART.out.size();
    DISPLAY_UART.inject(std::string("<GCODE:") + line + ">\n"); run_ms(ms);
    size_t p = DISPLAY_UART.out.find("<OK>", o), e = DISPLAY_UART.out.find("<ERR:", o);
    if (e != std::string::npos && (p == std::string::npos || e < p))
        return DISPLAY_UART.out.substr(e + 5, DISPLAY_UART.out.find('>', e) - e - 5);
    return p != std::string::npos ? "OK" : "НЕТ ОТВЕТА";
}
static void test_gcode() {
#if ELS_POS_SOURCE == POS_SOURCE_STEPS
    printf("[%7.1f с] [gcode] ", t_ns / 1e9); printf("G-код: M8, G0/G1/G91/G2, стоп, упоры\n");
    controls_neutral(); select_mode(1); run_ms(100);
    std::string r = gcode("G0 Z-1");
    CHECK(r.find("M8") != std::string::npos, "вне M8 принято: %s", r.c_str());
    select_mode(8); run_ms(200);
    touch("AXIS:Z:0"); touch("AXIS:X:0"); run_ms(50);
    CHECK(gcode("G21 G90 G7") == "OK", "модальные не приняты");
    r = gcode("G0 Z-5 X4", 3000);
    CHECK(r == "OK" && shown_z() == -5000 && shown_x() == 2000, "G0 Z-5 X4(⌀): %s, Z=%ld X=%ld", r.c_str(), shown_z(), shown_x());
    r = gcode("G1 Z-10 F300", 4000);
    CHECK(r == "OK" && shown_z() == -10000, "G1 Z-10: %s, Z=%ld", r.c_str(), shown_z());
    r = gcode("G91 G1 X-1 F120", 3000); gcode("G90");
    CHECK(r == "OK" && shown_x() == 1500, "G91 X-1(⌀): %s, X=%ld (ожидалось 1500 — радиус)", r.c_str(), shown_x());
    gcode("G0 Z0 X0", 5000);
    r = gcode("G2 Z-5 X10 R5", 8000);                          // четверть окружности: (0,0) → (−5, 5 радиус)
    CHECK(r == "OK" && shown_z() == -5000 && shown_x() == 5000, "G2 R5: %s, Z=%ld X=%ld", r.c_str(), shown_z(), shown_x());
    // стоп посреди хода: STOP уже в буфере, когда начинается движение
    long z0 = Motor_Z_Pos;
    size_t o = DISPLAY_UART.out.size();
    inject_later("<GCODE:STOP>\n", 1500);                    // «Стоп» на веб-странице через 1.5 с хода
    if (getenv("SIM_LATERDBG")) printf("      [later] запланировано на t=%.3f\n", s_later_t / 1e9);
    DISPLAY_UART.inject("<GCODE:G1 Z-40 F200>\n"); run_ms(4000);
    CHECK(DISPLAY_UART.out.find("ОСТАНОВЛЕНО", o) != std::string::npos && Motor_Z_Pos != z0 &&
          labs(Motor_Z_Pos - z0) < lround(35.0 / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z),
          "стоп посреди хода не сработал (Z %+ld шагов)", Motor_Z_Pos - z0);
    // упор: левый поставлен в текущей точке — дальше влево (Z+) нельзя
    gcode("G0 Z0 X0", 15000);
    select_mode(1); SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    bool lim = limit_Left_flag;
    select_mode(8); run_ms(200);
    r = gcode(Limit_Pos_Left > Motor_Z_Pos - 4 ? "G0 Z-20" : "G0 Z20", 200);   // за упор — в сторону роста Motor_Z_Pos
    long zl = Motor_Z_Pos;
    std::string r2 = gcode(shown_z() < 0 ? "G0 Z5" : "G0 Z-5", 200);
    CHECK(!lim || r2.find("УПОР") != std::string::npos || r.find("УПОР") != std::string::npos,
          "движение за упор принято (%s / %s, упор %d)", r.c_str(), r2.c_str(), lim);
    (void)zl;
    select_mode(1); SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    printf("    G0/G1/G91/G2 — в цель, стоп — %s\n", "есть");
    controls_neutral(); run_ms(100);
#endif
}

static void test_hand() {
    printf("[%7.1f с] [hand] ", t_ns / 1e9); printf("РГИ: ось Z, x1, 25 щелчков\n");
    select_mode(1);
    SET_IN(E2_PIN_HAND_X1, 0); SET_IN(E2_PIN_HAND_X10, 1); run_ms(100);
    SET_IN(E2_PIN_HAND_AXIS_Z, 0); run_ms(100);
    long z0 = Motor_Z_Pos, p0 = s_z.count;
    hand_turn(25, 20); run_ms(1500);
    long dz = Motor_Z_Pos - z0;
    long expect = 25L * HC_SCALE_1 * MOTOR_Z_STEP_PER_REV * McSTEP_Z / SCREW_Z;
    CHECK(labs(dz - expect) <= McSTEP_Z / 2, "РГИ Z: %ld шагов, ожидалось %ld (ядро округляет до полного шага)", dz, expect);
    CHECK(s_z.count - p0 == dz, "по выводу %ld, по счётчику %ld", s_z.count - p0, dz);
    printf("    Z +%ld шагов (ожидалось %ld)\n", dz, expect);
    hand_turn(-25, 20); run_ms(1500);
    CHECK(Motor_Z_Pos == z0, "РГИ не вернул Z: %ld vs %ld", Motor_Z_Pos, z0);
    SET_IN(E2_PIN_HAND_AXIS_Z, 1); run_ms(100);
}

static void test_modes_touch() {
    printf("[%7.1f с] [modes] ", t_ns / 1e9); printf("режимы и подрежимы с дисплея, строка LCD2004\n");
    controls_neutral(); run_ms(100);
    for (int m = 1; m <= 8; m++) {
        char b[8]; snprintf(b, sizeof b, "M%d", m); touch(b); run_ms(600);
        CHECK(Mode == m, "после %s Mode=%d", b, Mode);
        char want[24]; snprintf(want, sizeof want, "<LCD2004:%d,", m - 1);
        CHECK(DISPLAY_UART.out.rfind(want) != std::string::npos, "нет %s", want);
    }
    // «Внутренний»/«Наружный» без упоров запрещены: 7e2 остаётся в «Ручном» и пишет «УСТАНОВИТЕ УПОРЫ!»
    touch("M1"); touch("S1"); run_ms(100);
    CHECK(Sub_Mode_Feed == 2, "S1 без упоров: Sub_Mode_Feed=%d", Sub_Mode_Feed);
    CHECK(lcd_text().find("YCTAHOB") != std::string::npos || DISPLAY_UART.out.rfind("<ALERT:1>") != std::string::npos, "нет предупреждения об упорах");
    touch("S2"); run_ms(100);
}

// Упоры Z: правый — в текущей точке, левый — на len_ms ускоренного хода левее; суппорт — у правого
static bool setup_z_limits(double len_ms) {
    select_mode(1); controls_neutral(); run_ms(100);
    // снять оставшиеся от прошлых проверок упоры
    if (limit_Left_flag)  { SET_IN(E2_PIN_LIM_LEFT, 0);  run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1);  run_ms(100); }
    if (limit_Right_flag) { SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100); }
    if (limit_Front_flag) { SET_IN(E2_PIN_LIM_FRONT, 0); run_ms(150); SET_IN(E2_PIN_LIM_FRONT, 1); run_ms(100); }
    if (limit_Rear_flag)  { SET_IN(E2_PIN_LIM_REAR, 0);  run_ms(150); SET_IN(E2_PIN_LIM_REAR, 1);  run_ms(100); }
    SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100);
    // ускоренно влево: len_ms при механике автора; ядро ставит левый упор не ближе тормозного пути
    // ((MIN_RAPID − MAX_RAPID) × REPEAt × 2 шагов), а ускоренный ход ограничен частотой шагов — при большем
    // микрошаге он медленнее в мм/с, поэтому ехать, пока не отъедем на тормозной путь с запасом
    long need = Limit_Pos_Right + ((MIN_RAPID_MOTION - MAX_RAPID_MOTION) * REPEAt) * 2 * 5 / 4;
    SET_IN(E2_PIN_JOY_RAPID, 0); SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(len_ms);
    for (int i = 0; i < 100 && Motor_Z_Pos < need; i++) run_ms(50);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1500);
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    SET_IN(E2_PIN_JOY_RIGHT, 0); run_ms(len_ms + 3000);
    for (int i = 0; i < 200 && Motor_Z_Pos != Limit_Pos_Right; i++) run_ms(50);
    SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(1500);
    SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(100);
    return limit_Left_flag && limit_Right_flag && Motor_Z_Pos == Limit_Pos_Right;
}

static void test_thread_ext_cycle(int idx) {
    printf("[%7.1f с] [thread-ext] ", t_ns / 1e9); printf("автоцикл «Наружная резьба» #%d (%s)\n", idx, Thread_Info[idx].Thread_Print);
    bool ok = setup_z_limits(700);
    CHECK(ok, "упоры: L=%d R=%d Z=%ld упор R=%ld", limit_Left_flag, limit_Right_flag, Motor_Z_Pos, Limit_Pos_Right);
    long len = Limit_Pos_Left - Limit_Pos_Right;
    select_mode(3);
    char b[32]; snprintf(b, sizeof b, "THREAD_STEP:%d", idx); touch(b);
    SET_IN(E2_PIN_SUB_MAN, 1); SET_IN(E2_PIN_SUB_EXT, 0); run_ms(200);
    CHECK(Sub_Mode_Thread == 3, "подрежим %d, err1=%d err2=%d", Sub_Mode_Thread, err_1_flag, err_2_flag);
    long x0 = Motor_X_Pos, z0 = Motor_Z_Pos;
    spindle(300); run_ms(300);
    s_z_starts.clear();
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0;
    while (!Complete_flag && t < 300000) { run_ms(100); t += 100; }
    bool done = Complete_flag;
    bool lcd_done = lcd_text().find("O..PA..") != std::string::npos || lcd_text().find("3ABEP") != std::string::npos;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
    spindle(0); run_ms(100);
    int expect_passes = Thread_Info[idx].Pass + PASS_FINISH;
    long first = -1; int passes = 0, bad = 0;
    for (auto& m : s_z_starts) if (m.dir == 0) {          // DIR = 0 — влево (CW), рабочий ход резьбы
        passes++;
        if (first < 0) first = m.line; else if (labs(m.line - first) > 1) bad++;
    }
    CHECK(done, "цикл не завершился за %.0f с", t / 1000);
    CHECK(lcd_done, "на LCD нет «ОПЕРАЦИЯ ЗАВЕРШЕНА»");
    CHECK(passes == expect_passes, "рабочих проходов %d, ожидалось %d", passes, expect_passes);
    CHECK(bad == 0, "%d проходов начались не на той линии энкодера", bad);
    CHECK(Motor_Z_Pos == z0 && Motor_X_Pos == x0, "суппорт не вернулся: Z %ld→%ld, X %ld→%ld", z0, Motor_Z_Pos, x0, Motor_X_Pos);
    printf("    длина %.2f мм, проходов %d (ожидалось %d), все с линии %ld, время %.1f с\n",
           len * (SCREW_Z / 100.0) / (MOTOR_Z_STEP_PER_REV * McSTEP_Z), passes, expect_passes, first, t / 1000);
    // выход из цикла: подрежим «Ручной», снять упоры
    SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK");
    select_mode(1);
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100);
}

static void test_feed_ext_cycle() {
    printf("[%7.1f с] [feed-ext] ", t_ns / 1e9); printf("автоцикл «Наружная подача»: 3 прохода по 0.20 мм на диаметр\n");
    bool ok = setup_z_limits(600);
    CHECK(ok, "упоры не установлены");
    select_mode(1); touch("FEED:20"); touch("AP:20");
    SET_IN(E2_PIN_SUB_MAN, 1); SET_IN(E2_PIN_SUB_EXT, 0); run_ms(200);
    CHECK(Sub_Mode_Feed == 3, "подрежим %d", Sub_Mode_Feed);
    Pass_Total = 3; Pass_Nr = 1;                      // число проходов (на пульте — кнопками)
    long x0 = Motor_X_Pos, z0 = Motor_Z_Pos;
    spindle(600); run_ms(200);
    s_z_starts.clear();
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0;
    while (!Complete_flag && t < 120000) { run_ms(100); t += 100; }
    bool done = Complete_flag;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
    spindle(0); run_ms(100);
    long infeed = (int)((float)MOTOR_X_STEP_PER_REV * (20 / 2) / SCREW_X + 0.5) * McSTEP_X;
    std::vector<long> xs;
    for (auto& m : s_z_starts) if (m.dir == 0) xs.push_back(m.x - x0);
    CHECK(done, "цикл не завершился");
    CHECK(xs.size() == 3, "рабочих проходов %zu", xs.size());
    for (size_t i = 0; i < xs.size(); i++)
        CHECK(xs[i] == (long)(i + 1) * infeed, "проход %zu: X %+ld, ожидалось %+ld", i + 1, xs[i], (long)(i + 1) * infeed);
    CHECK(Motor_Z_Pos == z0, "Z не вернулся: %ld vs %ld", Motor_Z_Pos, z0);
    printf("    проходов %zu, X на проходах:", xs.size()); for (long v : xs) printf(" %+ld", v);
    printf(" шагов (по %ld), %.1f с, X в конце %+ld\n", infeed, t / 1000, Motor_X_Pos - x0);
    SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK");
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100);
}

// Ось X: подача на себя/от себя и ускоренный ход
static void test_x_axis() {
    printf("[%7.1f с] [x-axis] ", t_ns / 1e9); printf("подача X (джойстик вверх/вниз)\n");
    select_mode(1); touch("FEED:10");
    spindle(600); run_ms(200);
    long x0 = Motor_X_Pos, p0 = s_x.count;
    if (getenv("SIM_DEBUG")) printf("      старт: пин X=%u, dir=%u\n", (unsigned)E2_ODR(E2_PIN_X_STEP), (unsigned)E2_ODR(E2_PIN_X_DIR));
    SET_IN(E2_PIN_JOY_UP, 0); run_ms(4000); SET_IN(E2_PIN_JOY_UP, 1); run_ms(800);
    if (getenv("SIM_DEBUG")) printf("      конец: пин X=%u, по выводу %ld, по счётчику %ld\n", (unsigned)E2_ODR(E2_PIN_X_STEP), s_x.count - p0, Motor_X_Pos - x0);
    long dx = Motor_X_Pos - x0;
    CHECK(dx != 0, "X не двигался");
    // при отпускании джойстика 7e2 округляет позицию до целого шага (±McSTEP/2)
    CHECK(labs(labs(s_x.count - p0) - labs(dx)) <= McSTEP_X / 2, "по выводу %ld, по счётчику %ld", s_x.count - p0, dx);
    double per_rev = fabs((double)dx) / (4.0 * 600 / 60);
    printf("    X %+ld шагов за 4 с (%.1f шаг/об)\n", dx, per_rev);
    long x1 = Motor_X_Pos;
    SET_IN(E2_PIN_JOY_DOWN, 0); run_ms(4000); SET_IN(E2_PIN_JOY_DOWN, 1); run_ms(800);
    CHECK((Motor_X_Pos - x1) * dx < 0, "X вниз не поехал обратно");
    spindle(0); run_ms(50);
}

// Резьба при вращении шпинделя против часовой (левая резьба/обратный ход)
static void test_thread_ccw() {
    printf("[%7.1f с] [thread-ccw] ", t_ns / 1e9); printf("резьба 1.00 мм, шпиндель против часовой\n");
    select_mode(3); touch("THREAD_STEP:11");
    spindle(-300); run_ms(300);
    s_z_at_rev.clear();
    long z0 = s_z.count;
    SET_IN(E2_PIN_JOY_RIGHT, 0); run_ms(60000.0 / 300 * 8);
    long dz = s_z.count - z0;
    SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(500);
    double revs = 8.0;
    printf("    шагов %ld за ~%.0f об (%.1f шаг/об)\n", dz, revs, fabs((double)dz) / revs);
    CHECK(dz != 0, "мотор Z не двигался");
    spindle(0); run_ms(50);
}

static void test_hand_x10_x() {
    printf("[%7.1f с] [hand-x10] ", t_ns / 1e9); printf("РГИ: ось X, x10, 10 щелчков\n");
    select_mode(1);
    SET_IN(E2_PIN_HAND_X1, 1); SET_IN(E2_PIN_HAND_X10, 0); run_ms(100);
    SET_IN(E2_PIN_HAND_AXIS_X, 0); run_ms(100);
    long x0 = Motor_X_Pos;
    hand_turn(10, 40); run_ms(3000);
    long dx = Motor_X_Pos - x0;
    long expect = 10L * HC_SCALE_10 * MOTOR_X_STEP_PER_REV / 2 * McSTEP_X / SCREW_X;
    CHECK(labs(dx) == expect, "РГИ X: %ld шагов, ожидалось ±%ld", dx, expect);
    printf("    X %+ld шагов (ожидалось ±%ld: 10 щелчков × 0.1 мм на диаметр)\n", dx, expect);
    hand_turn(-10, 40); run_ms(3000);
    CHECK(Motor_X_Pos == x0, "РГИ X не вернул: %ld vs %ld", Motor_X_Pos, x0);
    SET_IN(E2_PIN_HAND_AXIS_X, 1); SET_IN(E2_PIN_HAND_X10, 1); run_ms(100);
}

static void test_divider() {
    printf("[%7.1f с] [divider] ", t_ns / 1e9); printf("делилка: угол шпинделя на дисплей\n");
    select_mode(7);
    touch("DIVN:6"); touch("DIVM:2");
    spindle(30); run_ms(1000); spindle(0); run_ms(300);
    char want[32]; snprintf(want, sizeof want, "<ANGLE:%lu>", (unsigned long)Spindle_Angle);
    CHECK(DISPLAY_UART.out.rfind(want) != std::string::npos, "нет %s", want);
    long expect_mdeg = (long)Enc_Pos * 360000L / ENC_TICK;
    CHECK((long)Spindle_Angle == expect_mdeg, "угол %lu, Enc_Pos %d", (unsigned long)Spindle_Angle, Enc_Pos);
    CHECK(Required_Angle == 60000, "требуемый угол %lu (ожидалось 60000 = 60°)", (unsigned long)Required_Angle);
    printf("    угол %.3f°, метка 2 из 6 → %.1f°\n", Spindle_Angle / 1000.0, Required_Angle / 1000.0);
}

// Конус: X (на радиус) ведомая от Z; уклон = 1 / (Cs + Cm/10000) для механики 7e2 (винты 2 и 1 мм)
static void test_cone(int idx, double slope) {
    printf("[%7.1f с] [cone] ", t_ns / 1e9); printf("конус #%d (%s), ожидаемый уклон %.4f\n", idx, Cone_Info[idx].Cone_Print, slope);
    select_mode(4);
    char b[24]; snprintf(b, sizeof b, "CONE:%d", idx); touch(b);
    touch("FEED:20");
    spindle(600); run_ms(200);
    long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(5000); SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1500);
    double dz_mm = (Motor_Z_Pos - z0) * (SCREW_Z / 100.0) / (MOTOR_Z_STEP_PER_REV * McSTEP_Z);
    double dx_mm = (Motor_X_Pos - x0) * (SCREW_X / 100.0) / (MOTOR_X_STEP_PER_REV * McSTEP_X);
    double k = fabs(dx_mm / dz_mm);
    double x_step_mm = (SCREW_X / 100.0) / (MOTOR_X_STEP_PER_REV * McSTEP_X);
    // 4 шага X или 0.1 % (делитель конуса хранится с 4 знаками после запятой)
    CHECK(fabs(fabs(dx_mm) - slope * fabs(dz_mm)) <= fmax(4 * x_step_mm, 0.001 * fabs(dx_mm)), "уклон %.4f, ожидалось %.4f (X %.4f мм вместо %.4f)", k, slope, fabs(dx_mm), slope * fabs(dz_mm));
    printf("    Z %.3f мм, X %.3f мм (радиус), уклон %.4f\n", dz_mm, dx_mm, k);
    spindle(0); run_ms(50);
}

static void test_sphere() {
    printf("[%7.1f с] [sphere] ", t_ns / 1e9); printf("шар R=5 мм, ножка 0, резец 2 мм, шаг 0.5 мм\n");
    select_mode(6); controls_neutral(); run_ms(100);
    touch("SPHERE:500"); touch("BAR:0");
    // упоры: правый (Z) и задний (X) — в текущей точке
    SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100);
    SET_IN(E2_PIN_LIM_REAR, 0); run_ms(150); SET_IN(E2_PIN_LIM_REAR, 1); run_ms(100);
    CHECK(limit_Right_flag && limit_Rear_flag, "упоры R=%d B=%d", limit_Right_flag, limit_Rear_flag);
    SET_IN(E2_PIN_SUB_MAN, 1); SET_IN(E2_PIN_SUB_EXT, 0); run_ms(200);
    CHECK(Sub_Mode_Sphere == 3, "подрежим шара %d (err1=%d err2=%d)", Sub_Mode_Sphere, err_1_flag, err_2_flag);
    long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
    spindle(600); run_ms(200);
    s_x_turns.clear(); s_x_last_dir = -1;
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0;
    while (!Complete_flag && t < 300000) { run_ms(100); t += 100; }
    bool done = Complete_flag;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
    spindle(0); run_ms(100);
    CHECK(done, "шар не завершён за %.0f с", t / 1000);
    CHECK((long)s_x_turns.size() == Pass_Total_Sphr + 1, "врезаний X %zu, ожидалось %ld", s_x_turns.size(), Pass_Total_Sphr + 1);
    // Глубина врезания до экватора: R − √(R² − (R − w·n)²) (на радиус)
    double R = 5.0, w = Cutting_Width / 100.0, worst = 0;
    double xs = (SCREW_X / 100.0) / (MOTOR_X_STEP_PER_REV * McSTEP_X);
    for (size_t n = 1; n <= s_x_turns.size() && n <= (size_t)Pass_Total_Sphr / 2; n++) {
        double depth = (s_x_turns[n - 1].x - x0) * xs;
        // + авторская поправка на набегающие микрошаги: McStep_Lag − Excess_Lag шагов
        double expect = R - sqrt(R * R - (R - w * n) * (R - w * n)) + (s_x_turns[n - 1].lag - Excess_Lag) * xs;
        worst = std::max(worst, fabs(depth - expect));
        if (getenv("SIM_DEBUG")) printf("      n=%zu lag=%d z=%+.3f depth=%.4f expect=%.4f\n", n, s_x_turns[n - 1].lag, (s_x_turns[n - 1].z - z0) * (SCREW_Z / 100.0) / (MOTOR_Z_STEP_PER_REV * McSTEP_Z), depth, expect);
    }
    CHECK(worst <= 2 * xs, "отклонение профиля %.4f мм", worst);
    printf("    врезаний %zu, отклонение профиля до экватора ≤ %.3f мм, %.1f с\n", s_x_turns.size(), worst, t / 1000);
    SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK");
}

// Общий прогон автоцикла: упоры Z, режим, подрежим (пины переключателя), джойстик влево до «ЗАВЕРШЕНА»
struct CycleResult { bool done; int passes; std::vector<MoveStart> starts; long dz, dx; double secs; };
static CycleResult run_cycle(const char* name, int mode, bool internal, std::function<void()> prep, double rpm) {
    printf("[%7.1f с] [cycle] %s\n", t_ns / 1e9, name);
    bool ok = setup_z_limits(600);
    CHECK(ok, "%s: упоры не установлены", name);
    select_mode(mode);
    prep();
    SET_IN(E2_PIN_SUB_MAN, 1); if (internal) SET_IN(E2_PIN_SUB_INT, 0); else SET_IN(E2_PIN_SUB_EXT, 0); run_ms(200);
    long x0 = Motor_X_Pos, z0 = Motor_Z_Pos;
    spindle(rpm); run_ms(300);
    s_z_starts.clear();
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0;
    while (!Complete_flag && t < 400000) { run_ms(100); t += 100; }
    CycleResult r{Complete_flag, 0, {}, Motor_Z_Pos - z0, Motor_X_Pos - x0, t / 1000};
    for (auto& m : s_z_starts) if (m.dir == 0) { r.passes++; r.starts.push_back(m); }
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
    spindle(0); run_ms(100);
    // выход: подрежим «Ручной», снять упоры
    SET_IN(E2_PIN_SUB_INT, 1); SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK");
    select_mode(1);
    if (limit_Left_flag)  { SET_IN(E2_PIN_LIM_LEFT, 0);  run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1);  run_ms(100); }
    if (limit_Right_flag) { SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100); }
    CHECK(r.done, "%s: цикл не завершился за %.0f с", name, r.secs);
    printf("    завершён=%d, рабочих проходов %d, %.1f с, Z %+ld X %+ld шагов\n", r.done, r.passes, r.secs, r.dz, r.dx);
    return r;
}

static long line_of(const MoveStart& m) { return m.line; }

static void test_more_cycles() {
    // Внутренняя резьба 1.5 мм
    auto r1 = run_cycle("резьба «Внутренний» 1.50 мм", 3, true, [] { touch("THREAD_STEP:13"); }, 300);
    CHECK(r1.passes == Thread_Info[13].Pass + PASS_FINISH, "внутр. резьба: проходов %d", r1.passes);
    for (auto& m : r1.starts) CHECK(labs(line_of(m) - line_of(r1.starts[0])) <= 1, "внутр. резьба: проход не в нитку");
    // Двухзаходная резьба 1.5 мм: вторая нитка смещена на пол-оборота
    auto r2 = run_cycle("резьба «Наружный», 2 захода (Ph=2)", 3, false, [] { touch("THREAD_STEP:13"); touch("PH:2"); }, 300);
    // 7e2: второй заход — тот же угол шпинделя, старт по Z сдвинут на шаг резьбы
    std::vector<long> zs;
    for (auto& m : r2.starts) if (std::find(zs.begin(), zs.end(), m.z) == zs.end()) zs.push_back(m.z);
    std::sort(zs.begin(), zs.end());
    long pitch_steps = lround(1.5 / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z);
    printf("    старты по Z (шаги):"); for (long z : zs) printf(" %ld", z); printf("; шаг резьбы = %ld шагов\n", pitch_steps);
    CHECK(r2.passes == (Thread_Info[13].Pass + PASS_FINISH) * 2, "2 захода: проходов %d", r2.passes);
    CHECK(zs.size() == 2 && zs[1] - zs[0] == pitch_steps, "2 захода: старты по Z не сдвинуты на шаг");
    for (auto& m : r2.starts) CHECK(labs(line_of(m) - line_of(r2.starts[0])) <= 1, "2 захода: разный угол шпинделя на старте");
    touch("PH:1");
    // Подача «Внутренний»
    auto r3 = run_cycle("подача «Внутренний», 2 прохода по 0.20", 1, true, [] { touch("FEED:20"); touch("AP:20"); Pass_Total = 2; Pass_Nr = 1; }, 600);
    CHECK(r3.passes == 2, "внутр. подача: проходов %d", r3.passes);
    // Асинхронная подача «Наружный»
    auto r4 = run_cycle("асинхр. подача «Наружный», 2 прохода по 0.20", 2, false, [] { touch("AFEED:200"); touch("AP:20"); Pass_Total = 2; Pass_Nr = 1; }, 0);
    CHECK(r4.passes == 2, "асинхр. автоцикл: проходов %d", r4.passes);
    // Конус «Наружный»
    auto r5 = run_cycle("конус 30° «Наружный», 2 прохода", 4, false, [] { touch("CONE:1"); touch("FEED:20"); touch("AP:20"); Pass_Total = 2; Pass_Nr = 1; }, 600);
    CHECK(r5.passes == 2, "конус автоцикл: проходов %d", r5.passes);
}

// Ход многозаходной резьбы: Ph × шаг за оборот
static void test_thread_multistart() {
    printf("[%7.1f с] [thread-ph] резьба 1.00 мм, 2 захода: ход за оборот\n", t_ns / 1e9);
    controls_neutral(); select_mode(3); touch("THREAD_STEP:11"); touch("PH:2");
    spindle(200); run_ms(300); s_z_at_rev.clear();
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(60000.0 / 200 * 10);
    double spr = steps_per_rev(5);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(800); spindle(0); run_ms(50); touch("PH:1");
    double want = 2.0 / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z;   // ход 2.00 мм за оборот
    printf("    шагов/об %.2f (ожидалось %.0f — ход 2.00 мм)\n", spr, want);
    CHECK(fabs(spr - want) < 0.5, "2 захода: шагов/об %.2f, ожидалось %.0f", spr, want);
}

// Тахометр: в режиме резьбы (INT0) — импульс на каждый оборот шпинделя
static void test_tacho() {
    printf("[%7.1f с] [tacho] выход тахометра в режиме «Резьба»\n", t_ns / 1e9);
    controls_neutral(); select_mode(3);
    spindle(300); run_ms(500);
    long t0 = s_tacho_rises, l0 = spindle_lines; run_ms(4000);
    double revs = (spindle_lines - l0) / (double)ENC_LINE_PER_REV;
    long pulses = s_tacho_rises - t0;
    printf("    импульсов %ld за %.1f об\n", pulses, revs);
    CHECK(labs(pulses - lround(revs)) <= 1, "тахометр: %ld импульсов на %.1f об", pulses, revs);
    spindle(0); run_ms(50);
}

// ============================================================================
// Матрица автоциклов (SIM_CYCLES=1): упоры, число и глубина проходов, счётчики на LCD2004
// и новом дисплее по ходу цикла, завершение и возврат в исходную точку.
// SIM_CYC_ONLY=<подстрока> — только случаи с этой подстрокой в названии.
// ============================================================================
static int thread_idx(const char* print) {               // индекс резьбы по надписи таблицы ("1.50mm", "18tpi"…)
    for (int i = 0; i < (int)TOTAL_THREADS; i++) {
        std::string t = Thread_Info[i].Thread_Print;
        t.erase(0, t.find_first_not_of(' ')); t.erase(t.find_last_not_of(' ') + 1);
        if (t == print) return i;
    }
    return -1;
}
static long trailing_num(const std::string& s) {         // число в конце строки LCD («Циклов:   5»)
    size_t e = s.find_last_of("0123456789");
    if (e == std::string::npos) return -1;
    size_t b = s.find_last_not_of("0123456789", e);
    return atol(s.substr(b + 1, e - b).c_str());
}
static void press_key(const char* k, int n) { for (int i = 0; i < n; i++) { touch(k); run_ms(60); } }

// Длина между упорами задаётся временем ускоренного хода; с разгоном это прямая со смещением —
// калибруем по двум точкам
static double s_rapid_k = 0, s_rapid_b = 0;              // мм = k·мс + b
static double z_len_mm() { return (Limit_Pos_Left - Limit_Pos_Right) * (SCREW_Z / 100.0) / (MOTOR_Z_STEP_PER_REV * McSTEP_Z); }
static bool setup_z_len(double mm) {
    if (s_rapid_k == 0) {
        bool ok1 = setup_z_limits(800);  double a = z_len_mm();
        bool ok2 = setup_z_limits(1600); double b = z_len_mm();
        printf("    калибровка ускоренного хода: 800 мс → %.2f мм, 1600 мс → %.2f мм\n", a, b);
        CHECK(ok1 && ok2 && b > a, "калибровка длины между упорами не удалась");
        s_rapid_k = (b - a) / 800.0; s_rapid_b = a - s_rapid_k * 800;
    }
    return setup_z_limits(std::max(800.0, (mm - s_rapid_b) / s_rapid_k));
}

struct CycCase {
    std::string name; int mode; bool internal; double len_mm; double rpm;
    std::function<void()> prep;          // параметры в «Ручном» (кнопки/касания)
    int expect_passes;                    // рабочих проходов Z (с чистовыми и заходами)
    std::function<long(int)> infeed;      // X рабочего прохода k (1..) от старта, шаги (nullptr — не проверять)
};

static void run_cyc(const CycCase& c) {
    printf("[%7.1f с] [cyc] %s\n", t_ns / 1e9, c.name.c_str());
    bool ok = setup_z_len(c.len_mm);
    CHECK(ok, "%s: упоры Z не установлены", c.name.c_str());
    long L = Limit_Pos_Left, R = Limit_Pos_Right;
    select_mode(c.mode);
    c.prep();
    long pt_set = Pass_Total;
    SET_IN(E2_PIN_SUB_MAN, 1); if (c.internal) SET_IN(E2_PIN_SUB_INT, 0); else SET_IN(E2_PIN_SUB_EXT, 0); run_ms(300);
    CHECK(!err_1_flag && !err_2_flag, "%s: подрежим не включился (err1=%d err2=%d)", c.name.c_str(), err_1_flag, err_2_flag);
    long x0 = Motor_X_Pos, z0 = Motor_Z_Pos;
    size_t mark = DISPLAY_UART.out.size();
    spindle(c.rpm); run_ms(300);
    s_z_starts.clear();
    s_env_ox = Motor_X_Pos - s_car_x; s_env_oz = Motor_Z_Pos - s_car_z;   // суппорт стоит: позиция = каретка
    s_env_zmin = s_env_zmax = z0; s_env_xmin = s_env_xmax = x0; s_env_on = true;
    SET_IN(E2_PIN_JOY_LEFT, 0);                           // старт: джойстик в сторону реза и держать
    double t = 0;
    // ускоренный ход ограничен частотой шагов: при микрошаге больше 4 он медленнее в мм/с — больше времени
    double t_max = 900000.0 * (McSTEP_Z > 4 ? McSTEP_Z / 4.0 * 1.5 : 1.0);
    while (!Complete_flag && t < t_max) { run_ms(100); t += 100; }
    bool done = Complete_flag;
    run_ms(200);
    std::string lcd_end = lcd_text();
    s_env_on = false;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000);
    spindle(0); run_ms(100);
    std::string disp = DISPLAY_UART.out.substr(mark);

    // рабочие проходы (влево) и возвраты (вправо); счётчики снимаем в начале возврата — к этому
    // моменту LCD2004 (перерисовка 40 мс) и новый дисплей уже обновлены
    std::vector<MoveStart> work, back;
    for (auto& m : s_z_starts) (m.dir == 0 ? work : back).push_back(m);
    const long tolZ = 4, tolX = McSTEP_X * 2;
    long pitch = c.mode == 3 ? lround(Thread_Info[Thread_Step].Step / (SCREW_Z / 100.0) * MOTOR_Z_STEP_PER_REV * McSTEP_Z) : 0;
    long xmax_in = 0;
    for (int k = 1; c.infeed && k <= c.expect_passes; k++) xmax_in = std::max(xmax_in, c.infeed(k));
    int sgn = c.internal ? -1 : 1;

    CHECK(done, "%s: цикл не завершился за %.0f с", c.name.c_str(), t / 1000);
    CHECK((int)work.size() == c.expect_passes, "%s: рабочих проходов %zu, ожидалось %d", c.name.c_str(), work.size(), c.expect_passes);
    // упоры Z: ни шага за левый; за правый — только на сдвиг заходов многозаходной резьбы
    CHECK(s_env_zmax <= L + tolZ, "%s: Z ушёл за левый упор на %ld шагов", c.name.c_str(), s_env_zmax - L);
    CHECK(s_env_zmin >= R - pitch * (Ph - 1) - tolZ, "%s: Z ушёл за правый упор на %ld шагов", c.name.c_str(), R - s_env_zmin);
    // X: от отскока за старт до полной глубины, не дальше
    if (c.infeed) {
        long lo = sgn > 0 ? x0 - REBOUND_X - tolX : x0 - xmax_in - tolX;
        long hi = sgn > 0 ? x0 + xmax_in + tolX : x0 + REBOUND_X + tolX;
        CHECK(s_env_xmin >= lo && s_env_xmax <= hi, "%s: X вне [%+ld..%+ld]: %+ld..%+ld", c.name.c_str(), lo - x0, hi - x0, s_env_xmin - x0, s_env_xmax - x0);
    }
    // каждый проход дошёл до левого упора (возврат начинается у него)
    int short_cut = 0;
    for (auto& b : back) if (fabs(b.cz - L) > tolZ) short_cut++;
    CHECK(short_cut == 0, "%s: %d возвратов начались не у левого упора", c.name.c_str(), short_cut);
    // врезание по формуле 7e2
    if (c.infeed) {
        int bad = 0;
        for (size_t i = 0; i < work.size(); i++) {
            long want = c.infeed((int)(i / Ph) + 1);
            long got = lround((work[i].cx - x0) * sgn);
            if (labs(got - want) > McSTEP_X) { if (bad++ < 3) printf("      проход %zu: X %+ld, по формуле %+ld\n", i + 1, got, want); }
        }
        CHECK(bad == 0, "%s: %d проходов с неверным врезанием", c.name.c_str(), bad);
    }
    // счётчик «осталось» на LCD2004 и на новом дисплее совпадает и не растёт
    int mism = 0, nonmono = 0; long prev = 1 << 30;
    long cycl = Thread_Info[Thread_Step].Pass + PASS_FINISH + Pass_Fin + Thr_Pass_Summ;
    {
        size_t p = disp.rfind("<THREAD_CYCL:");
        if (p != std::string::npos) cycl = atol(disp.c_str() + p + 13);
    }
    for (auto& b : back) {
        long lcd_left = trailing_num(b.lcd_row2);
        long nr = -1, tot = -1;
        sscanf(b.disp_pass.c_str(), "%ld,%ld", &nr, &tot);
        if (c.mode == 3) tot = cycl;
        long disp_left = tot - nr + 1;
        if (lcd_left != disp_left) { if (mism++ < 3) printf("      LCD «%s» (осталось %ld), дисплей PASS %s → %ld\n", b.lcd_row2.c_str(), lcd_left, b.disp_pass.c_str(), disp_left); }
        if (lcd_left > prev) nonmono++;
        prev = lcd_left;
    }
    CHECK(mism == 0, "%s: счётчик проходов LCD и нового дисплея разошёлся %d раз", c.name.c_str(), mism);
    CHECK(nonmono == 0, "%s: счётчик «осталось» на LCD рос %d раз", c.name.c_str(), nonmono);
    CHECK(lcd_end.find("3ABEP") != std::string::npos, "%s: на LCD нет «ОПЕРАЦИЯ ЗАВЕРШЕНА»", c.name.c_str());
    CHECK(disp.find("<ALERT:3>") != std::string::npos, "%s: новый дисплей не получил «ОПЕРАЦИЯ ЗАВЕРШЕНА»", c.name.c_str());
    // Конец цикла (7e2): Z — к правому упору (старт). X: резьба и аподача — в ноль; подача и конус —
    // резец остаётся на глубине последнего прохода (Feed.ino: Limit_Pos_Rear + REBOUND_X), видно итоговый размер
    CHECK(Motor_Z_Pos == z0, "%s: Z не вернулся к старту: %+ld", c.name.c_str(), Motor_Z_Pos - z0);
    if (c.mode == 3 || c.mode == 2) CHECK(Motor_X_Pos == x0, "%s: X не вернулся в ноль: %+ld", c.name.c_str(), Motor_X_Pos - x0);
    else if (c.infeed) CHECK(labs((Motor_X_Pos - x0) * sgn - c.infeed(c.expect_passes)) <= McSTEP_X,
                             "%s: X в конце %+ld, глубина последнего прохода %+ld", c.name.c_str(), Motor_X_Pos - x0, c.infeed(c.expect_passes) * sgn);
    printf("    длина %.1f мм, проходов %zu/%d (Pass_Total %ld), X %+ld..%+ld, %.0f с; LCD «осталось»:",
           (L - R) * (SCREW_Z / 100.0) / (MOTOR_Z_STEP_PER_REV * McSTEP_Z), work.size(), c.expect_passes, pt_set,
           s_env_xmin - x0, s_env_xmax - x0, t / 1000);
    for (auto& b : back) printf(" %ld", trailing_num(b.lcd_row2));
    printf("\n");

    // выход: «Ручной», снять упоры, вернуть заходы
    SET_IN(E2_PIN_SUB_INT, 1); SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK"); touch("PH:1");
    select_mode(1);
    if (limit_Left_flag)  { SET_IN(E2_PIN_LIM_LEFT, 0);  run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1);  run_ms(100); }
    if (limit_Right_flag) { SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100); }
}

// Врезание 7e2: подача/аподача — Ap/2 на проход (Ap на диаметр, целочисленное деление как в Feed.ino)
static std::function<long(int)> feed_infeed(int ap) {
    long v = (int)((float)MOTOR_X_STEP_PER_REV * (ap / 2) / SCREW_X + 0.5) * McSTEP_X;
    return [v](int k) { return k * v; };
}
// Асинхронная подача (aFeed.ino): съём на радиус («Съём R») — Ap целиком, k-й проход на k·Ap от нуля
static std::function<long(int)> afeed_infeed(int ap) {
    long v = (int)(((float)MOTOR_X_STEP_PER_REV * ap / SCREW_X + 0.5) * McSTEP_X);
    return [v](int k) { return k * v; };
}
// Резьба: глубина k-го прохода по Thread.ino (√-закон, первый — √0.3); чистовые — на полной глубине.
// Рабочая высота профиля: наружная P·0.866·(1−1/6−1/8), внутренняя P·0.866·(1−1/4−1/18)
static std::function<long(int)> thread_infeed(int idx, bool internal) {
    return [idx, internal](int k) {
        int N = Thread_Info[idx].Pass + Thr_Pass_Summ;
        if (k > N) k = N;
        float Step_mm = Thread_Info[idx].Step;
        float H = internal ? (Step_mm * 0.866) - (Step_mm * 0.866 / 4) - (Step_mm * 0.866 / 18)
                           : (Step_mm * 0.866) - (Step_mm * 0.866 / 6) - (Step_mm * 0.866 / 8);
        float d = k == 1 ? H / sqrt(N - 1) * sqrt(0.3) : H / sqrt(N - 1) * sqrt(k - 1);
        return long(d / ((float)SCREW_X / 100 / MOTOR_X_STEP_PER_REV) + 0.5) * McSTEP_X + McSTEP_X;
    };
}

static void set_passes(int n) {   // число проходов кнопками «Влево»/«Вправо» в экране 1 (как на пульте)
    for (int i = 0; i < 20 && Pass_Total > 1; i++) { touch("KEY:LEFT"); run_ms(60); }
    press_key("KEY:RIGHT", n - 1);
}

static void test_cycle_matrix() {
    // механика с люфтом, равным компенсации из els_config.h (BACKLASH_*_UM; 0 — как у автора)
    // после включения сторона выбора люфта неизвестна — как оператор, подвигаем суппорт перед работой
    select_mode(2); touch("AFEED:300"); run_ms(100);
    SET_IN(E2_PIN_JOY_DOWN, 0); run_ms(300); SET_IN(E2_PIN_JOY_DOWN, 1); run_ms(300);
    SET_IN(E2_PIN_JOY_UP, 0);   run_ms(300); SET_IN(E2_PIN_JOY_UP, 1);   run_ms(300);
    select_mode(1);
    s_bl_x = e2_bl_x; s_bl_z = e2_bl_z;
    // люфт в модели выбран в ту же сторону, что считает прошивка (e2_bl_dir_*), иначе — сдвиг на поллюфта
    s_car_x = s_x.count - e2_bl_loaded_x * s_bl_x / 2.0;
    s_car_z = s_z.count - e2_bl_loaded_z * s_bl_z / 2.0;
    printf("    модель люфта: X %ld, Z %ld шагов (= компенсации BACKLASH_X_UM %d, BACKLASH_Z_UM %d мкм)\n",
           s_bl_x, s_bl_z, (int)BACKLASH_X_UM, (int)BACKLASH_Z_UM);
    std::vector<CycCase> cs;
    // ── Подача (синхронная): число проходов × съём, наружная и внутренняя ──
    for (bool in : {false, true})
        for (int n : {1, 3, 5})
            for (int ap : {10, 50}) {
                char nm[96]; snprintf(nm, sizeof nm, "подача %s, %d прох. × съём %.2f мм, 30 мм", in ? "внутр." : "наружн.", n, ap / 100.0);
                cs.push_back({nm, 1, in, 30, 600, [n, ap] { char b[24]; touch("FEED:20"); snprintf(b, sizeof b, "AP:%d", ap); touch(b); set_passes(n); },
                              n, feed_infeed(ap)});
            }
    // ── Асинхронная подача (без вращения шпинделя) ──
    for (bool in : {false, true})
        for (int n : {2, 4}) {
            char nm[96]; snprintf(nm, sizeof nm, "аподача %s, %d прох. × 0.20 мм, 20 мм", in ? "внутр." : "наружн.", n);
            cs.push_back({nm, 2, in, 20, 0, [n] { touch("AFEED:300"); touch("AP:20"); set_passes(n); }, n, afeed_infeed(20)});
        }
    // ── Конусы ──
    for (int m : {4, 5})
        for (bool in : {false, true}) {
            char nm[96]; snprintf(nm, sizeof nm, "конус %s 30° %s, 3 прохода × 0.20 мм", m == 4 ? "<" : ">", in ? "внутр." : "наружн.");
            cs.push_back({nm, m, in, 15, 600, [] { touch("CONE:1"); touch("FEED:20"); touch("AP:20"); set_passes(3); }, 3, nullptr});
        }
    // ── Резьба: шаги, длина 70 мм, внутренняя, 2 захода, +1 чистовой, дюймовая ──
    struct T { const char* p; double len; bool in; int ph; int fin; double rpm; };
    for (T t : std::vector<T>{ {"0.50mm", 20, false, 1, 0, 600}, {"1.00mm", 20, false, 1, 0, 500}, {"1.50mm", 70, false, 1, 0, 400},
                               {"2.00mm", 20, false, 1, 0, 300}, {"3.00mm", 20, false, 1, 0, 200}, {"1.50mm", 20, true, 1, 0, 400},
                               {"1.00mm", 20, true, 1, 1, 500}, {"1.50mm", 20, false, 2, 0, 300}, {"18tpi", 20, false, 1, 0, 400} }) {
        int idx = thread_idx(t.p);
        if (idx < 0) { CHECK(false, "нет резьбы %s в таблице", t.p); continue; }
        char nm[128]; snprintf(nm, sizeof nm, "резьба %s %s, %.0f мм%s%s", t.p, t.in ? "внутр." : "наружн.", t.len,
                               t.ph > 1 ? ", 2 захода" : "", t.fin ? ", +1 чистовой" : "");
        int fin = t.fin, ph = t.ph;
        int passes = (Thread_Info[idx].Pass + Thr_Pass_Summ + PASS_FINISH + Pass_Fin + fin) * ph;
        cs.push_back({nm, 3, t.in, t.len, t.rpm, [idx, fin, ph] {
                          char b[32]; snprintf(b, sizeof b, "THREAD_STEP:%d", idx); touch(b);
                          snprintf(b, sizeof b, "PH:%d", ph); touch(b);
                          if (fin) { touch("PARAM_OK"); run_ms(100); press_key("KEY:RIGHT", fin); touch("PARAM_OK"); touch("PARAM_OK"); run_ms(100); }
                      }, passes, thread_infeed(idx, t.in)});
    }
    const char* only = getenv("SIM_CYC_ONLY");
    for (auto& c : cs) {
        if (only && c.name.find(only) == std::string::npos) continue;
        int fin0 = Pass_Fin;
        run_cyc(c);
        if (Pass_Fin != fin0) {                     // вернуть число чистовых
            select_mode(3); touch("PARAM_OK"); run_ms(100);
            for (int i = 0; i < 10 && Pass_Fin > fin0; i++) { touch("KEY:LEFT"); run_ms(60); }
            touch("PARAM_OK"); touch("PARAM_OK"); select_mode(1);
        }
    }
}


// ============================================================================
// Эксперимент с люфтом (SIM_BACKLASH=1): насколько реально врезается резец в автоциклах 7e2,
// если у поперечного винта люфт. Ноль — подвод к детали со стороны врезания или с обратной.
// ============================================================================
static void jog_x(int dir, double ms) {                   // dir: +1 — к детали (передний упор), −1 — от неё
    if (dir > 0) { SET_IN(E2_PIN_JOY_UP, 0);   run_ms(ms); SET_IN(E2_PIN_JOY_UP, 1); }
    else         { SET_IN(E2_PIN_JOY_DOWN, 0); run_ms(ms); SET_IN(E2_PIN_JOY_DOWN, 1); }
    run_ms(300);
}

static void backlash_case(const char* what, int mode, std::function<void()> prep, double rpm,
                          int last_dir, long expect_steps) {
    setup_z_len(15);
    select_mode(2); touch("AFEED:300"); run_ms(100);       // подвод X в аподаче — без вращения шпинделя
    jog_x(-last_dir, 400); jog_x(last_dir, 400);           // последнее движение перед нулём — last_dir
    select_mode(mode); prep();
    SET_IN(E2_PIN_SUB_MAN, 1); SET_IN(E2_PIN_SUB_EXT, 0); run_ms(300);
    double c0 = s_car_x; long m0 = Motor_X_Pos;
    s_car_x_max = s_car_x; s_car_x_min = s_car_x;
    spindle(rpm); run_ms(300);
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0;
    while (!Complete_flag && t < 600000) { run_ms(100); t += 100; }
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1000); spindle(0); run_ms(100);
    double real = s_car_x_max - c0;                        // насколько резец реально ушёл в деталь
    double mm_per = (SCREW_X / 100.0) / (MOTOR_X_STEP_PER_REV * McSTEP_X);
    printf("    %-36s ноль после движения %s: задано %.3f мм, резец реально %.3f мм, недорез %.3f мм\n",
           what, last_dir > 0 ? "К детали " : "ОТ детали", expect_steps * mm_per, real * mm_per, (expect_steps - real) * mm_per);
    (void)m0;
    if (getenv("SIM_FIXDBG")) printf("      [доводка] знак %d, ноль цикла %d (s %d m %ld), доведён проход %d, блок %d\n",
        e2_scale_sign[1], e2_null_ok, (int)e2_null_s, e2_null_m, e2_fixed_pass, e2_loss_block);
    if (getenv("SIM_FIXDBG")) {
        const std::string& o = DISPLAY_UART.out;
        for (const char* w : {"ПРОПУСК", "РАСХОЖДЕНИЕ", "НЕ СОВПАДАЕТ", "НЕ ГОТОВА"}) {
            size_t p = o.rfind(w);
            if (p != std::string::npos) printf("      [сообщение] %s\n", o.substr(p, o.find('>', p) - p).c_str());
        }
    }
#if SCALE_FIX_X && ELS_POS_SOURCE == POS_SOURCE_DRO_UART
    // доводка X по линейке: глубина по линейке от нуля цикла — люфт и сторона подвода не важны
    CHECK(fabs((expect_steps - real) * mm_per) < 0.010, "%s, ноль %s: с доводкой X недорез %.3f мм",
          what, last_dir > 0 ? "к детали" : "от детали", (expect_steps - real) * mm_per);
#endif
    SET_IN(E2_PIN_SUB_EXT, 1); SET_IN(E2_PIN_SUB_MAN, 0); run_ms(200);
    touch("ALERT_OK"); select_mode(1);
    if (limit_Left_flag)  { SET_IN(E2_PIN_LIM_LEFT, 0);  run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1);  run_ms(100); }
    if (limit_Right_flag) { SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100); }
}

// Пропуск шагов (BACKLOG-025): каретка «теряет» 0.25 мм — на остановке сообщение и блок джойстика
static void test_steploss() {
#if STEP_LOSS_CHECK && ELS_POS_SOURCE == POS_SOURCE_DRO_UART   // DRO-заглушка следует за кареткой (линеек в заглушке нет)
    printf("[%7.1f с] [steploss] ", t_ns / 1e9); printf("контроль пропуска шагов по DRO\n");
    controls_neutral(); select_mode(2); touch("AFEED:300"); touch("S2"); run_ms(200);
    jog_x(1, 700); run_ms(500); jog_x(-1, 700); run_ms(500);          // направление линейки (ход > 2 мм), опора
    CHECK(e2_scale_sign[1] != 0 && !e2_loss_block, "направление линейки X не определено (%d)", e2_scale_sign[1]);
    size_t o = DISPLAY_UART.out.size();
    // посреди хода 200 шагов (0.25 мм) не дошли до каретки (стоя на месте скачок DRO — это обнуление блока)
    // ход в ту же сторону, что и предыдущий: без заданного люфта разворот не сверяется
    SET_IN(E2_PIN_JOY_DOWN, 0); run_ms(150); s_lost_x += 200; run_ms(150); SET_IN(E2_PIN_JOY_DOWN, 1); run_ms(700);
    CHECK(e2_loss_block && DISPLAY_UART.out.find("ПРОПУСК ШАГОВ X", o) != std::string::npos,
          "пропуск 0.25 мм не замечен (блок %d)", e2_loss_block);
    long m = Motor_X_Pos;
    SET_IN(E2_PIN_JOY_UP, 0); run_ms(300);
    CHECK(Motor_X_Pos == m, "при блокировке джойстик двигает X");
    SET_IN(E2_PIN_JOY_UP, 1); run_ms(1300);
    CHECK(!e2_loss_block, "блокировка не снялась после отпускания джойстика");
    s_lost_x = 0; e2_scale_reref();
    touch("ALERT_OK"); controls_neutral(); run_ms(200);
#endif
}

static void test_backlash() {
    double mm_per = (SCREW_X / 100.0) / (MOTOR_X_STEP_PER_REV * McSTEP_X);
    for (double bl_mm : {0.0, 0.10, 0.20}) {
        s_bl_x = lround(bl_mm / mm_per);
        s_car_x = s_x.count; s_car_z = s_z.count;
        printf("[люфт поперечного винта %.2f мм, отскок REBOUND_X %.2f мм, компенсация BACKLASH_X_UM = %d мкм]\n",
               bl_mm, REBOUND_X * mm_per, (int)BACKLASH_X_UM);
        for (int d : {+1, -1}) {
            backlash_case("подача 3 прох. × съём ⌀1.00 (R 0.5)", 1,
                          [] { touch("FEED:20"); touch("AP:100"); set_passes(3); }, 600, d,
                          feed_infeed(100)(3));
            int idx = thread_idx("1.25mm");
            backlash_case("резьба 1.25 наружн.", 3,
                          [idx] { char b[32]; snprintf(b, sizeof b, "THREAD_STEP:%d", idx); touch(b); touch("PH:1"); }, 400, d,
                          thread_infeed(idx, false)(Thread_Info[idx].Pass + Thr_Pass_Summ));
        }
    }
}

int main() {
    controls_neutral();
    // SIM_MECH="энкодер,шагов Z,микрошаг Z,винт Z,шагов X,микрошаг X,винт X" — весь набор на другой механике
    // (записывается во флеш до старта: проверяет и загрузку настроек), например SIM_MECH=3600,200,8,150,200,4,125
    if (const char* sm = getenv("SIM_MECH")) {
        E2Mech m = E2_MECH_CONFIG;
        sscanf(sm, "%d,%d,%d,%d,%d,%d,%d", &m.enc_lines, &m.z_steps, &m.z_mc, &m.z_screw, &m.x_steps, &m.x_mc, &m.x_screw);
        const char* err = e2_mech_apply(m);
        if (err) { printf("SIM_MECH: %s\n", err); return 1; }
        e2_mech_save();
        printf("Механика: энкодер %d, Z %d×%d винт %.2f, X %d×%d винт %.2f\n", m.enc_lines, m.z_steps, m.z_mc,
               m.z_screw / 100.0, m.x_steps, m.x_mc, m.x_screw / 100.0);
    }
    setup();
    check_cnt_resets();
    run_ms(500);
    test_boot();
    if (e2_mech_is_stamp(E2_MECH_CONFIG) && !getenv("SIM_MECH")) test_mech();
    if (getenv("SIM_MOVEDBG")) {
        for (long n : {5L, 8L, -5L, 40L}) {
            long c0 = s_x.count; e2_cal_abort = false; e2_cal_joy = false; e2_cal_move('X', n); run_ms(5);
            printf("e2_cal_move(X, %ld): импульсов %ld, каретка %.0f\n", n, s_x.count - c0, s_car_x);
        }
        return 0;
    }   // проверки — под механику автора
    if (getenv("SIM_BACKLASH")) {                    // эксперимент: люфт и точность автоциклов
        controls_neutral(); run_ms(200);
        test_backlash();
        return 0;
    }
    if (getenv("SIM_CYCLES")) {                      // только матрица автоциклов
        controls_neutral(); run_ms(200);
        test_cycle_matrix();
        printf("\nИТОГ: %d проверок OK, %d FAIL\n", s_pass, s_fail);
        return s_fail ? 1 : 0;
    }
    test_modes_touch();
    controls_neutral(); run_ms(100);
    test_thread(11, 1.00, 300);
    test_thread(12, 1.25, 300);
    test_thread(14, 1.75, 200);
    test_thread(0, 0.20, 600);
    test_thread(16, 2.50, 120);
    test_thread(30, 1.411, 300);   // 18tpi
    test_thread_sync();
    test_feed(10);
    test_feed(25);
    test_afeed(100);
    test_afeed(400);
    test_rapid();
    test_hand();
    test_limit();
    test_limit_no_switch();
    test_axis_set();
    test_steploss();
    test_cal_step();
    test_physical_dupes();
    test_gcode();
    test_thread_ext_cycle(13);
    test_feed_ext_cycle();
    test_x_axis();
    test_thread_ccw();
    test_hand_x10_x();
    test_divider();
    test_cone(0, 1.0);                     // 45°
    test_cone(1, tan(30 * M_PI / 180));    // 30°
    test_cone(55, 1.0 / 20);               // 1:10 (на диаметр)
    test_cone(45, 1.0 / 2 / 19.212);       // KM0 (Морзе 0: 1:19.212)
    test_sphere();
    test_thread_multistart();
    test_tacho();
    test_more_cycles();
    CHECK(Serial.out.find("[IO] Джойстик Влево: замкнуто на GND") != std::string::npos, "монитор входов не сообщил о джойстике");
    CHECK(Serial.out.find("[IO] Энкодер шпинделя:") != std::string::npos, "монитор входов не сообщил об энкодере");
    CHECK(Serial.out.find("[IO] РГИ:") != std::string::npos, "монитор входов не сообщил о РГИ");
    printf("[step] минимальная длительность уровня STEP: Z %.2f мкс (%ld фронтов), X %.2f мкс (%ld фронтов)\n",
           s_ez.min_cyc / 168.0, s_ez.n, s_ex.min_cyc / 168.0, s_ex.n);
    CHECK(s_ez.min_cyc >= (uint64_t)STEP_PULSE_MIN_US * 168 && s_ex.min_cyc >= (uint64_t)STEP_PULSE_MIN_US * 168,
          "импульс STEP короче %d мкс", STEP_PULSE_MIN_US);
    printf("\nLCD:\n%s", lcd_text().c_str());
    printf("\nИТОГ: %d проверок OK, %d FAIL\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}

long e2_dbg_pulses_x(void) { return s_x.count; }
