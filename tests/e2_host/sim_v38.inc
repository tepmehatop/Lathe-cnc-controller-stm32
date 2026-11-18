// Проверки функций модов 7e2_Mod_lcd2004RU_A_Lach V3.4…V3.8 (перенесены в Core/e2/orig, 2026-11-18).
// Подключается из sim.cpp. Обычные, негативные и попарные (pairwise) сочетания:
//   [v38-tables]  таблица конусов V3.8 (+1:30), пересчёт под механику;
//   [v38-keys]    комбинации кнопок меню: обнуление линеек «ускоренная»+◀/▶, ввод диаметра (подэкран 3);
//   [v38-dist]    дистанционные лимиты (подэкран 4), кнопками пульта и с экрана (DIST:, EMUL:STOP_*);
//   [v38-kzr]     профиль резьбы (Kzr 1..4): переключение и глубина врезания в автоцикле — попарно;
//   [v38-pause]   пауза автоциклов: с экрана (PAUSE/RESUME) и кнопками ▼/▲; залипшие флаги касаний;
//   [v38-cone]    новые автоциклы конусов (наружные < и >, внутренний <, внутренний > — «невозможен»);
//   [v38-cut]     отрезание вразбежку (Raz=2): переключение, параметры, автоцикл, негативные;
//   [v38-afeed]   асинхронный автоцикл: округление съёма, число проходов сохраняется;
//   [v38-groove]  ручейки/«ёлочка» (синхронный торцевой автоцикл с отскоком Z 0 и натягом).

static void btn_press(std::initializer_list<int> which, double ms = 100) {   // «Выбор» — 2 опроса подряд (40–80 мс), дольше ~120 мс — автоповтор (Menu() раз в 40 мс)
    // which: 0 Left 1 Right 2 Up 3 Down 4 Select (активный уровень — 0)
    for (int w : which) switch (w) {
        case 0: SET_IN(E2_PIN_BTN_LEFT, 0); break;   case 1: SET_IN(E2_PIN_BTN_RIGHT, 0); break;
        case 2: SET_IN(E2_PIN_BTN_UP, 0); break;     case 3: SET_IN(E2_PIN_BTN_DOWN, 0); break;
        case 4: SET_IN(E2_PIN_BTN_SELECT, 0); break;
    }
    run_ms(ms);
    SET_IN(E2_PIN_BTN_LEFT, 1); SET_IN(E2_PIN_BTN_RIGHT, 1); SET_IN(E2_PIN_BTN_UP, 1);
    SET_IN(E2_PIN_BTN_DOWN, 1); SET_IN(E2_PIN_BTN_SELECT, 1);
    run_ms(100);
}
enum { B_L, B_R, B_U, B_D, B_S };
static void rapid(bool on) { SET_IN(E2_PIN_JOY_RAPID, on ? 0 : 1); run_ms(30); }
static void lim_btn(int pin_sel) {   // 0 L, 1 R, 2 F, 3 B
    switch (pin_sel) {
        case 0: SET_IN(E2_PIN_LIM_LEFT, 0); run_ms(150); SET_IN(E2_PIN_LIM_LEFT, 1); break;
        case 1: SET_IN(E2_PIN_LIM_RIGHT, 0); run_ms(150); SET_IN(E2_PIN_LIM_RIGHT, 1); break;
        case 2: SET_IN(E2_PIN_LIM_FRONT, 0); run_ms(150); SET_IN(E2_PIN_LIM_FRONT, 1); break;
        case 3: SET_IN(E2_PIN_LIM_REAR, 0); run_ms(150); SET_IN(E2_PIN_LIM_REAR, 1); break;
    }
    run_ms(100);
}
static void clear_limits() {
    if (limit_Left_flag)  lim_btn(0);
    if (limit_Right_flag) lim_btn(1);
    if (limit_Front_flag) lim_btn(2);
    if (limit_Rear_flag)  lim_btn(3);
}
static void sub_pins(int s) {   // 1 Внутр, 2 Ручн, 3 Наруж
    SET_IN(E2_PIN_SUB_INT, s == 1 ? 0 : 1); SET_IN(E2_PIN_SUB_MAN, s == 2 ? 0 : 1); SET_IN(E2_PIN_SUB_EXT, s == 3 ? 0 : 1);
    run_ms(200);
}
static void to_manual() { sub_pins(2); touch("ALERT_OK"); run_ms(100); }
static long steps_x_per_mm() { return (long)MOTOR_X_STEP_PER_REV * McSTEP_X * 100 / SCREW_X; }
static long steps_z_per_mm() { return (long)MOTOR_Z_STEP_PER_REV * McSTEP_Z * 100 / SCREW_Z; }
static bool disp_sent(const char* s) { return DISPLAY_UART.out.find(s) != std::string::npos; }

// ── [v38-tables] ─────────────────────────────────────────────────────────────
static void test_v38_tables() {
    printf("[%7.1f с] [v38-tables] таблица конусов V3.8 (+1:30), пересчёт под механику\n", t_ns / 1e9);
    CHECK(TOTAL_CONE == 65, "конусов %d, ожидалось 65", (int)TOTAL_CONE);
    auto nm = [](int i) { std::string s; for (const char* c = Cone_Info[i].Cone_Print; *c; c++) if (*c != ' ' && *c != 5) s += *c; return s; };
    CHECK(nm(0) == "45" && nm(1) == "30", "в начале 45° и 30°: %s %s", nm(0).c_str(), nm(1).c_str());
    for (int g = 1; g <= 45; g++) CHECK(nm(1 + g) == std::to_string(g), "ряд градусов: [%d]=%s, ожидалось %d", 1 + g, nm(1 + g).c_str(), g);
    CHECK(nm(47) == "KM0" && nm(53) == "KM6", "Морзе: %s..%s", nm(47).c_str(), nm(53).c_str());
    const char* ratios[] = {"1:4", "1:5", "1:7", "1:10", "1:16", "1:20", "1:24", "1:30", "3:25", "1:50", "7:64"};
    for (int i = 0; i < 11; i++) CHECK(nm(54 + i) == ratios[i], "конусность [%d]=%s, ожидалось %s", 54 + i, nm(54 + i).c_str(), ratios[i]);
    // строки, добавленные в V3.8, совпадают с теми, что были в начале таблицы
    CHECK(Cone_Info[31].Cs_Div == Cone_Info[1].Cs_Div && abs(Cone_Info[31].Cm_Div - Cone_Info[1].Cm_Div) <= 1, "30° в ряду (%d,%d) ≠ 30° в начале (%d,%d)",
          Cone_Info[31].Cs_Div, Cone_Info[31].Cm_Div, Cone_Info[1].Cs_Div, Cone_Info[1].Cm_Div);
    CHECK(Cone_Info[46].Cs_Div == Cone_Info[0].Cs_Div && Cone_Info[46].Cm_Div == Cone_Info[0].Cm_Div, "45° в ряду ≠ 45° в начале");
    // дисплею уходит угол для нового индекса
    touch("CONE:61"); run_ms(20);
    CHECK(disp_sent("<CONE_ANGLE:19>"), "CONE:61 (1:30) — нет CONE_ANGLE:19");
    touch("CONE:62"); run_ms(20);
    CHECK(disp_sent("<CONE_ANGLE:69>"), "CONE:62 (3:25) — нет CONE_ANGLE:69");
    touch("CONE:0");
    // экран: параметры по умолчанию V3.8
    CHECK(PASS_FINISH == 1 && Excess_Lag == 1, "PASS_FINISH=%d Excess_Lag=%d, ожидалось 1 1", PASS_FINISH, Excess_Lag);
    CHECK(Kzr == 1 && Raz == 1, "после старта Kzr=%d Raz=%d, ожидалось 1 1 (шар)", Kzr, Raz);
    CHECK(TENSION_Z == 1 && TENSION_Z_mm == 0, "натяг Z по умолчанию: %d микрошаг, %ld сот", TENSION_Z, TENSION_Z_mm);
}

