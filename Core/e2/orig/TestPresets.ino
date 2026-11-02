//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ***** ТЕСТОВЫЕ ПРЕСЕТЫ Arduino → Новый Дисплей ***** ///////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Команда в Serial Monitor: PT:<N>  (N = 1..55)
//
// Устанавливает внутренние переменные Arduino в предустановленные значения
// и отправляет полное состояние на новый дисплей через UART.
//
// ТЕСТ: Arduino → Новый Дисплей (прямая интеграция)
//   1. Введи PT:1 в Serial Monitor Arduino
//   2. Новый дисплей должен показать значения из таблицы TEST_SCENARIOS.md
//
// ТЕСТ: Новый Дисплей → Arduino (обратная интеграция)
//   1. Введи PT:N для установки режима
//   2. На новом дисплее нажми кнопку режима/подрежима (тачскрин)
//   3. Старый дисплей должен обновить значения
//
// Соответствие: PT:N ↔ ESP32 тест-сценарий #N (5×DN на дисплее)
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// --------------------------------------------------------------------------
// Вспомогательные функции установки подрежима (без проверки лимитов!)
// --------------------------------------------------------------------------
static void _pt_sub_int() {
  Sub_Mode_Feed    = Sub_Mode_Feed_Int;
  Sub_Mode_aFeed   = Sub_Mode_aFeed_Int;
  Sub_Mode_Thread  = Sub_Mode_Thread_Int;
  Sub_Mode_Cone    = Sub_Mode_Cone_Int;
  Sub_Mode_Sphere  = Sub_Mode_Sphere_Int;
}
static void _pt_sub_man() {
  Sub_Mode_Feed    = Sub_Mode_Feed_Man;
  Sub_Mode_aFeed   = Sub_Mode_aFeed_Man;
  Sub_Mode_Thread  = Sub_Mode_Thread_Man;
  Sub_Mode_Cone    = Sub_Mode_Cone_Man;
  Sub_Mode_Sphere  = Sub_Mode_Sphere_Man;
}
static void _pt_sub_ext() {
  Sub_Mode_Feed    = Sub_Mode_Feed_Ext;
  Sub_Mode_aFeed   = Sub_Mode_aFeed_Ext;
  Sub_Mode_Thread  = Sub_Mode_Thread_Ext;
  Sub_Mode_Cone    = Sub_Mode_Cone_Ext;
  Sub_Mode_Sphere  = Sub_Mode_Sphere_Ext;
}

// --------------------------------------------------------------------------
// Thread_Step индексы (Thread_Info[N].Step):
//   9=0.75mm  11=1.00mm  13=1.50mm  15=2.00mm  16=2.50mm  17=3.00mm
//   22=3.175mm(8tpi)  37=0.907mm(28tpi)  48=0.907mm(G1/8)
// --------------------------------------------------------------------------

