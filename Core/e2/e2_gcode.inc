// [STM32] Исполнитель G-кода (BACKLOG-001). Подключается в конце e2_core.cpp, после e2_calib.inc.
//
// Строки приходят с дисплея (<GCODE:…>, файлы — с веб-страницы дисплея); ответ — <OK> или <ERR:причина>.
// Паузы G4, M0 и конец программы M2/M30 исполнитель дисплея обрабатывает сам.
// Работает только в режиме M8 (у автора «Резерв», не используется) и на остановленном станке.
//
// Координаты — те же, что на экране (нули оператора). X по умолчанию — диаметр (G7), G8 — радиус.
// G90/G91 — абсолютные/приращения, G21/G20 — мм/дюймы, G94 — подача F в мм/мин (G95 — не поддерживается).
// G0 — ускоренно (GCODE_RAPID_MM_MIN), G1 — подача F, G2/G3 — дуга (центр I K от начала или R), разбивается на
// отрезки не длиннее 0.1 мм; G28 — в ноль (сначала X, потом Z). M3/M4/M5/M8/M9, T, S, N — принимаются без действия.
//
// Движение: X и Z одновременно (Брезенхэм), разгон/торможение; счётчики ядра (Motor_*_Pos, Z_pos, X_pos, MX_pos)
// обновляются как в прерываниях автора, компенсация люфта работает; поставленные упоры не пересекаются.
// Прервать — джойстик или <GCODE:STOP> (кнопка «Стоп» веб-страницы).

#ifndef GCODE_RAPID_MM_MIN
#define GCODE_RAPID_MM_MIN   600     // ускоренный ход G0, мм/мин
#endif
#define E2_GC_MAX_SPS        8000    // шагов/с на ось — потолок
#define E2_GC_RAMP           400     // шагов разгона/торможения

struct E2Gc {
   bool  abs = true, diam = true;     // G90 / G7
   float unit = 1.0f;                 // мм на единицу (G21 — 1, G20 — 25.4)
   float feed = 100;                  // F: мм/мин или мм/об (per_rev)
   bool  per_rev = false;             // G95 / G99 (Fanuc) — подача на оборот
   int   motion = 0;                  // модальное G0/G1/G2/G3
   float z = 0, x = 0;                // текущее положение программы, мм (x — радиус)
   bool  known = false;               // опорная точка снята
   long  rz = 0, rx = 0;              // опорная точка: счётчики Motor_*_Pos …
   float rdz = 0, rdx = 0;            // … и показание экрана в ней, мм. Цели — по шагам от неё (у DRO задержка)
};
static E2Gc e2_gc;
static inline double e2_sq(double v) { return v * v; }

// мкм на шаг и знак «экран против счётчика»: по шагам экран = −Z_pos × k; с DRO/линейками — по знаку линейки
static double e2_gc_k(int ax) { return e2_um_per_step(ax); }
static int    e2_gc_sign(int ax)                 // 0 — направление линейки ещё не известно
{
   if (e2_pos.source == POS_SOURCE_STEPS || !e2_scale_axis_ok(ax)) return -1;
   return e2_scale_sign[ax];
}
// показание экрана, мм (X — радиус)
static float  e2_gc_disp_mm(int ax)
{
   if (e2_pos.source == POS_SOURCE_STEPS || !e2_scale_axis_ok(ax))
      return -(ax ? X_pos : Z_pos) * e2_gc_k(ax) / 1000;
   return (ax ? dro_pos_x : dro_pos_y) / 1000.0f;
}

static uint32_t e2_gc_report_t = 0;
static void e2_gc_report(void)
{
   if (millis() - e2_gc_report_t < 250) return;
   e2_gc_report_t = millis();
   e2_pos_refresh();
   Display_On_Position_Change();
}