// ── [v38-keys] ───────────────────────────────────────────────────────────────
static void test_v38_keys() {
    printf("[%7.1f с] [v38-keys] обнуление линеек «ускоренная»+◀/▶, ввод ⌀ на подэкране 3\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); run_ms(100);
    SelectMenu = 1;
    touch("AXIS:Z:-12345"); touch("AXIS:X:2500"); touch("AXIS:D:40000"); run_ms(50);
    Pass_Total = 5;
    // негатив: без «ускоренной» ◀ ▶ — проходы, линейки не трогает
    btn_press({B_L});
    CHECK(Pass_Total == 4 && shown_z() == -12340, "◀ без ускоренной: проходов %ld, Z %ld", Pass_Total, shown_z());
    btn_press({B_R});
    CHECK(Pass_Total == 5 && shown_x() == 2500, "▶ без ускоренной: проходов %ld, X %ld", Pass_Total, shown_x());
    // «ускоренная»+◀ — Z в 0, «ускоренная»+▶ — X в 0; проходы не меняются
    rapid(true); btn_press({B_L}); rapid(false);
    CHECK(shown_z() == 0 && shown_x() == 2500 && Pass_Total == 5, "уск+◀: Z=%ld X=%ld проходов %ld", shown_z(), shown_x(), Pass_Total);
    rapid(true); btn_press({B_R}); rapid(false);
    CHECK(shown_x() == 0 && Pass_Total == 5, "уск+▶: X=%ld проходов %ld", shown_x(), Pass_Total);
    CHECK(shown_d() == 40000, "диаметр не должен меняться при обнулении линеек: %ld", shown_d());
    // pairwise по режимам: обнуление работает в M2..M6 (в M6 — и шар, и отрезание)
    int modes[] = {2, 3, 4, 5, 6};
    for (int m : modes) {
        select_mode(m); SelectMenu = 1;
        touch("AXIS:Z:5000"); touch("AXIS:X:700"); run_ms(30);
        rapid(true); btn_press({B_L}); btn_press({B_R}); rapid(false);
        CHECK(shown_z() == 0 && shown_x() == 0, "режим %d: обнуление уск+◀/▶ → Z=%ld X=%ld", m, shown_z(), shown_x());
    }
    // ввод диаметра — подэкран 3. M1 «Ручной»: Выбор 1 → 3 (подэкрана 2 нет без «Наружный» и упоров X)
    select_mode(1); SelectMenu = 1; touch("AXIS:D:20000"); run_ms(30);
    btn_press({B_S});
    CHECK(SelectMenu == 3, "M1 «Ручной»: «Выбор» 1 → %d, ожидалось 3", SelectMenu);
    struct { std::initializer_list<int> k; bool rp; long d; const char* n; } cs[] = {
        {{B_U}, false, 100, "▲ +0.10"}, {{B_D}, false, -100, "▼ −0.10"},
        {{B_U}, true, 10000, "уск+▲ +10"}, {{B_D}, true, -10000, "уск+▼ −10"},
        {{B_U, B_L}, false, 10, "◀+▲ +0.01"}, {{B_D, B_L}, false, -10, "◀+▼ −0.01"},
        {{B_U, B_R}, false, 1000, "▶+▲ +1"}, {{B_D, B_R}, false, -1000, "▶+▼ −1"},
    };
    for (auto& c : cs) {
        long d0 = shown_d();
        if (c.rp) rapid(true);
        btn_press(c.k);
        if (c.rp) rapid(false);
        CHECK(shown_d() - d0 == c.d, "⌀ %s: изменился на %ld мкм, ожидалось %ld", c.n, shown_d() - d0, c.d);
    }
    btn_press({B_L, B_R});
    CHECK(shown_d() == 0, "◀+▶ — обнулить ⌀: %ld", shown_d());
    // подэкран 3 → 4 → 1
    btn_press({B_S}); CHECK(SelectMenu == 4, "3 → %d, ожидалось 4", SelectMenu);
    btn_press({B_S}); CHECK(SelectMenu == 1, "4 → %d, ожидалось 1", SelectMenu);
    // негатив: на подэкране 1 ▲ меняет съём, а не ⌀
    long d0 = shown_d(); int ap0 = Ap;
    btn_press({B_U});
    CHECK(shown_d() == d0 && Ap != ap0, "подэкран 1: ▲ поменял ⌀ (%ld→%ld) или не поменял съём (%d→%d)", d0, shown_d(), ap0, Ap);
    touch("AP:0");
}

// ── [v38-dist] ───────────────────────────────────────────────────────────────
static void test_v38_dist() {
    printf("[%7.1f с] [v38-dist] дистанционные лимиты (подэкран 4)\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); run_ms(100); clear_limits();
    SelectMenu = 4;
    long zs = steps_z_per_mm(), xs = steps_x_per_mm();
    // ввод расстояния кнопками: ▲ +0.1, уск+▲ +10, ◀+▲ +0.005, ▶+▲ +1, ◀+▶ — 0
    touch("DIST:0");
    btn_press({B_U}); CHECK(Distance_mm == 200, "▲: %ld (0.0005 мм), ожидалось 200", Distance_mm);
    rapid(true); btn_press({B_U}); rapid(false); CHECK(Distance_mm == 20200, "уск+▲: %ld", Distance_mm);
    btn_press({B_U, B_L}); CHECK(Distance_mm == 20210, "◀+▲: %ld", Distance_mm);
    btn_press({B_U, B_R}); CHECK(Distance_mm == 22210, "▶+▲: %ld", Distance_mm);
    btn_press({B_D, B_R}); CHECK(Distance_mm == 20210, "▶+▼: %ld", Distance_mm);
    btn_press({B_L, B_R}); CHECK(Distance_mm == 0, "◀+▶: %ld", Distance_mm);
    // негатив: ниже нуля не уходит
    btn_press({B_D}); btn_press({B_D, B_L}); CHECK(Distance_mm == 0, "▼ при 0: %ld", Distance_mm);
    // с экрана — в мкм; дисплею — DIST в мкм
    touch("DIST:12345"); run_ms(20);
    CHECK(Distance_mm == 24690 && disp_sent("<DIST:12345>"), "DIST:12345 → %ld", Distance_mm);
    touch("DIST:-5"); CHECK(Distance_mm == 24690, "негатив DIST:-5 принят");
    touch("DIST:1000000"); CHECK(Distance_mm == 24690, "негатив DIST:1000000 принят");
    // pairwise: 4 кнопки × (пульт / экран EMUL)
    long z = Motor_Z_Pos, x = Motor_X_Pos;
    long dz = lround(12.345 * zs), dx = lround(12.345 * xs);
    struct { int b; const char* emul; bool* flag; long* pos; long exp; long tol; const char* n; } L[] = {
        {0, nullptr,       &limit_Left_flag,  &Limit_Pos_Left,  z + dz, McSTEP_Z, "левый (пульт)"},
        {1, "EMUL:STOP_R", &limit_Right_flag, &Limit_Pos_Right, z - dz, McSTEP_Z, "правый (экран)"},
        {2, "EMUL:STOP_F", &limit_Front_flag, &Limit_Pos_Front, x + dx, McSTEP_X, "передний (экран)"},
        {3, nullptr,       &limit_Rear_flag,  &Limit_Pos_Rear,  x - dx, McSTEP_X, "задний (пульт)"},
    };
    for (auto& l : L) {
        if (getenv("SIM_DBG38")) printf("      [dbg] перед %s: lbf=%d jx=%d jz=%d sub=%02X SM=%d Lim=%02X err0=%d\n", l.n, limit_button_flag, Joy_X_flag, Joy_Z_flag, (unsigned)Submode_Read, SelectMenu, (unsigned)Limit_Buttons_Read, err_0_flag);
        if (l.emul) touch(l.emul); else lim_btn(l.b);
        CHECK(*l.flag && labs(*l.pos - l.exp) <= l.tol, "%s: флаг %d, упор %ld, ожидалось %ld±%ld", l.n, *l.flag, *l.pos, l.exp, l.tol);
    }
    // повторное нажатие — снять
    lim_btn(0); touch("EMUL:STOP_R"); lim_btn(2); touch("EMUL:STOP_B");
    CHECK(!limit_Left_flag && !limit_Right_flag && !limit_Front_flag && !limit_Rear_flag, "повторное нажатие не сняло упоры");
    // негатив: правый упор на расстоянии, которое ближе тормозного пути к левому, не ставится
    touch("DIST:500"); lim_btn(0);
    long l_pos = Limit_Pos_Left;
    touch("DIST:0"); lim_btn(1);
    CHECK(!limit_Right_flag && limit_Left_flag && Limit_Pos_Left == l_pos, "правый упор у самого левого поставился");
    lim_btn(0);
    // на подэкране 1 упор ставится в текущей точке (расстояние не учитывается)
    touch("DIST:50000"); SelectMenu = 1; lim_btn(0);
    CHECK(limit_Left_flag && labs(Limit_Pos_Left - Motor_Z_Pos) <= McSTEP_Z, "подэкран 1: упор не в текущей точке (%ld vs %ld)", Limit_Pos_Left, Motor_Z_Pos);
    lim_btn(0);
    // негатив: в «Наружном» (включился — упоры L/R стоят) кнопки упоров не работают
    SelectMenu = 1; touch("DIST:0");
    lim_btn(1);
    SET_IN(E2_PIN_JOY_LEFT, 0); SET_IN(E2_PIN_JOY_RAPID, 0);
    for (int i = 0; i < 300 && Motor_Z_Pos < Limit_Pos_Right + 3 * zs; i++) run_ms(20);
    SET_IN(E2_PIN_JOY_LEFT, 1); SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(800);
    lim_btn(0);
    CHECK(limit_Left_flag && limit_Right_flag, "упоры L/R для проверки «Наружного»");
    SelectMenu = 4; touch("DIST:5000"); sub_pins(3);
    CHECK(Sub_Mode_Feed == Sub_Mode_Feed_Ext, "«Наружный» не включился (%d)", Sub_Mode_Feed);
    lim_btn(2); touch("EMUL:STOP_B");
    CHECK(!limit_Front_flag && !limit_Rear_flag, "упор X поставился в подрежиме «Наружный»");
    to_manual(); SelectMenu = 1; clear_limits();
    // режимы с подэкраном 4: M2, M3, M4, M5 (у M6 — нет): «Выбор» доходит до 4
    int ms[] = {2, 3, 4, 5};
    for (int m : ms) {
        select_mode(m); SelectMenu = 1;
        int seen4 = 0;
        for (int i = 0; i < 5; i++) { btn_press({B_S}); if (SelectMenu == 4) seen4++; }
        CHECK(seen4 > 0, "режим %d: «Выбор» не доходит до подэкрана 4", m);
        SelectMenu = 1;
    }
    select_mode(6); SelectMenu = 1;
    for (int i = 0; i < 5; i++) { btn_press({B_S}); CHECK(SelectMenu != 4, "шар: подэкран 4 есть, а не должен"); }
    SelectMenu = 1; select_mode(1); touch("DIST:0");
}

// Прогон автоцикла с наблюдением за осями. Упоры Z ставит setup_z_limits (если нужно).
struct AutoRun { bool done; long xmax, xmin, zmax, zmin; double secs; int pauses; };
static AutoRun auto_run(int mode, int sub, int joy_pin_sel, double rpm, double timeout_ms,
                        std::function<void(double)> tick = nullptr) {
    // joy_pin_sel: 0 влево, 1 вправо
    select_mode(mode);
    sub_pins(sub);
    AutoRun r{false, Motor_X_Pos, Motor_X_Pos, Motor_Z_Pos, Motor_Z_Pos, 0, 0};
    if (rpm) { spindle(rpm); run_ms(300); }
    if (joy_pin_sel == 0) SET_IN(E2_PIN_JOY_LEFT, 0); else SET_IN(E2_PIN_JOY_RIGHT, 0);
    double t = 0; bool was_p = false;
    while (!Complete_flag && t < timeout_ms) {
        double dt = getenv("SIM_DBG38X") && mode == 4 && sub == 1 && t < 900 ? 2 : 20;
        run_ms(dt); t += dt;
        r.xmax = std::max(r.xmax, Motor_X_Pos); r.xmin = std::min(r.xmin, Motor_X_Pos);
        r.zmax = std::max(r.zmax, Motor_Z_Pos); r.zmin = std::min(r.zmin, Motor_Z_Pos);
        if (isPausedAfterStep && !was_p) r.pauses++;
        was_p = isPausedAfterStep;
        if (tick) tick(t);
        if (getenv("SIM_DBG38X") && t < 900 && mode == 4 && sub == 1)
            printf("      [x] t=%.3f Step=%d Z=%ld X=%ld F=%ld stepX=%d stepZ=%d t5b=%d t2a=%d t2b=%d pulses=%ld OCR5A=%u\n", t / 1000, Step, Motor_Z_Pos, Motor_X_Pos, Limit_Pos_Front, Step_X_flag, Step_Z_flag, e2_en_t5b, e2_en_t2a, e2_en_t2b, s_x.count, (unsigned)OCR5A);
        static int s_prev_step = -1;
        if (getenv("SIM_DBG38") && Step != s_prev_step) {
            printf("      [step] abs=%.3f t=%.2f Step %d→%d Z=%ld X=%ld L=%ld R=%ld F=%ld B=%ld pass=%ld\n", t_ns / 1e9, t / 1000, s_prev_step, Step,
                   Motor_Z_Pos, Motor_X_Pos, Limit_Pos_Left, Limit_Pos_Right, Limit_Pos_Front, Limit_Pos_Rear, Pass_Nr);
            s_prev_step = Step;
        }
    }
    r.done = Complete_flag; r.secs = t / 1000;
    return r;
}
static void auto_stop() {
    SET_IN(E2_PIN_JOY_LEFT, 1); SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(800);
    spindle(0); run_ms(100);
    to_manual();
}

// ── [v38-kzr] ────────────────────────────────────────────────────────────────
static int thr_idx(double step) {
    for (int i = 0; i < (int)TOTAL_THREADS; i++) if (fabs(Thread_Info[i].Step - step) < 1e-4) return i;
    return -1;
}
static double kzr_depth(int k, double P, bool internal) {
    switch (k) {
        case 1: return P * (internal ? 0.60139 : 0.61342);
        case 2: return P * 0.640327;
        case 3: return P * 0.61342;
        default: return P * 0.5 + (P < 2.0 ? 0.15 : P <= 5.0 ? 0.25 : 0.5);
    }
}
static void test_v38_kzr() {
    printf("[%7.1f с] [v38-kzr] профиль резьбы: переключение и глубина врезания\n", t_ns / 1e9);
    controls_neutral(); select_mode(3); SelectMenu = 1; touch("KZR:1");
    // «ускоренная» + «Выбор» на подэкране 1 листает 1→2→3→4→1, подэкран не меняется
    int seq[] = {2, 3, 4, 1};
    for (int v : seq) {
        rapid(true); btn_press({B_S}); rapid(false);
        CHECK(Kzr == v && SelectMenu == 1, "уск+Выбор: Kzr=%d (ожидалось %d), подэкран %d", Kzr, v, SelectMenu);
    }
    btn_press({B_S}); CHECK(SelectMenu == 2 && Kzr == 1, "Выбор без ускоренной: подэкран %d, Kzr %d", SelectMenu, Kzr);
    SelectMenu = 1;
    // экран: KZR:n, негатив — 0, 5, мусор
    touch("KZR:3"); CHECK(Kzr == 3 && disp_sent("<KZR:3>"), "KZR:3 → %d", Kzr);
    touch("KZR:0"); CHECK(Kzr == 3, "KZR:0 принят"); touch("KZR:5"); CHECK(Kzr == 3, "KZR:5 принят");
    touch("KZR:x"); CHECK(Kzr == 3, "KZR:x принят");
    touch("KZR:1");
    // глубина — попарно: профиль × наружн./внутр. × шаг (вкл. ветки трапеции <2, 2..5, >5 мм)
    struct C { int k; bool in; double P; };
    std::vector<C> cs = {
        {1, false, 1.5}, {1, true, 2.5}, {2, false, 2.5}, {2, true, 1.0}, {3, false, 1.0}, {3, true, 1.5},
        {4, false, 1.5}, {4, true, 2.5}, {4, false, 6.0},
    };
    for (auto& c : cs) {
        int idx = thr_idx(c.P == 6.0 ? 4.0 : c.P);
        CHECK(idx >= 0, "нет резьбы %.2f", c.P);
        if (idx < 0) continue;
        float saved = Thread_Info[idx].Step;
        if (c.P == 6.0) Thread_Info[idx].Step = 6.0;   // ветка «трапеция > 5 мм»: глубина считается по Step
        printf("    профиль %d %s P=%.2f: ", c.k, c.in ? "внутр." : "наружн.", c.P); fflush(stdout);
        bool ok = setup_z_limits(600);
        CHECK(ok, "упоры не установлены");
        select_mode(3); char b[32]; snprintf(b, sizeof b, "THREAD_STEP:%d", idx); touch(b);
        snprintf(b, sizeof b, "KZR:%d", c.k); touch(b);
        long x0 = Motor_X_Pos;
        AutoRun r = auto_run(3, c.in ? 1 : 3, 0, 120, 900000);
        auto_stop();
        Thread_Info[idx].Step = saved;
        double mm = (double)(c.in ? x0 - r.xmin : r.xmax - x0) / steps_x_per_mm();
        // ядро: round(глубина / шаг) × McSTEP + McSTEP; внутр. — врезание «от себя», наружн. — «к оси»
        long exp_steps = lround(kzr_depth(c.k, c.P, c.in) / ((double)SCREW_X / 100 / MOTOR_X_STEP_PER_REV)) * McSTEP_X + McSTEP_X;
        long got = c.in ? x0 - r.xmin : r.xmax - x0;
        printf("глубина %.4f мм (%ld шагов, ожидалось %ld), %.0f с\n", mm, got, exp_steps, r.secs);
        CHECK(r.done, "профиль %d P=%.2f: цикл не завершился", c.k, c.P);
        CHECK(labs(got - exp_steps) <= McSTEP_X, "профиль %d %s P=%.2f: глубина %ld шагов, ожидалось %ld", c.k, c.in ? "вн" : "нар", c.P, got, exp_steps);
        clear_limits();
    }
    // негатив: во время движения профиль не меняется
    touch("KZR:1");
    bool ok = setup_z_limits(600); CHECK(ok, "упоры");
    select_mode(3); touch("THREAD_STEP:13");
    int k_mid = -1;
    auto_run(3, 3, 0, 300, 4000, [&](double t) { if (t > 2000 && k_mid < 0) { touch("KZR:4"); k_mid = Kzr; } });
    CHECK(k_mid == 1, "KZR:4 во время цикла принят (Kzr=%d)", k_mid);
    auto_stop(); clear_limits(); touch("KZR:1");
}

// ── [v38-pause] ──────────────────────────────────────────────────────────────
static void test_v38_pause() {
    printf("[%7.1f с] [v38-pause] пауза автоцикла: экран и кнопки, залипшие флаги касаний\n", t_ns / 1e9);
    // негатив: вне автоцикла PAUSE/RESUME не принимаются
    controls_neutral(); select_mode(1); run_ms(100);
    touch("PAUSE"); run_ms(50);
    CHECK(!want_pause && !isPausedAfterStep, "PAUSE вне автоцикла принят");
    touch("RESUME"); run_ms(50);
    CHECK(!button_up_flag, "RESUME вне паузы оставил button_up_flag");
    // залипший флаг: касание ▼ до цикла не должно ставить автоцикл на паузу
    touch("KEY:DN"); touch("KEY:UP"); run_ms(50);
    CHECK(!button_down_flag && !button_up_flag, "после касаний ▼▲ флаги кнопок остались: dn=%d up=%d", button_down_flag, button_up_flag);
    // резьба 1.50 наружн.: пауза с экрана, потом кнопками пульта
    for (int via_keys = 0; via_keys < 2; via_keys++) {
        bool ok = setup_z_limits(600); CHECK(ok, "упоры");
        select_mode(3); touch("THREAD_STEP:13"); touch("KZR:1");
        int total = Thread_Info[13].Pass + PASS_FINISH;
        bool asked = false, resumed = false; long pz = 0, px = 0, sz = 0, sx = 0; bool still = true;
        double paused_at = -1; int pass_at = 0;
        auto r = auto_run(3, 3, 0, 300, 900000, [&](double t) {
            if (!asked && Pass_Nr >= 3) {
                asked = true;
                if (via_keys) btn_press({B_D}); else touch("PAUSE");
            }
            if (isPausedAfterStep && paused_at < 0) {
                paused_at = t; pz = Motor_Z_Pos; px = Motor_X_Pos; sz = s_z.count; sx = s_x.count; pass_at = Pass_Nr;
            }
            if (paused_at >= 0 && !resumed && t - paused_at > 3000) {
                still = s_z.count == sz && s_x.count == sx && Motor_Z_Pos == pz && Motor_X_Pos == px;
                resumed = true;
                if (via_keys) btn_press({B_U}); else touch("RESUME");
            }
        });
        const char* how = via_keys ? "кнопки ▼/▲" : "экран PAUSE/RESUME";
        CHECK(paused_at >= 0, "%s: пауза не наступила", how);
        CHECK(still, "%s: на паузе оси двигались", how);
        CHECK(pz == Null_Z_Pos, "%s: пауза не в начале прохода (Z %ld, старт %ld)", how, pz, Null_Z_Pos);
        CHECK(disp_sent("<ALERT:5>"), "%s: дисплею не ушло ALERT:5", how);
        CHECK(r.done, "%s: после продолжения цикл не завершился", how);
        CHECK(r.pauses == 1, "%s: пауз %d, ожидалась 1 (флаг продолжения залип?)", how, r.pauses);
        printf("    %s: пауза на проходе %d из %d, %.1f с стоим, цикл завершён=%d\n", how, pass_at, total, 3.0, r.done);
        auto_stop(); clear_limits();
    }
    // пауза снимается, если джойстик вернуть в нейтраль (как у автора: Joy_NoPressed)
    {
        bool ok = setup_z_limits(600); CHECK(ok, "упоры");
        select_mode(1); touch("FEED:20"); touch("AP:20"); Pass_Total = 4; Pass_Nr = 1;
        bool asked = false;
        auto r = auto_run(1, 3, 0, 600, 60000, [&](double t) {
            if (!asked && Pass_Nr >= 2) { asked = true; touch("PAUSE"); }
            if (isPausedAfterStep) { SET_IN(E2_PIN_JOY_LEFT, 1); }
        });
        (void)r; run_ms(200);
        CHECK(!isPausedAfterStep && !want_pause, "джойстик в нейтрали не снял паузу");
        CHECK(!disp_sent("<ALERT:5>") || DISPLAY_UART.out.rfind("<ALERT:0>") > DISPLAY_UART.out.rfind("<ALERT:5>"),
              "после снятия паузы джойстиком дисплей не получил ALERT:0");
        auto_stop(); clear_limits();
    }
    // пауза в синхронной/асинхронной подаче — попарно: режим × наружн./внутр.
    struct { int m; int sub; const char* n; } cs[] = {{1, 3, "синхр. наружн."}, {1, 1, "синхр. внутр."}, {2, 3, "асинхр. наружн."}, {2, 1, "асинхр. внутр."}};
    for (auto& c : cs) {
        bool ok = setup_z_limits(600); CHECK(ok, "упоры");
        select_mode(c.m);
        if (c.m == 1) touch("FEED:20"); else touch("AFEED:300");
        touch("AP:20"); Pass_Total = 3; Pass_Nr = 1;
        bool asked = false, resumed = false; double pa = -1;
        auto r = auto_run(c.m, c.sub, 0, c.m == 1 ? 600 : 0, 120000, [&](double t) {
            if (!asked && t > 1500) { asked = true; touch("PAUSE"); }
            if (isPausedAfterStep && pa < 0) pa = t;
            if (pa >= 0 && !resumed && t - pa > 1000) { resumed = true; touch("RESUME"); }
        });
        CHECK(pa >= 0 && r.done && r.pauses == 1, "%s: пауза %s, завершён %d, пауз %d", c.n, pa >= 0 ? "была" : "нет", r.done, r.pauses);
        printf("    %s: пауза %.1f с, завершён %d\n", c.n, pa / 1000, r.done);
        auto_stop(); clear_limits();
    }
}

// ── [v38-cone] ───────────────────────────────────────────────────────────────
static void test_v38_cone() {
    printf("[%7.1f с] [v38-cone] автоциклы конусов V3.7/V3.8 (без упоров, упоры ставятся сами)\n", t_ns / 1e9);
    long zs = steps_z_per_mm(), xs = steps_x_per_mm();
    long otvod = ((long)MOTOR_Z_STEP_PER_REV * 100L + SCREW_Z / 2L) / SCREW_Z * McSTEP_Z;   // 1 мм
    // pairwise: (режим/подрежим/джойстик) × угол × съём × проходы
    struct C { int mode, sub, joy; int cone; int ap; int passes; const char* n; };
    // попарно (ортогональный массив L9: каждая пара значений любых двух факторов встречается):
    // вид автоцикла {> наружн., < наружн., < внутр.} × конус {30°, 1:10, КМ2} × съём {0.20, 0.50 = отскок X (зависание
    // V3.4), 0.36} × проходов {1, 2, 3}; плюс 45° внутр. — крутой конус
    static const int L9[9][4] = {{0,0,0,0},{0,1,1,1},{0,2,2,2},{1,0,1,2},{1,1,2,0},{1,2,0,1},{2,0,2,1},{2,1,0,2},{2,2,1,0}};
    struct K { int mode, sub, joy; const char* n; } kinds[] = {{5, 3, 0, "конус > наружн."}, {4, 3, 1, "конус < наружн."}, {4, 1, 0, "конус < внутр."}};
    struct Cn { int idx; const char* n; } cones[] = {{1, "30°"}, {57, "1:10"}, {49, "КМ2"}};
    int aps[] = {20, 50, 36}, pss[] = {1, 2, 3};
    static char names[10][96];
    std::vector<C> cs;
    for (int i = 0; i < 9; i++) {
        const int* f = L9[i];
        snprintf(names[i], sizeof names[i], "%s, %s, %.2f×%d", kinds[f[0]].n, cones[f[1]].n, aps[f[2]] / 100.0, pss[f[3]]);
        cs.push_back({kinds[f[0]].mode, kinds[f[0]].sub, kinds[f[0]].joy, cones[f[1]].idx, aps[f[2]], pss[f[3]], names[i]});
    }
    cs.push_back({4, 1, 0, 0, 20, 3, "конус < внутр., 45°, 0.20×3"});
    for (auto& c : cs) {
        printf("    %s: ", c.n); fflush(stdout);
        controls_neutral(); select_mode(1); clear_limits(); run_ms(100);
        select_mode(c.mode); SelectMenu = 1;
        char b[32]; snprintf(b, sizeof b, "CONE:%d", c.cone); touch(b);
        touch("FEED:20"); snprintf(b, sizeof b, "AP:%d", c.ap); touch(b);
        Pass_Total = c.passes; Pass_Nr = 1;
        long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
        double slope = e2_cone_slope(Cone_Info[c.cone]);
        // экран: длина конуса за все проходы (подэкран 2) — до запуска, после цикла проходы сбрасываются в 1
        long len_scr = e2_cone_len();
        double len_exp = (double)c.passes * c.ap / 2.0 / slope * ((double)zs / xs) * ((double)xs / zs);   // 0.01 мм
        CHECK(fabs(len_scr - len_exp) <= 1 + len_exp * 0.002, "%s: длина на экран %ld, ожидалось %.0f (0.01 мм)", c.n, len_scr, len_exp);
        bool resumed = false; double pa = -1; long pz = 0;
        auto r = auto_run(c.mode, c.sub, c.joy, 400, 600000, [&](double t) {
            if (isPausedAfterStep && pa < 0) { pa = t; pz = Motor_Z_Pos; }
            if (pa >= 0 && !resumed && t - pa > 1500) { resumed = true; touch("RESUME"); }
        });
        bool internal = c.sub == 1;
        CHECK(!err_1_flag, "%s: «Установите упоры» — конусам V3.8 упоры не нужны", c.n);
        CHECK(r.done, "%s: цикл не завершился за %.0f с", c.n, r.secs);
        CHECK(limit_Left_flag && limit_Right_flag, "%s: после цикла упоры L=%d R=%d, ожидались оба", c.n, limit_Left_flag, limit_Right_flag);
        // глубина на радиус: проходы × съём/2 у торца (старта) + ход по образующей на отводе 1 мм за старт
        long infx = (long)(((unsigned long long)MOTOR_X_STEP_PER_REV * McSTEP_X * c.ap + SCREW_X) / (2ULL * SCREW_X));
        long depth = internal ? x0 - r.xmin : r.xmax - x0;
        long exp_depth = c.passes * infx + lround(slope * xs);
        CHECK(labs(depth - exp_depth) <= 2 * McSTEP_X, "%s: глубина %ld шагов, ожидалось %ld", c.n, depth, exp_depth);
        // в конце резец — у дальнего упора на образующей: X = старт ∓ 0.1 мм (первый проход считается с +0.2 на ⌀)
        long exp_xf = internal ? x0 + xs / 10 : x0 - xs / 10;
        CHECK(labs(Motor_X_Pos - exp_xf) <= 2 * McSTEP_X, "%s: X в конце %+ld, ожидалось %+ld", c.n, Motor_X_Pos - x0, exp_xf - x0);
        // длина конуса по Z: от старта до дальнего упора = (проходы·съём/2 + 0.1 мм) / уклон
        long far = c.mode == 5 || (c.mode == 4 && internal) ? Limit_Pos_Left - z0 : z0 - Limit_Pos_Right;
        double exp_far = ((double)c.passes * c.ap / 200.0 + 0.1) / slope * zs;
        CHECK(fabs(far - exp_far) <= 2 * McSTEP_Z + exp_far * 0.003, "%s: длина конуса %ld шагов, ожидалось %.0f", c.n, far, exp_far);
        // ближний упор — на 1 мм за стартом (отвод)
        long near_lim = c.mode == 5 || internal ? Limit_Pos_Right : Limit_Pos_Left;
        long exp_near = c.mode == 5 || internal ? z0 - otvod : z0 + otvod;
        CHECK(labs(near_lim - exp_near) <= McSTEP_Z, "%s: ближний упор %ld, ожидалось %ld", c.n, near_lim, exp_near);
        // внутренний: на последнем проходе — пауза с отводом на 30 мм вправо
        if (internal) {
            CHECK(pa >= 0, "%s: нет паузы на последнем проходе", c.n);
            CHECK(pa < 0 || z0 - pz >= 30 * zs, "%s: отвод на паузе %.2f мм, ожидалось ≥ 30", c.n, (double)(z0 - pz) / zs);
        }
        // экран: длина конуса (подэкран 2) — формула V3.8
        printf("глубина %.3f мм, длина %.2f мм (на экран %ld.%02ld), %.0f с\n", (double)depth / xs, (double)far / zs, len_scr / 100, len_scr % 100, r.secs);
        auto_stop();
        // V3.8: конус вернул упоры для чистового прохода — снять
        clear_limits();
    }
    // внутренний конус > — «Режим невозможен»: джойстик не двигает
    controls_neutral(); select_mode(5); sub_pins(1);
    CHECK(Sub_Mode_Cone == Sub_Mode_Cone_Int, "конус > «Внутренний» не включился");
    long sz = s_z.count, sx = s_x.count;
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(1500); SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(300);
    SET_IN(E2_PIN_JOY_RIGHT, 0); run_ms(1500); SET_IN(E2_PIN_JOY_RIGHT, 1); run_ms(300);
    CHECK(s_z.count == sz && s_x.count == sx, "конус > внутр.: оси двигались (Z %ld X %ld шагов)", s_z.count - sz, s_x.count - sx);
    to_manual();
    // негатив: конус < наружн. джойстиком влево (не в ту сторону) — ничего не режет
    select_mode(4); touch("AP:20"); Pass_Total = 2; sub_pins(3);
    sz = s_z.count; sx = s_x.count;
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(1500);
    long moved = labs(s_z.count - sz) + labs(s_x.count - sx);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(300);
    CHECK(!Complete_flag && moved == 0, "конус < наружн., джойстик влево: движение %ld шагов", moved);
    to_manual(); clear_limits();
}

// ── [v38-cut] ────────────────────────────────────────────────────────────────
static void test_v38_cut() {
    printf("[%7.1f с] [v38-cut] отрезание вразбежку (V3.5)\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); clear_limits(); run_ms(100);
    // негатив: RAZ вне режима «Шар»
    touch("RAZ:2"); CHECK(Raz == 1, "RAZ:2 в режиме подачи принят");
    select_mode(6); SelectMenu = 1;
    CHECK(Raz == 1 && Ap == 100 && Pass_Total == 1, "M6 после включения: Raz=%d Ap=%d проходов %ld", Raz, Ap, Pass_Total);
    // «ускоренная» + «Выбор» — шар ↔ отрезание
    rapid(true); btn_press({B_S}); rapid(false);
    CHECK(Raz == 2 && SelectMenu == 1, "уск+Выбор: Raz=%d подэкран %d", Raz, SelectMenu);
    rapid(true); btn_press({B_S}); rapid(false);
    CHECK(Raz == 1, "уск+Выбор второй раз: Raz=%d", Raz);
    touch("RAZ:2"); CHECK(Raz == 2 && disp_sent("<RAZ:2>"), "RAZ:2 → %d", Raz);
    touch("RAZ:3"); CHECK(Raz == 2, "RAZ:3 принят");
    // отрезание: «Выбор» 1 → 3 (подэкрана 2 нет), 3 → 1
    if (getenv("SIM_DBG38")) printf("      [dbg] отрезание: ksf=%d jx=%d jz=%d rapid=%d sel=%02X Raz=%d SM=%d Mode=%d err0=%d\n", key_sel_flag, Joy_X_flag, Joy_Z_flag, (int)Button_Rapid, (unsigned)Button_Sel_Read, Raz, SelectMenu, Mode, err_0_flag);
    btn_press({B_S}); CHECK(SelectMenu == 3, "отрезание: Выбор 1 → %d, ожидалось 3", SelectMenu);
    btn_press({B_S}); CHECK(SelectMenu == 1, "отрезание: Выбор 3 → %d", SelectMenu);
    // кнопки: ▲▼ — врезаний (диаметр = врезаний × шаг), ◀▶ — шаг, уск+◀▶ — смещение
    btn_press({B_U}); btn_press({B_U}); CHECK(Pass_Total == 3 && Raz_R == 3, "▲▲: врезаний %ld, ⌀ %ld", Pass_Total, Raz_R);
    btn_press({B_R}); CHECK(Ap == 200 && Raz_R == 6, "▶: шаг %d, ⌀ %ld", Ap, Raz_R);
    btn_press({B_L}); btn_press({B_L}); CHECK(Ap == 100, "◀◀: шаг %d (минимум 1.00)", Ap);
    rapid(true); btn_press({B_R}); rapid(false); CHECK(Staggered_Width == 75, "уск+▶: смещение %d", Staggered_Width);
    rapid(true); btn_press({B_L}); btn_press({B_L}); btn_press({B_L}); btn_press({B_L}); rapid(false);
    CHECK(Staggered_Width == 10, "уск+◀×4: смещение %d (минимум 0.10)", Staggered_Width);
    btn_press({B_D}); btn_press({B_D}); btn_press({B_D}); CHECK(Pass_Total == 1, "▼▼▼: врезаний %ld (минимум 1)", Pass_Total);
    // экран: CUT_D, STAG, AP; негатив
    touch("AP:100"); touch("CUT_D:450"); CHECK(Pass_Total == 5 && Raz_R == 5, "CUT_D:450 при шаге 1.00 → врезаний %ld", Pass_Total);
    touch("CUT_D:0"); CHECK(Pass_Total == 5, "CUT_D:0 принят");
    touch("STAG:2"); CHECK(Staggered_Width == 50, "STAG:2 → %d", Staggered_Width);
    touch("STAG:9"); CHECK(Staggered_Width == 50, "STAG:9 принят");
    touch("STAG:-1"); CHECK(Staggered_Width == 50, "STAG:-1 принят");
    // автоцикл: упоры правый + задний в точке начала, «Наружный», джойстик влево
    touch("FEED:5"); touch("CUT_D:400");   // 4 врезания по 1.00 на диаметр → 2 мм на радиус
    lim_btn(1); lim_btn(3);
    CHECK(limit_Right_flag && limit_Rear_flag, "упоры R/B для отрезания");
    long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
    s_x_turns.clear(); s_x_last_dir = -1;
    std::vector<long> zs_seen; long jy_moves = -1;
    auto r = auto_run(6, 3, 0, 600, 300000, [&](double t) {
        if (std::find(zs_seen.begin(), zs_seen.end(), Motor_Z_Pos) == zs_seen.end() && !Step_Z_flag) zs_seen.push_back(Motor_Z_Pos);
        if (t > 1000 && jy_moves < 0) {   // негатив: джойстик вверх/вниз в автоотрезании игнорируется
            long sx = s_x.count; SET_IN(E2_PIN_JOY_UP, 0); run_ms(200); SET_IN(E2_PIN_JOY_UP, 1); jy_moves = 0; (void)sx;
        }
    });
    long infx = steps_x_per_mm() / 2;                         // 1.00 на ⌀ = 0.50 на радиус — ровно, без лишних микрошагов
    long infz = (long)((float)MOTOR_Z_STEP_PER_REV * 50 / 2 / SCREW_Z + 0.5) * McSTEP_Z;   // автор: смещение / 2 (+ отскок Z)
    CHECK(r.done, "отрезание не завершилось");
    CHECK(labs((r.xmax - x0) - 4 * infx) <= McSTEP_X, "глубина %ld шагов, ожидалось %ld (4 × 0.50 на радиус)", r.xmax - x0, 4 * infx);
    CHECK((long)s_x_turns.size() == 4, "врезаний %zu, ожидалось 4", s_x_turns.size());
    // резец ходит между двумя канавками: старт и старт + смещение + отскок Z
    bool two = true;
    for (auto& tr : s_x_turns) two &= tr.z == z0 || tr.z == z0 + infz + REBOUND_Z;
    CHECK(two, "врезания не в двух канавках (смещение %ld + отскок %d шагов)", infz, REBOUND_Z);
    printf("    отрезание ⌀4: врезаний %zu, глубина %.3f мм на радиус, канавки Z:", s_x_turns.size(), (double)(r.xmax - x0) / steps_x_per_mm());
    for (auto& tr : s_x_turns) printf(" %+ld", tr.z - z0); printf(" шагов, %.0f с\n", r.secs);
    auto_stop(); clear_limits();
    // попарно (L9): шаг {1, 2, 3 мм} × смещение {0.10, 0.50, 2.00} × врезаний {1, 3, 4} × подача {0.05, 0.10, 0.20}
    {
        static const int L9[9][4] = {{0,0,0,0},{0,1,1,1},{0,2,2,2},{1,0,1,2},{1,1,2,0},{1,2,0,1},{2,0,2,1},{2,1,0,2},{2,2,1,0}};
        int steps[] = {100, 200, 300}, stg[] = {0, 2, 8}, plunges[] = {1, 3, 4}, feeds[] = {5, 10, 20};
        for (auto& f : L9) {
            int ap = steps[f[0]], si = stg[f[1]], n = plunges[f[2]], fd = feeds[f[3]];
            controls_neutral(); select_mode(1); clear_limits(); select_mode(6); SelectMenu = 1; touch("RAZ:2");
            char b[32];
            snprintf(b, sizeof b, "AP:%d", ap); touch(b);
            snprintf(b, sizeof b, "CUT_D:%d", ap * n); touch(b);
            snprintf(b, sizeof b, "STAG:%d", si); touch(b);
            snprintf(b, sizeof b, "FEED:%d", fd); touch(b);
            CHECK(Pass_Total == n && Raz_R == ap * n / 100, "шаг %d, врезаний %d: Pass_Total %ld, ⌀ %ld", ap, n, Pass_Total, Raz_R);
            lim_btn(1); lim_btn(3);
            long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
            s_x_turns.clear(); s_x_last_dir = -1;
            auto r = auto_run(6, 3, 0, 600, 400000);
            long infx = (long)((float)MOTOR_X_STEP_PER_REV * 0.5 * ap / SCREW_X + 0.5) * McSTEP_X;
            long infz = (long)((float)MOTOR_Z_STEP_PER_REV * Staggered_Width_array[si] / 2 / SCREW_Z + 0.5) * McSTEP_Z;
            bool grooves = true;
            for (size_t k = 0; k < s_x_turns.size(); k++)
                grooves &= s_x_turns[k].z == (k % 2 ? z0 + infz + REBOUND_Z : z0);
            CHECK(r.done && (long)s_x_turns.size() == n, "отрезание %.0f×%d, смещ. %.2f, подача %.2f: завершён %d, врезаний %zu",
                  ap / 100.0, n, Staggered_Width_array[si] / 100.0, fd / 100.0, r.done, s_x_turns.size());
            CHECK(labs((r.xmax - x0) - n * infx) <= McSTEP_X, "отрезание %.0f×%d: глубина %ld шагов, ожидалось %ld", ap / 100.0, n, r.xmax - x0, n * infx);
            CHECK(grooves, "отрезание %.0f×%d, смещ. %.2f: врезания не чередуются между канавками", ap / 100.0, n, Staggered_Width_array[si] / 100.0);
            printf("    pairwise: шаг %.0f, смещ. %.2f, врезаний %d, подача %.2f — %s, %.0f с\n", ap / 100.0,
                   Staggered_Width_array[si] / 100.0, n, fd / 100.0, r.done ? "завершён" : "НЕ завершён", r.secs);
            auto_stop(); clear_limits();
        }
    }
    // шар по-прежнему шар; RAZ:T — переключить (кнопка экрана)
    select_mode(6); touch("RAZ:1"); touch("RAZ:T"); CHECK(Raz == 2, "RAZ:T: шар → %d", Raz); touch("RAZ:T"); CHECK(Raz == 1, "RAZ:T: → %d", Raz);
    touch("RAZ:1"); CHECK(Raz == 1, "RAZ:1");
    select_mode(1);
}

// ── [v38-afeed] ──────────────────────────────────────────────────────────────
static void test_v38_afeed() {
    printf("[%7.1f с] [v38-afeed] асинхронный автоцикл: округление съёма (V3.4), проходы сохраняются\n", t_ns / 1e9);
    int aps[] = {20, 25};    // 25 на микрошаге 4: раньше +2 микрошага на проход
    for (int ap : aps) {
        bool ok = setup_z_limits(600); CHECK(ok, "упоры");
        select_mode(2); touch("AFEED:300"); char b[16]; snprintf(b, sizeof b, "AP:%d", ap); touch(b);
        Pass_Total = 3; Pass_Nr = 1;
        long x0 = Motor_X_Pos;
        auto r = auto_run(2, 3, 0, 0, 120000);
        long per = (long)((float)MOTOR_X_STEP_PER_REV * ap / SCREW_X + 0.5) * McSTEP_X;
        CHECK(r.done, "асинхр. Ap=%d: не завершился", ap);
        CHECK(r.xmax - x0 == 3 * per, "асинхр. Ap=%d: глубина %ld шагов, ожидалось %ld (3 × %ld)", ap, r.xmax - x0, 3 * per, per);
        CHECK(Pass_Total == 3, "асинхр.: после цикла проходов %ld, ожидалось 3 (сохраняется)", Pass_Total);
        printf("    съём %.2f R: глубина %ld шагов = 3 × %ld, проходов после цикла %ld\n", ap / 100.0, r.xmax - x0, per, Pass_Total);
        auto_stop(); clear_limits();
    }
    // синхронный — по-прежнему сбрасывает в 1 (как у автора)
    bool ok = setup_z_limits(600); CHECK(ok, "упоры");
    select_mode(1); touch("FEED:20"); touch("AP:20"); Pass_Total = 2; Pass_Nr = 1;
    auto r = auto_run(1, 3, 0, 600, 120000);
    CHECK(r.done && Pass_Total == 1, "синхр.: завершён %d, проходов после %ld (ожидалось 1)", r.done, Pass_Total);
    auto_stop(); clear_limits();
}

// ── [v38-groove] ─────────────────────────────────────────────────────────────
static void test_v38_groove() {
    printf("[%7.1f с] [v38-groove] ручейки / «ёлочка»: торцевой автоцикл, отскок Z 0, натяг (V3.4)\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); clear_limits(); run_ms(100);
    // упоры X: передний — глубина, задний — отвод (на 2 мм дальше)
    long x0 = Motor_X_Pos;
    SET_IN(E2_PIN_JOY_UP, 0); SET_IN(E2_PIN_JOY_RAPID, 0);
    for (int i = 0; i < 400 && Motor_X_Pos < x0 + steps_x_per_mm(); i++) run_ms(20);
    SET_IN(E2_PIN_JOY_UP, 1); run_ms(800);
    lim_btn(2);
    SET_IN(E2_PIN_JOY_DOWN, 0);
    for (int i = 0; i < 600 && Motor_X_Pos > x0 - 2 * steps_x_per_mm(); i++) run_ms(20);
    SET_IN(E2_PIN_JOY_DOWN, 1); SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(800);
    lim_btn(3);
    CHECK(limit_Front_flag && limit_Rear_flag, "упоры X для ручейков");
    touch("FEED:5"); touch("AP:234"); Pass_Total = 3; Pass_Nr = 1;   // 3 ручья по 2.34 мм (поликлин PL)
    sub_pins(3);
    // подэкран 2 (только «Наружный» с упорами X): отскок Z — 0 (◀), натяг +0.02 (▲ ×2)
    btn_press({B_S});
    CHECK(SelectMenu == 2, "M1 «Наружный» с упорами X: «Выбор» 1 → %d, ожидалось 2", SelectMenu);
    btn_press({B_L}); btn_press({B_U}); btn_press({B_U});
    CHECK(OTSKOK_Z == 1 && TENSION_Z_mm == 2, "отскок Z %d, натяг %ld сот", OTSKOK_Z, TENSION_Z_mm);
    SelectMenu = 1;
    long z0 = Motor_Z_Pos;
    s_x_turns.clear(); s_x_last_dir = -1;
    spindle(600); run_ms(300);
    SET_IN(E2_PIN_JOY_UP, 0);
    double t = 0;
    while (!Complete_flag && t < 300000) { run_ms(50); t += 50; }
    bool done = Complete_flag;
    SET_IN(E2_PIN_JOY_UP, 1); run_ms(800); spindle(0);
    long step = (long)((float)MOTOR_Z_STEP_PER_REV * 234 / SCREW_Z + 0.5) * McSTEP_Z;
    CHECK(done, "ручейки: цикл не завершился за %.0f с", t / 1000);
    CHECK((long)s_x_turns.size() == 3, "ручьёв %zu, ожидалось 3", s_x_turns.size());
    // отскок Z снят: первый ручей — напротив старта, дальше через шаг
    for (size_t i = 0; i < s_x_turns.size(); i++) {
        long dz = s_x_turns[i].z - z0;
        CHECK(labs(dz - (long)i * step) <= TENSION_Z + McSTEP_Z, "ручей %zu: Z %+ld, ожидалось %+ld", i + 1, dz, (long)i * step);
    }
    printf("    ручьёв %zu, Z:", s_x_turns.size()); for (auto& tr : s_x_turns) printf(" %+ld", tr.z - z0); printf(" (шаг %ld), %.0f с\n", step, t / 1000);
    to_manual();
    SelectMenu = 2; btn_press({B_R}); btn_press({B_D}); btn_press({B_D}); SelectMenu = 1;   // вернуть отскок и натяг
    CHECK(OTSKOK_Z == REBOUND_Z && TENSION_Z == 1, "отскок/натяг не вернулись: %d %d", OTSKOK_Z, TENSION_Z);
    clear_limits();
}

static void test_v38_stagger() {   // комбинация «как пальцами»: модификатор раньше, отпускание по очереди
    controls_neutral(); select_mode(1); SelectMenu = 3; touch("AXIS:D:20000"); run_ms(50);
    long m0 = MX_pos;
    SET_IN(E2_PIN_BTN_LEFT, 0); run_ms(60); SET_IN(E2_PIN_BTN_UP, 0); run_ms(100);
    if (getenv("SIM_DBG38")) printf("      [stg] после нажатия: MX %+ld\n", MX_pos - m0);
    SET_IN(E2_PIN_BTN_LEFT, 1); run_ms(10); SET_IN(E2_PIN_BTN_UP, 1); run_ms(200);
    printf("    [stagger] ◀ (60 мс) + ▲: MX %+ld (ожидалось %+ld)\n", MX_pos - m0, -(long)LINEIKA);
    SelectMenu = 1;
}

// Отпустить джойстик посреди автоцикла конуса и взять снова: что делает ядро (V3.8 — Joy_NoPressed сбрасывает Step)
static void test_v38_cone_release() {
    printf("[%7.1f с] [v38-release] конус > наружн.: джойстик отпущен на 2-м проходе и снова взят\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); clear_limits(); select_mode(5);
    touch("CONE:57"); touch("FEED:20"); touch("AP:50"); Pass_Total = 3; Pass_Nr = 1;
    long z0 = Motor_Z_Pos, x0 = Motor_X_Pos;
    sub_pins(3); spindle(400); run_ms(300);
    SET_IN(E2_PIN_JOY_LEFT, 0);
    double t = 0; while (Pass_Nr < 2 && t < 200000) { run_ms(20); t += 20; }
    run_ms(3000);
    long zr = Motor_Z_Pos, xr = Motor_X_Pos; int st = Step;
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(1500);
    printf("    отпущен: Step %d, Z %+ld X %+ld от старта; после нейтрали Step %d, Pass_Nr %ld, Null Z %+ld X %+ld\n",
           st, zr - z0, xr - x0, Step, Pass_Nr, Null_Z_Pos - z0, Null_X_Pos - x0);
    long sx = s_x.count, sz = s_z.count;
    SET_IN(E2_PIN_JOY_LEFT, 0); run_ms(20000);
    printf("    снова взят на 20 с: Step %d, Pass_Nr %ld, сдвиг Z %ld X %ld шагов, X %+ld от старта, завершён %d\n",
           Step, Pass_Nr, s_z.count - sz, s_x.count - sx, Motor_X_Pos - x0, Complete_flag);
    SET_IN(E2_PIN_JOY_LEFT, 1); run_ms(800); spindle(0); to_manual(); clear_limits();
}

// Импульсы STEP на выводе = счётчик ядра после серии движений (подачи, ускоренные, резьба, асинхрон) — как hil_mech
static void test_v38_pulses() {
    printf("[%7.1f с] [v38-pulses] импульсы STEP = Motor_*_Pos после серии движений\n", t_ns / 1e9);
    controls_neutral(); select_mode(1); clear_limits(); run_ms(200);
    long pz0 = s_z.count, px0 = s_x.count, mz0 = Motor_Z_Pos, mx0 = Motor_X_Pos;
    spindle(600);
    struct { int mode; const char* pre; int pin; int rapid; double ms; } mv[] = {
        {1, "FEED:20", 0, 0, 1500}, {1, "FEED:20", 1, 0, 1500}, {1, "FEED:20", 2, 0, 1200}, {1, "FEED:20", 3, 0, 1200},
        {1, "FEED:20", 0, 1, 800}, {1, "FEED:20", 1, 1, 800}, {3, "THREAD_STEP:13", 0, 0, 1500}, {3, "THREAD_STEP:13", 1, 0, 1500},
        {2, "AFEED:200", 0, 0, 1500}, {2, "AFEED:200", 1, 0, 1500}, {2, "AFEED:200", 2, 1, 700}, {2, "AFEED:200", 3, 1, 700},
        {1, "FEED:5", 0, 0, 300}, {1, "FEED:5", 1, 0, 300}, {1, "FEED:5", 2, 0, 300}, {1, "FEED:5", 3, 0, 300},
    };
    for (auto& m : mv) {
        select_mode(m.mode); touch(m.pre);
        int pins[] = {0, 0, 0, 0};
        if (m.rapid) SET_IN(E2_PIN_JOY_RAPID, 0);
        switch (m.pin) { case 0: SET_IN(E2_PIN_JOY_LEFT, 0); break; case 1: SET_IN(E2_PIN_JOY_RIGHT, 0); break;
                         case 2: SET_IN(E2_PIN_JOY_UP, 0); break; default: SET_IN(E2_PIN_JOY_DOWN, 0); }
        (void)pins;
        run_ms(m.ms);
        SET_IN(E2_PIN_JOY_LEFT, 1); SET_IN(E2_PIN_JOY_RIGHT, 1); SET_IN(E2_PIN_JOY_UP, 1); SET_IN(E2_PIN_JOY_DOWN, 1);
        SET_IN(E2_PIN_JOY_RAPID, 1); run_ms(1200);
        long dz = (s_z.count - pz0) - (Motor_Z_Pos - mz0), dx = (s_x.count - px0) - (Motor_X_Pos - mx0);
        if (dz || dx) printf("      после M%d %s джойстик %d%s: импульсы − счётчик: Z %+ld, X %+ld\n", m.mode, m.pre, m.pin, m.rapid ? " быстро" : "", dz, dx);
    }
    spindle(0); run_ms(200);
    long dz = (s_z.count - pz0) - (Motor_Z_Pos - mz0), dx = (s_x.count - px0) - (Motor_X_Pos - mx0);
    // 7e2 при каждой остановке округляет счётчик до полного шага (Joy_NoPressed) — мотор без Enable доворачивает сам;
    // импульсов при этом нет: расхождение — не больше McSTEP/2 на остановку (16 остановок)
    int n = sizeof mv / sizeof mv[0];
    CHECK(labs(dz) <= n * McSTEP_Z / 2 && labs(dx) <= n * McSTEP_X / 2, "импульсы и счётчик разошлись больше округления: Z %+ld, X %+ld", dz, dx);
    printf("    импульсы − счётчик ядра: Z %+ld, X %+ld (округление 7e2 до полного шага, не больше %d)\n", dz, dx, n * McSTEP_Z / 2);
}

static void test_v38_all() {
    if (getenv("SIM_PULSES")) { test_v38_pulses(); return; }
    test_v38_pulses();
    if (getenv("SIM_RELEASE")) { test_v38_cone_release(); return; }
    if (getenv("SIM_STAGGER")) { test_v38_stagger(); return; }
    test_v38_tables();
    test_v38_keys();
    test_v38_dist();
    test_v38_kzr();
    test_v38_pause();
    test_v38_cone();
    test_v38_cut();
    test_v38_afeed();
    test_v38_groove();
}
