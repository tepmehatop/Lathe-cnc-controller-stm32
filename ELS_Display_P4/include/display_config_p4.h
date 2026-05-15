/*
 * display_config_p4.h
 * Конфигурация дисплея JC8012P4A1C (ESP32-P4, 10.1", 800x1280)
 * Используется в связке с ELS_Display/include/display_config.h
 */

#pragma once

// ─── Параметры дисплея ─────────────────────────────────────────────────────
// Физическое разрешение: 800x1280 (портрет)
// Логическое разрешение: 1280x800 (LVGL вращение 90° → ландшафт)
// Это совместимо с существующим UI, разработанным для ландшафтного режима.

#define P4_LCD_H_RES   800    // физическая ширина (LVGL hor_res для init)
#define P4_LCD_V_RES   1280   // физическая высота (LVGL ver_res для init)
#define P4_LCD_ROTATION LV_DISP_ROT_90  // поворот → логика: 1280x800

// ─── GPIO пины LCD ─────────────────────────────────────────────────────────
#define P4_LCD_RST    27   // сброс LCD
#define P4_LCD_BL     23   // подсветка

// ─── GPIO пины тачскрина (GSL3680 I2C) ────────────────────────────────────
#define P4_TP_SDA     7
#define P4_TP_SCL     8
#define P4_TP_RST     22
#define P4_TP_INT     21

// ─── UART для связи со STM32 ───────────────────────────────────────────────
// TODO: уточнить по схеме модуля JC8012P4A1C (разъём UART)
// ESP32-P4: UART1 доступен через GPIO16(RX)/GPIO17(TX) по умолчанию
#define P4_UART_RX_PIN  16
#define P4_UART_TX_PIN  17
#define P4_UART_BAUD    57600
