#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Режимы работы ELS (0-based STM32, ESP32 получает +1 → 1-based)
// Порядок совпадает с Arduino: Mode_Feed=1, Mode_aFeed=2, ...
// ============================================================
typedef enum {
    MODE_FEED     = 0,  // Синхронная подача (мм/об)
    MODE_AFEED    = 1,  // Асинхронная подача (мм/мин)
    MODE_THREAD   = 2,  // Нарезка резьбы
    MODE_CONE_L   = 3,  // Конус левый <
    MODE_CONE_R   = 4,  // Конус правый >
    MODE_SPHERE   = 5,  // Сфера (шароточка)
    MODE_DIVIDER  = 6,  // Делитель
    MODE_RESERVE  = 7,  // Резерв
} ELS_Mode_t;

// Подрежимы (0-based, Arduino: Int=1 Man=2 Ext=3)
typedef enum {
    SUBMODE_INTERNAL = 0,  // Внутренний (Int)
    SUBMODE_MANUAL   = 1,  // Ручной (Man)
    SUBMODE_EXTERNAL = 2,  // Наружный (Ext)
} ELS_Submode_t;

// Синонимы для совместимости с портируемым кодом Arduino
#define Sub_Mode_Thread_Int  SUBMODE_INTERNAL
#define Sub_Mode_Thread_Man  SUBMODE_MANUAL
#define Sub_Mode_Thread_Ext  SUBMODE_EXTERNAL
#define Sub_Mode_Feed_Int    SUBMODE_INTERNAL
#define Sub_Mode_Feed_Man    SUBMODE_MANUAL
#define Sub_Mode_Feed_Ext    SUBMODE_EXTERNAL
#define Sub_Mode_aFeed_Int   SUBMODE_INTERNAL
#define Sub_Mode_aFeed_Man   SUBMODE_MANUAL
#define Sub_Mode_aFeed_Ext   SUBMODE_EXTERNAL
#define Sub_Mode_Cone_Int    SUBMODE_INTERNAL
#define Sub_Mode_Cone_Man    SUBMODE_MANUAL
#define Sub_Mode_Cone_Ext    SUBMODE_EXTERNAL
#define Sub_Mode_Sphere_Int  SUBMODE_INTERNAL
#define Sub_Mode_Sphere_Man  SUBMODE_MANUAL
#define Sub_Mode_Sphere_Ext  SUBMODE_EXTERNAL

