// [STM32] Линейки в автоциклах (BACKLOG-025). Подключается в конце e2_core.cpp, после e2_calib.inc.
//
// 1. Направление и масштаб линейки — по ходу оси от любой остановки больше 2 мм (шаги против линейки; люфт
//    до 0.5 мм на оценку почти не влияет, сторона люфта для этого не нужна; скачок показания на стоящей оси —
//    ноль на блоке DRO — принимается). Если масштаб
//    не сходится с механикой больше чем в 1.4 раза (например, блок DRO показывает X в диаметрах) — функции
//    этой оси выключаются, на экран — сообщение; следующий сошедшийся ход их включает обратно.
// 2. Контроль пропуска шагов: на каждой остановке оси (300 мс без шагов — линейка успела обновиться) приращение
//    линейки сравнивается с приращением счётчика шагов. Расхождение больше допуска — сообщение и блокировка
//    движения от джойстика, пока джойстик не отпущен. Счётчик ядра — положение каретки только при настроенном
//    люфте (компенсация); пока люфт не задан, отрезки с разворотом не сверяются — только ходы в одну сторону.
// 3. Доводка X перед проходом (подача, асинхронная подача, резьба; «Внутренний»/«Наружный»): в начале цикла,
//    пока X стоит на нуле цикла (Null_X_Pos), запоминается показание линейки. После врезания, до начала
//    прохода, X доезжает по линейке до глубины «ноль + врезание» — физическими шагами, счётчик ядра не
//    меняется (по нему автомат цикла узнаёт, что врезание закончено). Так глубина не зависит от люфта и
//    пропущенных шагов.

#define E2_STILL_MS     300     // ось стоит столько — линейка/DRO успели прислать свежее показание
#define E2_FIX_MAX_UM   1000    // доводка больше — это не люфт, а сбой: остановиться

struct E2Track { long m; int32_t s; long m_seen; uint32_t still_ms; bool ref; long sm; int32_t ss; bool sref;
                 int8_t mdir; bool turned; };   // mdir — последнее направление счётчика, turned — был разворот
static E2Track  e2_tr[2];               // 0 — Z, 1 — X
static int8_t   e2_scale_sign[2];       // знак линейки относительно шагов (0 — ещё не известен)
static bool     e2_scale_bad[2];        // масштаб не сошёлся — функции оси выключены
static bool     e2_loss_block = false;  // пропуск шагов: движение от джойстика заблокировано
static uint32_t e2_joy_free_ms = 0;
static int32_t  e2_null_s;              // ноль цикла X: показание линейки и шаги
static long     e2_null_m;
static bool     e2_null_ok = false;
static long     e2_fixed_target = 0;    // уже доведено: цель и проход
static int      e2_fixed_pass = -1;

static double  e2_um_per_step(int ax)
{
   return ax ? (double)SCREW_X * 10 / ((long)MOTOR_X_STEP_PER_REV * McSTEP_X)
             : (double)SCREW_Z * 10 / ((long)MOTOR_Z_STEP_PER_REV * McSTEP_Z);
}
static bool    e2_scale_axis_ok(int ax)
{
   if (e2_pos.source == POS_SOURCE_STEPS || e2_scale_bad[ax]) return false;
   return e2_pos.source != POS_SOURCE_SCALES || DRV_Scales_Present(ax ? SCALE_AXIS_X : SCALE_AXIS_Z);
}
static int32_t e2_scale_now(int ax) { return ax ? dro_pos_x : dro_pos_y; }
// Показание уже настоящее: от блока DRO пришёл хотя бы один пакет / линейка шлёт кадры. До этого
// dro_pos_* = 0 — опорная точка по нему испортила бы направление и масштаб.
static bool    e2_scale_fresh(int ax)
{
   if (e2_pos.source == POS_SOURCE_DRO_UART) return DRV_DRO_GetPacketCount() > 0;
   return DRV_Scales_IsOk(ax ? SCALE_AXIS_X : SCALE_AXIS_Z);
}
static long    e2_motor_now(int ax) { return ax ? Motor_X_Pos : Motor_Z_Pos; }

void e2_scale_reref(void) { e2_tr[0].ref = e2_tr[1].ref = false; }

static void e2_scale_msg(const char* what, int ax, long um)
{
   char b[64];
   snprintf(b, sizeof(b), "%s %c: %s%ld.%02ld ММ", what, ax ? 'X' : 'Z', um < 0 ? "-" : "",
            labs(um) / 1000, labs(um) % 1000 / 10);
   Display_Send_CMD("ALERT", b);
   Serial.print(F("[SCALE] ")); Serial.println(b);
}