// Отрезок до (tz, tx) — шаги Motor_*_Pos; mm_min — скорость по траектории. false — прервано.
static bool e2_gc_segment(long tz, long tx, float mm_min)
{
   for (int pass = 0; pass < 3; pass++) {             // 2-й/3-й проход — добрать сдвиг компенсации люфта
      long dz = tz - Motor_Z_Pos, dx = tx - Motor_X_Pos;
      if (!dz && !dx) return true;
      int sz = dz > 0 ? 1 : -1, sx = dx > 0 ? 1 : -1;
      long az = labs(dz), ax = labs(dx), n = az > ax ? az : ax;
      if (dz) { if (sz > 0) Motor_Z_CW(); else Motor_Z_CCW(); Motor_Z_Dir = sz > 0 ? CW : CCW; }
      if (dx) { if (sx > 0) Motor_X_CW(); else Motor_X_CCW(); Motor_X_Dir = sx > 0 ? CW : CCW; }
      delayMicroseconds(20);
      double len_um = sqrt(e2_sq(az * e2_gc_k(0)) + e2_sq(ax * e2_gc_k(1)));
      double t_us = len_um / 1000 / (mm_min / 60) * 1e6;           // время отрезка
      uint32_t period = (uint32_t)(t_us / n);
      if (period < 1000000 / E2_GC_MAX_SPS) period = 1000000 / E2_GC_MAX_SPS;
      long ez = n / 2, ex = n / 2;
      uint32_t t = micros();
      for (long i = 0; i < n; i++) {
         bool stz = false, stx = false;
         ez -= az; if (ez < 0) { ez += n; stz = true; }
         ex -= ax; if (ex < 0) { ex += n; stx = true; }
         if (stz) Motor_Z_SetPulse();
         if (stx) Motor_X_SetPulse();
         delayMicroseconds(5);
         if (stz) { Motor_Z_RemovePulse(); Motor_Z_Pos += sz; Z_pos += sz; }
         if (stx) { Motor_X_RemovePulse(); Motor_X_Pos += sx; X_pos += sx; MX_pos += sx; }
         if ((i & 31) == 0) {
            e2_cal_poll();
            if (e2_cal_abort) return false;
            e2_gc_report();
         }
         long edge = i < n - i ? i : n - i;                         // разгон и торможение: с 4× периода
         uint32_t p = edge >= E2_GC_RAMP ? period : period + 3 * period * (E2_GC_RAMP - edge) / E2_GC_RAMP;
         uint32_t el = micros() - t;
         if (el < p) delayMicroseconds(p - el);
         t += p;
      }
   }
   return Motor_Z_Pos == tz && Motor_X_Pos == tx;
}

// В (z, x) мм программы (x — радиус)
static const char* e2_gc_goto(float z, float x, float mm_min)
{
   long tz = e2_gc.rz + lround((z - e2_gc.rdz) * 1000 / e2_gc_k(0) * e2_gc_sign(0));
   long tx = e2_gc.rx + lround((x - e2_gc.rdx) * 1000 / e2_gc_k(1) * e2_gc_sign(1));
   if ((limit_Left_flag && tz > Limit_Pos_Left) || (limit_Right_flag && tz < Limit_Pos_Right) ||
       (limit_Front_flag && tx > Limit_Pos_Front) || (limit_Rear_flag && tx < Limit_Pos_Rear))
      return "ЗА УПОРОМ";
   if (!e2_gc_segment(tz, tx, mm_min)) return "ОСТАНОВЛЕНО";
   e2_gc.z = z; e2_gc.x = x;
   return nullptr;
}

static bool e2_gc_word(const char* s, char w, float* v)
{
   for (const char* p = s; *p; p++)
      if (*p == w && (p == s || !isalpha((unsigned char)p[-1]))) { *v = atof(p + 1); return true; }
   return false;
}

