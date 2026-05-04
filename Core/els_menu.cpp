/**
 * @file  els_menu.cpp
 * @brief Обработка команд меню — Этап 14
 *
 * Обрабатывает:
 *  - TOUCH команды от ESP32 (M1-M8, S1-S3, KEY:*)
 *  - (TODO) физические кнопки GPIO (Этап 10)
 *
 * При любом изменении режима/подрежима/selectmenu:
 *  - Обновляет els_state
 *  - Отправляет обновление на ESP32
 *  - LCD обновится автоматически в следующем цикле DRV_LCD2004_PrintELS()
 */

#include "els_menu.h"
#include "els_state.h"
#include "els_control.h"
#include "els_config.h"
#include "els_tables.h"
#include "els_settings.h"
#include "../Drivers/drv_display.h"
#include "../Drivers/drv_beeper.h"
#include "../Drivers/drv_inputs.h"
#include <Arduino.h>

// Forward declarations
static void _key_select(void);
static void _exec_gcode(const char* line);

// ============================================================
// Состояние джойстика от тачскрина
// Физический джойстик переопределяет тачскрин при активном нажатии,
// но при возврате в нейтраль очищает оба источника.
// ============================================================
static int8_t  s_touch_joy_y   = 0; // +1/-1/0 от TOUCH:JOY:*
static int8_t  s_touch_joy_x   = 0;
static uint8_t s_touch_rapid   = 0; // 0=быстро (RAPID_ON), 1=подача (RAPID_OFF)

// ============================================================
// Отправить базовое состояние (mode/sub/sm/feed/afeed/ap/pass) на ESP32
// Вызывается при любом изменении состояния, чтобы ESP32 не расходился
// ============================================================
#if USE_ESP32_DISPLAY
static void _send_basic_state(void) {
    DRV_Display_SendMode(els.mode, els.submode);
    DRV_Display_SendSelectMenu(els.select_menu);
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
    DRV_Display_SendInt("AP", els.Ap);
    DRV_Display_SendInt2("PASS", els.Pass_Nr, els.Pass_Total);
}
#endif

// ============================================================
// Применить режим и обновить ESP32
// ============================================================
static void _set_mode(ELS_Mode_t mode) {
    if (els.mode == mode) {
        // Режим уже верный — ресинхронизируем ESP32 (он мог дрейфовать после инжекции)
#if USE_ESP32_DISPLAY
        _send_basic_state();
#endif
        return;
    }
    ELS_Control_Stop();
    els.mode = mode;
    // Сброс счётчиков цикла при смене режима
    els.select_menu   = 1;
    els.Ap            = 0;
    els.Pass_Nr       = 1;
    els.Thr_Pass_Summ = 0;
    els.Pass_Fin      = 0;
    // Синхронизируем generic submode с sub текущего режима
    switch (mode) {
        case MODE_FEED:    els.submode = (ELS_Submode_t)els.sub_feed;   break;
        case MODE_AFEED:   els.submode = (ELS_Submode_t)els.sub_afeed;  break;
        case MODE_THREAD:  els.submode = (ELS_Submode_t)els.sub_thread; break;
        case MODE_CONE_L:
        case MODE_CONE_R:  els.submode = (ELS_Submode_t)els.sub_cone;   break;
        case MODE_SPHERE:  els.submode = (ELS_Submode_t)els.sub_sphere; break;
        default: break;
    }
#if USE_ESP32_DISPLAY
    DRV_Display_SendMode(els.mode, els.submode);
    DRV_Display_SendSelectMenu(els.select_menu);
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
    DRV_Display_SendInt("AP", 0);
    DRV_Display_SendInt2("PASS", els.Pass_Nr, els.Pass_Total);
    if (mode == MODE_THREAD) {
        uint8_t ts = els.Thread_Step < TOTAL_THREADS ? els.Thread_Step : 0;
        DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
        DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
        DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
        int16_t ph = (els.Ph > 0) ? els.Ph : 1;
        DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
        int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
        DRV_Display_SendInt("THREAD_CYCL",  cycl);
        DRV_Display_SendInt("PASS_FIN",     (int32_t)PASS_FINISH + els.Pass_Fin);
    } else if (mode == MODE_AFEED) {
        DRV_Display_SendInt("DIVN", (int32_t)els.Total_Tooth);
        DRV_Display_SendInt("DIVM", (int32_t)els.Current_Tooth);
    } else if (mode == MODE_SPHERE) {
        DRV_Display_SendInt("SPHERE", els.Sph_R_mm);
        DRV_Display_SendInt("BAR",    els.Bar_R_mm);
    }
#endif
    DRV_Beeper_Tone(1000, 30);  // короткий звук при смене режима
}