// Из e2_service(): остановки осей, направление линейки, пропуск шагов
void e2_scale_track(void)
{
   if (e2_pos.source == POS_SOURCE_STEPS) return;
   uint32_t now = millis();
   if (e2_loss_block) {                                   // снять блокировку — джойстик отпущен на 1 с
      if ((Joy_Read & 0x0F) != 0x0F) e2_joy_free_ms = now;
      else if (now - e2_joy_free_ms > 1000) { e2_loss_block = false; e2_scale_reref(); }
   }
   for (int ax = 0; ax < 2; ax++) {
      E2Track& t = e2_tr[ax];
      long m = e2_motor_now(ax);
      // разворот — по направлению выданных шагов (e2_bl_loaded_*): счётчик ядро ещё и округляет до полного
      // шага после остановки, его «шаг назад» разворотом не считать
      int8_t d = ax ? e2_bl_loaded_x : e2_bl_loaded_z;
      if (t.mdir && d && d != t.mdir) t.turned = true;
      if (d) t.mdir = d;
      if (m != t.m_seen) { t.m_seen = m; t.still_ms = now; continue; }
      if (now - t.still_ms < E2_STILL_MS) continue;
      bool present = e2_pos.source != POS_SOURCE_STEPS &&
                     (e2_pos.source != POS_SOURCE_SCALES || DRV_Scales_Present(ax ? SCALE_AXIS_X : SCALE_AXIS_Z));
      if (!present || !e2_scale_fresh(ax)) { t.ref = t.sref = false; continue; }
      int32_t s = e2_scale_now(ax);
      double k = e2_um_per_step(ax);
      if (!e2_scale_sign[ax] || e2_scale_bad[ax]) {      // направление и масштаб линейки
         if (!t.sref || m == t.sm) { t.sm = m; t.ss = s; t.sref = true; }   // стоим: скачок (ноль на DRO) — принять
         else if (fabs((m - t.sm) * k) >= 2000) {
            double r = (s - t.ss) / ((m - t.sm) * k);
            t.sm = m; t.ss = s;
            if (fabs(r) < 0.7 || fabs(r) > 1.4) {
               if (!e2_scale_bad[ax])
                  Display_Send_CMD("ALERT", ax ? "ЛИНЕЙКА X НЕ СОВПАДАЕТ С МЕХАНИКОЙ" : "ЛИНЕЙКА Z НЕ СОВПАДАЕТ С МЕХАНИКОЙ");
               e2_scale_bad[ax] = true;
               Serial.print(F("[SCALE] масштаб линейки / шаги = ")); Serial.println(r);
            } else {                                     // сошлось (в том числе после ложного «не совпадает»)
               e2_scale_bad[ax] = false;
               e2_scale_sign[ax] = r > 0 ? 1 : -1;
            }
         }
      }
      if (e2_scale_bad[ax]) { t.ref = false; continue; }
      // контроль шагов — когда сторона люфта известна (до первого движения счётчик — ещё не каретка)
      if (!(ax ? e2_bl_loaded_x : e2_bl_loaded_z)) { t.ref = false; continue; }
      if (!t.ref) { t.m = m; t.s = s; t.ref = true; t.turned = false; continue; }
      long dm = m - t.m;
      if (dm == 0) { t.s = s; t.turned = false; continue; }                // не ехали: ноль на блоке DRO, ввод оси, доводка
      double ds = s - t.s;
      t.m = m; t.s = s;
      bool turned = t.turned;
      t.turned = false;
      if (!e2_scale_sign[ax]) continue;
      // люфт не задан (калибровка) — на развороте каретка отстаёт на люфт: такой отрезок не сверять
      if (turned && !(ax ? e2_bl_x : e2_bl_z)) continue;
      long err = lround(ds - e2_scale_sign[ax] * dm * k);  // мкм: линейка против шагов
      if (e2_pos.loss_check && labs(err) > e2_pos.loss_tol_um && !e2_loss_block) {
         e2_loss_block = true;
         e2_joy_free_ms = now;
         e2_scale_msg("ПРОПУСК ШАГОВ", ax, err);
         Beep_Error();
      }
   }
}

bool e2_scale_blocked(void) { return e2_loss_block; }   // Joy_UpPressed/Joy_DownPressed

