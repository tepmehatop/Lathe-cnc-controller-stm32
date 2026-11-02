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

// Фронты STEP
struct StepLog { long count = 0; uint32_t prev = 1; uint64_t last_edge = 0; uint64_t min_level_ns = ~0ull; };
static StepLog s_z, s_x;
static std::vector<long> s_z_at_rev;   // счётчик шагов Z на каждом обороте шпинделя

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
    chk(s_z, E2_ODR(E2_PIN_Z_STEP), E2_ODR(E2_PIN_Z_DIR));
    chk(s_x, E2_ODR(E2_PIN_X_STEP), E2_ODR(E2_PIN_X_DIR));
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
        if (ev < 0) { t_ns = t_end; sim_now_us = t_ns / 1000; return; }
        t_ns = nxt; sim_now_us = t_ns / 1000;
        if (ev < 3) { s_ts[ev].t_cnt0 = t_ns; s_timers[ev].cb(); }
        else if (ev == 3) {
            spindle_edge();
            double q_ns = 60e9 / (fabs(spindle_rpm) * ENC_LINE_PER_REV * 4);
            spindle_next = t_ns + (uint64_t)q_ns;
        } else {
            loop_next = t_ns + LOOP_PERIOD_NS;
            in_loop++; loop(); in_loop--;
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
    printf("[boot] старт, заставка LCD, обмен с дисплеем\n");
    std::string t = lcd_text();
    CHECK(DISPLAY_UART.out.find("<PING>") != std::string::npos, "нет PING дисплею");
    CHECK(DISPLAY_UART.out.find("<MODE:1>") != std::string::npos, "нет MODE:1 в начальном состоянии");
    CHECK(Serial.out.find("SETUP() ЗАВЕРШЕН") != std::string::npos, "setup() не завершился");
    CHECK(Mode == 1, "режим после старта %d, ожидался 1 (Подача)", Mode);
}

static void test_thread(int idx, double pitch_mm, double rpm) {
    printf("[thread] резьба #%d шаг %.3f мм, %g об/мин\n", idx, pitch_mm, rpm);
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
    printf("[thread-sync] повторный проход попадает в ту же нитку\n");
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
    printf("[feed] синхронная подача %.2f мм/об\n", feed / 100.0);
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
    printf("[afeed] асинхронная подача %d мм/мин\n", afeed);
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
    printf("[rapid] ускоренный ход Z с кнопкой\n");
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
    printf("[limit] упор слева останавливает подачу\n");
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
    printf("[hand] РГИ: ось Z, x1, 25 щелчков\n");
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
    printf("[modes] режимы и подрежимы с дисплея, строка LCD2004\n");
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
    printf("\nLCD:\n%s", lcd_text().c_str());
    printf("\nИТОГ: %d проверок OK, %d FAIL\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