static const char* e2_gc_exec(const char* line)
{
   float v;
   // модальные и служебные
   for (const char* p = line; (p = strchr(p, 'G')); p++) {
      int g = atoi(p + 1);
      switch (g) {
         case 0: case 1: case 2: case 3: e2_gc.motion = g; break;
         case 7:  e2_gc.diam = true;  break;
         case 8:  e2_gc.diam = false; break;
         case 20: e2_gc.unit = 25.4f; break;
         case 21: e2_gc.unit = 1.0f;  break;
         case 90: e2_gc.abs = true;   break;
         case 91: e2_gc.abs = false;  break;
         case 94: case 98: e2_gc.per_rev = false; break;              // мм/мин (ISO / Fanuc)
         case 95: case 99: e2_gc.per_rev = true;  break;              // мм/об — по оборотам шпинделя
         case 50: {                                                   // Fanuc: S — предел оборотов; X/Z — смена координат
            float t;
            if (e2_gc_word(line, 'X', &t) || e2_gc_word(line, 'Z', &t)) return "G50 X/Z (СМЕНА КООРДИНАТ) НЕ ПОДДЕРЖИВАЕТСЯ";
            return nullptr;
         }
         case 96: case 97: break;                                     // скорость резания / обороты — шпиндель вручную
         case 28: {
            const char* err = e2_gc_goto(e2_gc.z, 0, GCODE_RAPID_MM_MIN);
            return err ? err : e2_gc_goto(0, 0, GCODE_RAPID_MM_MIN);
         }
         case 4: case 17: case 18: case 40: case 54: case 80: break;   // пауза — на дисплее; плоскость, компенсации — нет
         default: { static char b[40]; snprintf(b, sizeof(b), "G%d НЕ ПОДДЕРЖИВАЕТСЯ", g); return b; }
      }
   }
   if (e2_gc_word(line, 'F', &v)) { if (v <= 0) return "F <= 0"; e2_gc.feed = v * e2_gc.unit; }
   bool hz = e2_gc_word(line, 'Z', &v); float z = hz ? v * e2_gc.unit : 0;
   float xv; bool hx = e2_gc_word(line, 'X', &xv); float x = hx ? xv * e2_gc.unit / (e2_gc.diam ? 2 : 1) : 0;
   if (!hz && !hx) return nullptr;                              // нет перемещения
   float tz = hz ? (e2_gc.abs ? z : e2_gc.z + z) : e2_gc.z;
   float tx = hx ? (e2_gc.abs ? x : e2_gc.x + x) : e2_gc.x;
   if (e2_gc.motion == 0) return e2_gc_goto(tz, tx, GCODE_RAPID_MM_MIN);
   // подача на оборот: мм/мин = F × обороты (обороты меряются раз в 250 мс; синхронизации со шпинделем нет)
   float feed = e2_gc.feed;
   if (e2_gc.per_rev && e2_gc.motion) {
      if (e2_rpm == 0) return "ШПИНДЕЛЬ СТОИТ: G99/G95 - ПОДАЧА НА ОБОРОТ, ВКЛЮЧИТЕ ШПИНДЕЛЬ";
      feed = e2_gc.feed * e2_rpm;
   }
   if (e2_gc.motion == 1) return e2_gc_goto(tz, tx, feed);
   // дуга: центр — I (по X, радиус) и K (по Z) от начала, или R
   float ci, ck;
   bool hi = e2_gc_word(line, 'I', &ci), hk = e2_gc_word(line, 'K', &ck), hr;
   float r; hr = e2_gc_word(line, 'R', &r);
   float z0 = e2_gc.z, x0 = e2_gc.x, cz, cx;
   bool cw = e2_gc.motion == 2;
   if (hi || hk) {
      cz = z0 + (hk ? ck * e2_gc.unit : 0);
      cx = x0 + (hi ? ci * e2_gc.unit : 0);
   } else if (hr) {
      r *= e2_gc.unit;
      float mz = (z0 + tz) / 2, mx = (x0 + tx) / 2, dz = tz - z0, dx = tx - x0, d = sqrtf(dz * dz + dx * dx);
      if (d < 1e-6f || fabsf(r) < d / 2 - 1e-4f) return "ДУГА: R МЕНЬШЕ ПОЛОВИНЫ ХОРДЫ";
      float h = sqrtf(fmaxf(r * r - d * d / 4, 0));
      // центр слева/справа от хорды: плоскость ZX, G2 — по часовой; R < 0 — дуга больше 180°
      float s = (cw ? -1 : 1) * (r > 0 ? 1 : -1);
      cz = mz - s * h * dx / d; cx = mx + s * h * dz / d;
   } else return "ДУГА БЕЗ I/K ИЛИ R";
   float a0 = atan2f(x0 - cx, z0 - cz), a1 = atan2f(tx - cx, tz - cz);
   float rad = sqrtf(e2_sq(z0 - cz) + e2_sq(x0 - cx));
   float da = a1 - a0;
   if (cw)  { while (da >= 0) da -= 2 * (float)M_PI; }          // по часовой в плоскости Z (гориз.) — X (верт.)
   else     { while (da <= 0) da += 2 * (float)M_PI; }
   if (fabsf(tz - z0) < 1e-6f && fabsf(tx - x0) < 1e-6f) da = cw ? -2 * (float)M_PI : 2 * (float)M_PI;
   int segs = (int)ceilf(fabsf(da) * rad / 0.1f); if (segs < 1) segs = 1;
   for (int i = 1; i <= segs; i++) {
      float a = a0 + da * i / segs;
      float pz = i == segs ? tz : cz + rad * cosf(a), px = i == segs ? tx : cx + rad * sinf(a);
      const char* err = e2_gc_goto(pz, px, feed);
      if (err) return err;
   }
   return nullptr;
}