// В начале Joy_LeftPressed()/Joy_RightPressed(): true — автоцикл/движение в этот раз не вызывать
bool e2_scale_gate(void)
{
   if (e2_loss_block) return true;
   if (!e2_pos.fix_x || !e2_scale_axis_ok(1)) return false;
   int sub;
   if (Mode == Mode_Feed)        sub = Sub_Mode_Feed;
   else if (Mode == Mode_aFeed)  sub = Sub_Mode_aFeed;
   else if (Mode == Mode_Thread) sub = Sub_Mode_Thread;
   else return false;
   if (sub != Sub_Mode_Feed_Int && sub != Sub_Mode_Feed_Ext) return false;   // «Ручной» (во всех режимах — 2)
   E2Track& t = e2_tr[1];
   bool still = Step_X_flag == OFF && Motor_X_Pos == t.m_seen && millis() - t.still_ms >= E2_STILL_MS;
   if (!cycle_flag && Pass_Nr == 1 && Motor_X_Pos == Null_X_Pos) {     // перед первым проходом: ноль цикла
      if (still) { e2_null_s = dro_pos_x; e2_null_m = Null_X_Pos; e2_null_ok = true; e2_fixed_pass = -1; }
      static uint32_t s_warn = 0;                       // направление линейки ещё не известно — сказать один раз
      if (!e2_scale_sign[1] && millis() - s_warn > 10000) {
         s_warn = millis();
         Display_Send_CMD("ALERT", "ДОВОДКА X НЕ ГОТОВА: ПРОВЕДИТЕ X НА 2 ММ");
      }
      return false;
   }
   if (!cycle_flag || !e2_null_ok || !e2_scale_sign[1]) return false;
   long target = sub == Sub_Mode_Feed_Ext ? Limit_Pos_Front : Limit_Pos_Rear;
   int infeed_dir = sub == Sub_Mode_Feed_Ext ? 1 : -1;   // в сторону детали: наружный — к переднему упору
   if (Motor_X_Pos != target || (e2_fixed_pass == Pass_Nr && e2_fixed_target == target)) return false;
   if (!still) return true;                            // врезание кончилось — ждать свежую линейку

   double k = e2_um_per_step(1) * e2_scale_sign[1];    // мкм линейки на шаг
   long bl = e2_bl_x;
   e2_bl_x = 0;                                        // доводка — без сдвига счётчика компенсацией
   for (int i = 0; i < 3; i++) {
      long want = e2_null_s + lround((target - e2_null_m) * k);
      long err = want - dro_pos_x;
      long steps = lround(err / k);
#ifdef E2_HOST_DEBUG
      extern long e2_dbg_pulses_x(void);
      printf("      [fix] импульсов X всего %ld\n", e2_dbg_pulses_x());
      printf("      [fix] проход %d цель %ld (ноль %ld/%d) хочу %ld есть %ld ошибка %ld шагов %ld\n",
             Pass_Nr, target, e2_null_m, (int)e2_null_s, want, (long)dro_pos_x, err, steps);
#endif
      if (labs(steps) < McSTEP_X || labs(err) < 3) break;   // меньше полного шага / 3 мкм — на месте
      // только в сторону врезания: назад малой поправкой каретку не сдвинуть (люфт), а её следующий ход собьётся
      if (steps * infeed_dir < 0 && labs(err) < E2_FIX_MAX_UM) break;
      if (labs(err) > E2_FIX_MAX_UM) {
         e2_loss_block = true;
         e2_joy_free_ms = millis();
         e2_scale_msg("РАСХОЖДЕНИЕ ЛИНЕЙКИ", 1, err);
         Beep_Error();
         break;
      }
      int8_t dir = steps > 0 ? 1 : -1;
      if (bl && e2_bl_loaded_x && e2_bl_loaded_x != dir) steps += dir * bl;   // разворот: сначала выбрать люфт
      e2_cal_abort = false;
      e2_cal_joy = false;                              // джойстик в цикле удерживается — не прерывать
      e2_cal_move('X', steps);
      e2_cal_read('X');                                // свежее показание (джойстик не прерывает)
      e2_cal_joy = true;
      Serial.print(F("[SCALE] доводка X: ")); Serial.print(err); Serial.println(F(" мкм"));
   }
   e2_bl_x = bl;
   e2_fixed_pass = Pass_Nr;
   e2_fixed_target = target;
   e2_scale_reref();
   return true;
}