static void _set_submode(uint8_t sub) {
    switch (els.mode) {
        case MODE_FEED:    els.sub_feed   = sub; break;
        case MODE_AFEED:   els.sub_afeed  = sub; break;
        case MODE_THREAD:  els.sub_thread = sub; break;
        case MODE_CONE_L:
        case MODE_CONE_R:  els.sub_cone   = sub; break;
        case MODE_SPHERE:  els.sub_sphere = sub; break;
        default: break;
    }
    els.submode = (ELS_Submode_t)sub;
#if USE_ESP32_DISPLAY
    _send_basic_state();
#endif
}

static void _set_select_menu(uint8_t sm) {
    if (sm < 1) sm = 1;
    if (sm > 3) sm = 3;
    els.select_menu = sm;
#if USE_ESP32_DISPLAY
    _send_basic_state();
#endif
}

// ============================================================
// Callback от ESP32: обработка TOUCH команд
// ============================================================
static void _on_display_rx(const DispRxCmd_t* rx) {
    // Режимы M1-M8
    switch (rx->touch) {
        case TOUCH_M1: _set_mode(MODE_FEED);    return;
        case TOUCH_M2: _set_mode(MODE_AFEED);   return;
        case TOUCH_M3: _set_mode(MODE_THREAD);  return;
        case TOUCH_M4: _set_mode(MODE_CONE_L);  return;
        case TOUCH_M5: _set_mode(MODE_CONE_R);  return;
        case TOUCH_M6: _set_mode(MODE_SPHERE);  return;
        case TOUCH_M7: _set_mode(MODE_DIVIDER); return;
        case TOUCH_M8: _set_mode(MODE_RESERVE); return;

        // Подрежимы S1/S2/S3
        case TOUCH_S1: _set_submode(SUBMODE_INTERNAL); return;
        case TOUCH_S2: _set_submode(SUBMODE_MANUAL);   return;
        case TOUCH_S3: _set_submode(SUBMODE_EXTERNAL); return;

        // Двойной тап на row6 → переключение select_menu (как физическая кнопка SELECT)
        case TOUCH_PARAM_OK: _key_select(); return;

        // Навигация по меню (SelectMenu)
        case TOUCH_KEY_LEFT:
            if (els.select_menu > 1) _set_select_menu(els.select_menu - 1);
            else {
#if USE_ESP32_DISPLAY
                _send_basic_state();
#endif
            }
            return;
        case TOUCH_KEY_RIGHT:
            _set_select_menu(els.select_menu + 1);
            return;

        // Кнопки вверх/вниз — изменение параметров (Thread_Step / Cone_Step)
        case TOUCH_KEY_UP:
            if (els.mode == MODE_THREAD) {
                if (els.Thread_Step > 0) {
                    els.Thread_Step--;
                    uint8_t ts = els.Thread_Step;
                    DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
                    DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
                    DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
                    int16_t ph = (els.Ph > 0) ? els.Ph : 1;
                    DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
                    int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
                    DRV_Display_SendInt("THREAD_CYCL",  cycl);
                }
            } else if (els.mode == MODE_CONE_L || els.mode == MODE_CONE_R) {
                if (els.Cone_Step > 0) els.Cone_Step--;
            }
#if USE_ESP32_DISPLAY
            _send_basic_state();
#endif
            return;
        case TOUCH_KEY_DN:
            if (els.mode == MODE_THREAD) {
                extern const uint8_t TOTAL_THREADS;
                if (els.Thread_Step < TOTAL_THREADS - 1) {
                    els.Thread_Step++;
                    uint8_t ts = els.Thread_Step;
                    DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
                    DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
                    DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
                    int16_t ph = (els.Ph > 0) ? els.Ph : 1;
                    DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
                    int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
                    DRV_Display_SendInt("THREAD_CYCL",  cycl);
                }
            } else if (els.mode == MODE_CONE_L || els.mode == MODE_CONE_R) {
                extern const uint8_t TOTAL_CONE;
                if (els.Cone_Step < TOTAL_CONE - 1) els.Cone_Step++;
            }
#if USE_ESP32_DISPLAY
            _send_basic_state();
#endif
            return;

        // Джойстик — управление движением через тачскрин
        // Флаги s_touch_joy_* читаются в ELS_Menu_Process() и
        // записываются в els.joy_y/joy_x когда физический джойстик в нейтрали.
        case TOUCH_JOY_LEFT:  s_touch_joy_y = +1; return;
        case TOUCH_JOY_RIGHT: s_touch_joy_y = -1; return;
        case TOUCH_JOY_UP:    s_touch_joy_x = +1; return;
        case TOUCH_JOY_DOWN:  s_touch_joy_x = -1; return;
        case TOUCH_JOY_STOP:  s_touch_joy_y = 0; s_touch_joy_x = 0; return;
        // RAPID_ON = кнопка "RAPID" нажата на экране → быстрый ход
        // RAPID_OFF = отпущена → режим подачи
        case TOUCH_RAPID_ON:  s_touch_rapid = 0; return;
        case TOUCH_RAPID_OFF: s_touch_rapid = 1; return;
        case TOUCH_ALERT_OK:
            els.err_1_flag = false;
            els.err_2_flag = false;
            return;

        case TOUCH_GCODE:
            _exec_gcode(rx->gcode_line);
            return;

        default: break;
    }

    // Параметрические команды: AP:N, FEED:N и т.д.
    if (rx->has_value) {
        if (strcmp(rx->cmd, "FEED") == 0) {
            uint16_t v = (uint16_t)rx->value;
            if (v >= 1 && v <= 2500) {
                els.Feed_mm = v;
                DRV_Display_SendInt("FEED", (int32_t)v);
            }
        } else if (strcmp(rx->cmd, "AFEED") == 0) {
            uint16_t v = (uint16_t)rx->value;
            if (v >= 10 && v <= 400) {
                els.aFeed_mm = v;
                DRV_Display_SendInt("AFEED", (int32_t)v);
            }
        } else if (strcmp(rx->cmd, "AP") == 0) {
            int16_t v = (int16_t)rx->value;
            if (v >= 0 && v <= 990) els.Ap = v;
            DRV_Display_SendInt("AP", (int32_t)els.Ap);
        } else if (strcmp(rx->cmd, "THREAD_STEP") == 0) {
            int n = (int)rx->value;
            if (n >= 0 && n < (int)TOTAL_THREADS) {
                els.Thread_Step = (uint8_t)n;
                // Обновить шаг резьбы в состоянии
                els.thread_pitch = (int32_t)(Thread_Info[n].Step * 1000.0f);
                // Отправить подтверждение на ESP32
                DRV_Display_SendCmd("THREAD_NAME", Thread_Info[n].Thread_Print);
                DRV_Display_SendInt("THREAD", (int32_t)(Thread_Info[n].Step * 100.0f));
                DRV_Display_SendInt("RPM_LIM", Thread_Info[n].Limit_Print);
                int16_t ph = (els.Ph > 0) ? els.Ph : 1;
                DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[n].Step * 100.0f * ph));
                int32_t cycl = (int32_t)Thread_Info[n].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
                DRV_Display_SendInt("THREAD_CYCL", cycl);
            }
        } else if (strcmp(rx->cmd, "CONE") == 0) {
            if (rx->value >= 0 && rx->value < (long)TOTAL_CONE) {
                els.Cone_Step = (uint8_t)rx->value;
                uint8_t cs = els.Cone_Step;
                if (cs >= TOTAL_CONE_ANGLES) cs = TOTAL_CONE_ANGLES - 1;
                DRV_Display_SendInt("CONE",       cs);
                DRV_Display_SendInt("CONE_ANGLE", Cone_Angle_x10[cs]);
            }
        } else if (strcmp(rx->cmd, "BAR") == 0) {
            int32_t v = rx->value;
            if (v >= 0 && v <= 9900) {
                els.Bar_R_mm = v;
                DRV_Display_SendInt("BAR", v);
            }
        } else if (strcmp(rx->cmd, "SPHERE") == 0) {
            int32_t v = rx->value;
            if (v >= 50 && v <= 9999) {
                els.Sph_R_mm = v;
                DRV_Display_SendInt("SPHERE", v);
            }
        } else if (strcmp(rx->cmd, "PASSES") == 0) {
            if (rx->value > 0) els.Pass_Total = rx->value;
        }
        ELS_Settings_MarkDirty();
