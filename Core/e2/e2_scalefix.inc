// [STM32] Линейки в автоциклах (BACKLOG-025). Подключается в конце e2_core.cpp, после e2_calib.inc.
//
// 1. Направление и масштаб линейки — по первому ходу оси больше 0.5 мм (шаги против линейки). Если масштаб
//    не сходится с механикой больше чем в 1.4 раза (например, блок DRO показывает X в диаметрах) — функции
//    этой оси выключаются, на экран — сообщение.
// 2. Контроль пропуска шагов: на каждой остановке оси (300 мс без шагов — линейка успела обновиться) приращение
//    линейки сравнивается с приращением счётчика шагов. Расхождение больше допуска — сообщение и блокировка
//    движения от джойстика, пока джойстик не отпущен. Счётчик ядра — положение каретки только при настроенном
//    люфте (компенсация), поэтому без калибровки люфта на развороте возможны ложные срабатывания.
// 3. Доводка X перед проходом (подача, асинхронная подача, резьба; «Внутренний»/«Наружный»): в начале цикла,
//    пока X стоит на нуле цикла (Null_X_Pos), запоминается показание линейки. После врезания, до начала
//    прохода, X доезжает по линейке до глубины «ноль + врезание» — физическими шагами, счётчик ядра не
//    меняется (по нему автомат цикла узнаёт, что врезание закончено). Так глубина не зависит от люфта и
//    пропущенных шагов.

#define E2_STILL_MS     300     // ось стоит столько — линейка/DRO успели прислать свежее показание
#define E2_FIX_MAX_UM   1000    // доводка больше — это не люфт, а сбой: остановиться

struct E2Track { long m; int32_t s; long m_seen; uint32_t still_ms; bool ref; };
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
      if (m != t.m_seen) { t.m_seen = m; t.still_ms = now; continue; }
      if (now - t.still_ms < E2_STILL_MS) continue;
      // до первого движения сторона люфта неизвестна — счётчик ещё не положение каретки
      if (!e2_scale_axis_ok(ax) || !(ax ? e2_bl_loaded_x : e2_bl_loaded_z)) { t.ref = false; continue; }
      int32_t s = e2_scale_now(ax);
      if (!t.ref) { t.m = m; t.s = s; t.ref = true; continue; }
      long dm = m - t.m;
      if (dm == 0) { t.s = s; continue; }                // не ехали: ноль на блоке DRO, ввод оси, доводка
      double k = e2_um_per_step(ax);
      double ds = s - t.s;
      t.m = m; t.s = s;
      if (!e2_scale_sign[ax]) {                          // направление и масштаб — по ходу больше 0.5 мм
         if (fabs(dm * k) < 500) continue;
         double r = ds / (dm * k);
         if (fabs(r) < 0.7 || fabs(r) > 1.4) {
            e2_scale_bad[ax] = true;
            Display_Send_CMD("ALERT", ax ? "ЛИНЕЙКА X НЕ СОВПАДАЕТ С МЕХАНИКОЙ" : "ЛИНЕЙКА Z НЕ СОВПАДАЕТ С МЕХАНИКОЙ");
            Serial.print(F("[SCALE] масштаб линейки / шаги = ")); Serial.println(r);
         } else e2_scale_sign[ax] = r > 0 ? 1 : -1;
         continue;
      }
      long err = lround(ds - e2_scale_sign[ax] * dm * k);  // мкм: линейка против шагов
      if (e2_pos.loss_check && labs(err) > e2_pos.loss_tol_um && !e2_loss_block) {
         e2_loss_block = true;
         e2_joy_free_ms = now;
         e2_scale_msg("ПРОПУСК ШАГОВ", ax, err);
         Beep_Error();
      }
   }
}

// В начале Joy_LeftPressed()/Joy_RightPressed(): true — автоцикл/движение в этот раз не вызывать
bool e2_scale_gate(void)
{
   if (e2_loss_block) return true;
   if (!e2_pos.fix_x || !e2_scale_axis_ok(1) || !e2_scale_sign[1]) return false;
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
      return false;
   }
   if (!cycle_flag || !e2_null_ok) return false;
   long target = sub == Sub_Mode_Feed_Ext ? Limit_Pos_Front : Limit_Pos_Rear;
   if (Motor_X_Pos != target || (e2_fixed_pass == Pass_Nr && e2_fixed_target == target)) return false;
   if (!still) return true;                            // врезание кончилось — ждать свежую линейку

   double k = e2_um_per_step(1) * e2_scale_sign[1];    // мкм линейки на шаг
   long bl = e2_bl_x;
   e2_bl_x = 0;                                        // доводка — без сдвига счётчика компенсацией
   for (int i = 0; i < 3; i++) {
      long want = e2_null_s + lround((target - e2_null_m) * k);
      long err = want - dro_pos_x;
      long steps = lround(err / k);
      if (labs(steps) < McSTEP_X || labs(err) < 3) break;   // меньше полного шага / 3 мкм — на месте
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
      e2_cal_joy = true;
      e2_cal_read('X');
      Serial.print(F("[SCALE] доводка X: ")); Serial.print(err); Serial.println(F(" мкм"));
   }
   e2_bl_x = bl;
   e2_fixed_pass = Pass_Nr;
   e2_fixed_target = target;
   e2_scale_reref();
   return true;
}
