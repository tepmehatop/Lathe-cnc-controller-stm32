// [STM32] Калибровка шага винта и люфта по линейкам (BACKLOG-029). Подключается в конце e2_core.cpp.
//
// Ось проезжает три раза: +P (выбрать люфт), +N, −N (разворот), −P — и возвращается на место
// (шагов вперёд и назад поровну). Линейка читается на остановках:
//   +N без разворота          → микрон на шаг k = Δлинейки / N → шаг винта = k × шагов на оборот;
//   −N после разворота        → каретка прошла (N − B)·k → люфт B = N − Δ / k;
//   +P после разворота назад  → то же для другой стороны: B = P − Δ / k.
// Знак k — направление линейки относительно мотора, в расчёте он учитывается сам.
// Пока идёт калибровка, loop() стоит: линейки/DRO читаются здесь, прервать — джойстиком или
// кнопкой СТОП экрана (CAL_STOP). Компенсация люфта на время калибровки выключена.
// Ответ экрану: <CAL:RUN:Z>, затем <CAL:RES:Z,шаг винта мкм,люфт мкм,нм на шаг> или <CAL:ERR:причина>.

static bool e2_cal_abort = false;
static bool e2_cal_joy = true;           // джойстик прерывает (калибровка); доводка X в цикле — нет
static char e2_cal_rx[16];

// Линейки/DRO обновить; заодно смотреть, не пришёл ли от экрана CAL_STOP и не тронут ли джойстик
static void e2_cal_poll(void)
{
   e2_pos_update();
   while (DISPLAY_UART.available()) {
      memmove(e2_cal_rx, e2_cal_rx + 1, sizeof(e2_cal_rx) - 2);
      e2_cal_rx[sizeof(e2_cal_rx) - 2] = (char)DISPLAY_UART.read();
      e2_cal_rx[sizeof(e2_cal_rx) - 1] = 0;
      if (strstr(e2_cal_rx, "CAL_STOP")) e2_cal_abort = true;
   }
   if (e2_cal_joy && (Joy_Read & 0x0F) != 0x0F) e2_cal_abort = true;
}

// Показание линейки оси, мкм: подождать, пока придут свежие данные (DRO — пакеты 50 Гц)
static int32_t e2_cal_read(char ax)
{
   uint32_t t0 = millis();
   while (millis() - t0 < 300 && !e2_cal_abort) { e2_cal_poll(); delay(1); }
   return ax == 'Z' ? dro_pos_y : dro_pos_x;
}

// n шагов (знак — направление) с разгоном и торможением: 400 → 1600 шагов/с
static void e2_cal_move(char ax, long n)
{
   if (ax == 'Z') { if (n > 0) Motor_Z_CW(); else Motor_Z_CCW(); }
   else           { if (n > 0) Motor_X_CW(); else Motor_X_CCW(); }
   delayMicroseconds(20);                         // DIR раньше STEP
   long cnt = n > 0 ? n : -n;
   const long RAMP = 200;
   uint32_t t = micros();
   for (long i = 0; i < cnt && !e2_cal_abort; i++) {
      long edge = i < cnt - i ? i : cnt - i;
      uint32_t period = edge >= RAMP ? 625 : 2500 - (2500 - 625) * edge / RAMP;
      if (ax == 'Z') { Motor_Z_SetPulse(); delayMicroseconds(5); Motor_Z_RemovePulse(); }
      else           { Motor_X_SetPulse(); delayMicroseconds(5); Motor_X_RemovePulse(); }
      if ((i & 15) == 0) e2_cal_poll();
      uint32_t el = micros() - t;                 // дождаться периода (delayMicroseconds — и в симуляции)
      if (el < period) delayMicroseconds(period - el);
      t += period;
   }
}