#if USE_ESP32_DISPLAY
        _send_basic_state();
#endif
    }
}

// ============================================================
// Публичный API
// ============================================================
void ELS_Menu_Init(void) {
#if USE_ESP32_DISPLAY
    DRV_Display_SetRxCallback(_on_display_rx);
#endif
}

// ============================================================
// Вспомогательные функции смены режима через физический переключатель
// ============================================================
static void _switch_mode_hw(ELS_Mode_t new_mode) {
    if (els.mode == new_mode) return;
    ELS_Control_Stop();
    els.mode          = new_mode;
    els.select_menu   = 1;
    els.Ap            = 0;
    els.Pass_Nr       = 1;
    els.Thr_Pass_Summ = 0;
    els.Pass_Fin      = 0;
    switch (new_mode) {
        case MODE_FEED:    els.submode = (ELS_Submode_t)els.sub_feed;   break;
        case MODE_AFEED:   els.submode = (ELS_Submode_t)els.sub_afeed;  break;
        case MODE_THREAD:  els.submode = (ELS_Submode_t)els.sub_thread; break;
        case MODE_CONE_L:
        case MODE_CONE_R:  els.submode = (ELS_Submode_t)els.sub_cone;   break;
        case MODE_SPHERE:  els.submode = (ELS_Submode_t)els.sub_sphere; break;
        default: break;
    }
#if USE_ESP32_DISPLAY
    DRV_Display_SendMode(els.mode, els.submode);
    DRV_Display_SendSelectMenu(1);
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
    DRV_Display_SendInt("AP", 0);
    DRV_Display_SendInt2("PASS", els.Pass_Nr, els.Pass_Total);
    if (new_mode == MODE_THREAD) {
        uint8_t ts = els.Thread_Step < TOTAL_THREADS ? els.Thread_Step : 0;
        DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
        DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
        DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
        int16_t ph = (els.Ph > 0) ? els.Ph : 1;
        DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
        int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
        DRV_Display_SendInt("THREAD_CYCL",  cycl);
        DRV_Display_SendInt("PASS_FIN",     (int32_t)PASS_FINISH + els.Pass_Fin);
    } else if (new_mode == MODE_AFEED) {
        DRV_Display_SendInt("DIVN", (int32_t)els.Total_Tooth);
        DRV_Display_SendInt("DIVM", (int32_t)els.Current_Tooth);
    } else if (new_mode == MODE_SPHERE) {
        DRV_Display_SendInt("SPHERE", els.Sph_R_mm);
        DRV_Display_SendInt("BAR",    els.Bar_R_mm);
    }
#endif
    DRV_Beeper_Tone(1000, 30);
}

