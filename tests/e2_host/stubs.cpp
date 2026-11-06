// Заглушки драйверов координат для проверки ядра на компьютере
#include "../../Drivers/drv_dro.h"
#include "../../Drivers/drv_scales.h"
int32_t g_stub_dro_x = 0, g_stub_dro_y = 0;
void     DRV_DRO_Init(void) {}
void     DRV_DRO_Process(void) {}
int32_t  DRV_DRO_GetX(void) { return g_stub_dro_x; }
int32_t  DRV_DRO_GetY(void) { return g_stub_dro_y; }
void     DRV_Scales_Init(void) {}
void     DRV_Scales_Process(void) {}
uint8_t  g_stub_scale_present[2] = {0, 0};
int32_t  g_stub_scale_um[2] = {0, 0};
uint8_t  DRV_Scales_Present(ScaleAxis a) { return g_stub_scale_present[a]; }
int32_t  DRV_Scales_GetUm(ScaleAxis a) { return g_stub_scale_um[a]; }
uint8_t  DRV_Scales_IsOk(ScaleAxis) { return 1; }
const char* DRV_Scales_TypeName(ScaleAxis) { return "stub"; }
