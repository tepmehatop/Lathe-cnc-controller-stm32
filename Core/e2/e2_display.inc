// [STM32] Копия Core/e2/orig/UartDisplay.ino. Изменения помечены «[STM32]»:
// аппаратный UART вместо SoftwareSerial, неблокирующая отправка, команды DISP и GCODE.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** UART ВЫВОД К НОВОМУ ДИСПЛЕЮ (ESP32-S3 JC4827W543) ***** //////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Подключение: Arduino Mega  SoftwareSerial (TX=Pin51 → STM32 → ESP32 GPIO44, RX=Pin52 ← STM32 ← ESP32 GPIO43)
//             ESP32-S3      P1 разъём (RX=GPIO44, TX=GPIO43) — пины не меняются!
// ВАЖНО: пины 0/1 (Serial) заняты SerialEmulation.ino!
//
// Пины 18(TX1)/19(RX1) были освобождены → возвращены ручному энкодеру (Hand Encoder 100 Lines)
// Пины 20(SDA)/21(SCL) временно: Wire(I2C LCD) + энкодер шпинделя делят пины.
//   При записи на старый LCD (I2C) возможны кратковременные помехи в INT0 шпинделя.
//   Это временно — после окончания тестирования старый LCD будет отключён и пины 20/21
//   будут полностью возвращены энкодеру шпинделя.
//
// ЗНАК ПОЗИЦИЙ: Print.ino показывает позиции с ИНВЕРСИЕЙ знака:
//   если Size_Z_mm > 0 → "Z-XX.XX" (минус!), если <= 0 → "Z XX.XX"
//   Поэтому отправляем -(Size_Z_mm) чтобы новый дисплей совпадал со старым.
//
// ДИАПАЗОНЫ Thread_Info[] (67 записей):
//   [0..19]  — метрическая (0.20мм..4.50мм)
//   [20..45] — дюймовая    (6tpi..80tpi+)
//   [46..55] — G-трубная   (G 1/16..G 2)
//   [56..65] — K-трубная   (K 1/16..K 2)

// [STM32] Аппаратный USART3 вместо SoftwareSerial: TX = PB10 → ESP32 RX, RX = PB11 ← ESP32 TX
#define DISPLAY_BAUD DISP_UART_BAUD
HardwareSerial DISPLAY_UART(E2_APIN(E2_PIN_DISP_RX), E2_APIN(E2_PIN_DISP_TX));

// [STM32] Копия каждой отправленной дисплею строки в Serial. На Mega она была всегда;
// здесь выключена: при полном обновлении (~30 строк) Serial тормозил главный цикл.
#ifndef E2_DISP_TX_ECHO
#define E2_DISP_TX_ECHO 0
#endif

// Границы категорий Thread_Info[] (67 записей: [0-19] метр, [20-46] дюйм, [47-56] G, [57-66] K)
#define THR_IDX_INCH_START   20
#define THR_IDX_GPIPE_START  47   // Thread_Info[47] = G 1/16  (46 = 80tpi — последний дюймовый!)
#define THR_IDX_KPIPE_START  57   // Thread_Info[57] = K 1/16  (56 = G 2 — последний G-трубный!)