// <GCODE:строка> с дисплея
void e2_gcode_line(const char* line)
{
   const char* err = nullptr;
   while (*line == ' ') line++;
   if (!strcmp(line, "STOP")) { Display_Send_CMD("OK", nullptr); return; }   // стоп между строками — уже стоим
   if (*line == 'O' || *line == '%') { Display_Send_CMD("OK", nullptr); return; }   // номер программы, начало/конец ленты
   static int s_mode = -1;
   if (Mode != s_mode) { s_mode = Mode; e2_gc.known = false; }      // вошли в M8 — опорная точка заново
   if (Mode != Mode_Reserve) err = "ВКЛЮЧИТЕ РЕЖИМ M8 (G-КОД)";
   else if (Joy_Z_flag || Joy_X_flag) err = "ОТПУСТИТЕ ДЖОЙСТИК";
   else if (!e2_gc_sign(0) || !e2_gc_sign(1)) err = "ПРОВЕДИТЕ ОСИ ДЖОЙСТИКОМ НА 2 ММ (НАПРАВЛЕНИЕ ЛИНЕЕК)";
   if (!err) {
      if (!e2_gc.known) {
         e2_pos_refresh();
         e2_gc.rz = Motor_Z_Pos; e2_gc.rx = Motor_X_Pos;
         e2_gc.z = e2_gc.rdz = e2_gc_disp_mm(0); e2_gc.x = e2_gc.rdx = e2_gc_disp_mm(1);
         e2_gc.known = true;
      }
      bool ena_z = Read_Z_Ena_State, ena_x = Read_X_Ena_State;
      if (!ena_z) Motor_Z_Enable();
      if (!ena_x) Motor_X_Enable();
      e2_cal_abort = false; e2_cal_joy = true;
      e2_cal_rx_clear();
      err = e2_gc_exec(line);
      e2_scale_reref();
      e2_pos_refresh();
      Display_On_Position_Change();
      if (err && !strcmp(err, "ОСТАНОВЛЕНО")) e2_gc.known = false;   // положение программы — заново с экрана
   }
   if (err) { Display_Send_CMD("ERR", err); Serial.print(F("[GCODE] ")); Serial.print(line); Serial.print(F(" — ")); Serial.println(err); }
   else Display_Send_CMD("OK", nullptr);
}

// Выход из M8 / ввод оси — положение программы заново с экрана
void e2_gcode_reset(void) { e2_gc.known = false; }
