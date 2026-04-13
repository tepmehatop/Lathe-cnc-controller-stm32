#include "els_state.h"
#include "els_tables.h"
#include <string.h>

ELS_State_t els;

void ELS_State_Init(void) {
    memset(&els, 0, sizeof(els));

    // Режим
    els.mode         = MODE_FEED;
    els.sub_feed     = SUBMODE_MANUAL;    // Sub_Mode_Feed_Man
    els.sub_afeed    = SUBMODE_MANUAL;
    els.sub_thread   = SUBMODE_MANUAL;    // Sub_Mode_Thread_Man
    els.sub_cone     = SUBMODE_MANUAL;
    els.sub_sphere   = SUBMODE_MANUAL;
    els.submode      = SUBMODE_MANUAL;
    els.select_menu  = 1;

    // Подача
    els.Feed_mm      = 10;    // 0.10 мм/об
    els.aFeed_mm     = 100;   // 100 мм/мин
    els.feed         = 10;
    els.afeed        = 100;

    // Резьба
    els.Thread_Step  = 0;     // первая строка таблицы (0.20mm)
    els.thread_pitch = 200;   // 0.200 мм = 200 × 0.001мм
    els.thread_starts = 1;
    els.Ph           = 1;
    els.Pass_Fin     = 0;
    els.Thr_Pass_Summ = 0;
    els.Gewinde_flag = 0;

    // Конус
    els.Cone_Step    = 0;
    els.ConL_Thr_flag = false;
    els.ConR_Thr_flag = false;
    els.CThr_flag    = false;

    // Цикл
    els.Pass_Total   = 1;
    els.Pass_Nr      = 1;
    els.Ap           = 0;

    // Отскок/натяг (REBOUND_Z = 200 микрошагов по умолчанию)
    els.OTSKOK_Z     = 200;
    els.OTSKOK_Z_mm  = 0;
    els.TENSION_Z    = 0;
    els.TENSION_Z_mm = 0;

    // Сфера
    els.Sph_R_mm        = 1000;  // 1.000 мм (радиус), диаметр = 2мм
    els.Bar_R_mm        = 0;
    els.Pass_Total_Sphr = 1;
    els.Cutter_Step     = 4;     // индекс → Cutter_Width_array[4] = 200
    els.Cutting_Step    = 2;     // индекс → Cutting_Width_array[2] = 50
    els.Cutter_Width    = 200;
    els.Cutting_Width   = 50;

    // Делитель
    els.Total_Tooth      = 1;
    els.Current_Tooth    = 1;
    els.Spindle_Angle    = 0;
    els.Required_Angle   = 0;

    // Лимиты
    els.limit_y_left    = -1073741824L;
    els.limit_y_right   =  1073741824L;
    els.limit_x_front   = -1073741824L;
    els.limit_x_rear    =  1073741824L;
    els.limits_enabled  = 0;

    // Флаги ошибок (err_0 в Arduino стартует = true, но у нас нет проверки джойстика)
    els.err_0_flag   = false;
    els.err_1_flag   = false;
    els.err_2_flag   = false;
    els.Complete_flag = false;
}