// Смена подрежима через 3-позиционный переключатель
static void _switch_submode_hw(uint8_t sub) {
    // Для переключения в Internal нужны установленные лимиты
    // Пока упрощённо — просто меняем
    switch (els.mode) {
        case MODE_FEED:    els.sub_feed   = sub; break;
        case MODE_AFEED:   els.sub_afeed  = sub; break;
        case MODE_THREAD:  els.sub_thread = sub; break;
        case MODE_CONE_L:
        case MODE_CONE_R:  els.sub_cone   = sub; break;
        case MODE_SPHERE:  els.sub_sphere = sub; break;
        default: break;
    }
    els.submode = (ELS_Submode_t)sub;
    ELS_Control_Stop();
#if USE_ESP32_DISPLAY
    DRV_Display_SendMode(els.mode, els.submode);
#endif
    DRV_Beeper_Tone(1200, 20);
}

// ============================================================
// Key UP — увеличить параметр в текущем режиме/меню
// ============================================================
static void _key_up(void) {
    bool rapid = (DRV_Inputs_GetJoy() & JOY_RAPID) != 0;
    switch (els.mode) {
        case MODE_FEED:
            if (els.select_menu == 1) {
                int16_t step = rapid ? 10 : 2;
                if (els.Ap + step <= 990) { els.Ap += step; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                // сброс X (диаметр вводился) — через меню
                DRV_Beeper_Tone(1000, 15);
            } else if (els.select_menu == 3) {
                if (els.TENSION_Z < 100) { els.TENSION_Z++; DRV_Beeper_Tone(1000, 15); }
            }
            break;

        case MODE_AFEED:
            if (els.select_menu == 1) {
                int16_t step = rapid ? 5 : 1;
                if (els.Ap + step <= 990) { els.Ap += step; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                if (els.Total_Tooth < 255) { els.Total_Tooth++; els.Current_Tooth = 1; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15); // сброс X
            }
            break;

        case MODE_THREAD:
            if (els.select_menu == 1) {
                if (els.Thread_Step < (uint8_t)(TOTAL_THREADS - 1)) {
                    els.Thread_Step++;
                    while (els.Thread_Step < (uint8_t)(TOTAL_THREADS - 1) &&
                           Thread_Info[els.Thread_Step].Ks_Div_Z / els.Ph == 0)
                        els.Thread_Step++;
                    DRV_Beeper_Tone(1000, 15);
                }
            } else if (els.select_menu == 2) {
                if (!els.CThr_flag && els.Ph < 90) {
                    els.Ph++;
                    if (Thread_Info[els.Thread_Step].Ks_Div_Z / els.Ph == 0 && els.Ph > 1) els.Ph--;
                    DRV_Beeper_Tone(1000, 15);
                }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        case MODE_CONE_L:
        case MODE_CONE_R:
            if (els.select_menu == 1) {
                if (els.Cone_Step < (uint8_t)(TOTAL_CONE - 1)) { els.Cone_Step++; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                if (els.Ap < 990) { els.Ap += rapid ? 10 : 2; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        case MODE_SPHERE:
            if (els.select_menu == 1) {
                if      (els.Sph_R_mm < 1250) { els.Sph_R_mm += 25;  DRV_Beeper_Tone(1000, 15); }
                else if (els.Sph_R_mm < 2500) { els.Sph_R_mm += 50;  DRV_Beeper_Tone(1000, 15); }
                else if (els.Sph_R_mm < 4750) { els.Sph_R_mm += 250; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                if (els.Cutter_Step < 9) { els.Cutter_Step++; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        default: break;
    }
#if USE_ESP32_DISPLAY
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
    DRV_Display_SendInt("AP", els.Ap);
    if (els.mode == MODE_THREAD) {
        uint8_t ts = els.Thread_Step < TOTAL_THREADS ? els.Thread_Step : 0;
        DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
        DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
        DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
        int16_t ph = (els.Ph > 0) ? els.Ph : 1;
        DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
        int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
        DRV_Display_SendInt("THREAD_CYCL",  cycl);
    }
#endif
    ELS_Settings_MarkDirty();
}

// ============================================================
// Key DOWN — уменьшить параметр
// ============================================================
static void _key_down(void) {
    bool rapid = (DRV_Inputs_GetJoy() & JOY_RAPID) != 0;
    switch (els.mode) {
        case MODE_FEED:
            if (els.select_menu == 1) {
                if (rapid) { if (els.Ap > 10) els.Ap -= 10; else if (els.Ap > 0) els.Ap -= 2; }
                else        { if (els.Ap >  0) els.Ap -= 2; }
                DRV_Beeper_Tone(1000, 15);
            } else if (els.select_menu == 2) {
                DRV_Beeper_Tone(1000, 15); // сброс Z
            } else if (els.select_menu == 3) {
                if (els.TENSION_Z > 0) { els.TENSION_Z--; DRV_Beeper_Tone(1000, 15); }
            }
            break;

        case MODE_AFEED:
            if (els.select_menu == 1) {
                if (rapid) { if (els.Ap > 5) els.Ap -= 5; else if (els.Ap > 0) els.Ap--; }
                else        { if (els.Ap > 0) els.Ap--; }
                DRV_Beeper_Tone(1000, 15);
            } else if (els.select_menu == 2) {
                if (els.Total_Tooth > 1) { els.Total_Tooth--; els.Current_Tooth = 1; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        case MODE_THREAD:
            if (els.select_menu == 1) {
                if (els.Thread_Step > 0) { els.Thread_Step--; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                if (els.Ph > 1) { els.Ph--; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        case MODE_CONE_L:
        case MODE_CONE_R:
            if (els.select_menu == 1) {
                if (els.Cone_Step > 0) { els.Cone_Step--; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 2) {
                if (els.Ap > 0) { els.Ap -= rapid ? 10 : 2; if (els.Ap < 0) els.Ap = 0; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        case MODE_SPHERE:
            if (els.select_menu == 1) {
                if      (els.Sph_R_mm > 2500) { els.Sph_R_mm -= 250; DRV_Beeper_Tone(1000, 15); }
                else if (els.Sph_R_mm > 1250) { els.Sph_R_mm -= 50;  DRV_Beeper_Tone(1000, 15); }
                else if (els.Sph_R_mm >   50) { els.Sph_R_mm -= 25;  DRV_Beeper_Tone(1000, 15); }
                if (els.Sph_R_mm < els.Bar_R_mm) els.Bar_R_mm = els.Sph_R_mm;
            } else if (els.select_menu == 2) {
                if (els.Cutter_Step > 0) { els.Cutter_Step--; DRV_Beeper_Tone(1000, 15); }
            } else if (els.select_menu == 3) {
                DRV_Beeper_Tone(1000, 15);
            }
            break;

        default: break;
    }
#if USE_ESP32_DISPLAY
    DRV_Display_SendFeed(els.Feed_mm, els.aFeed_mm);
    DRV_Display_SendInt("AP", els.Ap);
    if (els.mode == MODE_THREAD) {
        uint8_t ts = els.Thread_Step < TOTAL_THREADS ? els.Thread_Step : 0;
        DRV_Display_SendCmd("THREAD_NAME", Thread_Info[ts].Thread_Print);
        DRV_Display_SendInt("THREAD",       (int32_t)(Thread_Info[ts].Step * 100.0f));
        DRV_Display_SendInt("RPM_LIM",      Thread_Info[ts].Limit_Print);
        int16_t ph = (els.Ph > 0) ? els.Ph : 1;
        DRV_Display_SendInt("THREAD_TRAVEL", (int32_t)(Thread_Info[ts].Step * 100.0f * ph));
        int32_t cycl = (int32_t)Thread_Info[ts].Pass + PASS_FINISH + els.Pass_Fin + els.Thr_Pass_Summ;
        DRV_Display_SendInt("THREAD_CYCL",  cycl);
    }
#endif
    ELS_Settings_MarkDirty();
}

// ============================================================
// Key SELECT — цикл по select_menu
// ============================================================
static void _key_select(void) {
    uint8_t sm = els.select_menu;
    switch (els.mode) {
        case MODE_FEED:
            // В Ext режиме с лимитами: 1→2→3→1, иначе 1→2→1
            if (els.sub_feed == SUBMODE_EXTERNAL &&
                (els.limits_enabled & 0x0C)) {  // FRONT+REAR установлены
                if      (sm == 1) sm = 2;
                else if (sm == 2) sm = 3;
                else              sm = 1;
            } else {
                sm = (sm == 1) ? 2 : 1;
            }
            break;
        case MODE_AFEED:
        case MODE_THREAD:
        case MODE_CONE_L:
        case MODE_CONE_R:
        case MODE_SPHERE:
            if      (sm == 1) sm = 2;
            else if (sm == 2) sm = 3;
            else              sm = 1;
            break;
        case MODE_DIVIDER:
            els.Enc_Pos = 0; // Divider: SELECT сбрасывает позицию
            sm = 1;
            break;
        default: break;
    }
    _set_select_menu(sm);
    DRV_Beeper_Tone(1000, 30);
}

// ============================================================
// Вызывать из loop() — обработка физических кнопок
// ============================================================
void ELS_Menu_Process(void) {
    // ── 1. Переключатель режима (M1-M8) — PG8-PG15 ──
    // s_mode: бит 7 = M1(Feed), бит 6 = M2(aFeed), ..., бит 0 = M8(Reserve)
    static uint8_t s_mode_old = 0xFF;
    uint8_t mode_raw = DRV_Inputs_GetMode();
    if (mode_raw != s_mode_old && mode_raw != 0 && !els.running) {
        s_mode_old = mode_raw;
        if      (mode_raw & 0x80) _switch_mode_hw(MODE_FEED);
        else if (mode_raw & 0x40) _switch_mode_hw(MODE_AFEED);
        else if (mode_raw & 0x20) _switch_mode_hw(MODE_THREAD);
        else if (mode_raw & 0x10) _switch_mode_hw(MODE_CONE_L);
        else if (mode_raw & 0x08) _switch_mode_hw(MODE_CONE_R);
        else if (mode_raw & 0x04) _switch_mode_hw(MODE_SPHERE);
        else if (mode_raw & 0x02) _switch_mode_hw(MODE_DIVIDER);
        else if (mode_raw & 0x01) _switch_mode_hw(MODE_RESERVE);
    }

    // ── 2. Переключатель подрежима (Int/Man/Ext) — PG5-PG7 ──
    static uint8_t s_sub_old = 0xFF;
    uint8_t sub_raw = DRV_Inputs_GetSubmode();
    if (sub_raw != s_sub_old && !els.running) {
        s_sub_old = sub_raw;
        _switch_submode_hw(sub_raw);
    }

    // ── 3. Кнопки навигации (PG0-PG4) с автоповтором ──
    static BtnState_t s_btn_old = BTN_NONE;
    static uint16_t   s_key_cycle = 0;
    BtnState_t btn = DRV_Inputs_GetBtn();

    // Одновременное UP+DOWN = плавающие пины без железа → игнорируем
    if ((btn & BTN_UP) && (btn & BTN_DOWN)) btn = BTN_NONE;

    if (btn != s_btn_old) {
        // Новое нажатие
        s_key_cycle = 0;
        if (btn & BTN_UP)     { _key_up();     }
        if (btn & BTN_DOWN)   { _key_down();   }
        if (btn & BTN_SELECT) { _key_select(); }
        // BTN_LEFT/RIGHT зарезервированы для будущих функций
    } else if (btn != BTN_NONE) {
        // Автоповтор
        s_key_cycle++;
        if (s_key_cycle > DELAY_ENTER_KEYCYCLE) {
            s_key_cycle = DELAY_ENTER_KEYCYCLE - DELAY_INTO_KEYCYCLE;
            if (btn & BTN_UP)   _key_up();
            if (btn & BTN_DOWN) _key_down();
        }
    }
    s_btn_old = btn;

    // ── 4. Джойстик — обновление els.joy_y/joy_x ─────────────────────────
    // Физический джойстик имеет приоритет над тачскрином.
    // При физической нейтрали — тачскрин-состояние тоже сбрасывается
    // (безопасность: станок не остаётся в движении если отпустить физ. джойстик).
    //
    // Обновление происходит ВСЕГДА (не только в Manual submode) чтобы корректно
    // останавливать моторы. Сам els_control.cpp принимает движение только в Man.
    {
        JoyState_t joy = DRV_Inputs_GetJoy();
        bool phys_active = (joy & (JOY_LEFT | JOY_RIGHT | JOY_UP | JOY_DOWN)) != 0;

        if (phys_active) {
            // Физический джойстик активен — использовать его состояние
            els.joy_y = (joy & JOY_LEFT) ? +1 :
                        (joy & JOY_RIGHT) ? -1 : 0;
            els.joy_x = (joy & JOY_UP)   ? +1 :
                        (joy & JOY_DOWN)  ? -1 : 0;
            els.joy_rapid = (joy & JOY_RAPID) ? 1 : 0;
            // Сбросить тачскрин-состояние (физ. джойстик перехватил управление)
            s_touch_joy_y = 0;
            s_touch_joy_x = 0;
        } else {
            // Физический нейтраль → использовать тачскрин-состояние
            els.joy_y     = s_touch_joy_y;
            els.joy_x     = s_touch_joy_x;
            els.joy_rapid = s_touch_rapid;
        }

        // err_0_flag: Arduino-совместимая проверка нейтрали джойстика при старте.
        // Блокирует запуск пока джойстик не окажется в нейтральном положении.
        if (els.err_0_flag && !phys_active && els.joy_y == 0 && els.joy_x == 0) {
            els.err_0_flag = false;
        }
    }
}

// ============================================================
// GCode executor: фаза 5 — приём и разбор строки от ESP32
// Фаза 6-7: реальное движение (TODO)
// ============================================================
static void _exec_gcode(const char* line) {
#if USE_ESP32_DISPLAY
    // Найти первое слово G или M (пропускаем Nxxx)
    const char* p = line;
    while (*p && *p != 'G' && *p != 'g' && *p != 'M' && *p != 'm') p++;

    if (!*p) {
        // Пустая строка — OK
        DRV_Display_SendGCodeAck(true, nullptr);
        return;
    }

    char cmd_ch = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    int  cmd_num = atoi(p + 1);

    if (cmd_ch == 'G') {
        switch (cmd_num) {
            case 0:
            case 1: {
                // G0/G1: быстрое/рабочее перемещение
                // Фаза 5: логируем координаты, отвечаем OK
                // Фаза 6-7: реализация движения по X/Z
                const char* xp = strchr(line, 'X');
                if (!xp) xp = strchr(line, 'x');
                const char* zp = strchr(line, 'Z');
                if (!zp) zp = strchr(line, 'z');
                float xv = xp ? atof(xp + 1) : 0.0f;
                float zv = zp ? atof(zp + 1) : 0.0f;
                Serial.printf("[gc] G%d X=%.3f Z=%.3f\n", cmd_num, xv, zv);
                DRV_Display_SendGCodeAck(true, nullptr);
                break;
            }
            case 4:
                // G4: dwell — обрабатывается на ESP32, STM32 не получает
                DRV_Display_SendGCodeAck(true, nullptr);
                break;
            case 28:
                // G28: возврат в исходную — TODO фаза 6
                Serial.println("[gc] G28 home (stub)");
                DRV_Display_SendGCodeAck(true, nullptr);
                break;
            default:
                // Неизвестный G-код — принимаем, не блокируем
                Serial.printf("[gc] G%d (pass)\n", cmd_num);
                DRV_Display_SendGCodeAck(true, nullptr);
                break;
        }
    } else {
        // M-коды: M0/M2/M30 перехватывает ESP32, сюда не попадают
        Serial.printf("[gc] M%d (pass)\n", cmd_num);
        DRV_Display_SendGCodeAck(true, nullptr);
    }
#endif
}