// ============================================================
// Глобальное состояние станка ELS (полный порт Arduino переменных)
// ============================================================
typedef struct {

    // ── Режим и подрежимы ───────────────────────────────────
    ELS_Mode_t    mode;          // текущий режим (Mode)
    uint8_t       sub_feed;      // Sub_Mode_Feed   (0=Int,1=Man,2=Ext)
    uint8_t       sub_afeed;     // Sub_Mode_aFeed
    uint8_t       sub_thread;    // Sub_Mode_Thread
    uint8_t       sub_cone;      // Sub_Mode_Cone
    uint8_t       sub_sphere;    // Sub_Mode_Sphere
    // Для обратной совместимости (generic submode)
    ELS_Submode_t submode;       // = sub для текущего режима

    uint8_t       select_menu;   // SelectMenu: 1/2/3

    // ── Позиции из DRO (0.001 мм = 1 мкм) ──────────────────
    int32_t pos_x;      // Ось X (поперечная), инвертирована относительно DRO
    int32_t pos_y;      // Ось Y (продольная),  инвертирована относительно DRO

    // ── Позиции в сотках мм (0.01 мм) — как в Arduino ──────
    // Обновляются каждый цикл из pos_x/pos_y
    int32_t Size_X_mm;   // = pos_x / 10
    int32_t Size_Z_mm;   // = pos_y / 10
    int32_t MSize_X_mm;  // = pos_x / 5  (диаметр × 2)

    // ── Параметры подачи ────────────────────────────────────
    uint16_t Feed_mm;    // подача мм/об × 100  (10 = 0.10 мм/об)
    uint16_t aFeed_mm;   // асинхронная подача, мм/мин

    // Старые поля — используются в els_control.cpp
    int32_t feed;        // = Feed_mm (мм/об × 100)
    int32_t afeed;       // = aFeed_mm

    // ── Параметры резьбы ────────────────────────────────────
    uint8_t  Thread_Step;     // индекс в Thread_Info[] (0..TOTAL_THREADS-1)
    int32_t  thread_pitch;    // шаг резьбы 0.001мм (вычисляется из таблицы)
    uint8_t  thread_starts;   // = Ph
    int16_t  Ph;              // количество заходов (1..8)
    int16_t  Pass_Fin;        // доп. чистовых проходов  (PASS_FINISH + Pass_Fin)
    int16_t  Thr_Pass_Summ;   // суммарное кол-во пройденных проходов
    int16_t  Gewinde_flag;    // номер прохода при многозаходной резьбе

    // ── Параметры конуса ────────────────────────────────────
    uint8_t  Cone_Step;       // индекс в Cone_Info[] (0..TOTAL_CONE-1)
    bool     ConL_Thr_flag;   // конус L + резьба
    bool     ConR_Thr_flag;   // конус R + резьба
    bool     CThr_flag;       // вспомогательный флаг конус+резьба

    // ── Параметры цикла (Feed / aFeed / Thread / Cone) ──────
    int32_t  Pass_Total;      // всего проходов в цикле
    int32_t  Pass_Nr;         // текущий проход (счётчик убывает)
    int16_t  Ap;              // глубина съёма за проход, сотки мм (0.01мм)

    // ── Отскок и натяг оси Z ────────────────────────────────
    int16_t  OTSKOK_Z;        // отскок в микрошагах (REBOUND_Z по умолч.)
    int32_t  OTSKOK_Z_mm;     // отскок в 0.01мм для отображения
    int16_t  TENSION_Z;       // натяг (преднатяг) в шагах
    int32_t  TENSION_Z_mm;    // натяг в 0.01мм для отображения

    // Старые поля совместимости
    int32_t  otskok_y;        // = OTSKOK_Z (в шагах)
    int32_t  tension_y;       // = TENSION_Z (в шагах)

    // ── Сфера (шароточка) ───────────────────────────────────
    int32_t  Sph_R_mm;        // радиус шара в 0.001мм (Sph_R_mm × 2 = диаметр)
    int32_t  Bar_R_mm;        // радиус ножки в 0.001мм
    int32_t  Pass_Total_Sphr; // количество заходов сферы
    uint8_t  Cutter_Step;     // индекс ширины резца в Cutter_Width_array[]
    uint8_t  Cutting_Step;    // индекс шага резания в Cutting_Width_array[]
    int16_t  Cutter_Width;    // ширина резца, 0.01мм
    int16_t  Cutting_Width;   // шаг резания по оси, 0.01мм

    // ── Делитель / угол шпинделя ────────────────────────────
    int32_t  Enc_Pos;         // позиция делителя (тики шпиндельного энкодера)
    uint32_t Spindle_Angle;   // текущий угол шпинделя × 1000 (°)
    uint32_t Required_Angle;  // требуемый угол × 1000 (°)
    uint8_t  Total_Tooth;     // делений на круг (Total_Tooth)
    uint8_t  Current_Tooth;   // текущее деление

    // ── Лимиты ──────────────────────────────────────────────
    int32_t limit_y_left;    // Limit_Pos_Left
    int32_t limit_y_right;   // Limit_Pos_Right
    int32_t limit_x_front;   // Limit_Pos_Front
    int32_t limit_x_rear;    // Limit_Pos_Rear
    uint8_t limits_enabled;

    // ── Флаги ошибок и состояний ────────────────────────────
    bool err_0_flag;     // Джойстик не в нейтрали (стартовая проверка)
    bool err_1_flag;     // УСТАНОВИТЕ УПОРЫ
    bool err_2_flag;     // УСТАНОВИТЕ СУППОРТ В ИСХОДНУЮ
    bool Complete_flag;  // ОПЕРАЦИЯ ЗАВЕРШЕНА

    // ── Флаги движения ──────────────────────────────────────
    bool spindle_flag;    // шпиндель вращается
    bool feed_Z_flag;     // синхронная подача Z активна
    bool feed_X_flag;     // синхронная подача X активна
    uint8_t running;      // движение активно (общий флаг)
    uint8_t spindle_dir;  // направление шпинделя: 0=CW, 1=CCW

    // ── Позиции моторов (в микрошагах) ──────────────────────
    int32_t Motor_Z_Pos;  // положение мотора Z
    int32_t Motor_X_Pos;  // положение мотора X

    // ── RPM шпинделя ────────────────────────────────────────
    int32_t spindle_rpm;

    // ── Джойстик (управление осями вручную) ──────────────────
    // Устанавливаются из els_menu.cpp по физическому джойстику или
    // TOUCH команде от ESP32. Читаются в els_control.cpp.
    int8_t  joy_y;      // +1 = вперёд (к задн. бабке), -1 = назад, 0 = стоп
    int8_t  joy_x;      // +1 = к передн. точке, -1 = назад, 0 = стоп
    uint8_t joy_rapid;  // 0 = RAPID не нажат → быстрый ход; 1 = нажат → подача

    // ── GCode позиционирование (Phase 6) ────────────────────
    bool    gcode_motion;    // true = GCode-ход активен (ELS_Control_Update не трогает оси)
    int32_t gcode_target_y;  // целевая pos_y (0.001мм) по оси Z каретки
    int32_t gcode_target_x;  // целевая pos_x (0.001мм) по оси X резцедержателя
    bool    gcode_has_y;     // Z-ось задействована в текущем ходе
    bool    gcode_has_x;     // X-ось задействована

} ELS_State_t;

extern ELS_State_t els;

void ELS_State_Init(void);