static const char* e2_calibrate(char ax, int32_t len_um, int32_t* screw_um, int32_t* bl_um, int32_t* k_nm)
{
   if (e2_pos.source == POS_SOURCE_STEPS) return "НУЖНЫ ЛИНЕЙКИ ИЛИ БЛОК DRO";
   if (e2_pos.source == POS_SOURCE_SCALES && !DRV_Scales_Present(ax == 'Z' ? SCALE_AXIS_Z : SCALE_AXIS_X))
      return "НА ЭТОЙ ОСИ НЕТ ЛИНЕЙКИ";
   if (!e2_machine_idle()) return "ОСТАНОВИТЕ ШПИНДЕЛЬ И ДВИЖЕНИЕ";
   const E2Mech& m = e2_mech;
   long spr = ax == 'Z' ? (long)m.z_steps * m.z_mc : (long)m.x_steps * m.x_mc;   // шагов на оборот винта
   int screw = ax == 'Z' ? m.z_screw : m.x_screw;                                // 0.01 мм
   long N = (long)((double)len_um * spr / (screw * 10));                         // шагов на L
   long P = spr * 100 / screw;                                                   // 1 мм — больше любого люфта
   if (N < 4 * P) return "СЛИШКОМ КОРОТКИЙ ХОД";

   long& bl = ax == 'Z' ? e2_bl_z : e2_bl_x;                                    // компенсацию — выключить
   long bl_saved = bl; bl = 0;
   bool ena = ax == 'Z' ? Read_Z_Ena_State : Read_X_Ena_State;
   if (!ena) { if (ax == 'Z') Motor_Z_Enable(); else Motor_X_Enable(); }
   e2_cal_abort = false;
   memset(e2_cal_rx, 0, sizeof(e2_cal_rx));

   double k_sum = 0, b_sum = 0; int k_n = 0, b_n = 0;
   int32_t s_prev = 0; bool have_prev = false;
   const int CYCLES = 3;
   for (int c = 0; c < CYCLES && !e2_cal_abort; c++) {
      e2_cal_move(ax, P);       int32_t s0 = e2_cal_read(ax);
      e2_cal_move(ax, N);       int32_t s1 = e2_cal_read(ax);
      double k = (double)(s1 - s0) / N;                                          // мкм на шаг, со знаком
      e2_cal_move(ax, -N);      int32_t s2 = e2_cal_read(ax);
      e2_cal_move(ax, -P);      int32_t s3 = e2_cal_read(ax);
      if (e2_cal_abort) break;
      k_sum += k; k_n++;
      if (k != 0) {
         b_sum += N - (s1 - s2) / k; b_n++;                                       // разворот + → −
         if (have_prev) { b_sum += P - (s0 - s_prev) / k; b_n++; }               // разворот − → +
      }
      s_prev = s3; have_prev = true;
      char p[24]; snprintf(p, sizeof(p), "%c:%d/%d", ax, c + 1, CYCLES);
      Display_Send_CMD("CAL_RUN", p);
   }

   e2_scale_reref();                             // каретка могла сместиться на люфт — новая опорная точка
   if (!ena) {                                   // Motor_*_Disable() у автора — блок без do-while
      if (ax == 'Z') { Motor_Z_Disable(); } else { Motor_X_Disable(); }
   }
   bl = bl_saved;
   if (e2_cal_abort) return "ПРЕРВАНО";
   double k = k_sum / k_n;
   if (fabs(k) * spr < 50) return "ЛИНЕЙКА НЕ ДВИЖЕТСЯ";                          // меньше 0.05 мм на оборот
   double b = b_n ? b_sum / b_n : 0;
   if (b < 0) b = 0;
   *k_nm = (int32_t)lround(fabs(k) * 1000);
   *screw_um = (int32_t)lround(fabs(k) * spr);
   *bl_um = (int32_t)lround(b * fabs(k));
   return nullptr;
}

// Команда экрана CAL:<Z|X>:<мкм хода>
void e2_cal_from_display(const char* prm)
{
   char ax = prm[4];
   int32_t len = atol(prm + 6);
   Display_Send_CMD("CAL_RUN", ax == 'Z' ? "Z:0/3" : "X:0/3");
   int32_t screw = 0, bl = 0, k = 0;
   const char* err = (ax == 'Z' || ax == 'X') ? e2_calibrate(ax, len, &screw, &bl, &k) : "НЕВЕРНАЯ КОМАНДА";
   char b[64];
   if (err) {
      Display_Send_CMD("CAL_ERR", err);
      Beep_Error();
   } else {
      snprintf(b, sizeof(b), "%c,%ld,%ld,%ld", ax, (long)screw, (long)bl, (long)k);
      Display_Send_CMD("CAL_RES", b);
      BeepBeep();
   }
   Serial.print(F("[CAL] ")); Serial.print(ax); Serial.print(' ');
   Serial.println(err ? err : b);
}
