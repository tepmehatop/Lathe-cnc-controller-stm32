#include "els_state.h"
#include <string.h>

ELS_State_t els;

void ELS_State_Init(void) {
    memset(&els, 0, sizeof(els));
    els.mode            = MODE_FEED;
    els.submode         = SUBMODE_EXTERNAL;
    els.feed            = 10;   // 0.10 мм/об по умолчанию
    els.afeed           = 100;  // 1.00 мм/мин по умолчанию
    els.thread_pitch    = 1000; // 1.000 мм по умолчанию
    els.thread_starts   = 1;
    els.limit_y_left    = -1073741824L;
    els.limit_y_right   =  1073741824L;
    els.limit_x_front   = -1073741824L;
    els.limit_x_rear    =  1073741824L;
    els.limits_enabled  = 0;
}
