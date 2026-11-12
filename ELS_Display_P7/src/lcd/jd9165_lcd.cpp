// 7" JC1060P470: JD9165 по MIPI-DSI. По образцу jd9365_lcd.cpp (10.1"), тайминги — по версии панели.
#include "Arduino.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include "display_config_p7.h"
#include "esp_lcd_jd9165.h"
#include "jd9165_lcd.h"

extern "C" {
extern const jd9165_lcd_init_cmd_t jd9165_init_v1[];
extern const uint16_t jd9165_init_v1_size;
extern const jd9165_lcd_init_cmd_t jd9165_init_v2[];
extern const uint16_t jd9165_init_v2_size;
}

static esp_lcd_panel_handle_t s_panel = nullptr;
static esp_lcd_panel_io_handle_t s_io = nullptr;

void jd9165_lcd::set_backlight(bool on)
{
    if (_bl >= 0) gpio_set_level((gpio_num_t)_bl, on ? 1 : 0);
}

void jd9165_lcd::begin()
{
    // Питание MIPI DSI PHY 2.5 В — внутренний LDO VO3 (как у 10.1")
    esp_ldo_channel_handle_t ldo = nullptr;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = 3;
    ldo_cfg.voltage_mv = 2500;
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo));

    if (_bl >= 0) {
        gpio_config_t bk = {};
        bk.pin_bit_mask = 1ULL << _bl;
        bk.mode = GPIO_MODE_OUTPUT;
        gpio_config(&bk);
    }
    set_backlight(false);   // до инициализации JD9165 светит лавандовым полем — подсветку позже

    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = 2;
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = 750;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &bus));

    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &s_io));

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.virtual_channel = 0;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = P7_LCD_H_RES;
    dpi.video_timing.v_size = P7_LCD_V_RES;
#if P7_PANEL_V2
    // Панель V2 (ESPHome «JC1060P470-V2» / демо Guition New_Panel)
    dpi.dpi_clock_freq_mhz = 52;
    dpi.video_timing.hsync_pulse_width = 24;  dpi.video_timing.hsync_back_porch = 136; dpi.video_timing.hsync_front_porch = 160;
    dpi.video_timing.vsync_pulse_width = 2;   dpi.video_timing.vsync_back_porch = 21;  dpi.video_timing.vsync_front_porch = 12;
#else
    // Старая панель (ESPHome «JC1060P470»)
    dpi.dpi_clock_freq_mhz = 54;
    dpi.video_timing.hsync_pulse_width = 40;  dpi.video_timing.hsync_back_porch = 160; dpi.video_timing.hsync_front_porch = 160;
    dpi.video_timing.vsync_pulse_width = 10;  dpi.video_timing.vsync_back_porch = 23;  dpi.video_timing.vsync_front_porch = 12;
#endif

    jd9165_vendor_config_t vendor = {};
#if P7_PANEL_V2
    vendor.init_cmds = jd9165_init_v2;  vendor.init_cmds_size = jd9165_init_v2_size;
#else
    vendor.init_cmds = jd9165_init_v1;  vendor.init_cmds_size = jd9165_init_v1_size;
#endif
    vendor.mipi_config.dsi_bus = bus;
    vendor.mipi_config.dpi_config = &dpi;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = _lcd_rst;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    set_backlight(true);
}

uint16_t *jd9165_lcd::frame_buffer()
{
    void *fb = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK) return nullptr;
    return (uint16_t *)fb;
}

void jd9165_lcd::lcd_draw_bitmap(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t *color_data)
{
    esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, color_data);
}
