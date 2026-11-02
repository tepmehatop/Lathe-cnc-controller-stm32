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
void sim_gpio_changed(GPIO_TypeDef* g, uint32_t o, uint32_t n) {
    uint64_t c = t_ns_for_cyc() * 168 / 1000 + s_cyc_extra;
    auto chk = [&](EdgeLog& l, uint32_t bit) {
        if (((o ^ n) & bit) == 0) return;
        if (l.last && c - l.last < l.min_cyc) l.min_cyc = c - l.last;
        l.last = c; l.n++;
    };
    if (g == E2_GPIO(E2_PIN_Z_STEP)) chk(s_ez, E2_BIT(E2_PIN_Z_STEP));
    if (g == E2_GPIO(E2_PIN_X_STEP)) chk(s_ex, E2_BIT(E2_PIN_X_STEP));
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
struct MoveStart { long line; long x; int dir; };
static std::vector<MoveStart> s_z_starts;   // начала движений Z (после паузы ≥ 50 мс)
static uint64_t s_z_last_step = 0;
struct XTurn { long z; long x; int lag; };
static std::vector<XTurn> s_x_turns;   // точки, где X сменил направление с «вперёд» на «назад»
static int s_x_last_dir = -1;

#define PIN_BIT(...) E2_BIT(__VA_ARGS__)
static void set_in(GPIO_TypeDef* g, uint32_t bit, int level) { if (level) g->IDR |= bit; else g->IDR &= ~bit; }
#define SET_IN(pin, lvl) set_in(E2_GPIO(pin), E2_BIT(pin), (lvl))

static void sample_steps() {
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
            s_z_starts.push_back({spindle_lines_now(), Motor_X_Pos, (int)E2_DRV_RD(E2_PIN_Z_DIR)});
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

// Продвинуть время до t_end, обрабатывая события
static void run_until(uint64_t t_end, bool allow_loop) {
    for (;;) {
        uint64_t nxt = t_end; int ev = -1;
        for (int i = 0; i < 3; i++) if (s_timers[i].cb) {
            if (!s_ts[i].t_cnt0) s_ts[i].t_cnt0 = t_ns;       // таймер только что запущен
            uint64_t n = tim_next(i); if (n <= nxt) { nxt = n; ev = i; }
        }
        if (spindle_rpm != 0 && spindle_next <= nxt) { nxt = spindle_next; ev = 3; }
        if (allow_loop && loop_next <= nxt) { nxt = loop_next; ev = 4; }
        if (ev < 0) { if (t_end > t_ns) t_ns = t_end; sim_now_us = t_ns / 1000; return; }
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
    CHECK(fabs(spr - expect_avr) < 0.05, "шагов/об %.3f, по формуле 7e2 %.3f", spr, expect_avr);
    printf("    шагов/об %.3f (как у 7e2 на Mega: %.3f; идеально %.3f)\n", spr, expect_avr, ideal);
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

static void test_hand() {
    printf("[%7.1f с] [hand] ", t_ns / 1e9); printf("РГИ: ось Z, x1, 25 щелчков\n");
    select_mode(1);
    SET_IN(E2_PIN_HAND_X1, 0); SET_IN(E2_PIN_HAND_X10, 1); run_ms(100);
    SET_IN(E2_PIN_HAND_AXIS_Z, 0); run_ms(100);
    long z0 = Motor_Z_Pos, p0 = s_z.count;
    hand_turn(25, 20); run_ms(1500);
    long dz = Motor_Z_Pos - z0;
    long expect = 25L * HC_SCALE_1 * MOTOR_Z_STEP_PER_REV * McSTEP_Z / SCREW_Z;
    CHECK(dz == expect, "РГИ Z: %ld шагов, ожидалось %ld", dz, expect);
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
    SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); run_ms(100);
    SET_IN(E2_PIN_JOY_RAPID, 0); SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(len_ms); SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1500);
    SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); run_ms(100);
    SET_IN(E2_PIN_JOY_RIGHT, 0); run_ms(len_ms + 3000); SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(1500);
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
    CHECK(fabs(fabs(dx_mm) - slope * fabs(dz_mm)) <= 4 * x_step_mm, "уклон %.4f, ожидалось %.4f (X %.4f мм вместо %.4f)", k, slope, fabs(dx_mm), slope * fabs(dz_mm));
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

int main() {
    controls_neutral();
    setup();
    check_cnt_resets();
    run_ms(500);
    test_boot();
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
