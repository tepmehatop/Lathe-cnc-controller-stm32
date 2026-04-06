#pragma once
#include <stdint.h>

// Режимы работы ELS
typedef enum {
    MODE_FEED     = 0,  // Подача (мм/об)
    MODE_AFEED    = 1,  // Асинхронная подача (мм/мин)
    MODE_THREAD   = 2,  // Нарезка резьбы
    MODE_CONE     = 3,  // Конус
    MODE_SPHERE   = 4,  // Сфера
    MODE_DIVIDER  = 5,  // Делитель
    MODE_UNUSED6  = 6,
    MODE_UNUSED7  = 7,
} ELS_Mode_t;

// Подрежимы (направление резьбы/конуса)
typedef enum {
    SUBMODE_INTERNAL = 0,
    SUBMODE_MANUAL   = 1,
    SUBMODE_EXTERNAL = 2,
} ELS_Submode_t;

// Глобальное состояние станка
typedef struct {
    // Режим
    ELS_Mode_t   mode;
    ELS_Submode_t submode;

    // Позиции (единицы: 0.001 мм = 1 единица)
    int32_t pos_x;      // Позиция X (поперечная), из DRO или шагов
    int32_t pos_y;      // Позиция Y (продольная), из DRO или шагов

    // Параметры подачи
    int32_t feed;       // Подача мм/об × 100  (5=0.05, 200=2.00)
    int32_t afeed;      // Асинхронная подача мм/мин × 100

    // Параметры резьбы
    int32_t thread_pitch;   // Шаг резьбы, 0.001 мм
    uint8_t thread_starts;  // Количество заходов (1-8)
    int32_t thread_phase;   // Смещение фазы для многозаходной

    // Лимиты
    int32_t limit_y_left;
    int32_t limit_y_right;
    int32_t limit_x_front;
    int32_t limit_x_rear;
    uint8_t limits_enabled;

    // Отсчёт
    int32_t otskok_y;   // Отскок Y после резьбы (0.001 мм)
    int32_t tension_y;  // Натяг Y

    // Флаги
    uint8_t running;        // 1 = движение активно
    uint8_t spindle_dir;    // 1 = вперёд, 0 = назад

    // RPM шпинделя (вычисляется из энкодера)
    int32_t spindle_rpm;

} ELS_State_t;

extern ELS_State_t els;

void ELS_State_Init(void);
