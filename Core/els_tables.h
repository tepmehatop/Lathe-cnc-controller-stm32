#pragma once
#include <stdint.h>

// Таблица конусов (62 записи, как в Arduino Cone_Info[])
typedef struct {
    uint8_t  Cs_Div;
    int16_t  Cm_Div;
    char     Cone_Print[6];
} cone_info_t;

// Таблица резьб (67 записей, как в Arduino Thread_Info[])
typedef struct {
    uint8_t  Ks_Div_Z;
    int16_t  Km_Div_Z;
    uint8_t  Ks_Div_X;
    int16_t  Km_Div_X;
    char     Thread_Print[7];
    float    Step;
    uint8_t  Pass;
    int16_t  Limit_Print;
} thread_info_t;

// Ширина резца и шаг резания для режима Sphere
extern const int Cutter_Width_array[];
extern const int Cutting_Width_array[];
#define TOTAL_CUTTER_WIDTH 9
#define TOTAL_CUTTING_STEP 9

extern const cone_info_t   Cone_Info[];
extern const thread_info_t Thread_Info[];
extern const uint8_t       TOTAL_CONE;
extern const uint8_t       TOTAL_THREADS;