// [STM32] Категория резьбы с физических кнопок (BACKLOG-015): ▲/▼ с кнопкой ускоренного хода.
// dir > 0 — начало следующей категории (на последней — остаться); dir < 0 — начало текущей, если стоим
// не на нём, иначе начало предыдущей. Удержанная кнопка у автора повторяется — категория не чаще раза в 0.6 с.
static byte e2_thread_cat_step(byte step, int dir)
{
  static uint32_t s_last = 0;
  if (millis() - s_last < 600) return step;
  s_last = millis();
  static const byte starts[] = { 0, THR_IDX_INCH_START, THR_IDX_GPIPE_START, THR_IDX_KPIPE_START };
  int cat = 0;
  for (int i = 0; i < 4; i++) if (step >= starts[i]) cat = i;
  if (dir > 0) return cat < 3 ? starts[cat + 1] : step;
  if (step != starts[cat]) return starts[cat];
  return cat > 0 ? starts[cat - 1] : step;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ИНИЦИАЛИЗАЦИЯ ***** //////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Display_UART_Init()
{
  DISPLAY_UART.begin(DISPLAY_BAUD);
  delay(500);                         // Ждём пока ESP32 поднимется
  Display_Send_CMD("PING", nullptr);
  delay(200);
  Display_Send_All();                 // Отправляем начальное состояние Arduino
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** НИЗКОУРОВНЕВЫЕ ФУНКЦИИ ОТПРАВКИ ***** ///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Display_Send_CMD(const char* cmd, const char* params)
{
  // [STM32] Не блокировать цикл дольше 10 мс, если буфер передачи заполнен
  uint32_t t0 = millis();
  while (DISPLAY_UART.availableForWrite() < 40 && millis() - t0 < 10) {}
  DISPLAY_UART.print('<');
  DISPLAY_UART.print(cmd);
  if (params && params[0] != '\0') {
    DISPLAY_UART.print(':');
    DISPLAY_UART.print(params);
  }
  DISPLAY_UART.println('>');

#if E2_DISP_TX_ECHO
  Serial.print(F("DISP TX: <"));
  Serial.print(cmd);
  if (params && params[0]) { Serial.print(':'); Serial.print(params); }
  Serial.println('>');
#endif
}

void Display_Send_Int(const char* cmd, int32_t val)
{
  char buf[20];
  snprintf(buf, sizeof(buf), "%ld", (long)val);
  Display_Send_CMD(cmd, buf);
}

void Display_Send_Int2(const char* cmd, int32_t a, int32_t b)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld,%ld", (long)a, (long)b);
  Display_Send_CMD(cmd, buf);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ***** ///////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Угол конуса × 10 для каждого из 63 индексов Cone_Info[].
// Диапазоны: [0]=45°, [1]=30°, [2-30]=1°-29°, [31-44]=31°-44°,
//            [45-51]=KM0-KM6, [52-62]=1:4..3:25
static const int16_t s_cone_angle_x10[] = {
  450,  // [0]  45.0°
  300,  // [1]  30.0°
   10,  // [2]   1.0°
   20,  // [3]   2.0°
   30,  // [4]   3.0°
   40,  // [5]   4.0°
   50,  // [6]   5.0°
   60,  // [7]   6.0°
   70,  // [8]   7.0°
   80,  // [9]   8.0°
   90,  // [10]  9.0°
  100,  // [11] 10.0°
  110,  // [12] 11.0°
  120,  // [13] 12.0°
  130,  // [14] 13.0°
  140,  // [15] 14.0°
  150,  // [16] 15.0°
  160,  // [17] 16.0°
  170,  // [18] 17.0°
  180,  // [19] 18.0°
  190,  // [20] 19.0°
  200,  // [21] 20.0°
  210,  // [22] 21.0°
  220,  // [23] 22.0°
  230,  // [24] 23.0°
  240,  // [25] 24.0°
  250,  // [26] 25.0°
  260,  // [27] 26.0°
  270,  // [28] 27.0°
  280,  // [29] 28.0°
  290,  // [30] 29.0°
  310,  // [31] 31.0°
  320,  // [32] 32.0°
  330,  // [33] 33.0°
  340,  // [34] 34.0°
  350,  // [35] 35.0°
  360,  // [36] 36.0°
  370,  // [37] 37.0°
  380,  // [38] 38.0°
  390,  // [39] 39.0°
  400,  // [40] 40.0°
  410,  // [41] 41.0°
  420,  // [42] 42.0°
  430,  // [43] 43.0°
  440,  // [44] 44.0°
   30,  // [45] KM0 ~3.0°
   29,  // [46] KM1 ~2.9°
   29,  // [47] KM2 ~2.9°
   29,  // [48] KM3 ~2.9°
   32,  // [49] KM4 ~3.2°
   32,  // [50] KM5 ~3.2°
   30,  // [51] KM6 ~3.0°
  143,  // [52] 1:4  14.3°
  114,  // [53] 1:5  11.4°
   82,  // [54] 1:7   8.2°
   57,  // [55] 1:10  5.7°
   36,  // [56] 1:16  3.6°
   29,  // [57] 1:20  2.9°
   24,  // [58] 1:24  2.4°
   19,  // [59] 1:30  1.9°
   11,  // [60] 1:50  1.1°
   63,  // [61] 7:64  6.3°
   69,  // [62] 3:25  6.9°
};

// Нормализованный подрежим (1=Внутр, 2=Ручн, 3=Нар)
static int _Display_GetSubMode()
{
  switch (Mode)
  {
    case Mode_Feed:
      return (Sub_Mode_Feed == Sub_Mode_Feed_Int) ? 1 :
             (Sub_Mode_Feed == Sub_Mode_Feed_Man) ? 2 : 3;
    case Mode_aFeed:
      return (Sub_Mode_aFeed == Sub_Mode_aFeed_Int) ? 1 :
             (Sub_Mode_aFeed == Sub_Mode_aFeed_Man) ? 2 : 3;
    case Mode_Thread:
      return (Sub_Mode_Thread == Sub_Mode_Thread_Int) ? 1 :
             (Sub_Mode_Thread == Sub_Mode_Thread_Man) ? 2 : 3;
    case Mode_Cone_L:
    case Mode_Cone_R:
      return (Sub_Mode_Cone == Sub_Mode_Cone_Int) ? 1 :
             (Sub_Mode_Cone == Sub_Mode_Cone_Man) ? 2 : 3;
    case Mode_Sphere:
      return (Sub_Mode_Sphere == Sub_Mode_Sphere_Int) ? 1 :
             (Sub_Mode_Sphere == Sub_Mode_Sphere_Man) ? 2 : 3;
    default:
      return 2; // Manual
  }
}

// Рассчитать TOTAL Циклов для M3 (всего, не остаток).
// Остаток вычисляется на ESP32 из pass_nr: remaining = total - pass_nr + 1
static int _Display_GetThreadCycles()
{
  return Thread_Info[Thread_Step].Pass + PASS_FINISH + Pass_Fin + Thr_Pass_Summ;
}

// Отправить все данные о текущей резьбе (имя + RPM + Ход)
static void _Display_SendThread()
{
  // Имя резьбы — строка из таблицы ("1.50mm", " 8tpi ", "G  1/8", ...)
  Display_Send_CMD("THREAD_NAME", Thread_Info[Thread_Step].Thread_Print);

  // Шаг резьбы в мм × 100 (ВАЖНО: скобки исправляют приоритет каста)
  Display_Send_Int("THREAD", (int32_t)(Thread_Info[Thread_Step].Step * 100.0f));

  // Максимальные обороты для этой резьбы (Об/мин)
  Display_Send_Int("RPM_LIM", Thread_Info[Thread_Step].Limit_Print);

  // Ход резьбы = Шаг × Заходы (мм × 100, для Ph > 1)
  Display_Send_Int("THREAD_TRAVEL", (int32_t)(Thread_Info[Thread_Step].Step * 100.0f * Ph));

  // Циклов (как на старом LCD — из таблицы, не из Pass_Total)
  Display_Send_Int("THREAD_CYCL", _Display_GetThreadCycles());
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ОТПРАВИТЬ ПОЛНОЕ СОСТОЯНИЕ ***** ////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Display_Send_All()
{
  e2_alert_sent = -1;                 // [STM32] e2_service() перешлёт текущее сообщение LCD2004
  // 1. Режим (MODE_FEED=1 .. MODE_RESERVE=8)
  Display_Send_Int("MODE", (int)Mode);

  // 2. Подрежим (1=Внутренний, 2=Ручной, 3=Наружный)
  Display_Send_Int("SUBMODE", _Display_GetSubMode());

  // 3. Подача синхронная (мм/об × 100)
  Display_Send_Int("FEED", Feed_mm);

  // 4. Подача асинхронная (raw integer — как на старом дисплее)
  Display_Send_Int("AFEED", aFeed_mm);

  // 5. Данные резьбы: имя, шаг, RPM, ход, циклы
  _Display_SendThread();

  // 6. Позиция Z: ИНВЕРСИЯ ЗНАКА (Print.ino: если Size_Z_mm>0 → "Z-XX.XX")
  Display_Send_Int("POS_Z", -(int32_t)Size_Z_mm * 10L);

  // 7. Позиция X: ИНВЕРСИЯ ЗНАКА (аналогично)
  Display_Send_Int("POS_X", -(int32_t)Size_X_mm * 10L);

  // 8. Шар: радиус (0.01мм)
  Display_Send_Int("SPHERE", Sph_R_mm);

  // 9. Проходы (для M3 новый дисплей использует THREAD_CYCL, для остальных — PASS)
  Display_Send_Int2("PASS", Pass_Nr, Pass_Total);

  // 10. Съём за проход (0.01мм)
  Display_Send_Int("AP", Ap);

  // 11. Количество заходов (Ph: 1..8)
  Display_Send_Int("PH", Ph);

  // 12. Угол шпинделя: Arduino хранит в 0.001° (миллиградусы).
  //     ESP32 ожидает 0.1° (децыградусы). Делим на 100.
  //     Пример: Spindle_Angle=600 → 0.6° → шлём 6 → ESP32 показывает 0.6°
  Display_Send_Int("ANGLE", Spindle_Angle / 100);

  // 13. Индекс конуса + угол (градусы × 10)
  Display_Send_Int("CONE", Cone_Step);
  {
    int cs = constrain((int)Cone_Step, 0, (int)(sizeof(s_cone_angle_x10)/sizeof(s_cone_angle_x10[0])) - 1);
    Display_Send_Int("CONE_ANGLE", s_cone_angle_x10[cs]);
  }

  // 14. Делитель: кол-во делений и текущая метка
  Display_Send_Int("DIVN", Total_Tooth);
  Display_Send_Int("DIVM", Current_Tooth);

  // 15. Шар: радиус ножки (0.01мм) и кол-во заходов сферы
  Display_Send_Int("BAR", Bar_R_mm);
  Display_Send_Int("PASS_SPHR", Pass_Total_Sphr);

  // 16. Активный подэкран (SelectMenu: 1=основной, 2=параметры, 3=доп.)
  Display_Send_Int("SELECTMENU", SelectMenu);

  // 17. Отскок/натяг Z (для M1 SM=3)
  Display_Send_Int("OTSKOK_Z", (int32_t)OTSKOK_Z_mm * 10L);
  Display_Send_Int("TENSION_Z", (int32_t)TENSION_Z_mm * 10L);

  // 18. Диаметр заготовки (MSize_X_mm, для M1 SM=2)
  Display_Send_Int("DIAM_X", -(int32_t)MSize_X_mm * 10L);

  // [STM32] Механика станка (настройки, BACKLOG-028)
  e2_mech_send();
  e2_pos_send();

  // [STM32] Можно ли задавать оси с экрана (нет — при блоке DRO, там оси задаются на блоке)
  Display_Send_Int("AXIS_EDIT", e2_axis_editable() ? 1 : 0);

  // 19. Чистовых проходов (для M3 SM=2)
  Display_Send_Int("PASS_FIN", PASS_FINISH + Pass_Fin);

  // 20. Коническая резьба флаг (для M4/M5 SM=2)
  Display_Send_Int("CONE_THR", (ConL_Thr_flag || ConR_Thr_flag) ? 1 : 0);

  // 22. Ширина резца и шаг оси Z (для M6 SM=2)
  Display_Send_Int("CUTTER_W", Cutter_Width);
  Display_Send_Int("CUTTING_W", Cutting_Width);

  // 21. Состояние лимитов (L,R,F,B)
  {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d",
      limit_Left_flag  ? 1 : 0,
      limit_Right_flag ? 1 : 0,
      limit_Front_flag ? 1 : 0,
      limit_Rear_flag  ? 1 : 0);
    Display_Send_CMD("LIMITS", buf);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// [STM32] ***** ОТПРАВКА ПО ИЗМЕНЕНИЮ ***** /////////////////////////////////////////////////////////////////////////////////////////
// UartDisplay автора шлёт многие значения только в Display_Send_All (по READY и части кнопок), поэтому после
// кнопок пульта и части касаний новый дисплей показывал устаревшее (натяг, отскок, заходы шара, угол делилки…).
// Здесь — всё, что есть в Display_Send_All, кроме позиций (у них свой хук): при изменении шлём только его.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Display_Watch()
{
  struct W { int32_t last; };
  static W w[24];
  static bool s_init = false;
  int i = 0;
  auto chg = [&](int32_t v) {
    bool c = !s_init || w[i].last != v;
    w[i++].last = v;
    return c;
  };
  if (chg(Mode))                        Display_Send_Int("MODE", (int)Mode);
  if (chg(_Display_GetSubMode()))       Display_Send_Int("SUBMODE", _Display_GetSubMode());
  if (chg(SelectMenu))                  Display_Send_Int("SELECTMENU", SelectMenu);
  if (chg(Feed_mm))                     Display_Send_Int("FEED", Feed_mm);
  if (chg(aFeed_mm))                    Display_Send_Int("AFEED", aFeed_mm);
  if (chg(((int32_t)Thread_Step << 16) ^ (Ph << 8) ^ (PASS_FINISH + Pass_Fin) ^ ((int32_t)Mode << 24)))
                                        _Display_SendThread();
  if (chg(Sph_R_mm))                    Display_Send_Int("SPHERE", Sph_R_mm);
  if (chg(((int32_t)Pass_Nr << 16) ^ Pass_Total)) Display_Send_Int2("PASS", Pass_Nr, Pass_Total);
  if (chg(Ap))                          Display_Send_Int("AP", Ap);
  if (chg(Ph))                          Display_Send_Int("PH", Ph);
  if (chg(Cone_Step)) {
    Display_Send_Int("CONE", Cone_Step);
    int cs = constrain((int)Cone_Step, 0, (int)(sizeof(s_cone_angle_x10)/sizeof(s_cone_angle_x10[0])) - 1);
    Display_Send_Int("CONE_ANGLE", s_cone_angle_x10[cs]);
  }
  if (chg(Total_Tooth))                 Display_Send_Int("DIVN", Total_Tooth);
  if (chg(Current_Tooth))               Display_Send_Int("DIVM", Current_Tooth);
  if (chg(Bar_R_mm))                    Display_Send_Int("BAR", Bar_R_mm);
  if (chg(Pass_Total_Sphr))             Display_Send_Int("PASS_SPHR", Pass_Total_Sphr);
  if (chg(OTSKOK_Z_mm))                 Display_Send_Int("OTSKOK_Z", (int32_t)OTSKOK_Z_mm * 10L);
  if (chg(TENSION_Z_mm))                Display_Send_Int("TENSION_Z", (int32_t)TENSION_Z_mm * 10L);
  if (chg(PASS_FINISH + Pass_Fin))      Display_Send_Int("PASS_FIN", PASS_FINISH + Pass_Fin);
  if (chg((ConL_Thr_flag || ConR_Thr_flag) ? 1 : 0)) Display_Send_Int("CONE_THR", (ConL_Thr_flag || ConR_Thr_flag) ? 1 : 0);
  if (chg(Cutter_Width))                Display_Send_Int("CUTTER_W", Cutter_Width);
  if (chg(Cutting_Width))               Display_Send_Int("CUTTING_W", Cutting_Width);
  if (chg((limit_Left_flag ? 8 : 0) | (limit_Right_flag ? 4 : 0) | (limit_Front_flag ? 2 : 0) | (limit_Rear_flag ? 1 : 0))) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d", limit_Left_flag ? 1 : 0, limit_Right_flag ? 1 : 0,
             limit_Front_flag ? 1 : 0, limit_Rear_flag ? 1 : 0);
    Display_Send_CMD("LIMITS", buf);
  }
  // Угол шпинделя (делилка) — меняется непрерывно при вращении: не чаще 10 раз в секунду
  static uint32_t s_ang_t = 0;
  int32_t ang = Spindle_Angle / 100;
  if (millis() - s_ang_t >= 100) {
    if (chg(ang)) { Display_Send_Int("ANGLE", ang); s_ang_t = millis(); }
  } else {
    i++;
  }
  s_init = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ОТПРАВИТЬ АЛЕРТ ***** ///////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Display_Send_Alert(int type)
{
  Display_Send_Int("ALERT", type);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ХУКИ ДЛЯ СМЕНЫ РЕЖИМА ***** ////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Display_On_Mode_Change()
{
  Display_Send_Int("MODE", (int)Mode);
  Display_Send_Int("SUBMODE", _Display_GetSubMode());
  Display_Send_Int("SELECTMENU", SelectMenu);
}

void Display_On_SubMode_Change()
{
  Display_Send_Int("SUBMODE", _Display_GetSubMode());
}

void Display_On_Feed_Change()
{
  Display_Send_Int("FEED", Feed_mm);
  Display_Send_Int("AFEED", aFeed_mm);
}

void Display_On_Thread_Change()
{
  _Display_SendThread();
}

void Display_On_Position_Change()
{
  // ИНВЕРСИЯ ЗНАКА — соответствует логике Print.ino
  Display_Send_Int("POS_Z", -(int32_t)Size_Z_mm * 10L);
  Display_Send_Int("POS_X", -(int32_t)Size_X_mm * 10L);
  // [STM32] диаметр меняется вместе с X (LCD2004 показывает Ø постоянно), у автора слался только в Send_All
  Display_Send_Int("DIAM_X", -(int32_t)MSize_X_mm * 10L);
}

void Display_On_Pass_Change()
{
  Display_Send_Int2("PASS", Pass_Nr, Pass_Total);
  if (Mode == Mode_Thread)
    Display_Send_Int("THREAD_CYCL", _Display_GetThreadCycles());
  if (Mode == Mode_Sphere)
    Display_Send_Int("PASS_SPHR", Pass_Total_Sphr);
}

void Display_On_Alert(int type)
{
  Display_Send_Alert(type);
}

void Display_On_SelectMenu_Change()
{
  Display_Send_Int("SELECTMENU", SelectMenu);
}

void Display_On_Limits_Change()
{
  char buf[12];
  snprintf(buf, sizeof(buf), "%d,%d,%d,%d",
    limit_Left_flag  ? 1 : 0,
    limit_Right_flag ? 1 : 0,
    limit_Front_flag ? 1 : 0,
    limit_Rear_flag  ? 1 : 0);
  Display_Send_CMD("LIMITS", buf);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ПРИЁМ КОМАНД ОТ ДИСПЛЕЯ ***** ///////////////////////////////////////////////////////////////////////////////////////////////
// Дисплей может слать <TOUCH:KEY:UP>, <TOUCH:KEY:DN>, <TOUCH:PARAM_OK>,
// <TOUCH:M1>..<TOUCH:M8>, <TOUCH:S1>..<TOUCH:S3>, <TOUCH:THR_CAT>,
// <READY>, <PONG>
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static String s_disp_rx_buf = "";

void Display_UART_Process()
{
  while (DISPLAY_UART.available()) {
    char c = (char)DISPLAY_UART.read();
    if (c == '\n' || c == '\r') {
      if (s_disp_rx_buf.length() > 0) {
        Serial.print(F("DISP RX: ")); Serial.println(s_disp_rx_buf);
        _Display_ParseRx(s_disp_rx_buf);
        s_disp_rx_buf = "";
      }
    } else if (c == '<') {
      // [STM32] '<' всегда начинает новую команду: ESP32-P4 при загрузке печатает лог ROM
      // в этот же UART (GPIO37/38 = UART0) на другой скорости, и без сброса мусор
      // приклеивался к <READY> — STM32 не отвечал полным состоянием
      s_disp_rx_buf = "<";
    } else if (s_disp_rx_buf.length() < 128) {
      s_disp_rx_buf += c;
    }
  }
}

void _Display_ParseRx(String& raw)   // [STM32] без изменений, кроме DISP/GCODE
{
  if (raw.charAt(0) != '<') return;
  int end = raw.indexOf('>');
  if (end < 0) return;
  String inner = raw.substring(1, end);
  int colon = inner.indexOf(':');
  String cmd = (colon >= 0) ? inner.substring(0, colon) : inner;
  String prm = (colon >= 0) ? inner.substring(colon + 1) : "";

  // [STM32] DISP:<код> — дисплей сообщает свой тип; сверка с ELS_DISPLAY_TYPE из els_config.h
  if (cmd == "DISP") {
    long code = prm.toInt();
    if (code != ELS_DISPLAY_TYPE) {
      Serial.print(F("ВНИМАНИЕ: подключён дисплей типа ")); Serial.print(code);
      Serial.print(F(", а в els_config.h ELS_DISPLAY_TYPE = ")); Serial.println((int)ELS_DISPLAY_TYPE);
#ifndef ELS_RELEASE   // готовая прошивка из релиза работает с любым дисплеем — только строка в порт
      Display_Send_CMD("ALERT", "ДИСПЛЕЙ НЕ СОВПАДАЕТ С КОНФИГОМ STM32");
#endif
    }
    return;
  }
  // [STM32] GCODE:<строка> — G-код с дисплея (файлы — веб-страница дисплея), исполнитель — e2_gcode.inc
  if (cmd == "GCODE") {
    e2_gcode_line(prm.c_str());
    return;
  }
  if (cmd == "READY") {
    Serial.println(F("Дисплей: READY — отправляем состояние"));
    Display_Send_All();
  }
  else if (cmd == "PONG") {
    Serial.println(F("Дисплей: PONG → отправляем состояние"));
    Display_Send_All();
  }
  else if (cmd == "TOUCH") {
    if (prm == "KEY:UP" || prm == "KEY:DN") {
      // Сброс флагов перед вызовом (как SerialEmulation.ino)
      if (prm == "KEY:UP") { button_up_flag = false; Key_Up_Pressed(); }
      else                  { button_down_flag = false; Key_Down_Pressed(); }
      // Полное обновление — покрывает SM1 и SM2/SM3 (Ph, Total_Tooth, CThr, Cutter_Step и др.)
      Display_Send_All();
    }
    else if (prm == "KEY:LEFT")  { button_left_flag  = false; Key_Left_Pressed();  Display_Send_All(); }
    else if (prm == "KEY:RIGHT") { button_right_flag = false; Key_Right_Pressed(); Display_Send_All(); }
    else if (prm == "PARAM_OK")  { key_sel_flag = false; Key_Select_Pressed(); }
    // [STM32] Режим/подрежим с экрана — как с галетника: пока ось едет от джойстика, не переключаем
    // (у автора в Menu() переключатели читаются только при !Joy_Z_flag && !Joy_X_flag). Дисплей уже
    // показал новый режим заранее — возвращаем ему текущий. Без алерта: его OK шлёт ALERT_OK → Switch_Man().
    else if (((prm.length() == 2 && (prm[0] == 'M' || prm[0] == 'S') && isDigit(prm[1]))) && (Joy_Z_flag || Joy_X_flag)) {
      Serial.print(F("Дисплей: ")); Serial.print(prm); Serial.println(F(" — отказ, ось в движении"));
      Beep_Error();
      Display_Send_Int("MODE", (int)Mode);
      Display_Send_Int("SUBMODE", _Display_GetSubMode());
    }
    else if (prm == "M1") { Switch_Feed();    Beep(); }
    else if (prm == "M2") { Switch_aFeed();   Beep(); }
    else if (prm == "M3") { Switch_Thread();  Beep(); }
    else if (prm == "M4") { Switch_Cone_L();  Beep(); }
    else if (prm == "M5") { Switch_Cone_R();  Beep(); }
    else if (prm == "M6") { Switch_Sphere();  Beep(); }
    else if (prm == "M7") { Switch_Devider(); Beep(); }
    else if (prm == "M8") { Switch_Reserve(); Beep(); }
    else if (prm == "S1") { Switch_Int(); Beep(); }
    else if (prm == "S2") { Switch_Man(); Beep(); }
    else if (prm == "S3") { Switch_Ext(); Beep(); }
    else if (prm == "ALERT_OK") {
      Serial.println(F("Дисплей: ALERT_OK — вызываем Switch_Man()"));
      Switch_Man();
      Beep();
    }
    // Переключение категории резьбы: Метрическая → Дюймовая → G-трубная → K-трубная → Метрическая
    else if (prm == "THR_CAT") {
      if (Thread_Step < THR_IDX_INCH_START)
        Thread_Step = THR_IDX_INCH_START;
      else if (Thread_Step < THR_IDX_GPIPE_START)
        Thread_Step = THR_IDX_GPIPE_START;
      else if (Thread_Step < THR_IDX_KPIPE_START)
        Thread_Step = THR_IDX_KPIPE_START;
      else
        Thread_Step = 0;
      Print();
      _Display_SendThread();
      Beep();
      Serial.print(F("THR_CAT → Thread_Step=")); Serial.println(Thread_Step);
    }
    // SUBSEL: параметры, изменённые через тачскрин — ВСЕГДА вызываем Print() для обновления старого LCD
    else if (prm.startsWith("AP:"))   {
      Ap = prm.substring(3).toInt();
      Print();
      Display_Send_Int("AP", Ap);        // подтвердить новое значение AP новому дисплею
      Display_On_Feed_Change();
    }
    // FEED:N — прямая установка синхронной подачи от тачскрина
    else if (prm.startsWith("FEED:")) {
      int new_feed = prm.substring(5).toInt();
      if (new_feed >= 1) {
        Feed_mm = new_feed;
        Print();
        Display_On_Feed_Change();
        Serial.print(F("FEED: → Feed_mm=")); Serial.println(Feed_mm);
      }
    }
    // AFEED:N — прямая установка асинхронной подачи от тачскрина
    else if (prm.startsWith("AFEED:")) {
      int new_afeed = prm.substring(6).toInt();
      if (new_afeed >= 1) {
        aFeed_mm = new_afeed;
        Print();
        Display_On_Feed_Change();
        Serial.print(F("AFEED: → aFeed_mm=")); Serial.println(aFeed_mm);
      }
    }
    // SPHERE:N — прямая установка радиуса шара (Sph_R_mm в 0.01мм)
    else if (prm.startsWith("SPHERE:")) {
      int new_sphere_r = prm.substring(7).toInt();
      if (new_sphere_r >= 50) {
        Sph_R_mm = new_sphere_r;
        Sphere();
        Print();
        Display_Send_Int("SPHERE", Sph_R_mm);
        Display_Send_Int("PASS_SPHR", Pass_Total_Sphr);
        Beep();
        Serial.print(F("SPHERE: → Sph_R_mm=")); Serial.println(Sph_R_mm);
      }
    }
    else if (prm.startsWith("PH:"))   { Ph = prm.substring(3).toInt(); }
    else if (prm.startsWith("CONE:")) {
      Cone_Step = prm.substring(5).toInt();
      Print();
      // Отправить обновлённый конус обратно на дисплей
      Display_Send_Int("CONE", Cone_Step);
      int cs = constrain((int)Cone_Step, 0, (int)(sizeof(s_cone_angle_x10)/sizeof(s_cone_angle_x10[0])) - 1);
      Display_Send_Int("CONE_ANGLE", s_cone_angle_x10[cs]);
    }
    else if (prm.startsWith("DIVN:")) {
      Total_Tooth = prm.substring(5).toInt();
      if (Current_Tooth > Total_Tooth) Current_Tooth = Total_Tooth;
      Print();
      Display_Send_Int("DIVN", Total_Tooth);
    }
    else if (prm.startsWith("DIVM:")) {
      Current_Tooth = prm.substring(5).toInt();
      Print();
      Display_Send_Int("DIVM", Current_Tooth);
    }
    else if (prm.startsWith("BAR:")) {
      Bar_R_mm = prm.substring(4).toInt();
      Sphere();
      Print();
      Display_Send_Int("BAR", Bar_R_mm);
    }
    // THREAD_STEP:N — прямая установка шага резьбы (без навигации через Key_Up/Dn)
    else if (prm.startsWith("THREAD_STEP:")) {
      int new_step = prm.substring(12).toInt();
      if (new_step >= 0 && new_step < (int)TOTAL_THREADS) {
        Thread_Step = (byte)new_step;
        Thr_Pass_Summ = 0;
        Pass_Fin = 0;
        Print();
        _Display_SendThread();
        Beep();
      }
    }
    // [STM32] CFG_SET:<9 чисел> / CFG_DEFAULT — механика станка с экрана (BACKLOG-028)
    else if (prm.startsWith("CFG_SET:") || prm == "CFG_DEFAULT") {
      e2_mech_from_display(prm.c_str());
      Display_Send_All();
    }
    // [STM32] POS_SET:<9 чисел> / POS_DEFAULT — источник координат и линейки (BACKLOG-028)
    else if (prm.startsWith("POS_SET:") || prm == "POS_DEFAULT") {
      e2_pos_from_display(prm.c_str());
    }
    // [STM32] CAL:<Z|X>:<мкм хода> — калибровка шага винта и люфта по линейкам (BACKLOG-029)
    else if (prm.startsWith("CAL_STEP:")) {
      e2_cal_step_from_display(prm.c_str());
    }
    else if (prm.startsWith("CAL:")) {
      e2_cal_from_display(prm.c_str());
    }
    // [STM32] AXIS:<Z|X|D>:<мкм> — ввод/сброс оси с экрана (двойной тап — 0, удержание — клавиатура)
    else if (prm.startsWith("AXIS:") && prm.length() >= 8 && prm.charAt(6) == ':') {
      char ax = prm.charAt(5);
      if (!e2_axis_editable()) {
        Display_Send_CMD("ALERT", "ОСИ ЗАДАЮТСЯ НА БЛОКЕ DRO");
      } else if (ax == 'Z' || ax == 'X' || ax == 'D') {
        e2_axis_set(ax, prm.substring(7).toInt());
        Beep();
      }
    }
    // JOY:* — управление осями через джойстик-оверлей на тачскрине (US-013)
    // JOY:LEFT/RIGHT = ось Z, JOY:UP/DOWN = ось X, JOY:STOP = стоп, JOY:RAPID_ON/OFF = быстро
    else if (prm == "JOY:LEFT")      { Joy_LeftPressed();  }
    else if (prm == "JOY:RIGHT")     { Joy_RightPressed(); }
    else if (prm == "JOY:UP")        { Joy_UpPressed();    }
    else if (prm == "JOY:DOWN")      { Joy_DownPressed();  }
    else if (prm == "JOY:STOP")      { Joy_NoPressed();    }
    else if (prm == "JOY:RAPID_ON")  {
      g_emul_rapid = true;
      Serial.println(F("JOY:RAPID_ON — rapid on"));
    }
    else if (prm == "JOY:RAPID_OFF") {
      g_emul_rapid = false;
      Serial.println(F("JOY:RAPID_OFF — rapid off"));
    }
    // EMUL:* — периферийная эмуляция (US-014): симулирует физические входы без железа
    else if (prm == "EMUL:STOP_L") {
      limit_button_flag = false;
      Limit_Left_Pressed();
      Serial.print(F("EMUL:STOP_L — limit_Left_flag=")); Serial.println(limit_Left_flag);
    }
    else if (prm == "EMUL:STOP_R") {
      limit_button_flag = false;
      Limit_Right_Pressed();
      Serial.print(F("EMUL:STOP_R — limit_Right_flag=")); Serial.println(limit_Right_flag);
    }
    else if (prm == "EMUL:STOP_F") {
      limit_button_flag = false;
      Limit_Front_Pressed();
      Serial.print(F("EMUL:STOP_F — limit_Front_flag=")); Serial.println(limit_Front_flag);
    }
    else if (prm == "EMUL:STOP_B") {
      limit_button_flag = false;
      Limit_Rear_Pressed();
      Serial.print(F("EMUL:STOP_B — limit_Rear_flag=")); Serial.println(limit_Rear_flag);
    }
    else if (prm == "EMUL:LIM_L") {
      // Прямое переключение limit_Left_flag без проверки подрежима (для тестов)
      if (limit_Left_flag == OFF) {
        limit_Left_flag = ON;
        Limit_Pos_Left = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
        Limit_Left_LED_On();
      } else {
        limit_Left_flag = OFF;
        Limit_Pos_Left = Limit_Pos_Max;
        Limit_Left_LED_Off();
      }
      Display_On_Limits_Change();
      Serial.print(F("EMUL:LIM_L — limit_Left_flag=")); Serial.println(limit_Left_flag);
    }
    else if (prm == "EMUL:LIM_R") {
      // Прямое переключение limit_Right_flag без проверки подрежима
      if (limit_Right_flag == OFF) {
        limit_Right_flag = ON;
        Limit_Pos_Right = ((Motor_Z_Pos + McSTEP_Z / 2) & ~(McSTEP_Z - 1));
        Limit_Right_LED_On();
      } else {
        limit_Right_flag = OFF;
        Limit_Pos_Right = Limit_Pos_Min;
        Limit_Right_LED_Off();
      }
      Display_On_Limits_Change();
      Serial.print(F("EMUL:LIM_R — limit_Right_flag=")); Serial.println(limit_Right_flag);
    }
    else if (prm == "EMUL:JOY_L") { Joy_LeftPressed();  Serial.println(F("EMUL:JOY_L")); }
    else if (prm == "EMUL:JOY_R") { Joy_RightPressed(); Serial.println(F("EMUL:JOY_R")); }
    else if (prm == "EMUL:JOY_U") { Joy_UpPressed();    Serial.println(F("EMUL:JOY_U")); }
    else if (prm == "EMUL:JOY_D") { Joy_DownPressed();  Serial.println(F("EMUL:JOY_D")); }
    else if (prm == "EMUL:BTN_RAPID") {
      g_emul_rapid = !g_emul_rapid;
      Serial.print(F("EMUL:BTN_RAPID — rapid=")); Serial.println(g_emul_rapid ? F("ON") : F("OFF"));
    }
    else if (prm == "EMUL:BTN_OK") {
      key_sel_flag = false;
      Key_Select_Pressed();
      Serial.println(F("EMUL:BTN_OK"));
    }
    else if (prm == "EMUL:ENC_CW") {
      Hand_Count++;
      Serial.print(F("EMUL:ENC_CW — Hand_Count=")); Serial.println(Hand_Count);
    }
    else if (prm == "EMUL:ENC_CCW") {
      Hand_Count--;
      Serial.print(F("EMUL:ENC_CCW — Hand_Count=")); Serial.println(Hand_Count);
    }
  }
}
