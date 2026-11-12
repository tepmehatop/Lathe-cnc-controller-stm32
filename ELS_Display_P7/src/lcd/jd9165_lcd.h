#pragma once
// 7" JC1060P470: панель JD9165 по MIPI-DSI (2 линии), 1024×600 RGB565. Обёртка как у jd9365_lcd (10.1").
#include <stdint.h>

class jd9165_lcd
{
public:
    explicit jd9165_lcd(int8_t lcd_rst, int8_t bl_pin) : _lcd_rst(lcd_rst), _bl(bl_pin) {}
    void begin();
    void set_backlight(bool on);
    // Framebuffer DPI-панели (1024×600 RGB565, PSRAM): запись + lcd_draw_bitmap() с указателем
    // внутрь буфера = только сброс кэша, без копии.
    uint16_t *frame_buffer();
    void lcd_draw_bitmap(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t *color_data);

private:
    int8_t _lcd_rst, _bl;
};