void Set_Test_Preset(int n)
{
  // -- Сброс всех переменных к нейтральным значениям --
  Feed_mm      = 20;   aFeed_mm = 20;
  Thread_Step  = 11;   // 1.00mm по умолчанию
  Size_Z_mm    = 0;    Size_X_mm = 0;
  Pass_Nr      = 0;    Pass_Total = 0;
  Ap           = 0;    Ph = 1;   Cone_Step = 0;
  Sph_R_mm     = 1000; Bar_R_mm = 0;  Pass_Total_Sphr = 0;
  Total_Tooth  = 6;    Current_Tooth = 1;   Spindle_Angle = 0;
  // Сброс флагов ошибок — алертов при тесте не должно быть
  err_0_flag = false;  err_1_flag = false;  err_2_flag = false;

  // Для тестирования: все 4 лимита ON (без реального железа, Motor_Z/X_Pos=0)
  limit_Left_flag = ON;   Limit_Pos_Left  = 0;   Limit_Left_LED_On();
  limit_Right_flag = ON;  Limit_Pos_Right = 0;   Limit_Right_LED_On();
  limit_Front_flag = ON;  Limit_Pos_Front = 0;   Limit_Front_LED_On();
  limit_Rear_flag = ON;   Limit_Pos_Rear  = 0;   Limit_Rear_LED_On();

  switch (n)
  {
    // ── M1 ПОДАЧА синхронная ──────────────────────────────────────────────
    // PT:1  M1/S1 Внутр Подача=0.10 Z=+56.78 X=-8.50 Пр=1/6 Съём=0.10
    case 1:
      Mode=Mode_Feed; _pt_sub_int();
      Feed_mm=10; Size_Z_mm=5678; Size_X_mm=-850; Pass_Nr=1; Pass_Total=6; Ap=10;
      break;

    // PT:2  M1/S2 Ручной Подача=0.20 Z=+87.50 X=-12.30 Съём=0.20
    case 2:
      Mode=Mode_Feed; _pt_sub_man();
      Feed_mm=20; Size_Z_mm=8750; Size_X_mm=-1230; Ap=20;
      break;

    // PT:3  M1/S3 Нар Подача=0.30 Z=+34.12 X=-5.20 Пр=3/8 Съём=0.15
    case 3:
      Mode=Mode_Feed; _pt_sub_ext();
      Feed_mm=30; Size_Z_mm=3412; Size_X_mm=-520; Pass_Nr=3; Pass_Total=8; Ap=15;
      break;

    // PT:4  M1/S1 Внутр Подача=0.50 Z=+112.34 X=-21.00 Пр=5/10 Съём=0.25
    case 4:
      Mode=Mode_Feed; _pt_sub_int();
      Feed_mm=50; Size_Z_mm=11234; Size_X_mm=-2100; Pass_Nr=5; Pass_Total=10; Ap=25;
      break;

    // PT:5  M1/S3 Нар Подача=0.15 Z=-47.80 X=+3.60 Пр=8/10 Съём=0.15
    case 5:
      Mode=Mode_Feed; _pt_sub_ext();
      Feed_mm=15; Size_Z_mm=-4780; Size_X_mm=360; Pass_Nr=8; Pass_Total=10; Ap=15;
      break;

    // ── M2 ПОДАЧА асинхронная ─────────────────────────────────────────────
    // PT:6  M2/S1 Внутр аПодача=0.10 Z=+23.40 X=-7.20 Пр=2/8 Съём=0.10
    case 6:
      Mode=Mode_aFeed; _pt_sub_int();
      aFeed_mm=10; Size_Z_mm=2340; Size_X_mm=-720; Pass_Nr=2; Pass_Total=8; Ap=10;
      break;

    // PT:7  M2/S2 Ручной аПодача=0.30 Z=+63.20 X=-9.40 Съём=0.20
    case 7:
      Mode=Mode_aFeed; _pt_sub_man();
      aFeed_mm=30; Size_Z_mm=6320; Size_X_mm=-940; Ap=20;
      break;

    // PT:8  M2/S3 Нар аПодача=0.50 Z=+35.00 X=-8.00 Пр=7/8 Съём=0.25
    case 8:
      Mode=Mode_aFeed; _pt_sub_ext();
      aFeed_mm=50; Size_Z_mm=3500; Size_X_mm=-800; Pass_Nr=7; Pass_Total=8; Ap=25;
      break;

    // PT:9  M2/S1 Внутр аПодача=0.20 Z=+18.90 X=-3.50 Пр=4/12 Съём=0.15
    case 9:
      Mode=Mode_aFeed; _pt_sub_int();
      aFeed_mm=20; Size_Z_mm=1890; Size_X_mm=-350; Pass_Nr=4; Pass_Total=12; Ap=15;
      break;

    // PT:10 M2/S3 Нар аПодача=0.40 Z=-22.50 X=+1.80 Пр=0/6 Съём=0.20
    case 10:
      Mode=Mode_aFeed; _pt_sub_ext();
      aFeed_mm=40; Size_Z_mm=-2250; Size_X_mm=180; Pass_Total=6; Ap=20;
      break;

    // ── M3 РЕЗЬБА ─────────────────────────────────────────────────────────
    // PT:11 M3/S1 Внутр Резьба=1.00мм Z=+45.00 X=-5.00 Пр=5/10 Съём=0.10
    case 11:
      Mode=Mode_Thread; _pt_sub_int();
      Thread_Step=11; Size_Z_mm=4500; Size_X_mm=-500; Pass_Nr=5; Pass_Total=10; Ap=10;
      break;

    // PT:12 M3/S2 Ручной Резьба=1.50мм Z=+32.00 X=-4.50 Ph=2 Съём=0.15
    case 12:
      Mode=Mode_Thread; _pt_sub_man();
      Thread_Step=13; Size_Z_mm=3200; Size_X_mm=-450; Ph=2; Ap=15;
      break;

    // PT:13 M3/S3 Нар Резьба=2.00мм Z=+78.50 X=-6.00 Пр=8/10 Съём=0.20
    case 13:
      Mode=Mode_Thread; _pt_sub_ext();
      Thread_Step=15; Size_Z_mm=7850; Size_X_mm=-600; Pass_Nr=8; Pass_Total=10; Ap=20;
      break;

    // PT:14 M3/S2 Ручной Резьба=3.175мм(8tpi) Z=+52.00 X=-8.00 Ph=3 Съём=0.15
    case 14:
      Mode=Mode_Thread; _pt_sub_man();
      Thread_Step=22; Size_Z_mm=5200; Size_X_mm=-800; Ph=3; Ap=15;
      break;

    // PT:15 M3/S1 Внутр Резьба=0.907мм(G1/8) Z=+28.00 X=-3.50 Пр=3/6 Съём=0.08
    case 15:
      Mode=Mode_Thread; _pt_sub_int();
      Thread_Step=48; Size_Z_mm=2800; Size_X_mm=-350; Pass_Nr=3; Pass_Total=6; Ap=8;
      break;

    // PT:16 M3/S3 Нар Резьба=2.50мм Z=+60.00 X=-7.00 Пр=2/8 Ph=4 Съём=0.20
    case 16:
      Mode=Mode_Thread; _pt_sub_ext();
      Thread_Step=16; Size_Z_mm=6000; Size_X_mm=-700; Pass_Nr=2; Pass_Total=8; Ph=4; Ap=20;
      break;

    // PT:17 M3/S2 Ручной Резьба=0.75мм(M10x0.75) Z=+15.50 X=-2.00 Съём=0.08
    case 17:
      Mode=Mode_Thread; _pt_sub_man();
      Thread_Step=9; Size_Z_mm=1550; Size_X_mm=-200; Ap=8;
      break;

    // ── M4 КОНУС влево ────────────────────────────────────────────────────
    // PT:18 M4/S1 Внутр 45° Подача=0.15 Z=+18.30 X=-4.20 Пр=2/4 Съём=0.15
    case 18:
      Mode=Mode_Cone_L; _pt_sub_int();
      Feed_mm=15; Size_Z_mm=1830; Size_X_mm=-420; Pass_Nr=2; Pass_Total=4; Ap=15; Cone_Step=0;
      break;

    // PT:19 M4/S2 Ручной 1° Подача=0.25 Z=+42.70 X=-9.80 Съём=0.25
    case 19:
      Mode=Mode_Cone_L; _pt_sub_man();
      Feed_mm=25; Size_Z_mm=4270; Size_X_mm=-980; Ap=25; Cone_Step=2;
      break;

    // PT:20 M4/S3 Нар 7° Подача=0.30 Z=+55.00 X=-6.50 Пр=1/3 Съём=0.30
    case 20:
      Mode=Mode_Cone_L; _pt_sub_ext();
      Feed_mm=30; Size_Z_mm=5500; Size_X_mm=-650; Pass_Nr=1; Pass_Total=3; Ap=30; Cone_Step=8;
      break;

    // PT:21 M4/S1 Внутр 3° Подача=0.20 Z=+33.80 X=-11.20 Пр=3/6 Съём=0.20
    case 21:
      Mode=Mode_Cone_L; _pt_sub_int();
      Feed_mm=20; Size_Z_mm=3380; Size_X_mm=-1120; Pass_Nr=3; Pass_Total=6; Ap=20; Cone_Step=4;
      break;

    // ── M5 КОНУС вправо ───────────────────────────────────────────────────
    // PT:22 M5/S1 Внутр 45° Подача=0.15 Z=-18.30 X=-4.20 Пр=2/4 Съём=0.15
    case 22:
      Mode=Mode_Cone_R; _pt_sub_int();
      Feed_mm=15; Size_Z_mm=-1830; Size_X_mm=-420; Pass_Nr=2; Pass_Total=4; Ap=15; Cone_Step=0;
      break;

    // PT:23 M5/S2 Ручной 2° Подача=0.20 Z=-42.70 X=-9.80 Съём=0.20
    case 23:
      Mode=Mode_Cone_R; _pt_sub_man();
      Feed_mm=20; Size_Z_mm=-4270; Size_X_mm=-980; Ap=20; Cone_Step=3;
      break;

    // PT:24 M5/S3 Нар 6° Подача=0.30 Z=-55.00 X=-6.50 Пр=1/3 Съём=0.30
    case 24:
      Mode=Mode_Cone_R; _pt_sub_ext();
      Feed_mm=30; Size_Z_mm=-5500; Size_X_mm=-650; Pass_Nr=1; Pass_Total=3; Ap=30; Cone_Step=7;
      break;

    // PT:25 M5/S3 Нар 4° Подача=0.25 Z=-68.20 X=-14.50 Пр=4/6 Съём=0.25
    case 25:
      Mode=Mode_Cone_R; _pt_sub_ext();
      Feed_mm=25; Size_Z_mm=-6820; Size_X_mm=-1450; Pass_Nr=4; Pass_Total=6; Ap=25; Cone_Step=5;
      break;

    // ── M6 ШАР ────────────────────────────────────────────────────────────
    // PT:26 M6/S2 Ручной Шар=ø40мм Z=+12.50 X=-8.00 Пр=3/8 Ножка=10.00мм Заходов=80/3
    case 26:
      Mode=Mode_Sphere; _pt_sub_man();
      Size_Z_mm=1250; Size_X_mm=-800; Pass_Nr=3; Pass_Total=8; Ap=15;
      Sph_R_mm=2000; Bar_R_mm=500; Pass_Total_Sphr=80;
      break;

    // PT:27 M6/S3 Нар Шар=ø100мм Z=+50.00 X=-25.00 Пр=0/15 Ножка=20.00мм Заходов=200/0
    case 27:
      Mode=Mode_Sphere; _pt_sub_ext();
      Size_Z_mm=5000; Size_X_mm=-2500; Pass_Total=15; Ap=25;
      Sph_R_mm=5000; Bar_R_mm=1000; Pass_Total_Sphr=200;
      break;

    // PT:28 M6/S1 Внутр Шар=ø60мм Z=+30.00 X=-15.00 Пр=7/12 Ножка=15.00мм Заходов=120/7
    case 28:
      Mode=Mode_Sphere; _pt_sub_int();
      Size_Z_mm=3000; Size_X_mm=-1500; Pass_Nr=7; Pass_Total=12; Ap=20;
      Sph_R_mm=3000; Bar_R_mm=750; Pass_Total_Sphr=120;
      break;

    // PT:29 M6/S2 Ручной Шар=ø25мм Z=+8.50 X=-6.20 Пр=5/10 Ножка=17.00мм Заходов=50/5
    case 29:
      Mode=Mode_Sphere; _pt_sub_man();
      Size_Z_mm=850; Size_X_mm=-620; Pass_Nr=5; Pass_Total=10; Ap=10;
      Sph_R_mm=1250; Bar_R_mm=850; Pass_Total_Sphr=50;
      break;

    // ── M7 ДЕЛИТЕЛЬ ───────────────────────────────────────────────────────
    // PT:30 M7 Делений=6 Метка=1 Угол=60.0°
    case 30:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=6; Current_Tooth=1; Spindle_Angle=600;
      break;

    // PT:31 M7 Делений=12 Метка=7 Угол=210.0°
    case 31:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=12; Current_Tooth=7; Spindle_Angle=2100;
      break;

    // PT:32 M7 Делений=36 Метка=25 Угол=250.0°
    case 32:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=36; Current_Tooth=25; Spindle_Angle=2500;
      break;

    // PT:33 M7 Делений=200 Метка=100 Угол=180.0°
    // (NOTE: Total_Tooth — byte, макс 255; оригинальный сценарий ESP32 имеет 360 — недостижимо из Arduino)
    case 33:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=200; Current_Tooth=100; Spindle_Angle=1800;
      break;

    // ── M8 РЕЗЕРВ ─────────────────────────────────────────────────────────
    // PT:34 M8 Резерв — начало Z=0 X=0
    case 34:
      Mode=Mode_Reserve; _pt_sub_man();
      break;

    // PT:35 M8 Резерв — позиция Z=+95.00 X=-3.00
    case 35:
      Mode=Mode_Reserve; _pt_sub_man();
      Size_Z_mm=9500; Size_X_mm=-300;
      break;

    // ── M4/M5 КОНУС — типы KM и соотношения ──────────────────────────────
    // PT:36 M4/S3 Нар KM0 Подача=0.15 Z=+12.00 X=-4.50 Пр=1/3 Съём=0.15
    case 36:
      Mode=Mode_Cone_L; _pt_sub_ext();
      Feed_mm=15; Size_Z_mm=1200; Size_X_mm=-450; Pass_Nr=1; Pass_Total=3; Ap=15; Cone_Step=45;
      break;

    // PT:37 M5/S1 Внутр KM3 Подача=0.20 Z=-35.00 X=-12.00 Пр=2/5 Съём=0.20
    case 37:
      Mode=Mode_Cone_R; _pt_sub_int();
      Feed_mm=20; Size_Z_mm=-3500; Size_X_mm=-1200; Pass_Nr=2; Pass_Total=5; Ap=20; Cone_Step=48;
      break;

    // PT:38 M4/S2 Ручной KM5 Подача=0.25 Z=+65.00 X=-25.00 Съём=0.25
    case 38:
      Mode=Mode_Cone_L; _pt_sub_man();
      Feed_mm=25; Size_Z_mm=6500; Size_X_mm=-2500; Ap=25; Cone_Step=50;
      break;

    // PT:39 M5/S3 Нар 1:20 Подача=0.10 Z=-40.00 X=-8.00 Пр=0/4 Съём=0.10
    case 39:
      Mode=Mode_Cone_R; _pt_sub_ext();
      Feed_mm=10; Size_Z_mm=-4000; Size_X_mm=-800; Pass_Total=4; Ap=10; Cone_Step=57;
      break;

    // PT:40 M4/S1 Внутр 3:25 Подача=0.15 Z=+28.00 X=-9.60 Пр=3/6 Съём=0.15
    case 40:
      Mode=Mode_Cone_L; _pt_sub_int();
      Feed_mm=15; Size_Z_mm=2800; Size_X_mm=-960; Pass_Nr=3; Pass_Total=6; Ap=15; Cone_Step=62;
      break;

    // ── M3 РЕЗЬБА трубная G и K ───────────────────────────────────────────
    // PT:41 M3/S3 Нар G 1/8 (Thread_Step=48) Z=+28.00 X=-3.50 Пр=7/9 Съём=0.08
    case 41:
      Mode=Mode_Thread; _pt_sub_ext();
      Thread_Step=48; Size_Z_mm=2800; Size_X_mm=-350; Pass_Nr=7; Pass_Total=9; Ap=8;
      break;

    // PT:42 M3/S1 Внутр G 1/2 (Thread_Step=51) Z=+50.00 X=-8.00 Пр=5/17 Съём=0.10
    case 42:
      Mode=Mode_Thread; _pt_sub_int();
      Thread_Step=51; Size_Z_mm=5000; Size_X_mm=-800; Pass_Nr=5; Pass_Total=17; Ap=10;
      break;

    // PT:43 M3/S2 Ручной K 1/8 (Thread_Step=58) Z=+20.00 X=-4.00 Съём=0.08
    case 43:
      Mode=Mode_Thread; _pt_sub_man();
      Thread_Step=58; Size_Z_mm=2000; Size_X_mm=-400; Ap=8;
      break;

    // ── M1/M2 граничные значения позиций ─────────────────────────────────
    // PT:44 M1/S1 Внутр большие позиции Подача=0.50 Z=+999.99 X=-99.99 Пр=1/20 Съём=0.25
    case 44:
      Mode=Mode_Feed; _pt_sub_int();
      Feed_mm=50; Size_Z_mm=99999; Size_X_mm=-9999; Pass_Nr=1; Pass_Total=20; Ap=25;
      break;

    // PT:45 M2/S2 Ручной aFeed=1.50 Z=+200.00 X=-50.00 Съём=0.25
    case 45:
      Mode=Mode_aFeed; _pt_sub_man();
      aFeed_mm=150; Size_Z_mm=20000; Size_X_mm=-5000; Ap=25;
      break;

    // ── M7 ДЕЛИТЕЛЬ граничные значения ───────────────────────────────────
    // PT:46 M7 Делений=4 Метка=2 Угол=90.0°
    case 46:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=4; Current_Tooth=2; Spindle_Angle=900;
      break;

    // PT:47 M7 Делений=100 Метка=37 Угол=129.6°
    case 47:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=100; Current_Tooth=37; Spindle_Angle=1296;
      break;

    // PT:48 M7 Делений=3 Метка=1 Угол=120.0°
    case 48:
      Mode=Mode_Divider; _pt_sub_man();
      Total_Tooth=3; Current_Tooth=1; Spindle_Angle=1200;
      break;

    // ── M6 ШАР граничные значения ─────────────────────────────────────────
    // PT:49 M6/S3 Нар Шар=ø200мм Z=+100.00 X=-40.00 Пр=0/20 Ножка=30.00мм Заходов=300/0
    case 49:
      Mode=Mode_Sphere; _pt_sub_ext();
      Size_Z_mm=10000; Size_X_mm=-4000; Pass_Total=20; Ap=25;
      Sph_R_mm=10000; Bar_R_mm=1500; Pass_Total_Sphr=300;
      break;

    // PT:50 M6/S1 Внутр Шар=ø10мм Z=+5.00 X=-3.00 Пр=2/5 Ножка=4.00мм Заходов=20/2
    case 50:
      Mode=Mode_Sphere; _pt_sub_int();
      Size_Z_mm=500; Size_X_mm=-300; Pass_Nr=2; Pass_Total=5; Ap=5;
      Sph_R_mm=500; Bar_R_mm=200; Pass_Total_Sphr=20;
      break;

    // ── M3 РЕЗЬБА граничные значения ─────────────────────────────────────
    // PT:51 M3/S1 Внутр Резьба=0.20мм(min) Z=+10.00 X=-2.00 Пр=1/5 Съём=0.05
    case 51:
      Mode=Mode_Thread; _pt_sub_int();
      Thread_Step=0; Size_Z_mm=1000; Size_X_mm=-200; Pass_Nr=1; Pass_Total=5; Ap=5;
      break;

    // PT:52 M3/S3 Нар Резьба=4.50мм(max) Z=+120.00 X=-10.00 Пр=0/6 Съём=0.25
    case 52:
      Mode=Mode_Thread; _pt_sub_ext();
      Thread_Step=19; Size_Z_mm=12000; Size_X_mm=-1000; Pass_Total=6; Ap=25;
      break;

    // PT:53 M3/S2 Ручной Резьба=80tpi Z=+40.00 X=-6.00 Съём=0.08
    case 53:
      Mode=Mode_Thread; _pt_sub_man();
      Thread_Step=46; Size_Z_mm=4000; Size_X_mm=-600; Ap=8;
      break;

    // PT:54 M1/S2 Ручной нулевые позиции Z=0 X=0 Подача=0.10 Съём=0.10
    case 54:
      Mode=Mode_Feed; _pt_sub_man();
      Feed_mm=10; Ap=10;
      break;

    // PT:55 M5/S3 Нар 3:25 Подача=0.10 Z=-50.00 X=-15.00 Пр=1/4 Съём=0.10
    case 55:
      Mode=Mode_Cone_R; _pt_sub_ext();
      Feed_mm=10; Size_Z_mm=-5000; Size_X_mm=-1500; Pass_Nr=1; Pass_Total=4; Ap=10; Cone_Step=62;
      break;

    default:
      Serial.print(F("PT: неверный номер (1..55), получено: "));
      Serial.println(n);
      return;
  }

  Print();
  Display_Send_All();
  Serial.print(F("[ PT:"));
  Serial.print(n);
  Serial.println(F(" ] Пресет применён → отправлено на новый дисплей"));
}
