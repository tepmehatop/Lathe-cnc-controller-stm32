/**
 * @file main.cpp
 * @brief ELS Display - Main firmware for ESP32/ESP32-S3/ESP32-P4
 *
 * Supports:
 * - ESP32-2432S024 (2.4" 320x240)   - ILI9341 via TFT_eSPI
 * - JC4827W543     (4.3" 480x272)   - NV3041A via Arduino_GFX, ESP32-S3
 * - JC8012P4A1C    (10.1" 800x1280) - JD9365 via MIPI-DSI, ESP32-P4
 *
 * Выбор дисплея: -DJC4827W543_DISPLAY=1 или -DJC8012P4A1C_DISPLAY=1 в platformio.ini
 */

#include <Arduino.h>
#include <lvgl.h>
#include "display_config.h"
#include "uart_protocol.h"
#include "gcode_wifi.h"
#include "gcode_exec.h"

// ============================================================================
// USB CDC Screenshot — общий код для jc4827w543 и jc8012p4a1c
// ============================================================================
#if defined(DISPLAY_JC4827W543) || defined(DISPLAY_JC8012P4A1C)

// stb JPEG encoder (single-header)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// PSRAM framebuffer — RGB565 (заполняется в my_disp_flush)
static uint16_t* screen_fb = nullptr;

// JPEG буфер в PSRAM
static uint8_t*  jpeg_buf  = nullptr;
static uint32_t  jpeg_size = 0;
static uint32_t  jpeg_cap  = 0;

// Семафоры: capture task ↔ loop() task
static SemaphoreHandle_t sem_requested = nullptr;
static SemaphoreHandle_t sem_done      = nullptr;

// stb callback — дописывает данные в jpeg_buf
static void jpeg_write_cb(void* /*ctx*/, void* data, int size) {
    if (!jpeg_buf || jpeg_size + (uint32_t)size > jpeg_cap) return;
    memcpy(jpeg_buf + jpeg_size, data, (size_t)size);
    jpeg_size += (uint32_t)size;
}

// Конвертирует screen_fb RGB565 → JPEG в jpeg_buf
static void do_capture() {
    if (!screen_fb || !jpeg_buf) { jpeg_size = 0; return; }
#ifdef DISPLAY_JC8012P4A1C
    const int W = 800;   // физ. ширина (flush после sw_rotate даёт физ. координаты 800×1280)
    const int H = 1280;  // физ. высота
#else
    const int W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
#endif
    uint8_t* rgb = (uint8_t*)heap_caps_malloc((size_t)W * H * 3, MALLOC_CAP_SPIRAM);
    if (!rgb) { jpeg_size = 0; Serial.println("JPEG: ps_malloc rgb failed"); return; }
    for (int y = 0; y < H; y++) {
        const uint16_t* src = screen_fb + y * W;
        uint8_t*        dst = rgb + y * W * 3;
        for (int x = 0; x < W; x++) {
            uint16_t px = src[x];
            dst[x*3+0] = (uint8_t)(((px >> 11) & 0x1F) << 3);
            dst[x*3+1] = (uint8_t)(((px >>  5) & 0x3F) << 2);
            dst[x*3+2] = (uint8_t)( (px        & 0x1F) << 3);
        }
    }
    jpeg_size = 0;
    stbi_write_jpg_to_func(jpeg_write_cb, nullptr, W, H, 3, rgb, 80);
    free(rgb);
    Serial.printf("JPEG ready: %u bytes\n", jpeg_size);
}

// FreeRTOS задача с большим стеком (32KB) — ждёт запрос, кодирует JPEG
static void capture_task_fn(void* /*pvParameters*/) {
    while (true) {
        if (sem_requested && xSemaphoreTake(sem_requested, portMAX_DELAY) == pdTRUE) {
            do_capture();
            if (sem_done) xSemaphoreGive(sem_done);
        }
    }
}

#endif // DISPLAY_JC4827W543 || DISPLAY_JC8012P4A1C

// ============================================================================
// jc4827w543 only: WiFi + HTTP screenshot server
// ============================================================================
#ifdef DISPLAY_JC4827W543
#include <WiFi.h>
#include <esp_http_server.h>

static esp_err_t screenshot_handler(httpd_req_t* req) {
    if (!screen_fb || !jpeg_buf || !sem_requested || !sem_done) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not ready");
        return ESP_FAIL;
    }
    xSemaphoreGive(sem_requested);
    if (xSemaphoreTake(sem_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture timeout");
        return ESP_FAIL;
    }
    if (jpeg_size == 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    const size_t CHUNK = 4096;
    esp_err_t ret = ESP_OK;
    size_t sent = 0;
    while (sent < jpeg_size && ret == ESP_OK) {
        size_t chunk = jpeg_size - sent;
        if (chunk > CHUNK) chunk = CHUNK;
        ret = httpd_resp_send_chunk(req, (const char*)jpeg_buf + sent, (ssize_t)chunk);
        if (ret == ESP_OK) sent += chunk;
    }
    if (ret == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
    return ret;
}

static void handle_screenshot_server() { }
#endif // DISPLAY_JC4827W543

// Шрифты DejaVu с кириллицей
LV_FONT_DECLARE(font_dejavu_12);
LV_FONT_DECLARE(font_dejavu_14);
LV_FONT_DECLARE(font_dejavu_16);
LV_FONT_DECLARE(font_dejavu_22);
LV_FONT_DECLARE(font_dejavu_36);
LV_FONT_DECLARE(font_dejavu_48);

// Tahoma Bold — основной шрифт UI
LV_FONT_DECLARE(font_tahoma_bold_16);  // кириллица+latin, для row6 SM-индикатора
LV_FONT_DECLARE(font_tahoma_bold_22);  // кириллица+latin, для заголовков и подписей
LV_FONT_DECLARE(font_tahoma_bold_28);  // цифры (digit range only, 0x2B..0x3A)
LV_FONT_DECLARE(font_tahoma_bold_40);  // цифры + latin G,K,t,p,i — для тега типа резьбы и rpm_val
LV_FONT_DECLARE(font_tahoma_bold_48);  // цифры, для rpm_val (=дизайн: secondary 48px)
LV_FONT_DECLARE(font_tahoma_bold_50);  // цифры, tight glow-слой для "1650" (+2px)
LV_FONT_DECLARE(font_tahoma_bold_56);  // цифры (запасной)
LV_FONT_DECLARE(font_tahoma_bold_64);  // цифры (запасной)
LV_FONT_DECLARE(font_tahoma_bold_72);  // цифры, основной шрифт "1.50" (=дизайн: 72px)
LV_FONT_DECLARE(font_tahoma_bold_74);  // цифры, tight glow-слой для "1.50" (+2px)
LV_FONT_DECLARE(font_tahoma_bold_84);  // цифры (запасной)
// Остальные жирные (для демо-экрана)
LV_FONT_DECLARE(font_arial_bold_40);
LV_FONT_DECLARE(font_arial_bold_56);
LV_FONT_DECLARE(font_din_alt_40);
LV_FONT_DECLARE(font_din_alt_56);
LV_FONT_DECLARE(font_din_cond_40);
LV_FONT_DECLARE(font_din_cond_56);

// ============================================================================
// Display-specific includes
// ============================================================================

#ifdef DISPLAY_JC4827W543
    // JC4827W543: ESP32-S3 + NV3041A (QSPI) + GT911 Touch
    #include <Arduino_GFX_Library.h>
    #include <TouchLib.h>

    Arduino_DataBus *bus = new Arduino_ESP32QSPI(
        45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
    );
    Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true /* IPS */);
    TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, GT911_SLAVE_ADDRESS1, TOUCH_RST);

#elif defined(DISPLAY_JC8012P4A1C)
    // JC8012P4A1C: ESP32-P4 + JD9365 (MIPI-DSI 2-lane) + GSL3680 Touch
    // Физически 800x1280 (портрет), логически 1280x800 через LVGL поворот 90°
    #include "display_config_p4.h"
    #include "jd9365_lcd.h"
    #include "gsl3680_touch.h"

    static jd9365_lcd   p4_lcd(P4_LCD_RST);
    static gsl3680_touch p4_touch(P4_TP_SDA, P4_TP_SCL, P4_TP_RST, P4_TP_INT);
    // Буфер частичной отрисовки LVGL (P4_DRAW_BUF_LINES строк, внутренняя SRAM)
    static lv_color_t* p4_buf0 = nullptr;

#else
    // ESP32-2432S024: ESP32 + ILI9341 (SPI) + XPT2046 Touch
    #include <TFT_eSPI.h>
    TFT_eSPI tft = TFT_eSPI();
#endif

// ============================================================================
// Global objects
// ============================================================================

UartProtocol uart_protocol;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_WIDTH * 32];

// Current design (3 or 8)
uint8_t current_design = 8;  // Dark Theme Pro

// ============================================================================
// LVGL Display Driver
// ============================================================================

void IRAM_ATTR my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

#ifdef DISPLAY_JC4827W543
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    if (screen_fb) {
        uint16_t* src = (uint16_t*)&color_p->full;
        for (uint32_t row = 0; row < h; row++) {
            uint16_t* dst = screen_fb + (area->y1 + row) * SCREEN_WIDTH + area->x1;
            memcpy(dst, src + row * w, w * 2);
        }
    }
#elif defined(DISPLAY_JC8012P4A1C)
    // P4: частичная перерисовка. LVGL-область (1280×800) поворачивается прямо во
    // framebuffer панели (800×1280): fb[row=1279-lx][col=ly] = LVGL(lx,ly).
    // Колонки x1..x2 ложатся в непрерывный блок строк fb → один cache writeback,
    // lcd_draw_bitmap с указателем внутрь fb ничего не копирует.
    if (screen_fb) {
        const uint16_t* src = (const uint16_t*)&color_p->full;
        for (int lx = area->x1; lx <= area->x2; lx++) {
            uint16_t* dst = screen_fb + (uint32_t)(P4_LCD_V_RES - 1 - lx) * P4_LCD_H_RES + area->y1;
            const uint16_t* s = src + (lx - area->x1);
            for (uint32_t j = 0; j < h; j++) dst[j] = s[j * w];
        }
        int row0 = P4_LCD_V_RES - 1 - area->x2;
        int row1 = P4_LCD_V_RES - 1 - area->x1;
        p4_lcd.lcd_draw_bitmap(0, row0, P4_LCD_H_RES, row1 + 1,
                               screen_fb + (uint32_t)row0 * P4_LCD_H_RES);
    }
#else
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();
#endif

    lv_disp_flush_ready(disp);
}

// ============================================================================
// LVGL Touch Driver
// ============================================================================

// Имитация касания для автотестов (<TAP:x,y> или <TAP:x,y,мс> по USB): нажатие держится 120 мс
// или заданное время (долгое нажатие — от 400 мс)
static struct { bool active; lv_coord_t x, y; uint32_t until_ms; } g_sim_tap = {};
static void sim_tap(int x, int y, int ms = 120)
{
    g_sim_tap.x = (lv_coord_t)x;
    g_sim_tap.y = (lv_coord_t)y;
    g_sim_tap.until_ms = millis() + ms;
    g_sim_tap.active = true;
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    if (g_sim_tap.active) {
        data->point.x = g_sim_tap.x;
        data->point.y = g_sim_tap.y;
        if ((int32_t)(millis() - g_sim_tap.until_ms) < 0) {
            data->state = LV_INDEV_STATE_PR;
        } else {
            data->state = LV_INDEV_STATE_REL;
            g_sim_tap.active = false;
        }
        return;
    }
#ifdef DISPLAY_JC4827W543
    if (touch.read()) {
        TP_Point p = touch.getPoint(0);
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
#elif defined(DISPLAY_JC8012P4A1C)
    // GSL3680 Touch: mirror_x/y в конфиге → tx=799-raw_x, ty=1279-raw_y
    // Flush=rotation270CW: LVGL(lx,ly) → физ.(1279-lx, ly); обратно: lx=raw_y, ly=raw_x
    // С mirror: lx=1279-ty=raw_y, ly=799-tx=raw_x
    uint16_t tx = 0, ty = 0;
    bool pressed = p4_touch.getTouch(&tx, &ty);
    if (pressed) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = (lv_coord_t)(P4_LCD_V_RES - 1 - ty);
        data->point.y = (lv_coord_t)(P4_LCD_H_RES - 1 - tx);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
#else
    data->state = LV_INDEV_STATE_REL;
#endif
}

// ============================================================================
// FONT DEMO SCREEN — показывает 4 жирных шрифта для выбора
// Строки: 1=Arial Bold, 2=DIN Alternate, 3=DIN Condensed, 4=Tahoma Bold
// ============================================================================

#ifndef DISPLAY_JC8012P4A1C  // демо-шрифты (Arial/DIN/Tahoma 56) для 10.1" не генерируются
lv_obj_t* create_font_demo_screen()
{
    struct FontRow { const char* name; const lv_font_t* font56; const lv_font_t* font40; };
    static const FontRow rows[] = {
        {"1. Arial\nBold",    &font_arial_bold_56, &font_arial_bold_40},
        {"2. DIN Alt\nBold",  &font_din_alt_56,    &font_din_alt_40},
        {"3. DIN Cond\nBold", &font_din_cond_56,   &font_din_cond_40},
        {"4. Tahoma\nBold",   &font_tahoma_bold_56,&font_tahoma_bold_40},
    };

    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x050508), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Заголовок
    lv_obj_t* hdr = lv_label_create(screen);
    lv_obj_set_pos(hdr, UX(8), UY(3));
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(hdr, &font_dejavu_12, 0);
    lv_label_set_text(hdr, "FONT DEMO  [cyan=1.50 @ 56px]  [green=1650 @ 40px]");

    uint32_t bg_clr[] = {0x0d1a2a, 0x080e18, 0x0d1a2a, 0x080e18};
    int row_h = 62;

    for (int i = 0; i < 4; i++) {
        int y = 19 + i * row_h;

        // Фон строки
        lv_obj_t* row_bg = lv_obj_create(screen);
        lv_obj_set_size(row_bg, SCREEN_WIDTH, row_h);
        lv_obj_set_pos(row_bg, 0, y);
        lv_obj_set_style_bg_color(row_bg, lv_color_hex(bg_clr[i]), 0);
        lv_obj_set_style_radius(row_bg, 0, 0);
        lv_obj_set_style_border_side(row_bg, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row_bg, lv_color_hex(0x1a3040), 0);
        lv_obj_set_style_border_width(row_bg, US(1), 0);
        lv_obj_set_style_pad_all(row_bg, 0, 0);
        lv_obj_clear_flag(row_bg, LV_OBJ_FLAG_SCROLLABLE);

        // Название шрифта
        lv_obj_t* name_lbl = lv_label_create(row_bg);
        lv_obj_set_pos(name_lbl, UX(5), UY(8));
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x5a8a9a), 0);
        lv_obj_set_style_text_font(name_lbl, &font_dejavu_12, 0);
        lv_label_set_text(name_lbl, rows[i].name);

        // "1.50" — большой шрифт 56px
        lv_obj_t* val_56 = lv_label_create(row_bg);
        lv_obj_set_pos(val_56, UX(115), UY(2));
        lv_obj_set_style_text_color(val_56, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(val_56, rows[i].font56, 0);
        lv_label_set_text(val_56, "1.50");

        // "1650" — средний шрифт 40px
        lv_obj_t* val_40 = lv_label_create(row_bg);
        lv_obj_set_pos(val_40, UX(310), UY(10));
        lv_obj_set_style_text_color(val_40, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_font(val_40, rows[i].font40, 0);
        lv_label_set_text(val_40, "1650");
    }

    lv_scr_load(screen);
    return screen;
}
#endif  // !DISPLAY_JC8012P4A1C

// ============================================================================
// Хранит указатели на все динамически обновляемые виджеты UI
// Заполняется в create_dark_pro_ui(), используется в update_ui_values()
// ============================================================================
struct UIHandles {
    // Левая панель — основные значения
    lv_obj_t* primary_val;        // Большое значение cyan ("1.50")
    lv_obj_t* primary_val_glow;   // Glow-слой под primary_val
    lv_obj_t* primary_unit;       // Подпись ("ШАГ ММ") — тап в M3 → THR_CAT
    lv_obj_t* thread_tag;         // Тег типа резьбы рядом с числом ("tpi","G","K")
    lv_obj_t* secondary_val;      // Большое значение green ("1650")
    lv_obj_t* secondary_val_glow; // Glow-слой под secondary_val
    lv_obj_t* secondary_unit;     // Подпись ("RPM")
    lv_obj_t* diam_unit;          // Подпись "ДИАМЕТР" (Ø на LCD2004)
    lv_obj_t* diam_val;           // Диаметр заготовки, мм
    lv_obj_t* diam_box;           // Ячейка ДИАМЕТР (скрыта в делилке/резерве)
    lv_obj_t* rpm_bar;            // Прогресс-бар оборотов

    // Statusbar
    lv_obj_t* mode_lbl;           // "M3 - РЕЗЬБА"
    lv_obj_t* submode_lbl;        // "Наружная" / "Внутренняя" / "Ручная"
    lv_obj_t* thr_cat_lbl;        // Индикатор категории резьбы ("ДЮЙМ"/"G-ТРУБ") вверху

    // Правая панель — 3 строки (title + value)
    lv_obj_t* row1_title;
    lv_obj_t* row1_val;
    lv_obj_t* row2_title;
    lv_obj_t* row2_val;
    lv_obj_t* row3_title;
    lv_obj_t* row3_val;

    // Редактирование параметра (tap на primary value)
    lv_obj_t* edit_arrow_left;    // ▲ слева от большого значения — виден в edit mode
    lv_obj_t* edit_arrow_right;   // ▼ справа от большого значения — виден в edit mode

    // Меню выбора режима (overlay, скрыт по умолчанию)
    lv_obj_t* mode_menu;          // Контейнер-оверлей
    lv_obj_t* mode_btns[8];       // Кнопки M1..M8 (индекс 0=M1..7=M8)

    // Меню быстрого выбора типа резьбы (M3, double-tap, скрыт по умолчанию)
    lv_obj_t* thr_type_menu;

    // Индикаторы лимитов в statusbar — 4 стрелки (← → ↑ ↓)
    lv_obj_t* lim_L;  // Левый (Z-)  ←
    lv_obj_t* lim_R;  // Правый (Z+) →
    lv_obj_t* lim_F;  // Передний (X+) ↑
    lv_obj_t* lim_B;  // Задний (X-)   ↓

    // Статусные иконки-кружки (правый угол statusbar)
    lv_obj_t* s2_bg;    // фон кружка подрежима (S1/S2/S3)
    lv_obj_t* s2_lbl;   // текст внутри ("S1"/"S2"/"S3")
    lv_obj_t* pwr_bg;   // фон кружка мотора Z
    lv_obj_t* warn_bg;  // фон кружка алерта

    // Алерт-оверлей — всплывающее уведомление поверх экрана
    lv_obj_t* alert_box;          // Контейнер алерта (hidden по умолчанию)
    lv_obj_t* alert_msg;          // Текст алерта

    // Phase-2 тестовый режим — полоска вверху экрана (hidden по умолчанию)
    lv_obj_t* test_bar;           // Фоновый прямоугольник (красный)
    lv_obj_t* test_lbl;           // Текст "[N/23] M1/S1 описание"

    // Контейнеры строк правой панели (для подсветки при sub-edit)
    lv_obj_t* row1_box;
    lv_obj_t* row2_box;
    lv_obj_t* row3_box;

    // Строка 4 правой панели (ПРОХОДЫ/ЗАХОДОВ/ЦИКЛОВ — read-only)
    lv_obj_t* row4_title;
    lv_obj_t* row4_val;
    lv_obj_t* row4_box;

    // Ячейка типа резьбы (Layout K: spare1 в M3; Layout I: nullptr)
    lv_obj_t* thr_type_box;
    lv_obj_t* thr_type_title;
    lv_obj_t* thr_type_val;

    // Spare2 (col2 bottom): индикатор текущего SelectMenu (SM)
    lv_obj_t* sm_row6;

    // Джойстик-оверлей (US-013) — управление осями через тачскрин
    lv_obj_t* joystick_overlay;    // Контейнер-оверлей
    lv_obj_t* joy_rapid_btn;       // Кнопка БЫСТРО (подсвечивается когда активна)
    lv_obj_t* joy_rapid_lbl;       // Текст кнопки БЫСТРО

    // Контейнеры для двух вариантов компоновки (K и I)
    lv_obj_t* k_content;           // Панель компоновки K

    // Настройки
    lv_obj_t* settings_menu;       // Меню настроек (оверлей)

    // Блок для жёлтой рамки в режиме редактирования primary value
    // В K — featured cell box; в I — cell[0] box; nullptr → border на самом label (compat)
    lv_obj_t* primary_edit_box;
};

static UIHandles g_ui = {};
static bool s_sphere_alert = false;   // показан алерт "Режим невозможен!" (M6 + внутренний подрежим)

// ============================================================================
// Виджеты компоновки K; apply_layout_widgets() копирует их в поля g_ui.*
// ============================================================================
struct LayoutWidgets {
    lv_obj_t* primary_val;
    lv_obj_t* primary_val_glow;
    lv_obj_t* primary_unit;
    lv_obj_t* thread_tag;
    lv_obj_t* secondary_val;
    lv_obj_t* secondary_val_glow;
    lv_obj_t* secondary_unit;
    lv_obj_t* diam_unit; lv_obj_t* diam_val; lv_obj_t* diam_box;
    lv_obj_t* rpm_bar;
    lv_obj_t* row1_title; lv_obj_t* row1_val; lv_obj_t* row1_box;
    lv_obj_t* row2_title; lv_obj_t* row2_val; lv_obj_t* row2_box;
    lv_obj_t* row3_title; lv_obj_t* row3_val; lv_obj_t* row3_box;
    lv_obj_t* row4_title; lv_obj_t* row4_val; lv_obj_t* row4_box;
    lv_obj_t* thr_type_box; lv_obj_t* thr_type_title; lv_obj_t* thr_type_val;
    lv_obj_t* edit_arrow_left;
    lv_obj_t* edit_arrow_right;
    lv_obj_t* primary_edit_box;  // box to highlight on primary-value edit
};
// Цвета осей в заголовках (recolor LVGL): Y — каретка (по ГОСТ это Z, синий), X — поперечная.
// Красный/зелёный не берём — ими показан знак значения.
#define AXIS_Z      "#4da6ff Z#"   // продольная (вдоль оси шпинделя) — Z, как в G-коде и ISO; на LCD2004 автора и блоке DRO — Y
#define AXIS_X      "#e070ff X#"
#define DIAM_SIGN   "\xE2\x8C\x80"   // ⌀ (U+2300, есть в шрифтах подписей: tools/gen_p4_fonts.sh, gen_43_fonts.sh)
#define TITLE_DIAM  "#e070ff " DIAM_SIGN " \xD0\x94\xD0\x98\xD0\x90\xD0\x9C\xD0\x95\xD0\xA2\xD0\xA0#"   // ⌀ ДИАМЕТР — цветом оси X
static LayoutWidgets g_wk = {};   // K layout widgets

// Копирует виджеты из src в поля g_ui.*
static void apply_layout_widgets(const LayoutWidgets& w) {
    g_ui.primary_val       = w.primary_val;
    g_ui.primary_val_glow  = w.primary_val_glow;
    g_ui.primary_unit      = w.primary_unit;
    g_ui.thread_tag        = w.thread_tag;
    g_ui.secondary_val     = w.secondary_val;
    g_ui.secondary_val_glow= w.secondary_val_glow;
    g_ui.secondary_unit    = w.secondary_unit;
    g_ui.diam_unit         = w.diam_unit;
    g_ui.diam_val          = w.diam_val;
    g_ui.diam_box          = w.diam_box;
    g_ui.rpm_bar           = w.rpm_bar;
    g_ui.row1_title        = w.row1_title;
    g_ui.row1_val          = w.row1_val;
    g_ui.row1_box          = w.row1_box;
    g_ui.row2_title        = w.row2_title;
    g_ui.row2_val          = w.row2_val;
    g_ui.row2_box          = w.row2_box;
    g_ui.row3_title        = w.row3_title;
    g_ui.row3_val          = w.row3_val;
    g_ui.row3_box          = w.row3_box;
    g_ui.row4_title        = w.row4_title;
    g_ui.row4_val          = w.row4_val;
    g_ui.row4_box          = w.row4_box;
    g_ui.thr_type_box      = w.thr_type_box;
    g_ui.thr_type_title    = w.thr_type_title;
    g_ui.thr_type_val      = w.thr_type_val;
    g_ui.edit_arrow_left   = w.edit_arrow_left;
    g_ui.edit_arrow_right  = w.edit_arrow_right;
    g_ui.primary_edit_box  = w.primary_edit_box;
}

// ============================================================================
// Свечение текста: рендерим тот же текст glow_font-ом (на 2px крупнее main font)
// с высокой прозрачностью ЗА основным текстом. glow_font=nullptr → без свечения.
//
// ДИНАМИЧЕСКИЕ ОБНОВЛЕНИЯ: при смене значения нужно обновлять ОБЕ метки:
//   lv_label_set_text(main_lbl, new_val);
//   if (glow_lbl) lv_label_set_text(glow_lbl, new_val);
// Для этого glow_out != nullptr возвращает указатель на glow-слой.
// ============================================================================
static lv_obj_t* make_glow_label(lv_obj_t* parent,
                                  const lv_font_t* font,
                                  const lv_font_t* glow_font,
                                  lv_color_t color,
                                  const char* text,
                                  lv_align_t align,
                                  lv_coord_t x, lv_coord_t y,
                                  lv_coord_t glow_y_adj = 0,
                                  lv_opa_t glow_opa = LV_OPA_80,
                                  lv_obj_t** glow_out = nullptr)
{
    if (glow_font) {
        lv_obj_t* glow = lv_label_create(parent);
        lv_obj_set_style_text_color(glow, color, 0);
        lv_obj_set_style_text_font(glow, glow_font, 0);
        lv_obj_set_style_opa(glow, glow_opa, 0);
        lv_label_set_text(glow, text);
        lv_obj_align(glow, align, x, y - glow_y_adj);
        if (glow_out) *glow_out = glow;
    }
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

// UTF-8 строки режимов и подрежимов — file scope, видны во всём файле
static const char* const MODE_STRS[] = {
    "",
    "\xD0\xA1\xD0\x98\xD0\x9D\xD0\xA5\xD0\xA0\xD0\x9E\xD0\x9D",          // СИНХРОН
    "\xD0\x90\xD0\xA1\xD0\x98\xD0\x9D\xD0\xA5\xD0\xA0\xD0\x9E\xD0\x9D", // АСИНХРОН
    "\xD0\xA0\xD0\x95\xD0\x97\xD0\xAC\xD0\x91\xD0\x90",                  // РЕЗЬБА
    "\xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA3\xD0\xA1 <",                        // КОНУС <
    "\xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA3\xD0\xA1 >",                        // КОНУС >
    "\xD0\xA8\xD0\x90\xD0\xA0",                                            // ШАР
    "\xD0\x94\xD0\x95\xD0\x9B\xD0\x98\xD0\x9B\xD0\x9A\xD0\x90",          // ДЕЛИЛКА
    "G-\xD0\x9A\xD0\x9E\xD0\x94",                                  // G-КОД (M8; у автора — «Резерв», BACKLOG-001)
};

static const char* const SUBMODE_STRS[] = {
    "",
    "\xD0\x92\xD0\xBD\xD1\x83\xD1\x82\xD1\x80\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8F\xD1\x8F",  // Внутренняя
    "\xD0\xA0\xD1\x83\xD1\x87\xD0\xBD\xD0\xBE\xD0\xB9",                                    // Ручной
    "\xD0\x9D\xD0\xB0\xD1\x80\xD1\x83\xD0\xB6\xD0\xBD\xD0\xB0\xD1\x8F",                  // Наружная
};

// ============================================================================
// Таблица конусов — индексы должны совпадать с массивом в прошивке Arduino!
// ============================================================================
// Зеркало Cone_Info[].Cone_Print из Arduino (63 записи, \5 заменён на °)
// [0]=45°, [1]=30°, [2..44]=1°..44° (без 30°), [45..51]=KM0..KM6, [52..62]=1:4..3:25
static const char* const CONE_NAMES[] = {
    "45\xC2\xB0",  // [0]  45°
    "30\xC2\xB0",  // [1]  30°
    " 1\xC2\xB0",  // [2]   1°
    " 2\xC2\xB0",  // [3]   2°
    " 3\xC2\xB0",  // [4]   3°
    " 4\xC2\xB0",  // [5]   4°
    " 5\xC2\xB0",  // [6]   5°
    " 6\xC2\xB0",  // [7]   6°
    " 7\xC2\xB0",  // [8]   7°
    " 8\xC2\xB0",  // [9]   8°
    " 9\xC2\xB0",  // [10]  9°
    "10\xC2\xB0",  // [11] 10°
    "11\xC2\xB0",  // [12] 11°
    "12\xC2\xB0",  // [13] 12°
    "13\xC2\xB0",  // [14] 13°
    "14\xC2\xB0",  // [15] 14°
    "15\xC2\xB0",  // [16] 15°
    "16\xC2\xB0",  // [17] 16°
    "17\xC2\xB0",  // [18] 17°
    "18\xC2\xB0",  // [19] 18°
    "19\xC2\xB0",  // [20] 19°
    "20\xC2\xB0",  // [21] 20°
    "21\xC2\xB0",  // [22] 21°
    "22\xC2\xB0",  // [23] 22°
    "23\xC2\xB0",  // [24] 23°
    "24\xC2\xB0",  // [25] 24°
    "25\xC2\xB0",  // [26] 25°
    "26\xC2\xB0",  // [27] 26°
    "27\xC2\xB0",  // [28] 27°
    "28\xC2\xB0",  // [29] 28°
    "29\xC2\xB0",  // [30] 29°
    "31\xC2\xB0",  // [31] 31°
    "32\xC2\xB0",  // [32] 32°
    "33\xC2\xB0",  // [33] 33°
    "34\xC2\xB0",  // [34] 34°
    "35\xC2\xB0",  // [35] 35°
    "36\xC2\xB0",  // [36] 36°
    "37\xC2\xB0",  // [37] 37°
    "38\xC2\xB0",  // [38] 38°
    "39\xC2\xB0",  // [39] 39°
    "40\xC2\xB0",  // [40] 40°
    "41\xC2\xB0",  // [41] 41°
    "42\xC2\xB0",  // [42] 42°
    "43\xC2\xB0",  // [43] 43°
    "44\xC2\xB0",  // [44] 44°
    "KM0",          // [45] Конус Морзе 0
    "KM1",          // [46] Конус Морзе 1
    "KM2",          // [47] Конус Морзе 2
    "KM3",          // [48] Конус Морзе 3
    "KM4",          // [49] Конус Морзе 4
    "KM5",          // [50] Конус Морзе 5
    "KM6",          // [51] Конус Морзе 6
    "1:4",          // [52]
    "1:5",          // [53]
    "1:7",          // [54]
    "1:10",         // [55]
    "1:16",         // [56]
    "1:20",         // [57]
    "1:24",         // [58]
    "1:30",         // [59]
    "1:50",         // [60]
    "7:64",         // [61]
    "3:25",         // [62]
};
static const int CONE_COUNT = (int)(sizeof(CONE_NAMES) / sizeof(CONE_NAMES[0]));

// ============================================================================
// Таблица шагов резьбы — зеркало Thread_Info[] из Arduino-прошивки
// Используется для локальной навигации в edit mode (без ожидания ответа Arduino)
// mm_x100 = Step * 100 (округлённо), label = Thread_Print[7]
// ============================================================================
struct ThreadEntry { int16_t mm_x100; const char* label; };
static const ThreadEntry s_thread_table[] = {
    // Метрическая
    {  20, "0.20mm" }, {  25, "0.25mm" }, {  30, "0.30mm" }, {  35, "0.35mm" },
    {  40, "0.40mm" }, {  45, "0.45mm" }, {  50, "0.50mm" }, {  60, "0.60mm" },
    {  70, "0.70mm" }, {  75, "0.75mm" }, {  80, "0.80mm" }, { 100, "1.00mm" },
    { 125, "1.25mm" }, { 150, "1.50mm" }, { 175, "1.75mm" }, { 200, "2.00mm" },
    { 250, "2.50mm" }, { 300, "3.00mm" }, { 400, "4.00mm" }, { 450, "4.50mm" },
    // Дюймовая
    { 423, " 6tpi " }, { 363, " 7tpi " }, { 318, " 8tpi " }, { 282, " 9tpi " },
    { 254, "10tpi " }, { 231, "11tpi " }, { 212, "12tpi " }, { 195, "13tpi " },
    { 181, "14tpi " }, { 159, "16tpi " }, { 141, "18tpi " }, { 134, "19tpi " },
    { 127, "20tpi " }, { 116, "22tpi " }, { 106, "24tpi " }, {  98, "26tpi " },
    {  94, "27tpi " }, {  91, "28tpi " }, {  79, "32tpi " }, {  64, "40tpi " },
    {  58, "44tpi " }, {  53, "48tpi " }, {  45, "56tpi " }, {  42, "60tpi " },
    {  40, "64tpi " }, {  35, "72tpi " }, {  32, "80tpi " },
    // Трубная G 55°
    {  91, "G 1/16" }, {  91, "G  1/8" }, { 134, "G  1/4" }, { 134, "G  3/8" },
    { 181, "G  1/2" }, { 181, "G  3/4" }, { 231, "G 1   " }, { 231, "G1 1/4" },
    { 231, "G1 1/2" }, { 231, "G 2   " },
    // Трубная K 60°
    {  94, "K 1/16" }, {  94, "K  1/8" }, { 141, "K  1/4" }, { 141, "K  3/8" },
    { 181, "K  1/2" }, { 181, "K  3/4" }, { 221, "K 1   " }, { 221, "K1 1/4" },
    { 221, "K1 1/2" }, { 221, "K 2   " },
};
static const int THREAD_TABLE_SIZE = (int)(sizeof(s_thread_table) / sizeof(s_thread_table[0]));
// Индексы первых записей каждой категории резьбы
static const int THREAD_IDX_METRIC = 0;   // Метрическая: entries 0-19
static const int THREAD_IDX_INCH   = 20;  // Дюймовая: entries 20-46
static const int THREAD_IDX_GPIPE  = 47;  // G-труб: entries 47-56
static const int THREAD_IDX_KPIPE  = 57;  // K-труб: entries 57-66

// Диапазон категории (метрич./дюйм./G/K), в которую входит idx: барабан и ▲/▼ листают только её,
// чтобы после смены типа не попадать на резьбы соседней категории
static void thread_cat_range(int idx, int& lo, int& hi)
{
    if      (idx < THREAD_IDX_INCH)  { lo = THREAD_IDX_METRIC; hi = THREAD_IDX_INCH - 1; }
    else if (idx < THREAD_IDX_GPIPE) { lo = THREAD_IDX_INCH;   hi = THREAD_IDX_GPIPE - 1; }
    else if (idx < THREAD_IDX_KPIPE) { lo = THREAD_IDX_GPIPE;  hi = THREAD_IDX_KPIPE - 1; }
    else                             { lo = THREAD_IDX_KPIPE;  hi = THREAD_TABLE_SIZE - 1; }
}

// ============================================================================
// Состояние режима редактирования основного параметра (тап на большое значение)
// В режиме edit: UP/DOWN → локальная навигация по таблице + <TOUCH:PARAM_UP/DN>
// ============================================================================
struct EditParam {
    bool     active;
    uint32_t last_ms;    // millis() последнего действия → auto-exit через 5с
    int      local_step; // индекс в s_thread_table (для MODE_THREAD)
    int32_t  local_val;  // текущее значение (нативные единицы) для остальных режимов
};
static EditParam g_edit_param = {false, 0, 13, 0};

// ============================================================================
// Состояние редактирования параметра в правой панели (тап на строку)
// Варианты: M1/M2→Ap, M3→Ph, M4/M5→Cone_Step, M7→Total_Tooth/Current_Tooth
// ============================================================================
struct SubEdit {
    bool     active;
    int      row;           // 0=row1, 1=row2, 2=row3, 3=thr_type_box
    uint32_t last_ms;
    // Локальные копии параметров — обновляются при входе + навигации
    int16_t  ap;            // Съём (мм*100, 0..990)
    uint8_t  ph;            // Заходы резьбы (1..8)
    uint8_t  cone_idx;      // Индекс конуса
    uint16_t total_tooth;   // Делилка: делений (1..255)
    uint16_t current_tooth; // Делилка: текущая метка (1..total_tooth)
    int16_t  bar_r;         // ШАР: ножка радиус (мм*100, 0..9900)
    int16_t  sphere_r;      // ШАР: радиус шара (мм*100) — редактируется в thr_type_box
    uint16_t pass_total;    // M1/M2/M4/M5: всего проходов (KEY:LEFT/RIGHT)
    lv_timer_t* exit_timer; // Таймер отложенного выхода из sub_edit (grace period после confirm)
};
static SubEdit g_sub_edit = {false, -1, 0, 0, 1, 0, 1, 1, 0, 1000, 1, nullptr};

// ============================================================================
// Состояние алерта — всплывающее уведомление от Arduino
// ============================================================================
struct AlertState {
    bool     active;
    uint32_t show_ms;  // millis() когда показан → авто-скрытие через 5с
    bool     sticky;   // флаг ошибки ядра: без авто-скрытия, закрывает ALERT:0 от STM32
};
static AlertState g_alert = {false, 0, false};
static bool s_gcode_alert = false;   // ошибка G-кода: закрывает только OK, не ALERT:0 и не таймер

// ============================================================================
// Состояние джойстик-оверлея (US-013)
// ============================================================================
struct JoystickOverlayState {
    bool     active;
    uint32_t last_ms;  // millis() последней активности → авто-скрытие через 30с
    bool     rapid;    // Режим БЫСТРО активен
};
static JoystickOverlayState g_joystick = {false, 0, false};

// ============================================================================
// Универсальный барабан выбора параметра
// 5 меток + swipe + инерция. UART только при OK. Правые кнопки всегда доступны.
// ============================================================================
enum RollerTarget : uint8_t {
    RT_FEED, RT_AFEED, RT_THREAD,        // primary edit (g_edit_param)
    RT_AP, RT_CONE, RT_SPH_R, RT_BAR_R,  // sub-edit (g_sub_edit)
    RT_TOOTH_TOTAL, RT_TOOTH_CUR,         // divider sub-edit
};
struct RollerCfg {
    RollerTarget target;
    int32_t      val_min, val_max, val_step;
    const char*  title;
};
struct RollerState {
    bool        active;
    RollerCfg   cfg;
    lv_obj_t*   backdrop;
    lv_obj_t*   panel;
    lv_obj_t*   labels[5];
    int32_t     pending_val;
    int32_t     entry_val;
    int32_t     touch_accum;
    lv_timer_t* inertia_timer;
    float       velocity;
    float       vel_accum;
};
static RollerState g_roller = {};

// ============================================================================
// Демо-режим — тройное нажатие OK → 30-секундная демонстрация всех режимов
// ============================================================================
static bool     g_demo_active       = false;
static uint32_t g_demo_start_ms     = 0;
static uint32_t g_demo_last_step_ms = 0;
static int      g_demo_mode_idx     = -1;  // 0..7 = M1..M8, -1 = не инициализирован
// Тройной тап OK
static uint8_t  s_ok_tap_count   = 0;
static uint32_t s_ok_last_tap_ms = 0;

// Phase-2: тест-режим (5× быстрый DN вне edit → список сценариев)
static const int TEST_SCENARIO_COUNT = 60;
static bool     g_test_active    = false;
static int      g_test_idx       = 0;
static uint8_t  s_dn_rapid_cnt   = 0;
static uint32_t s_dn_rapid_ms    = 0;

// Forward declarations — нужны lambda-callbacks внутри create_dark_pro_ui
static void create_k_content(lv_obj_t* screen);
static void create_settings_menu(lv_obj_t* screen);
static void handle_up_btn_click();
static void handle_dn_btn_click();
static void handle_ok_btn_click();
static void apply_mode_layout(LatheMode mode);
static void update_ui_values(const LatheData& data);
static void update_thread_type_indicators(const char* label);
static void enter_edit_mode();
static void exit_edit_mode();
static void show_thr_type_menu();
static void jump_to_thread_type(int first_idx);
static void show_alert(const char* message);
static void dismiss_alert();
static void show_joystick_overlay();
static void hide_joystick_overlay();
static void update_limit_indicators(const LimitStatus& lim);
static void enter_sub_edit(int row);
static void exit_sub_edit();
static void highlight_sub_edit_row(int row, bool on);
static void open_roller(int32_t cur_val, const RollerCfg& cfg);
static void close_roller(bool confirm);
static void roller_update_labels();
static void sub_edit_step(bool up);
static uint8_t get_editable_rows(LatheMode mode);
static void apply_afeed_sm2_layout();
static void apply_feed_sm2_layout();
static void apply_feed_sm3_layout();
static void apply_thread_sm2_layout();
static void apply_cone_sm2_layout();
static void demo_start();
static void demo_stop();
static void demo_tick();
static void test_start();
static void test_stop();
static void test_apply(int idx);
static LatheMode s_last_mode = (LatheMode)0;
static uint8_t   s_last_select_menu = 0;  // tracks last SelectMenu for M2 layout changes

// ============================================================================
// Обработчики кнопок UP / OK / DN — общие для обоих вариантов компоновки
// ============================================================================

static void handle_up_btn_click()
{
    if (g_test_active) {
        g_test_idx = (g_test_idx + 1) % TEST_SCENARIO_COUNT;
        test_apply(g_test_idx);
        return;
    }
    // Idle: 5× подряд → тест-режим
    if (!g_sub_edit.active && !g_edit_param.active && !g_demo_active) {
        uint32_t now = millis();
        if (now - s_dn_rapid_ms > 2000) s_dn_rapid_cnt = 0;
        s_dn_rapid_ms = now;
        if (++s_dn_rapid_cnt >= 5) { s_dn_rapid_cnt = 0; test_start(); return; }
    }
    // Roller активен — UP двигает барабан (все режимы и sub-edit)
    if (g_roller.active) {
        if (g_roller.inertia_timer) { lv_timer_del(g_roller.inertia_timer); g_roller.inertia_timer = nullptr; }
        g_roller.velocity    = 0.0f;
        g_roller.pending_val = constrain(g_roller.pending_val + g_roller.cfg.val_step,
                                          g_roller.cfg.val_min, g_roller.cfg.val_max);
        roller_update_labels();
        return;
    }
    if (g_sub_edit.active) {
        sub_edit_step(true);
    } else if (g_edit_param.active) {
        g_edit_param.last_ms = millis();
        char buf[32];
        if (s_last_mode == MODE_THREAD) {
            { int lo, hi; thread_cat_range(g_edit_param.local_step, lo, hi);
              if (g_edit_param.local_step < hi) g_edit_param.local_step++; }
            const char* lbl = s_thread_table[g_edit_param.local_step].label;
            char fbuf[32]; char* fdst = fbuf;
            for (const char* fsrc = lbl; *fsrc && fdst < fbuf + sizeof(fbuf) - 1; fsrc++) {
                uint8_t fc = (uint8_t)*fsrc;
                if (fc == 0x2B || (fc >= 0x2D && fc <= 0x3A)) *fdst++ = *fsrc;
            }
            *fdst = '\0';
            if (fbuf[0] == '\0') snprintf(fbuf, sizeof(fbuf), "--");
            lv_label_set_text(g_ui.primary_val, fbuf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, fbuf);
            if (strstr(lbl, "tpi"))
                lv_label_set_text(g_ui.primary_unit, "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C");
            else if (lbl[0] == 'G')
                lv_label_set_text(g_ui.primary_unit, "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");
            else if (lbl[0] == 'K')
                lv_label_set_text(g_ui.primary_unit, "K-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");
            else
                lv_label_set_text(g_ui.primary_unit, "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");
            update_thread_type_indicators(lbl);
        } else if (s_last_mode == MODE_AFEED) {
            g_edit_param.local_val = constrain(g_edit_param.local_val + 5, 1, 9999);
            snprintf(buf, sizeof(buf), "%ld", g_edit_param.local_val);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        } else if (s_last_mode == MODE_FEED ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R ||
                   s_last_mode == MODE_SPHERE) {
            g_edit_param.local_val = constrain(g_edit_param.local_val + 5, 5, 9999);
            snprintf(buf, sizeof(buf), "%ld.%02ld",
                     g_edit_param.local_val / 100, g_edit_param.local_val % 100);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        }
        if (s_last_mode == MODE_THREAD) {
            char ts[24]; snprintf(ts, sizeof(ts), "THREAD_STEP:%d", (int)g_edit_param.local_step);
            uart_protocol.sendButtonPress(ts);
        } else if (s_last_mode == MODE_FEED || s_last_mode == MODE_SPHERE ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) {
            char fs[24]; snprintf(fs, sizeof(fs), "FEED:%d", (int)g_edit_param.local_val);
            uart_protocol.sendButtonPress(fs);
        } else if (s_last_mode == MODE_AFEED) {
            char fs[24]; snprintf(fs, sizeof(fs), "AFEED:%d", (int)g_edit_param.local_val);
            uart_protocol.sendButtonPress(fs);
        } else {
            uart_protocol.sendButtonPress("KEY:UP");
        }
    }
}

static void handle_dn_btn_click()
{
    if (g_test_active) {
        g_test_idx = (g_test_idx - 1 + TEST_SCENARIO_COUNT) % TEST_SCENARIO_COUNT;
        test_apply(g_test_idx);
        return;
    }
    // Roller активен — DN двигает барабан
    if (g_roller.active) {
        if (g_roller.inertia_timer) { lv_timer_del(g_roller.inertia_timer); g_roller.inertia_timer = nullptr; }
        g_roller.velocity    = 0.0f;
        g_roller.pending_val = constrain(g_roller.pending_val - g_roller.cfg.val_step,
                                          g_roller.cfg.val_min, g_roller.cfg.val_max);
        roller_update_labels();
        return;
    }
    if (g_sub_edit.active) {
        sub_edit_step(false);
    } else if (g_edit_param.active) {
        g_edit_param.last_ms = millis();
        char buf[32];
        if (s_last_mode == MODE_THREAD) {
            { int lo, hi; thread_cat_range(g_edit_param.local_step, lo, hi);
              if (g_edit_param.local_step > lo) g_edit_param.local_step--; }
            const char* lbl = s_thread_table[g_edit_param.local_step].label;
            char fbuf[32]; char* fdst = fbuf;
            for (const char* fsrc = lbl; *fsrc && fdst < fbuf + sizeof(fbuf) - 1; fsrc++) {
                uint8_t fc = (uint8_t)*fsrc;
                if (fc == 0x2B || (fc >= 0x2D && fc <= 0x3A)) *fdst++ = *fsrc;
            }
            *fdst = '\0';
            if (fbuf[0] == '\0') snprintf(fbuf, sizeof(fbuf), "--");
            lv_label_set_text(g_ui.primary_val, fbuf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, fbuf);
            if (strstr(lbl, "tpi"))
                lv_label_set_text(g_ui.primary_unit, "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C");
            else if (lbl[0] == 'G')
                lv_label_set_text(g_ui.primary_unit, "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");
            else if (lbl[0] == 'K')
                lv_label_set_text(g_ui.primary_unit, "K-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");
            else
                lv_label_set_text(g_ui.primary_unit, "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");
            update_thread_type_indicators(lbl);
        } else if (s_last_mode == MODE_AFEED) {
            g_edit_param.local_val = constrain(g_edit_param.local_val - 5, 1, 9999);
            snprintf(buf, sizeof(buf), "%ld", g_edit_param.local_val);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        } else if (s_last_mode == MODE_FEED ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R ||
                   s_last_mode == MODE_SPHERE) {
            g_edit_param.local_val = constrain(g_edit_param.local_val - 5, 5, 9999);
            snprintf(buf, sizeof(buf), "%ld.%02ld",
                     g_edit_param.local_val / 100, g_edit_param.local_val % 100);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        }
        if (s_last_mode == MODE_THREAD) {
            char ts[24]; snprintf(ts, sizeof(ts), "THREAD_STEP:%d", (int)g_edit_param.local_step);
            uart_protocol.sendButtonPress(ts);
        } else if (s_last_mode == MODE_FEED || s_last_mode == MODE_SPHERE ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) {
            char fs[24]; snprintf(fs, sizeof(fs), "FEED:%d", (int)g_edit_param.local_val);
            uart_protocol.sendButtonPress(fs);
        } else if (s_last_mode == MODE_AFEED) {
            char fs[24]; snprintf(fs, sizeof(fs), "AFEED:%d", (int)g_edit_param.local_val);
            uart_protocol.sendButtonPress(fs);
        } else {
            uart_protocol.sendButtonPress("KEY:DN");
        }
    }
}

static void handle_ok_btn_click()
{
    if (g_test_active) { test_stop(); return; }
    if (g_demo_active) { demo_stop(); return; }

    uint32_t now = millis();
    if (now - s_ok_last_tap_ms < 600) { s_ok_tap_count++; }
    else                               { s_ok_tap_count = 1; }
    s_ok_last_tap_ms = now;
    if (s_ok_tap_count >= 3) { s_ok_tap_count = 0; demo_start(); return; }

    if (g_roller.active) {
        RollerTarget tgt = g_roller.cfg.target;
        close_roller(true);  // sub_edit grace period / cleanup handled inside close_roller
        bool is_primary = (tgt == RT_FEED || tgt == RT_AFEED || tgt == RT_THREAD);
        if (is_primary) exit_edit_mode();
        return;
    }
    if (g_sub_edit.active) {
        exit_sub_edit();
    } else if (g_edit_param.active) {
        exit_edit_mode();
    } else if (s_last_mode == MODE_DIVIDER) {
        uart_protocol.sendButtonPress("PARAM_OK");
    }
}

// ============================================================================
// Тап по основному значению (K и I): вход/выход из редактирования.
// В M3 двойной тап открывает меню типа резьбы. Редактирование (барабан) открывается
// с задержкой окна двойного тапа — иначе барабан перекрывает экран и второй тап
// попадает в него, а не в основное значение.
// ============================================================================
static lv_timer_t* s_primary_tap_timer = nullptr;

static void primary_value_tap()
{
    if (s_last_mode == MODE_THREAD && !g_edit_param.active) {
        if (s_primary_tap_timer) {          // второй тап в окне 400 мс
            lv_timer_del(s_primary_tap_timer);
            s_primary_tap_timer = nullptr;
            show_thr_type_menu();
            return;
        }
        s_primary_tap_timer = lv_timer_create([](lv_timer_t* t) {
            s_primary_tap_timer = nullptr;
            lv_timer_del(t);
            enter_edit_mode();
        }, 400, nullptr);
        return;
    }
    if (g_edit_param.active) exit_edit_mode(); else enter_edit_mode();
}

// ============================================================================
// Вспомогательная функция создания ячейки параметра (общий стиль для K и I)
// ============================================================================
struct CellRefs { lv_obj_t* box; lv_obj_t* title; lv_obj_t* val; };

static CellRefs make_param_cell(lv_obj_t* parent, int x, int y, int w, int h,
                                 const char* title_text, bool editable = false,
                                 bool use_recolor = false)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(editable ? 0x120a00 : 0x0a1828), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(editable ? 0xffaa44 : 0x00d4ff), 0);
    lv_obj_set_style_border_width(box, US(3), 0);
    lv_obj_set_style_radius(box, US(4), 0);
    lv_obj_set_style_pad_all(box, US(4), 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // title: small label at top
    lv_obj_t* title = lv_label_create(box);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_text_font(title, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(editable ? 0xffaa44 : 0x6a9fb5), 0);
    lv_label_set_recolor(title, true);   // цвет букв осей (AXIS_X / AXIS_Z)
    lv_label_set_text(title, title_text);

    // val: vertically centered in remaining space below title
    // title_h≈16, val_h≈30, content=(h-8), remaining=(h-8-16-30)/2 (метрики шрифтов → US)
    int val_y = US(16) + ((h - US(8) - US(16) - US(30)) >> 1);
    if (val_y < US(16)) val_y = US(16);
    lv_obj_t* val = lv_label_create(box);
    lv_obj_set_pos(val, 0, val_y);
    lv_obj_set_style_text_font(val, &font_tahoma_bold_28, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xe0e0e0), 0);
    lv_label_set_text(val, "--");
    if (use_recolor) lv_label_set_recolor(val, true);

    return {box, title, val};
}

// ============================================================================
// Ввод и сброс осей с экрана (BACKLOG-027): двойной тап по ПОЗИЦИИ Y / X — ноль,
// долгое нажатие на Y / X / ⌀ — клавиатура. С блоком DRO STM32 шлёт AXIS_EDIT:0 —
// оси задаются на самом блоке, экран их только показывает.
// ============================================================================
// row1/row2 показывают оси (ПОЗИЦИЯ или ОСЬ Y/X) — по раскладкам apply_*_layout
static bool rows_show_axes()
{
    switch (uart_protocol.getData().select_menu) {
        case 1:  return s_last_mode != MODE_DIVIDER;                 // M8 (G-код) — тоже: ноль перед программой
        case 2:  return s_last_mode == MODE_FEED;
        case 3:  return s_last_mode != MODE_FEED && s_last_mode != MODE_RESERVE;
    }
    return false;
}

static bool axis_edit_allowed()
{
    if (uart_protocol.getData().axis_edit) return true;
    show_alert("\xD0\x9E\xD0\xA1\xD0\x98 \xD0\x97\xD0\x90\xD0\x94\xD0\x90\xD0\xAE\xD0\xA2\xD0\xA1\xD0\xAF "
               "\xD0\x9D\xD0\x90 \xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A\xD0\x95 DRO");   // ОСИ ЗАДАЮТСЯ НА БЛОКЕ DRO
    return false;
}

static void axis_send(char axis, int32_t um)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AXIS:%c:%ld", axis, (long)um);
    uart_protocol.sendButtonPress(cmd);
}

// Цифровая клавиатура (оси, настройки): значение — целое в единицах 10^-dec
// (мкм при dec = 3, 0.01 мм при dec = 2). Текущее значение видно сразу; первая цифра его заменяет.
typedef void (*NumpadDone)(int32_t v, intptr_t tag);
struct Numpad {
    lv_obj_t* back;       // затемнение на весь экран (nullptr — закрыта)
    lv_obj_t* val;        // набираемое значение
    const char* unit;
    uint8_t dec;          // знаков после точки
    uint8_t int_digits;   // цифр до точки
    bool sign;            // можно минус
    bool fresh;           // показано исходное значение — первая цифра его заменяет
    NumpadDone done;
    intptr_t tag;
    char buf[12];         // "-1234.567"
};
static Numpad g_numpad = {};

static void numpad_close()
{
    if (g_numpad.back) { lv_obj_del(g_numpad.back); g_numpad.back = nullptr; }
}

static void numpad_refresh()
{
    char t[24];
    snprintf(t, sizeof(t), "%s %s", g_numpad.buf[0] ? g_numpad.buf : "0", g_numpad.unit);
    lv_label_set_text(g_numpad.val, t);
    lv_obj_set_style_text_color(g_numpad.val, lv_color_hex(g_numpad.fresh ? 0x6a9fb5 : 0x00ff88), 0);
}

// "-12.3456" → целое в единицах 10^-dec (набор ограничивает число цифр)
static int32_t numpad_value()
{
    const char* p = g_numpad.buf;
    bool neg = (*p == '-'); if (neg) p++;
    int32_t ip = 0, frac = 0; int nf = 0;
    for (; *p && *p != '.'; p++) ip = ip * 10 + (*p - '0');
    if (*p == '.') for (p++; *p && nf < g_numpad.dec; p++, nf++) frac = frac * 10 + (*p - '0');
    for (; nf < g_numpad.dec; nf++) frac *= 10;
    int32_t scale = 1;
    for (int i = 0; i < g_numpad.dec; i++) scale *= 10;
    int32_t v = ip * scale + frac;
    return neg ? -v : v;
}

static void numpad_key(const char* k)
{
    char* b = g_numpad.buf;
    bool edit = !strcmp(k, "<") || !strcmp(k, ".") || (k[0] >= '0' && k[0] <= '9' && !k[1]);
    if (edit && g_numpad.fresh) b[0] = 0;          // первая цифра заменяет исходное значение
    if (edit || !strcmp(k, "+/-")) g_numpad.fresh = false;
    size_t n = strlen(b);
    const char* dot = strchr(b, '.');
    if (!strcmp(k, "<")) { if (n) b[n - 1] = 0; }
    else if (!strcmp(k, "+/-")) {
        if (!g_numpad.sign) return;
        if (b[0] == '-') memmove(b, b + 1, n);
        else if (n + 1 < sizeof(g_numpad.buf)) { memmove(b + 1, b, n + 1); b[0] = '-'; }
    }
    else if (!strcmp(k, ".")) { if (g_numpad.dec && !dot && n + 1 < sizeof(g_numpad.buf)) strcat(b, "."); }
    else if (k[0] >= '0' && k[0] <= '9' && !k[1]) {
        size_t digits = dot ? strlen(dot + 1) : n - (b[0] == '-');
        if (digits < (dot ? g_numpad.dec : g_numpad.int_digits) && n + 1 < sizeof(g_numpad.buf)) { b[n] = k[0]; b[n + 1] = 0; }
    }
    else if (k[0] == '\xD0' && k[1] == '\x92') {   // ВВОД
        bool has_digit = strpbrk(b, "0123456789") != nullptr;
        NumpadDone done = g_numpad.done; intptr_t tag = g_numpad.tag;
        int32_t v = numpad_value();
        numpad_close();
        if (has_digit && done) done(v, tag);
        return;
    }
    else { numpad_close(); return; }               // ОТМЕНА
    numpad_refresh();
}

// init — исходное значение текстом (nullptr — пусто, «0»)
static void numpad_show(const char* title, const char* unit, uint8_t dec, uint8_t int_digits, bool sign,
                        const char* init, NumpadDone done, intptr_t tag)
{
    numpad_close();
    g_numpad.unit = unit; g_numpad.dec = dec; g_numpad.int_digits = int_digits; g_numpad.sign = sign;
    g_numpad.done = done; g_numpad.tag = tag;
    g_numpad.fresh = init != nullptr;
    snprintf(g_numpad.buf, sizeof(g_numpad.buf), "%s", init ? init : "");

    lv_obj_t* back = lv_obj_create(lv_layer_top());
    g_numpad.back = back;
    lv_obj_set_size(back, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = lv_obj_create(back);
    lv_obj_set_size(panel, UX(320), UY(256));
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0a1622), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(panel, US(2), 0);
    lv_obj_set_style_radius(panel, US(8), 0);
    lv_obj_set_style_pad_all(panel, US(6), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(panel);
    lv_obj_set_style_text_font(t, &font_tahoma_bold_22, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_recolor(t, true);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    g_numpad.val = lv_label_create(panel);
    lv_obj_set_style_text_font(g_numpad.val, &font_tahoma_bold_28, 0);
    lv_obj_align(g_numpad.val, LV_ALIGN_TOP_RIGHT, 0, UY(26));
    numpad_refresh();

    static const char* map[] = {
        "7", "8", "9", "<", "\n",
        "4", "5", "6", "+/-", "\n",
        "1", "2", "3", "0", "\n",
        ".", "\xD0\x9E\xD0\xA2\xD0\x9C\xD0\x95\xD0\x9D\xD0\x90", "\xD0\x92\xD0\x92\xD0\x9E\xD0\x94", ""   // ОТМЕНА, ВВОД
    };
    lv_obj_t* m = lv_btnmatrix_create(panel);
    lv_btnmatrix_set_map(m, map);
    lv_btnmatrix_set_btn_width(m, 13, 2);              // ОТМЕНА
    lv_btnmatrix_set_btn_width(m, 14, 2);              // ВВОД
    if (!sign) lv_btnmatrix_set_btn_ctrl(m, 7, LV_BTNMATRIX_CTRL_DISABLED);    // +/-
    if (!dec)  lv_btnmatrix_set_btn_ctrl(m, 12, LV_BTNMATRIX_CTRL_DISABLED);   // .
    lv_obj_set_size(m, UX(306), UY(184));
    lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(m, &font_tahoma_bold_22, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, US(2), 0);
    lv_obj_set_style_pad_gap(m, US(4), 0);
    lv_obj_set_style_bg_color(m, lv_color_hex(0x1a2a3a), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(0xe0e0e0), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(0x3a4a5a), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_add_event_cb(m, [](lv_event_t* e) {
        lv_obj_t* o = lv_event_get_target(e);
        const char* k = lv_btnmatrix_get_btn_text(o, lv_btnmatrix_get_selected_btn(o));
        if (k) numpad_key(k);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void axis_kb_show(char axis)
{
    numpad_show(axis == 'D' ? TITLE_DIAM
              : axis == 'Z' ? "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z
                            : "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X,
                "\xD0\xBC\xD0\xBC", 3, 4, true, nullptr,                     // мм
                [](int32_t um, intptr_t a) { axis_send((char)a, um); }, axis);
}

// ============================================================================
// Создание панели ВАРИАНТ K: Featured cell + 3×2 grid + вертикальные кнопки
// ============================================================================
static void create_k_content(lv_obj_t* screen)
{
    lv_obj_t* panel = lv_obj_create(screen);
    lv_obj_set_size(panel, SCREEN_WIDTH, SCREEN_HEIGHT - UY(36));
    lv_obj_set_pos(panel, 0, UY(36));
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    g_ui.k_content = panel;

    const int PAD = UX(6), GAP = UX(4);
    const int RIGHT_W = UX(56);
    const int LEFT_W   = SCREEN_WIDTH - 2*PAD - RIGHT_W - GAP;
    const int INNER_H  = (SCREEN_HEIGHT - UY(36)) - 2*PAD;
    const int FEAT_H   = UY(72);
    const int GRID_Y   = PAD + FEAT_H + GAP;
    const int GRID_H   = INNER_H - FEAT_H - GAP;
    const int ROW_H    = (GRID_H - GAP) / 2;
    const int COL_W0   = (LEFT_W - 2*GAP) / 3;
    const int COL_W2   = LEFT_W - 2*COL_W0 - 2*GAP;
    const int RIGHT_X  = PAD + LEFT_W + GAP;
    int col_x[3] = { PAD, PAD + COL_W0 + GAP, PAD + 2*COL_W0 + 2*GAP };

    // ── Featured cell ────────────────────────────────────────────────────────
    lv_obj_t* feat = lv_obj_create(panel);
    lv_obj_set_size(feat, LEFT_W, FEAT_H);
    lv_obj_set_pos(feat, PAD, PAD);
    lv_obj_set_style_bg_color(feat, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_opa(feat, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(feat, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(feat, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(feat, US(4), 0);
    lv_obj_set_style_radius(feat, US(4), 0);
    lv_obj_set_style_pad_all(feat, 0, 0);
    lv_obj_clear_flag(feat, LV_OBJ_FLAG_SCROLLABLE);
    g_wk.primary_edit_box = feat;

    // Primary title/unit (tappable in M3 for THR_CAT)
    lv_obj_t* p_unit = lv_label_create(feat);
    lv_obj_set_pos(p_unit, UX(8), UY(5));
    lv_obj_set_style_text_font(p_unit, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(p_unit, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(p_unit, "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");  // ШАГ ММ
    lv_obj_add_flag(p_unit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p_unit, [](lv_event_t*) {
        static uint32_t s_utap = 0; uint32_t now = millis();
        if ((now - s_utap) < 400 && s_last_mode == MODE_THREAD) {
            if (g_edit_param.active) exit_edit_mode();
            uart_protocol.sendButtonPress("THR_CAT");
        }
        s_utap = now;
    }, LV_EVENT_CLICKED, nullptr);
    g_wk.primary_unit = p_unit;

    // Primary value (big, cyan, digit font) — vertically centered below title
    lv_obj_t* p_val = lv_label_create(feat);
    lv_obj_set_pos(p_val, UX(8), UY(28));
    lv_obj_set_style_text_font(p_val, &font_tahoma_bold_40, 0);
    lv_obj_set_style_text_color(p_val, lv_color_hex(0x00d4ff), 0);
    lv_label_set_text(p_val, "1.50");
    g_wk.primary_val      = p_val;
    g_wk.primary_val_glow = nullptr;

    // Thread tag (tpi/G/K — orange, right side, hidden by default)
    lv_obj_t* tag = lv_label_create(feat);
    lv_obj_set_style_text_color(tag, lv_color_hex(0xff8800), 0);
    lv_obj_set_style_text_font(tag, &font_tahoma_bold_28, 0);
    lv_label_set_text(tag, "");
    lv_obj_set_pos(tag, UX(200), UY(18));
    lv_obj_add_flag(tag, LV_OBJ_FLAG_HIDDEN);
    g_wk.thread_tag = tag;

    // Secondary unit label (right side) — same size as param cell titles
    lv_obj_t* s_unit = lv_label_create(feat);
    lv_obj_set_style_text_font(s_unit, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(s_unit, lv_color_hex(0x3a7a5a), 0);
    lv_label_set_text(s_unit, "RPM");
    lv_obj_align(s_unit, LV_ALIGN_TOP_RIGHT, -UX(8), UY(5));
    g_wk.secondary_unit = s_unit;

    // Secondary value (right side) — same font as param cell values, with gap below unit
    lv_obj_t* s_val = lv_label_create(feat);
    lv_obj_set_style_text_font(s_val, &font_tahoma_bold_28, 0);
    lv_obj_set_style_text_color(s_val, lv_color_hex(0x00ff88), 0);
    lv_label_set_text(s_val, "1650");
    lv_label_set_recolor(s_val, true);
    lv_obj_align(s_val, LV_ALIGN_TOP_RIGHT, -UX(8), UY(24));
    g_wk.secondary_val      = s_val;
    g_wk.secondary_val_glow = nullptr;


    // RPM bar — invisible stub (removed per user request; kept for API compat)
    lv_obj_t* rbar = lv_bar_create(feat);
    lv_obj_set_size(rbar, UX(1), UY(1));
    lv_obj_set_style_opa(rbar, LV_OPA_TRANSP, 0);
    lv_bar_set_range(rbar, 0, 3000);
    lv_bar_set_value(rbar, 0, LV_ANIM_OFF);
    g_wk.rpm_bar = rbar;

    // Edit arrows (hidden)
    lv_obj_t* arL = lv_label_create(feat);
    lv_obj_set_style_text_color(arL, lv_color_hex(0xffcc00), 0);
    lv_obj_set_style_text_font(arL, &lv_font_montserrat_20, 0);
    lv_label_set_text(arL, LV_SYMBOL_UP);
    lv_obj_set_pos(arL, UX(8), UY(20));
    lv_obj_add_flag(arL, LV_OBJ_FLAG_HIDDEN);
    g_wk.edit_arrow_left = arL;

    lv_obj_t* arR = lv_label_create(feat);
    lv_obj_set_style_text_color(arR, lv_color_hex(0xffcc00), 0);
    lv_obj_set_style_text_font(arR, &lv_font_montserrat_20, 0);
    lv_label_set_text(arR, LV_SYMBOL_DOWN);
    lv_obj_align(arR, LV_ALIGN_TOP_RIGHT, -UX(8), UY(20));
    lv_obj_add_flag(arR, LV_OBJ_FLAG_HIDDEN);
    g_wk.edit_arrow_right = arR;

    // Transparent tap zone — enter/exit primary edit + double-tap = thread type menu
    lv_obj_t* tap = lv_btn_create(feat);
    lv_obj_set_size(tap, LEFT_W - UX(100), FEAT_H - UY(8));
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_shadow_width(tap, 0, 0);
    lv_obj_add_event_cb(tap, [](lv_event_t*) { primary_value_tap(); }, LV_EVENT_CLICKED, nullptr);

    // ── Grid cells 3×2 ───────────────────────────────────────────────────────
    // Row 1: ПОЗИЦИЯ Z
    {
        auto r = make_param_cell(panel, col_x[0], GRID_Y, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
        g_wk.row1_box = r.box; g_wk.row1_title = r.title; g_wk.row1_val = r.val;
        lv_label_set_text(r.val, "+125.45");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0x00ff88), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)0);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_ztap = 0; uint32_t now = millis();
            bool dbl = (now - s_ztap) < 400; s_ztap = now;
            if (dbl && rows_show_axes()) {             // двойной тап — ноль оси Y
                exit_sub_edit();
                if (axis_edit_allowed()) axis_send('Z', 0);
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_SHORT_CLICKED, nullptr);
        lv_obj_add_event_cb(r.box, [](lv_event_t*) {   // долгое нажатие — ввод значения Y
            if (rows_show_axes() && axis_edit_allowed()) { exit_sub_edit(); axis_kb_show('Z'); }
        }, LV_EVENT_LONG_PRESSED, nullptr);
    }
    // Row 2: ПОЗИЦИЯ X
    {
        auto r = make_param_cell(panel, col_x[1], GRID_Y, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
        g_wk.row2_box = r.box; g_wk.row2_title = r.title; g_wk.row2_val = r.val;
        lv_label_set_text(r.val, "-42.78");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0xff5555), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)1);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_xtap = 0; uint32_t now = millis();
            bool dbl = (now - s_xtap) < 400; s_xtap = now;
            if (dbl && rows_show_axes()) {             // двойной тап — ноль оси X
                exit_sub_edit();
                if (axis_edit_allowed()) axis_send('X', 0);
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_SHORT_CLICKED, nullptr);
        lv_obj_add_event_cb(r.box, [](lv_event_t*) {   // долгое нажатие — ввод значения X
            if (rows_show_axes() && axis_edit_allowed()) { exit_sub_edit(); axis_kb_show('X'); }
        }, LV_EVENT_LONG_PRESSED, nullptr);
    }
    // Row 3: mode-specific param
    {
        auto r = make_param_cell(panel, col_x[0], GRID_Y + ROW_H + GAP, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");  // ПРОХОДЫ
        g_wk.row3_box = r.box; g_wk.row3_title = r.title; g_wk.row3_val = r.val;
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)2);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Row 4: secondary count — только отображение (переключение SM перенесено на row6)
    {
        auto r = make_param_cell(panel, col_x[1], GRID_Y + ROW_H + GAP, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB", false, true);  // ПРОХОДЫ
        g_wk.row4_box = r.box; g_wk.row4_title = r.title; g_wk.row4_val = r.val;
        lv_label_set_recolor(r.val, true);
    }
    // Spare1 (col2 bottom): tracked thread-type cell — shown in M3 only
    {
        auto r = make_param_cell(panel, col_x[2], GRID_Y + ROW_H + GAP, COL_W2, ROW_H,
                                  "\xD0\xA2\xD0\x98\xD0\x9F");  // ТИП
        g_wk.thr_type_box = r.box; g_wk.thr_type_title = r.title; g_wk.thr_type_val = r.val;
        lv_obj_set_style_border_color(r.box, lv_color_hex(0xffaa44), 0);
        lv_obj_set_style_text_color(r.title, lv_color_hex(0xffaa44), 0);
        lv_obj_set_style_text_color(r.val, lv_color_hex(0xffcc00), 0);
        lv_label_set_text(r.val, "--");
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);  // hidden until M3/M4/M5
        // Кликабельный — для M4/M5: sub-edit row 3 = СЪЁМ (Ap)
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r.box, [](lv_event_t*) {
            if (s_last_mode == MODE_THREAD) {
                if (g_edit_param.active) exit_edit_mode();
                show_thr_type_menu();
                return;
            }
            if (g_sub_edit.active && g_sub_edit.row == 3) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(3); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // ДИАМЕТР (Ø на LCD2004) — верх col2, в одном ряду с ПОЗИЦИЯ Y / X; двойной тап → PARAM_OK,
    // долгое нажатие — ввод диаметра (замерили деталь — ввели ⌀)
    {
        auto r = make_param_cell(panel, col_x[2], GRID_Y, COL_W2, ROW_H, TITLE_DIAM);
        g_wk.diam_unit = r.title; g_wk.diam_val = r.val; g_wk.diam_box = r.box;
        lv_label_set_text(r.val, "+0.00");
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r.box, [](lv_event_t*) {
            static uint32_t s_dtap = 0; uint32_t now = millis();
            bool dbl = (now - s_dtap) < 400; s_dtap = now;
            if (dbl) { exit_sub_edit(); uart_protocol.sendButtonPress("PARAM_OK"); }
        }, LV_EVENT_SHORT_CLICKED, nullptr);
        lv_obj_add_event_cb(r.box, [](lv_event_t*) {
            if (axis_edit_allowed()) { exit_sub_edit(); axis_kb_show('D'); }
        }, LV_EVENT_LONG_PRESSED, nullptr);
    }
    // SM-индикатор (название текущего SM, update_sm_row6) — в featured-панели между значением и RPM
    {
        lv_obj_t* dlbl = lv_label_create(feat);
        lv_obj_set_style_text_font(dlbl, &font_tahoma_bold_16, 0);
        lv_obj_set_style_text_color(dlbl, lv_color_hex(0x2a3a4a), 0);
        lv_label_set_long_mode(dlbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(dlbl, UX(130));
        lv_obj_set_style_text_align(dlbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(dlbl, LV_ALIGN_RIGHT_MID, -UX(100), 0);   // по центру панели при 1–3 строках
        lv_label_set_text(dlbl, "--");
        g_ui.sm_row6 = dlbl;
    }

    // ── Right vertical buttons ────────────────────────────────────────────────
    const int UP_H = UY(64), OK_H = UY(88), DN_H = UY(64);
    lv_obj_t* up_btn = lv_btn_create(panel);
    lv_obj_set_size(up_btn, RIGHT_W, UP_H);
    lv_obj_set_pos(up_btn, RIGHT_X, PAD);
    lv_obj_set_style_bg_color(up_btn, lv_color_hex(0x2a4a5a), 0);
    lv_obj_set_style_border_color(up_btn, lv_color_hex(0x3a6a8a), 0);
    lv_obj_set_style_border_width(up_btn, US(1), 0);
    lv_obj_set_style_radius(up_btn, US(4), 0);
    lv_obj_set_style_shadow_width(up_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(up_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_UP); }
    lv_obj_add_event_cb(up_btn, [](lv_event_t*) { handle_up_btn_click(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* ok_btn = lv_btn_create(panel);
    lv_obj_set_size(ok_btn, RIGHT_W, OK_H);
    lv_obj_set_pos(ok_btn, RIGHT_X, PAD + UP_H + GAP);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(ok_btn, US(1), 0);
    lv_obj_set_style_radius(ok_btn, US(4), 0);
    lv_obj_set_style_shadow_width(ok_btn, US(10), 0);
    lv_obj_set_style_shadow_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    { lv_obj_t* l = lv_label_create(ok_btn); lv_obj_center(l);
      lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
      lv_obj_set_style_text_font(l, &font_dejavu_14, 0);
      lv_label_set_text(l, "OK"); }
    lv_obj_add_event_cb(ok_btn, [](lv_event_t*) { handle_ok_btn_click(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* dn_btn = lv_btn_create(panel);
    lv_obj_set_size(dn_btn, RIGHT_W, DN_H);
    lv_obj_set_pos(dn_btn, RIGHT_X, PAD + UP_H + GAP + OK_H + GAP);
    lv_obj_set_style_bg_color(dn_btn, lv_color_hex(0x2a4a5a), 0);
    lv_obj_set_style_border_color(dn_btn, lv_color_hex(0x3a6a8a), 0);
    lv_obj_set_style_border_width(dn_btn, US(1), 0);
    lv_obj_set_style_radius(dn_btn, US(4), 0);
    lv_obj_set_style_shadow_width(dn_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(dn_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_DOWN); }
    lv_obj_add_event_cb(dn_btn, [](lv_event_t*) { handle_dn_btn_click(); }, LV_EVENT_CLICKED, nullptr);
}

// ============================================================================
// Меню настроек — оверлей (double-tap на иконку ⚠ в statusbar).
// Механика станка (BACKLOG-028): значения приходят от STM32 (<CFG:…>), правятся клавиатурой,
// СОХРАНИТЬ — CFG_SET (STM32 проверяет, применяет и пишет во флеш), ИЗ КОНФИГА — CFG_DEFAULT
// (значения из els_config.h). Ответ STM32 (CFG_RES) — в строке статуса.
// ============================================================================
// Индексы в LatheData::cfg: 0 энкодер, 1 шагов Z, 2 микрошаг Z, 3 винт Z (0.01 мм),
// 4 шагов X, 5 микрошаг X, 6 винт X, 7 люфт Z (мкм), 8 люфт X
struct CfgField { const char* name; int8_t idx_z, idx_x; uint8_t dec, digits; const char* unit; };
static const CfgField CFG_FIELDS[] = {
    { "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x92\xD0\x98\xD0\x9D\xD0\xA2\xD0\x90, \xD0\xBC\xD0\xBC", 3, 6, 2, 2, "\xD0\xBC\xD0\xBC" },   // ШАГ ВИНТА, мм
    { "\xD0\xA8\xD0\x90\xD0\x93\xD0\x9E\xD0\x92 \xD0\x9D\xD0\x90 \xD0\x9E\xD0\x91\xD0\x9E\xD0\xA0\xD0\x9E\xD0\xA2", 1, 4, 0, 4, "" },      // ШАГОВ НА ОБОРОТ
    { "\xD0\x9C\xD0\x98\xD0\x9A\xD0\xA0\xD0\x9E\xD0\xA8\xD0\x90\xD0\x93", 2, 5, 0, 3, "" },                                                  // МИКРОШАГ
    { "\xD0\x9B\xD0\xAE\xD0\xA4\xD0\xA2, \xD0\xBC\xD0\xBA\xD0\xBC", 7, 8, 0, 4, "\xD0\xBC\xD0\xBA\xD0\xBC" },                              // ЛЮФТ, мкм
    { "\xD0\xAD\xD0\x9D\xD0\x9A\xD0\x9E\xD0\x94\xD0\x95\xD0\xA0, \xD0\xBB\xD0\xB8\xD0\xBD\xD0\xB8\xD0\xB9", 0, -1, 0, 5, "" },           // ЭНКОДЕР, линий
};
struct CfgUi {
    int32_t   edit[9];       // правка до сохранения
    bool      dirty;         // есть несохранённые правки
    uint8_t   seen_seq;      // последний показанный CFG_RES
    lv_obj_t* val[9];
    lv_obj_t* status;
};
static CfgUi g_cfg = {};

static void cfg_fmt(char* b, size_t n, int idx, int32_t v)
{
    if (idx == 3 || idx == 6) snprintf(b, n, "%ld.%02ld", (long)(v / 100), (long)(v % 100));
    else snprintf(b, n, "%ld", (long)v);
}

static void cfg_set_status(const char* t, uint32_t color)
{
    lv_label_set_text(g_cfg.status, t);
    lv_obj_set_style_text_color(g_cfg.status, lv_color_hex(color), 0);
}

static void cfg_refresh()
{
    const LatheData& d = uart_protocol.getData();
    for (int i = 0; i < 9; i++) {
        if (!g_cfg.val[i]) continue;
        char b[16];
        if (!d.cfg_valid) { lv_label_set_text(g_cfg.val[i], "--"); continue; }
        cfg_fmt(b, sizeof(b), i, g_cfg.edit[i]);
        lv_label_set_text(g_cfg.val[i], b);
        lv_obj_set_style_text_color(g_cfg.val[i], lv_color_hex(g_cfg.edit[i] != d.cfg[i] ? 0xffcc00 : 0xe0e0e0), 0);
    }
}

static void cfg_open()
{
    const LatheData& d = uart_protocol.getData();
    memcpy(g_cfg.edit, d.cfg, sizeof(g_cfg.edit));
    g_cfg.dirty = false;
    g_cfg.seen_seq = d.cfg_res_seq;
    cfg_set_status(d.cfg_valid ? "" : "\xD0\x9D\xD0\x95\xD0\xA2 \xD0\x94\xD0\x90\xD0\x9D\xD0\x9D\xD0\xAB\xD0\xA5 \xD0\x9E\xD0\xA2 STM32",
                   0xff8800);                                            // НЕТ ДАННЫХ ОТ STM32
    cfg_refresh();
    if (g_ui.alert_box && !lv_obj_has_flag(g_ui.alert_box, LV_OBJ_FLAG_HIDDEN))
        dismiss_alert();                                   // подсказка Wi-Fi от первого тапа по ⚠
    lv_obj_clear_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
}

// Раз в 200 мс, пока меню открыто: новые значения от STM32 и ответ на сохранение
static void cfg_tick(lv_timer_t*)
{
    if (!g_ui.settings_menu || lv_obj_has_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN)) return;
    const LatheData& d = uart_protocol.getData();
    if (d.cfg_res_seq != g_cfg.seen_seq) {
        g_cfg.seen_seq = d.cfg_res_seq;
        bool ok = !strcmp(d.cfg_res, "OK");
        cfg_set_status(ok ? "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x95\xD0\x9D\xD0\x9E" : d.cfg_res,   // СОХРАНЕНО
                       ok ? 0x00ff88 : 0xff5555);
        if (ok) g_cfg.dirty = false;
    }
    if (!g_cfg.dirty && d.cfg_valid) memcpy(g_cfg.edit, d.cfg, sizeof(g_cfg.edit));
    cfg_refresh();
}

static void cfg_send_edit()
{
    if (!uart_protocol.getData().cfg_valid) return;
    const int32_t* v = g_cfg.edit;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "CFG_SET:%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld", (long)v[0], (long)v[1], (long)v[2],
             (long)v[3], (long)v[4], (long)v[5], (long)v[6], (long)v[7], (long)v[8]);
    uart_protocol.sendButtonPress(cmd);
    cfg_set_status("\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\xAF\xD0\x95\xD0\x9C...", 0x6a9fb5);   // СОХРАНЯЕМ...
}

static lv_obj_t* cfg_button(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg)
{
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x3a5a6a), 0);
    lv_obj_set_style_border_width(b, US(1), 0);
    lv_obj_set_style_radius(b, US(6), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, &font_tahoma_bold_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_label_set_text(l, text);
    return b;
}

// ============================================================================
// Калибровка шага винта и люфта по линейкам (BACKLOG-029): окно из «МЕХАНИКА СТАНКА».
// TOUCH CAL:<Z|X>:<мкм хода> — STM32 проезжает 3 раза туда-обратно и отвечает CAL_RES / CAL_ERR.
// ПРИМЕНИТЬ — шаг винта (округлённый до 0.01 мм, как хранит ядро) и люфт в настройки → CFG_SET.
// ============================================================================
// Два способа: 0 — по линейкам (станок меряет сам), 1 — по индикатору / штангенциркулю (без линеек):
// STM32 по шагам двигает ось (CAL_STEP), показания вводит оператор, результат считает экран.
struct CalUi {
    lv_obj_t* back;
    lv_obj_t* len_lbl[2];     // ход Y / X
    lv_obj_t* status;
    lv_obj_t* apply;
    int32_t   len_mm[2];
    uint8_t   seen_seq;
    lv_obj_t* tab[2];
    lv_obj_t* help;
    lv_obj_t* next;           // ДАЛЕЕ — по индикатору: индикатор обнулён
    int       method;
    int       ist;            // по индикатору: 0 — нет, 1 — люфт выбирается, 2 — ждём «обнулён», 3 — едет на L,
                              // 4 — ввод показания, 5 — возврат, 6 — ввод показания после возврата
    char      iax;
    int32_t   ilen_um, im1_um;
};
static CalUi g_cal = { nullptr, {nullptr, nullptr}, nullptr, nullptr, {20, 10}, 0 };

static void cal_method_refresh();
static void cal_send_step(int phase)
{
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "CAL_STEP:%c:%d:%ld", g_cal.iax, phase, (long)g_cal.ilen_um);
    uart_protocol.sendButtonPress(cmd);
}

static void cfg_send_edit();   // СОХРАНИТЬ из «МЕХАНИКА СТАНКА»

static void cal_close()
{
    if (g_cal.back) { lv_obj_del(g_cal.back); g_cal.back = nullptr; }
}

static void cal_status(const char* t, uint32_t color)
{
    lv_label_set_text(g_cal.status, t);
    lv_obj_set_style_text_color(g_cal.status, lv_color_hex(color), 0);
}

static void cal_len_refresh()
{
    for (int i = 0; i < 2; i++) {
        char b[16]; snprintf(b, sizeof(b), "%ld \xD0\xBC\xD0\xBC", (long)g_cal.len_mm[i]);   // мм
        lv_label_set_text(g_cal.len_lbl[i], b);
    }
}

// Ответы STM32 — раз в 200 мс, пока окно открыто
static void cal_tick(lv_timer_t*)
{
    if (!g_cal.back) return;
    const LatheData& d = uart_protocol.getData();
    if (d.cal_seq == g_cal.seen_seq) return;
    g_cal.seen_seq = d.cal_seq;
    char t[200];
    if (d.cal_state == 4) {                                   // по индикатору: фаза выполнена
        int ph = atoi(d.cal_text);
        if (ph == 1) {
            g_cal.ist = 2;
            cal_status("\xD0\x9E\xD0\xB1\xD0\xBD\xD1\x83\xD0\xBB\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xB8\xD0\xBD\xD0\xB4\xD0\xB8\xD0\xBA\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80 \xD0\xB8 \xD0\xBD\xD0\xB0\xD0\xB6\xD0\xBC\xD0\xB8\xD1\x82\xD0\xB5 \xD0\x94\xD0\x90\xD0\x9B\xD0\x95\xD0\x95", 0xffcc00);
            lv_obj_clear_flag(g_cal.next, LV_OBJ_FLAG_HIDDEN);
        } else if (ph == 2) {
            g_cal.ist = 4;
            numpad_show("\xD0\x9F\xD0\x9E\xD0\x9A\xD0\x90\xD0\x97\xD0\x90\xD0\x9D\xD0\x98\xD0\x95 \xD0\x98\xD0\x9D\xD0\x94\xD0\x98\xD0\x9A\xD0\x90\xD0\xA2\xD0\x9E\xD0\xA0\xD0\x90", "\xD0\xBC\xD0\xBC", 3, 3, false, nullptr, [](int32_t v, intptr_t) {
                g_cal.im1_um = v; g_cal.ist = 5;
                cal_status("\xD0\x92\xD0\xBE\xD0\xB7\xD0\xB2\xD1\x80\xD0\xB0\xD1\x82 \xD0\xBD\xD0\xB0 \xD1\x85\xD0\xBE\xD0\xB4...", 0x6a9fb5);
                cal_send_step(3);
            }, 0);
        } else if (ph == 3) {
            g_cal.ist = 6;
            numpad_show("\xD0\x9F\xD0\x9E\xD0\x9A\xD0\x90\xD0\x97\xD0\x90\xD0\x9D\xD0\x98\xD0\x95 \xD0\x9F\xD0\x9E\xD0\xA1\xD0\x9B\xD0\x95 \xD0\x92\xD0\x9E\xD0\x97\xD0\x92\xD0\xA0\xD0\x90\xD0\xA2\xD0\x90", "\xD0\xBC\xD0\xBC", 3, 2, true, nullptr, [](int32_t v, intptr_t) {
                g_cal.ist = 0;
                const LatheData& d = uart_protocol.getData();
                int zi = g_cal.iax == 'Z';
                int32_t screw_um = d.cfg[zi ? 3 : 6] * 10;              // шаг винта сейчас, мкм
                if (g_cal.ilen_um <= 0 || g_cal.im1_um <= 0) return;
                // проехали L по шагам — индикатор показал m1: реальный шаг винта = шаг × m1 / L
                int32_t screw_new = (int32_t)((int64_t)screw_um * g_cal.im1_um / g_cal.ilen_um);
                uart_protocol.setCalResult(g_cal.iax, screw_new, v < 0 ? -v : v);
            }, 0);
        }
        return;
    }
    if (d.cal_state != 4 && g_cal.ist && d.cal_state == 3) g_cal.ist = 0;
    if (d.cal_state == 1) {
        snprintf(t, sizeof(t), "\xD0\x98\xD0\x94\xD0\x81\xD0\xA2 \xD0\x9A\xD0\x90\xD0\x9B\xD0\x98\xD0\x91\xD0\xA0\xD0\x9E\xD0\x92\xD0\x9A\xD0\x90 %s: %s",   // ИДЁТ КАЛИБРОВКА Z: 1/3
                 d.cal_ax == 'Z' ? "Z" : "X", strchr(d.cal_text, ':') ? strchr(d.cal_text, ':') + 1 : "");
        cal_status(t, 0x6a9fb5);
        lv_obj_add_flag(g_cal.apply, LV_OBJ_FLAG_HIDDEN);
    } else if (d.cal_state == 3) {
        cal_status(d.cal_text, 0xff5555);
    } else if (d.cal_state == 2) {
        int zi = d.cal_ax == 'Z';
        int32_t cfg_screw = d.cfg[zi ? 3 : 6];                 // 0.01 мм
        int32_t new_screw = (d.cal_screw_um + 5) / 10;
        long dev = cfg_screw ? labs((long)d.cal_screw_um - cfg_screw * 10) * 1000 / (cfg_screw * 10) : 0;   // ‰
        snprintf(t, sizeof(t),
                 "%s: \xD0\xB2\xD0\xB8\xD0\xBD\xD1\x82 %ld.%03ld \xD0\xBC\xD0\xBC (\xD0\xB1\xD1\x8B\xD0\xBB\xD0\xBE %ld.%02ld, \xD0\xB1\xD1\x83\xD0\xB4\xD0\xB5\xD1\x82 %ld.%02ld), "
                 "\xD0\xBB\xD1\x8E\xD1\x84\xD1\x82 %ld \xD0\xBC\xD0\xBA\xD0\xBC%s",
                 zi ? "Z" : "X", (long)(d.cal_screw_um / 1000), (long)(d.cal_screw_um % 1000),
                 (long)(cfg_screw / 100), (long)(cfg_screw % 100), (long)(new_screw / 100), (long)(new_screw % 100),
                 (long)d.cal_bl_um,
                 dev > 200 ? "\n\xD0\x9E\xD0\xA2\xD0\x9B\xD0\x98\xD0\xA7\xD0\x90\xD0\x95\xD0\xA2\xD0\xA1\xD0\xAF \xD0\x91\xD0\x9E\xD0\x9B\xD0\x95\xD0\x95 20%: "
                             "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD1\x8C\xD1\x82\xD0\xB5 \xD0\xBB\xD0\xB8\xD0\xBD\xD0\xB5\xD0\xB9\xD0\xBA\xD1\x83 "
                             "(\xD0\xB4\xD0\xB8\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80/\xD1\x80\xD0\xB0\xD0\xB4\xD0\xB8\xD1\x83\xD1\x81)" : "");
        // «винт 1.920 мм (было 2.00, будет 1.92), люфт 80 мкм»; > 20 % — «отличается более 20%: проверьте линейку (диаметр/радиус)»
        cal_status(t, dev > 200 ? 0xffaa44 : 0x00ff88);
        lv_obj_clear_flag(g_cal.apply, LV_OBJ_FLAG_HIDDEN);
    }
}

static void cal_method_refresh()
{
    for (int i = 0; i < 2; i++) {
        bool on = g_cal.method == i;
        lv_obj_set_style_bg_color(g_cal.tab[i], lv_color_hex(on ? 0x0a3a2a : 0x10202c), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_cal.tab[i], 0), lv_color_hex(on ? 0x00ff88 : 0x6a9fb5), 0);
    }
    lv_label_set_text(g_cal.help, g_cal.method ? "\xD0\x98\xD0\xBD\xD0\xB4\xD0\xB8\xD0\xBA\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80 (\xD0\xB8\xD0\xBB\xD0\xB8 \xD1\x88\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB3\xD0\xB5\xD0\xBD\xD1\x86\xD0\xB8\xD1\x80\xD0\xBA\xD1\x83\xD0\xBB\xD1\x8C) \xD1\x83\xD0\xBF\xD0\xBE\xD1\x80\xD0\xBE\xD0\xBC \xD0\xB2 \xD0\xBA\xD0\xB0\xD1\x80\xD0\xB5\xD1\x82\xD0\xBA\xD1\x83 \xD0\xB8\xD0\xBB\xD0\xB8 \xD1\x81\xD1\x83\xD0\xBF\xD0\xBF\xD0\xBE\xD1\x80\xD1\x82. \xD0\x9E\xD1\x81\xD1\x8C \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xB5\xD1\x80\xD0\xB5\xD1\x82 \xD0\xBB\xD1\x8E\xD1\x84\xD1\x82, \xD0\xB2\xD1\x8B \xD0\xBE\xD0\xB1\xD0\xBD\xD1\x83\xD0\xBB\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xB8\xD0\xBD\xD0\xB4\xD0\xB8\xD0\xBA\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80, \xD0\xBE\xD1\x81\xD1\x8C \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB5\xD0\xB4\xD0\xB5\xD1\x82 \xD0\xB2\xD0\xBF\xD0\xB5\xD1\x80\xD1\x91\xD0\xB4 \xD0\xBD\xD0\xB0 \xD1\x85\xD0\xBE\xD0\xB4 \xD0\xB8 \xD0\xB2\xD0\xB5\xD1\x80\xD0\xBD\xD1\x91\xD1\x82\xD1\x81\xD1\x8F: \xD0\xB2\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBE\xD0\xB1\xD0\xB0 \xD0\xBF\xD0\xBE\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xBD\xD0\xB8\xD1\x8F." : "\xD0\xA3\xD0\xB1\xD0\xB5\xD1\x80\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xB3\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD0\xBA\xD1\x83 \xD0\xB8 \xD0\xBE\xD1\x82\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD1\x80\xD0\xB5\xD0\xB7\xD0\xB5\xD1\x86. \xD0\x9E\xD1\x81\xD1\x8C 3 \xD1\x80\xD0\xB0\xD0\xB7\xD0\xB0 \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB5\xD0\xB4\xD0\xB5\xD1\x82 \xD0\xB2\xD0\xBF\xD0\xB5\xD1\x80\xD1\x91\xD0\xB4 \xD0\xBD\xD0\xB0 \xD1\x85\xD0\xBE\xD0\xB4 + 1 \xD0\xBC\xD0\xBC \xD0\xB8 \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD0\xBD\xD0\xBE \xD0\xB8 \xD0\xB2\xD0\xB5\xD1\x80\xD0\xBD\xD1\x91\xD1\x82\xD1\x81\xD1\x8F \xD0\xBD\xD0\xB0 \xD0\xBC\xD0\xB5\xD1\x81\xD1\x82\xD0\xBE. \xD0\x9F\xD1\x80\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C: \xD0\xB4\xD0\xB6\xD0\xBE\xD0\xB9\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA\xD0\xBE\xD0\xBC \xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xA1\xD0\xA2\xD0\x9E\xD0\x9F.");
    lv_obj_add_flag(g_cal.next, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_cal.apply, LV_OBJ_FLAG_HIDDEN);
    cal_status("", 0x6a9fb5);
}

static void cal_show()
{
    cal_close();
    g_cal.seen_seq = uart_protocol.getData().cal_seq;
    lv_obj_t* back = lv_obj_create(lv_layer_top());
    g_cal.back = back;
    lv_obj_set_size(back, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(back);
    lv_obj_set_pos(title, UX(16), UY(6));
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title, "\xD0\x9A\xD0\x90\xD0\x9B\xD0\x98\xD0\x91\xD0\xA0\xD0\x9E\xD0\x92\xD0\x9A\xD0\x90");   // КАЛИБРОВКА

    // Способ: ПО ЛИНЕЙКАМ / ПО ИНДИКАТОРУ (без линеек и блока DRO — сразу по индикатору)
    static const char* TABS[2] = { "\xD0\x9F\xD0\x9E \xD0\x9B\xD0\x98\xD0\x9D\xD0\x95\xD0\x99\xD0\x9A\xD0\x90\xD0\x9C", "\xD0\x9F\xD0\x9E \xD0\x98\xD0\x9D\xD0\x94\xD0\x98\xD0\x9A\xD0\x90\xD0\xA2\xD0\x9E\xD0\xA0\xD0\xA3" };
    g_cal.method = uart_protocol.getData().pos_valid && uart_protocol.getData().pos_cfg[0] == 0 ? 1 : 0;
    g_cal.ist = 0;
    for (int i = 0; i < 2; i++) {
        lv_obj_t* b = cfg_button(back, TABS[i], 0x10202c, 0x6a9fb5);
        lv_obj_set_size(b, UX(136), UY(26)); lv_obj_set_pos(b, UX(188) + i * UX(140), UY(4));
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_dejavu_14, 0);
        g_cal.tab[i] = b;
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            g_cal.method = (int)(intptr_t)lv_event_get_user_data(e);
            g_cal.ist = 0;
            cal_method_refresh();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    g_cal.help = lv_label_create(back);
    lv_obj_set_width(g_cal.help, UX(448));
    lv_obj_set_pos(g_cal.help, UX(16), UY(36));
    lv_obj_set_style_text_font(g_cal.help, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(g_cal.help, lv_color_hex(0x9ab8c8), 0);
    lv_label_set_long_mode(g_cal.help, LV_LABEL_LONG_WRAP);

    const char* names[2] = { "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\x94\xD0\x9E\xD0\x9B\xD0\xAC\xD0\x9D\xD0\x90\xD0\xAF " AXIS_Z,   // ПРОДОЛЬНАЯ Y
                             "\xD0\x9F\xD0\x9E\xD0\x9F\xD0\x95\xD0\xA0\xD0\x95\xD0\xA7\xD0\x9D\xD0\x90\xD0\xAF " AXIS_X }; // ПОПЕРЕЧНАЯ X
    for (int i = 0; i < 2; i++) {
        int y = UY(104) + i * UY(36);
        lv_obj_t* n = lv_label_create(back);
        lv_obj_set_style_text_font(n, &font_dejavu_16, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0x9ab8c8), 0);
        lv_label_set_recolor(n, true);
        lv_label_set_text(n, names[i]);
        lv_obj_set_pos(n, UX(16), y + UY(7));
        // ход — правится клавиатурой
        lv_obj_t* lb = cfg_button(back, "--", 0x10202c, 0xe0e0e0);
        lv_obj_set_size(lb, UX(110), UY(32)); lv_obj_set_pos(lb, UX(190), y);
        g_cal.len_lbl[i] = lv_obj_get_child(lb, 0);
        lv_obj_set_style_text_font(g_cal.len_lbl[i], &font_tahoma_bold_22, 0);
        lv_obj_add_event_cb(lb, [](lv_event_t* e) {
            intptr_t i = (intptr_t)lv_event_get_user_data(e);
            char init[8]; snprintf(init, sizeof(init), "%ld", (long)g_cal.len_mm[i]);
            numpad_show("\xD0\xA5\xD0\x9E\xD0\x94, \xD0\xBC\xD0\xBC", "\xD0\xBC\xD0\xBC", 0, 3, false, init,   // ХОД, мм
                        [](int32_t v, intptr_t i) { g_cal.len_mm[i] = v < 5 ? 5 : v; cal_len_refresh(); }, i);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        // ПУСК
        lv_obj_t* go = cfg_button(back, "\xD0\x9F\xD0\xA3\xD0\xA1\xD0\x9A", 0x0a3a2a, 0x00ff88);   // ПУСК
        lv_obj_set_size(go, UX(150), UY(32)); lv_obj_set_pos(go, UX(314), y);
        lv_obj_add_event_cb(go, [](lv_event_t* e) {
            intptr_t i = (intptr_t)lv_event_get_user_data(e);
            lv_obj_add_flag(g_cal.apply, LV_OBJ_FLAG_HIDDEN);
            if (g_cal.method == 1) {                            // по индикатору: сначала выбрать люфт
                g_cal.iax = i ? 'X' : 'Z';
                g_cal.ilen_um = g_cal.len_mm[i] * 1000;
                g_cal.ist = 1;
                cal_send_step(1);
                cal_status("\xD0\x92\xD1\x8B\xD0\xB1\xD0\xB8\xD1\x80\xD0\xB0\xD1\x8E \xD0\xBB\xD1\x8E\xD1\x84\xD1\x82...", 0x6a9fb5);
                return;
            }
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "CAL:%c:%ld", i ? 'X' : 'Z', (long)g_cal.len_mm[i] * 1000);
            uart_protocol.sendButtonPress(cmd);
            cal_status("\xD0\x97\xD0\x90\xD0\x9F\xD0\xA3\xD0\xA1\xD0\x9A...", 0x6a9fb5);   // ЗАПУСК...
        }, LV_EVENT_CLICKED, (void*)i);
    }
    cal_len_refresh();

    g_cal.status = lv_label_create(back);
    lv_obj_set_width(g_cal.status, UX(448));
    lv_obj_set_pos(g_cal.status, UX(16), UY(176));
    lv_obj_set_style_text_font(g_cal.status, &font_dejavu_14, 0);
    lv_label_set_long_mode(g_cal.status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_cal.status, "");

    const int BY = UY(222), BH = UY(30), BW = UX(140);   // отступ от нижнего края: рамка подрезает
    g_cal.apply = cfg_button(back, "\xD0\x9F\xD0\xA0\xD0\x98\xD0\x9C\xD0\x95\xD0\x9D\xD0\x98\xD0\xA2\xD0\xAC", 0x0a3a2a, 0x00ff88);   // ПРИМЕНИТЬ
    lv_obj_set_size(g_cal.apply, BW, BH); lv_obj_set_pos(g_cal.apply, UX(16), BY);
    lv_obj_add_flag(g_cal.apply, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_cal.apply, [](lv_event_t*) {
        const LatheData& d = uart_protocol.getData();
        if (d.cal_state != 2 || !d.cfg_valid) return;
        int zi = d.cal_ax == 'Z';
        memcpy(g_cfg.edit, d.cfg, sizeof(g_cfg.edit));
        g_cfg.edit[zi ? 3 : 6] = (d.cal_screw_um + 5) / 10;      // шаг винта — 0.01 мм
        g_cfg.edit[zi ? 7 : 8] = d.cal_bl_um;                    // люфт
        g_cfg.dirty = true;
        cfg_send_edit();
        cal_close();                                             // ответ — в строке статуса «МЕХАНИКА СТАНКА»
    }, LV_EVENT_CLICKED, nullptr);

    g_cal.next = cfg_button(back, "\xD0\x94\xD0\x90\xD0\x9B\xD0\x95\xD0\x95", 0x0a3a2a, 0x00ff88);
    lv_obj_set_size(g_cal.next, BW, BH); lv_obj_set_pos(g_cal.next, UX(16), BY);
    lv_obj_add_flag(g_cal.next, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_cal.next, [](lv_event_t*) {         // индикатор обнулён — ехать на ход
        if (g_cal.ist != 2) return;
        g_cal.ist = 3;
        lv_obj_add_flag(g_cal.next, LV_OBJ_FLAG_HIDDEN);
        cal_status("\xD0\x95\xD0\xB4\xD1\x83 \xD0\xB2\xD0\xBF\xD0\xB5\xD1\x80\xD1\x91\xD0\xB4 \xD0\xBD\xD0\xB0 \xD1\x85\xD0\xBE\xD0\xB4...", 0x6a9fb5);
        cal_send_step(2);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* stop = cfg_button(back, "\xD0\xA1\xD0\xA2\xD0\x9E\xD0\x9F", 0x3a1010, 0xff7070);   // СТОП
    lv_obj_set_size(stop, BW, BH); lv_obj_set_pos(stop, UX(170), BY);
    lv_obj_add_event_cb(stop, [](lv_event_t*) { uart_protocol.sendButtonPress("CAL_STOP"); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* close_btn = cfg_button(back, "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC", 0x2a1a1a, 0xa08080);   // ЗАКРЫТЬ
    lv_obj_set_size(close_btn, BW, BH); lv_obj_set_pos(close_btn, UX(324), BY);
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) { cal_close(); }, LV_EVENT_CLICKED, nullptr);
    cal_method_refresh();
}

// ============================================================================
// Раздел «НАСТРОЙКИ» (тап по ⚠): механика, калибровка, Wi-Fi; внизу крупно — адрес точки доступа.
// Wi-Fi: сеть роутера задаётся здесь (хранится в памяти дисплея), поиск сетей, «забыть» — только точка доступа.
// ============================================================================
static void cfg_open();
struct HubUi { lv_obj_t* ov; lv_obj_t* info; lv_obj_t* addr; };
static HubUi g_hub = {};

static void hub_refresh()
{
    char b[96];
    const char* ip = GCodeWiFi_GetIP();
    if (!GCodeWiFi_IsAP() && !GCodeWiFi_IsConnected() && GCodeWiFi_GetStaSsid()[0]) {
        snprintf(b, sizeof(b), "%s %s...", "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBA", GCodeWiFi_GetStaSsid());
    } else if (GCodeWiFi_IsAP() || !GCodeWiFi_GetStaSsid()[0]) {
        snprintf(b, sizeof(b), "%s %s   %s %s", "\xD0\xA1\xD0\xB5\xD1\x82\xD1\x8C:", GCodeWiFi_GetApSsid(), "\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C:", GCodeWiFi_GetApPass());
    } else {
        snprintf(b, sizeof(b), "%s %s", "\xD0\xA0\xD0\xBE\xD1\x83\xD1\x82\xD0\xB5\xD1\x80:", GCodeWiFi_GetStaSsid());
    }
    lv_label_set_text(g_hub.info, b);
    snprintf(b, sizeof(b), "http://%s", (ip && *ip) ? ip : "...");
    lv_label_set_text(g_hub.addr, b);
}

static void hub_tick(lv_timer_t*)
{
    if (g_hub.ov && !lv_obj_has_flag(g_hub.ov, LV_OBJ_FLAG_HIDDEN)) hub_refresh();
}

static void hub_open()
{
    if (g_ui.alert_box && !lv_obj_has_flag(g_ui.alert_box, LV_OBJ_FLAG_HIDDEN)) dismiss_alert();
    hub_refresh();
    lv_obj_clear_flag(g_hub.ov, LV_OBJ_FLAG_HIDDEN);
}

// ── Wi-Fi: сеть роутера ─────────────────────────────────────────────────────
struct WifiUi { lv_obj_t* back; lv_obj_t* status; lv_obj_t* ssid; lv_obj_t* pass; lv_obj_t* kb; lv_obj_t* list; int shown_scan; };
static WifiUi g_wifi = {};

static void wifi_close()
{
    if (g_wifi.back) { lv_obj_del(g_wifi.back); g_wifi.back = nullptr; }
}

static void wifi_list_fill()
{
    lv_obj_clean(g_wifi.list);
    int n = GCodeWiFi_ScanCount();
    g_wifi.shown_scan = n;
    if (n < 0) { lv_list_add_text(g_wifi.list, "\xD0\x9F\xD0\xBE\xD0\xB8\xD1\x81\xD0\xBA \xD1\x81\xD0\xB5\xD1\x82\xD0\xB5\xD0\xB9..."); return; }
    if (n == 0) { lv_list_add_text(g_wifi.list, "\xD0\xA1\xD0\xB5\xD1\x82\xD0\xB8 \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B"); return; }
    for (int i = 0; i < n; i++) {
        lv_obj_t* b = lv_list_add_btn(g_wifi.list, nullptr, GCodeWiFi_ScanSsid(i));
        lv_obj_set_style_text_font(b, &font_dejavu_16, 0);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            lv_obj_t* btn = lv_event_get_target(e);
            lv_textarea_set_text(g_wifi.ssid, lv_list_get_btn_text(g_wifi.list, btn));
            lv_textarea_set_text(g_wifi.pass, "");
            lv_obj_add_state(g_wifi.pass, LV_STATE_FOCUSED);
            lv_obj_clear_state(g_wifi.ssid, LV_STATE_FOCUSED);
            lv_keyboard_set_textarea(g_wifi.kb, g_wifi.pass);
            lv_obj_add_flag(g_wifi.list, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, nullptr);
    }
}

static void wifi_tick(lv_timer_t*)
{
    if (!g_wifi.back) return;
    char b[96];
    const char* sta = GCodeWiFi_GetStaSsid();
    if (GCodeWiFi_IsConnected() && !GCodeWiFi_IsAP() && sta[0])
        snprintf(b, sizeof(b), "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xBE \xD0\xBA %s: http://%s", sta, GCodeWiFi_GetIP());
    else if (sta[0] && GCodeWiFi_IsAP())
        snprintf(b, sizeof(b), "%s %s %s", "\xD0\xA0\xD0\xBE\xD1\x83\xD1\x82\xD0\xB5\xD1\x80", sta, "\xD0\xBD\xD0\xB5\xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xB5\xD0\xBD, \xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xB0\xD0\xB5\xD1\x82 \xD1\x82\xD0\xBE\xD1\x87\xD0\xBA\xD0\xB0 \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xB0");
    else if (sta[0])
        snprintf(b, sizeof(b), "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBA %s...", sta);
    else
        snprintf(b, sizeof(b), "\xD0\xA0\xD0\xBE\xD1\x83\xD1\x82\xD0\xB5\xD1\x80 \xD0\xBD\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD0\xBD: \xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xB0\xD0\xB5\xD1\x82 \xD1\x82\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD1\x82\xD0\xBE\xD1\x87\xD0\xBA\xD0\xB0 \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xB0");
    lv_label_set_text(g_wifi.status, b);
    if (GCodeWiFi_ScanCount() != g_wifi.shown_scan) wifi_list_fill();
}

static lv_obj_t* wifi_ta(lv_obj_t* parent, int y, bool password)
{
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_max_length(ta, password ? 64 : 32);
    lv_obj_set_size(ta, UX(236), UY(30));
    lv_obj_set_pos(ta, UX(96), y);
    lv_obj_set_style_text_font(ta, &font_dejavu_16, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x10202c), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x3a5a6a), 0);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        lv_obj_t* t = lv_event_get_target(e);
        lv_keyboard_set_textarea(g_wifi.kb, t);
        lv_obj_add_flag(g_wifi.list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, nullptr);
    return ta;
}

static void wifi_show()
{
    wifi_close();
    lv_obj_t* back = lv_obj_create(lv_layer_top());
    g_wifi.back = back;
    lv_obj_set_size(back, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(back);
    lv_obj_set_pos(title, UX(16), UY(6));
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title, "WI-FI: \xD0\xA1\xD0\x95\xD0\xA2\xD0\xAC \xD0\xA0\xD0\x9E\xD0\xA3\xD0\xA2\xD0\x95\xD0\xA0\xD0\x90");

    lv_obj_t* close_btn = cfg_button(back, "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC", 0x2a1a1a, 0xa08080);
    lv_obj_set_size(close_btn, UX(110), UY(28)); lv_obj_set_pos(close_btn, UX(354), UY(4));
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) { wifi_close(); }, LV_EVENT_CLICKED, nullptr);

    g_wifi.status = lv_label_create(back);
    lv_obj_set_width(g_wifi.status, UX(448));
    lv_obj_set_pos(g_wifi.status, UX(16), UY(36));
    lv_obj_set_style_text_font(g_wifi.status, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(g_wifi.status, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_long_mode(g_wifi.status, LV_LABEL_LONG_DOT);

    const char* names[2] = { "\xD0\xA1\xD0\xB5\xD1\x82\xD1\x8C", "\xD0\x9F\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t* n = lv_label_create(back);
        lv_obj_set_style_text_font(n, &font_dejavu_16, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0x9ab8c8), 0);
        lv_label_set_text(n, names[i]);
        lv_obj_set_pos(n, UX(16), UY(60) + i * UY(34) + UY(6));
    }
    g_wifi.ssid = wifi_ta(back, UY(60), false);
    g_wifi.pass = wifi_ta(back, UY(94), true);
    lv_textarea_set_text(g_wifi.ssid, GCodeWiFi_GetStaSsid());

    lv_obj_t* scan = cfg_button(back, "\xD0\x9F\xD0\x9E\xD0\x98\xD0\xA1\xD0\x9A", 0x1a2a3a, 0x00d4ff);
    lv_obj_set_size(scan, UX(124), UY(30)); lv_obj_set_pos(scan, UX(340), UY(60));
    lv_obj_add_event_cb(scan, [](lv_event_t*) {
        GCodeWiFi_Scan();
        lv_obj_add_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wifi.list, LV_OBJ_FLAG_HIDDEN);
        wifi_list_fill();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* conn = cfg_button(back, "\xD0\x9F\xD0\x9E\xD0\x94\xD0\x9A\xD0\x9B\xD0\xAE\xD0\xA7\xD0\x98\xD0\xA2\xD0\xAC", 0x0a3a2a, 0x00ff88);
    lv_obj_set_size(conn, UX(124), UY(30)); lv_obj_set_pos(conn, UX(340), UY(94));
    lv_obj_set_style_text_font(lv_obj_get_child(conn, 0), &font_dejavu_14, 0);   // ПОДКЛЮЧИТЬ — длинное слово
    lv_obj_add_event_cb(conn, [](lv_event_t*) {
        GCodeWiFi_SetSta(lv_textarea_get_text(g_wifi.ssid), lv_textarea_get_text(g_wifi.pass));
        lv_obj_add_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    // ЗАБЫТЬ — раньше клавиатуры: открытая клавиатура её закрывает
    lv_obj_t* forget = cfg_button(back, "\xD0\x97\xD0\x90\xD0\x91\xD0\xAB\xD0\xA2\xD0\xAC (\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB6\xD0\xB0\xD1\x82\xD1\x8C)", 0x2a1a1a, 0xa08080);
    lv_obj_set_size(forget, UX(220), UY(28)); lv_obj_set_pos(forget, UX(16), UY(238));
    lv_obj_add_event_cb(forget, [](lv_event_t*) {               // держать — только точка доступа
        GCodeWiFi_SetSta("", "");
        lv_textarea_set_text(g_wifi.ssid, "");
        lv_textarea_set_text(g_wifi.pass, "");
    }, LV_EVENT_LONG_PRESSED, nullptr);

    // Нижняя часть: список найденных сетей или клавиатура (пока поле в фокусе)
    g_wifi.list = lv_list_create(back);
    lv_obj_set_size(g_wifi.list, UX(448), UY(100));
    lv_obj_set_pos(g_wifi.list, UX(16), UY(132));
    lv_obj_set_style_bg_color(g_wifi.list, lv_color_hex(0x0a1622), 0);
    lv_obj_set_style_text_font(g_wifi.list, &font_dejavu_16, 0);
    g_wifi.kb = lv_keyboard_create(back);
    lv_obj_set_size(g_wifi.kb, SCREEN_WIDTH, UY(136));
    lv_obj_align(g_wifi.kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(g_wifi.kb, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_wifi.kb, [](lv_event_t*) {          // ✓ или ✕ на клавиатуре — спрятать
        lv_obj_add_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wifi.list, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(g_wifi.kb, [](lv_event_t*) {
        lv_obj_add_flag(g_wifi.kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wifi.list, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CANCEL, nullptr);


    g_wifi.shown_scan = -2;
    wifi_tick(nullptr);
    if (GCodeWiFi_ScanCount() < 0) GCodeWiFi_Scan();   // первый поиск — сразу
}

// ── Линейки: источник координат, тип, разрешение, направление (BACKLOG-028) ──
// Сохранение — POS_SET: STM32 пишет во флеш и перезапускается (прерывания линеек настраиваются при старте).
struct ScalesUi { lv_obj_t* back; lv_obj_t* src[3]; lv_obj_t* val[8]; lv_obj_t* opt[2]; lv_obj_t* status; int32_t edit[12]; bool dirty; uint8_t seen_seq; };
static ScalesUi g_sc = {};
static const char* const SCALE_TYPES[6] = { "\xD0\x9D\xD0\x95\xD0\xA2", "\xD0\x9A\xD0\xB2\xD0\xB0\xD0\xB4\xD1\x80\xD0\xB0\xD1\x82\xD1\x83\xD1\x80\xD0\xB0", "Sylvac 48", "BIN6", "iGaging 21", "Digimatic" };

static void scales_close() { if (g_sc.back) { lv_obj_del(g_sc.back); g_sc.back = nullptr; } }

static void scales_refresh()
{
    const LatheData& d = uart_protocol.getData();
    for (int i = 0; i < 3; i++) {
        bool on = g_sc.edit[0] == i;
        lv_obj_set_style_bg_color(g_sc.src[i], lv_color_hex(on ? 0x0a3a2a : 0x10202c), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g_sc.src[i], 0), lv_color_hex(on ? 0x00ff88 : 0x6a9fb5), 0);
    }
    for (int ax = 0; ax < 2; ax++) {
        const int32_t* e = &g_sc.edit[1 + ax * 4];
        char b[24];
        lv_label_set_text(g_sc.val[ax * 4 + 0], SCALE_TYPES[e[0] >= 0 && e[0] < 6 ? e[0] : 0]);
        snprintf(b, sizeof(b), "%ld.%03ld", (long)(e[1] / 1000), (long)(e[1] % 1000));       // мкм на импульс
        lv_label_set_text(g_sc.val[ax * 4 + 1], b);
        lv_label_set_text(g_sc.val[ax * 4 + 2], e[2] ? "\xD0\x9E\xD0\x91\xD0\xA0\xD0\x90\xD0\xA2\xD0\x9D\xD0\x9E\xD0\x95" : "\xD0\x9F\xD0\xA0\xD0\xAF\xD0\x9C\xD0\x9E\xD0\x95");
        lv_label_set_text(g_sc.val[ax * 4 + 3], e[3] ? "\xD0\x98\xD0\x9D\xD0\x92\xD0\x95\xD0\xA0\xD0\xA1\xD0\x9D\xD0\xAB\xD0\x99" : "\xD0\x9F\xD0\xA0\xD0\xAF\xD0\x9C\xD0\x9E\xD0\x99");
        for (int k = 0; k < 4; k++)
            lv_obj_set_style_text_color(g_sc.val[ax * 4 + k], lv_color_hex(
                (d.pos_valid && e[k] != d.pos_cfg[1 + ax * 4 + k]) ? 0xffcc00 :
                g_sc.edit[0] == 2 ? 0xe0e0e0 : 0x5a6a7a), 0);
    }
    // ДОВОДКА X / КОНТРОЛЬ ШАГОВ (BACKLOG-025) — работают с блоком DRO и с линейками
    static const char* OPT[2] = { "\xD0\x94\xD0\x9E\xD0\x92\xD0\x9E\xD0\x94\xD0\x9A\xD0\x90 X", "\xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA2\xD0\xA0\xD0\x9E\xD0\x9B\xD0\xAC \xD0\xA8\xD0\x90\xD0\x93\xD0\x9E\xD0\x92" };
    for (int i = 0; i < 2; i++) {
        char b[48];
        bool on = g_sc.edit[9 + i] != 0;
        snprintf(b, sizeof(b), "%s: %s", OPT[i], on ? "\xD0\x92\xD0\x9A\xD0\x9B" : "\xD0\x92\xD0\xAB\xD0\x9A\xD0\x9B");
        lv_obj_t* l = lv_obj_get_child(g_sc.opt[i], 0);
        lv_label_set_text(l, b);
        bool changed = d.pos_valid && g_sc.edit[9 + i] != d.pos_cfg[9 + i];
        lv_obj_set_style_text_color(l, lv_color_hex(changed ? 0xffcc00 : g_sc.edit[0] == 0 ? 0x5a6a7a : on ? 0x00ff88 : 0x9ab8c8), 0);
    }
}

static void scales_tick(lv_timer_t*)
{
    if (!g_sc.back) return;
    const LatheData& d = uart_protocol.getData();
    if (d.cfg_res_seq != g_sc.seen_seq) {
        g_sc.seen_seq = d.cfg_res_seq;
        bool ok = !strcmp(d.cfg_res, "OK");
        lv_label_set_text(g_sc.status, ok ? "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x95\xD0\x9D\xD0\x9E, \xD0\x9F\xD0\x95\xD0\xA0\xD0\x95\xD0\x97\xD0\x90\xD0\x9F\xD0\xA3\xD0\xA1\xD0\x9A" : d.cfg_res);
        lv_obj_set_style_text_color(g_sc.status, lv_color_hex(ok ? 0x00ff88 : 0xff5555), 0);
        if (ok) g_sc.dirty = false;
    }
    if (!g_sc.dirty && d.pos_valid) memcpy(g_sc.edit, d.pos_cfg, sizeof(g_sc.edit));
    scales_refresh();
}

static void scales_changed() { g_sc.dirty = true; lv_label_set_text(g_sc.status, "\xD0\x9D\xD0\x95 \xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x95\xD0\x9D\xD0\x9E");
                               lv_obj_set_style_text_color(g_sc.status, lv_color_hex(0xffcc00), 0); scales_refresh(); }

static void scales_show()
{
    scales_close();
    const LatheData& d = uart_protocol.getData();
    memcpy(g_sc.edit, d.pos_cfg, sizeof(g_sc.edit));
    g_sc.dirty = false;
    g_sc.seen_seq = d.cfg_res_seq;
    lv_obj_t* back = lv_obj_create(lv_layer_top());
    g_sc.back = back;
    lv_obj_set_size(back, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(back);
    lv_obj_set_pos(title, UX(16), UY(6));
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title, "\xD0\x9A\xD0\x9E\xD0\x9E\xD0\xA0\xD0\x94\xD0\x98\xD0\x9D\xD0\x90\xD0\xA2\xD0\xAB \xD0\x98 \xD0\x9B\xD0\x98\xD0\x9D\xD0\x95\xD0\x99\xD0\x9A\xD0\x98");

    g_sc.status = lv_label_create(back);
    lv_obj_set_width(g_sc.status, UX(200));
    lv_obj_set_pos(g_sc.status, UX(264), UY(10));
    lv_obj_set_style_text_font(g_sc.status, &font_dejavu_14, 0);
    lv_obj_set_style_text_align(g_sc.status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_sc.status, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_sc.status, d.pos_valid ? "" : "\xD0\x9D\xD0\x95\xD0\xA2 \xD0\x94\xD0\x90\xD0\x9D\xD0\x9D\xD0\xAB\xD0\xA5 \xD0\x9E\xD0\xA2 STM32");
    lv_obj_set_style_text_color(g_sc.status, lv_color_hex(0xff8800), 0);

    // Источник координат: 3 кнопки
    static const char* SRC[3] = { "\xD0\x91\xD0\x95\xD0\x97 \xD0\x9B\xD0\x98\xD0\x9D\xD0\x95\xD0\x95\xD0\x9A", "\xD0\x91\xD0\x9B\xD0\x9E\xD0\x9A DRO", "\xD0\x9B\xD0\x98\xD0\x9D\xD0\x95\xD0\x99\xD0\x9A\xD0\x98" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = cfg_button(back, SRC[i], 0x10202c, 0x6a9fb5);
        lv_obj_set_size(b, UX(146), UY(30)); lv_obj_set_pos(b, UX(16) + i * UX(150), UY(38));
        g_sc.src[i] = b;
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            g_sc.edit[0] = (int)(intptr_t)lv_event_get_user_data(e); scales_changed();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    // Линейки по осям
    const int X_Z = UX(200), X_X = UX(336), COL_W = UX(128), Y0 = UY(88), DY = UY(26), RH = UY(24);
    const char* heads[2] = { "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\x94\xD0\x9E\xD0\x9B\xD0\xAC\xD0\x9D\xD0\x90\xD0\xAF " AXIS_Z, "\xD0\x9F\xD0\x9E\xD0\x9F\xD0\x95\xD0\xA0\xD0\x95\xD0\xA7\xD0\x9D\xD0\x90\xD0\xAF " AXIS_X };
    for (int c = 0; c < 2; c++) {
        lv_obj_t* h = lv_label_create(back);
        lv_obj_set_style_text_font(h, &font_dejavu_14, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x6a9fb5), 0);
        lv_label_set_recolor(h, true);
        lv_label_set_text(h, heads[c]);
        lv_obj_set_pos(h, c ? X_X : X_Z, UY(70));
    }
    static const char* ROWS[4] = { "\xD0\xA2\xD0\x98\xD0\x9F", "\xD0\x9C\xD0\x9A\xD0\x9C \xD0\x9D\xD0\x90 \xD0\x98\xD0\x9C\xD0\x9F\xD0\xA3\xD0\x9B\xD0\xAC\xD0\xA1", "\xD0\x9D\xD0\x90\xD0\x9F\xD0\xA0\xD0\x90\xD0\x92\xD0\x9B\xD0\x95\xD0\x9D\xD0\x98\xD0\x95", "\xD0\xA1\xD0\x98\xD0\x93\xD0\x9D\xD0\x90\xD0\x9B" };
    for (int r = 0; r < 4; r++) {
        lv_obj_t* n = lv_label_create(back);
        lv_obj_set_style_text_font(n, &font_dejavu_16, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0x9ab8c8), 0);
        lv_label_set_text(n, ROWS[r]);
        lv_obj_set_pos(n, UX(16), Y0 + r * DY + UY(5));
        for (int ax = 0; ax < 2; ax++) {
            lv_obj_t* b = cfg_button(back, "--", 0x10202c, 0xe0e0e0);
            lv_obj_set_size(b, COL_W, RH); lv_obj_set_pos(b, ax ? X_X : X_Z, Y0 + r * DY);
            g_sc.val[ax * 4 + r] = lv_obj_get_child(b, 0);
            lv_obj_add_event_cb(b, [](lv_event_t* e) {
                int k = (int)(intptr_t)lv_event_get_user_data(e);      // ось × 4 + строка
                int32_t& v = g_sc.edit[1 + k];
                switch (k % 4) {
                    case 0: v = (v + 1) % 6; scales_changed(); break;   // тип — по кругу
                    case 1: {                                          // разрешение, мкм на импульс
                        char init[16]; snprintf(init, sizeof(init), "%ld.%03ld", (long)(v / 1000), (long)(v % 1000));
                        numpad_show("\xD0\x9C\xD0\x9A\xD0\x9C \xD0\x9D\xD0\x90 \xD0\x98\xD0\x9C\xD0\x9F\xD0\xA3\xD0\x9B\xD0\xAC\xD0\xA1", "\xD0\xBC\xD0\xBA\xD0\xBC", 3, 3, false, init,
                                    [](int32_t nm, intptr_t k) { g_sc.edit[1 + k] = nm; scales_changed(); }, k);
                        break;
                    }
                    default: v = !v; scales_changed(); break;           // направление / сигнал
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)(ax * 4 + r));
        }
    }

    for (int i = 0; i < 2; i++) {                              // ДОВОДКА X / КОНТРОЛЬ ШАГОВ — тап переключает
        lv_obj_t* b = cfg_button(back, "--", 0x10202c, 0x9ab8c8);
        lv_obj_set_size(b, UX(220), UY(24)); lv_obj_set_pos(b, UX(16) + i * UX(228), UY(194));
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_dejavu_14, 0);
        g_sc.opt[i] = b;
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            int i = (int)(intptr_t)lv_event_get_user_data(e);
            g_sc.edit[9 + i] = !g_sc.edit[9 + i]; scales_changed();
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    const int BY = UY(222), BH = UY(30), BW = UX(140);
    lv_obj_t* save = cfg_button(back, "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x98\xD0\xA2\xD0\xAC", 0x0a3a2a, 0x00ff88);
    lv_obj_set_size(save, BW, BH); lv_obj_set_pos(save, UX(16), BY);
    lv_obj_add_event_cb(save, [](lv_event_t*) {
        const int32_t* v = g_sc.edit;
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "POS_SET:%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld", (long)v[0], (long)v[1],
                 (long)v[2], (long)v[3], (long)v[4], (long)v[5], (long)v[6], (long)v[7], (long)v[8],
                 (long)v[9], (long)v[10], (long)v[11]);
        uart_protocol.sendButtonPress(cmd);
        lv_label_set_text(g_sc.status, "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\xAF\xD0\x95\xD0\x9C...");
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* def = cfg_button(back, "\xD0\x98\xD0\x97 \xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA4\xD0\x98\xD0\x93\xD0\x90", 0x1a2a3a, 0x9ab8c8);
    lv_obj_set_size(def, BW, BH); lv_obj_set_pos(def, UX(170), BY);
    lv_obj_add_event_cb(def, [](lv_event_t*) { uart_protocol.sendButtonPress("POS_DEFAULT"); }, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(def, [](lv_event_t*) { lv_label_set_text(g_sc.status, "\xD0\x94\xD0\x95\xD0\xA0\xD0\x96\xD0\x98\xD0\xA2\xD0\x95 \xD0\x9D\xD0\x90\xD0\x96\xD0\x90\xD0\xA2\xD0\x9E\xD0\x99"); }, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_t* close_btn = cfg_button(back, "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC", 0x2a1a1a, 0xa08080);
    lv_obj_set_size(close_btn, BW, BH); lv_obj_set_pos(close_btn, UX(324), BY);
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) { scales_close(); }, LV_EVENT_CLICKED, nullptr);
    scales_refresh();
}

static void create_settings_hub(lv_obj_t* screen)
{
    lv_obj_t* ov = lv_obj_create(screen);
    g_hub.ov = ov;
    lv_obj_set_size(ov, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(ov);
    lv_obj_set_pos(title, UX(16), UY(6));
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title, "\xD0\x9D\xD0\x90\xD0\xA1\xD0\xA2\xD0\xA0\xD0\x9E\xD0\x99\xD0\x9A\xD0\x98");

    lv_obj_t* close_btn = cfg_button(ov, "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC", 0x2a1a1a, 0xa08080);
    lv_obj_set_size(close_btn, UX(110), UY(28)); lv_obj_set_pos(close_btn, UX(354), UY(4));
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) { lv_obj_add_flag(g_hub.ov, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);

    // Плитки 2×2
    struct Tile { const char* text; lv_event_cb_t cb; };
    static const Tile tiles[] = {
        { "\xD0\x9C\xD0\x95\xD0\xA5\xD0\x90\xD0\x9D\xD0\x98\xD0\x9A\xD0\x90",  [](lv_event_t*) { cfg_open(); } },
        { "\xD0\x9A\xD0\x90\xD0\x9B\xD0\x98\xD0\x91\xD0\xA0\xD0\x9E\xD0\x92\xD0\x9A\xD0\x90",   [](lv_event_t*) { cal_show(); } },
        { "WI-FI",  [](lv_event_t*) { wifi_show(); } },
        { "\xD0\x9B\xD0\x98\xD0\x9D\xD0\x95\xD0\x99\xD0\x9A\xD0\x98",[](lv_event_t*) { scales_show(); } },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = cfg_button(ov, tiles[i].text, 0x10202c, 0xe0e0e0);
        lv_obj_set_size(b, UX(220), UY(54));
        lv_obj_set_pos(b, UX(16) + (i % 2) * UX(228), UY(40) + (i / 2) * UY(62));
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_tahoma_bold_22, 0);
        lv_obj_add_event_cb(b, tiles[i].cb, LV_EVENT_CLICKED, nullptr);
    }

    g_hub.info = lv_label_create(ov);
    lv_obj_set_width(g_hub.info, UX(448));
    lv_obj_set_pos(g_hub.info, UX(16), UY(170));
    lv_obj_set_style_text_font(g_hub.info, &font_dejavu_16, 0);
    lv_obj_set_style_text_color(g_hub.info, lv_color_hex(0x9ab8c8), 0);
    lv_label_set_long_mode(g_hub.info, LV_LABEL_LONG_DOT);

    g_hub.addr = lv_label_create(ov);
    lv_obj_set_width(g_hub.addr, UX(448));
    lv_obj_set_pos(g_hub.addr, UX(16), UY(196));
    lv_obj_set_style_text_font(g_hub.addr, &font_tahoma_bold_28, 0);   // в 40 есть только цифры и пара букв
    lv_obj_set_style_text_color(g_hub.addr, lv_color_hex(0xffcc00), 0);
    lv_label_set_long_mode(g_hub.addr, LV_LABEL_LONG_DOT);

    lv_timer_create(hub_tick, 500, nullptr);
    lv_timer_create(scales_tick, 300, nullptr);
    lv_timer_create(wifi_tick, 300, nullptr);
}

static void create_settings_menu(lv_obj_t* screen)
{
    lv_obj_t* ov = lv_obj_create(screen);
    lv_obj_set_size(ov, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);   // непрозрачно: экран под ним не просвечивает
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    g_ui.settings_menu = ov;

    lv_obj_t* title = lv_label_create(ov);
    lv_obj_set_pos(title, UX(16), UY(6));
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title,
        "\xD0\x9C\xD0\x95\xD0\xA5\xD0\x90\xD0\x9D\xD0\x98\xD0\x9A\xD0\x90 "
        "\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9D\xD0\x9A\xD0\x90");                      // МЕХАНИКА СТАНКА

    g_cfg.status = lv_label_create(ov);
    lv_obj_set_width(g_cfg.status, UX(220));
    lv_obj_set_pos(g_cfg.status, UX(248), UY(10));
    lv_obj_set_style_text_font(g_cfg.status, &font_dejavu_14, 0);
    lv_obj_set_style_text_align(g_cfg.status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_cfg.status, LV_LABEL_LONG_DOT);
    lv_label_set_text(g_cfg.status, "");

    const int X_NAME = UX(16), X_Z = UX(200), X_X = UX(336), COL_W = UX(128), ROW_Y0 = UY(58), ROW_DY = UY(32), ROW_H = UY(29);
    // заголовки колонок: продольная — Y на экране (как ПОЗИЦИЯ Y), поперечная — X
    const char* heads[2] = { "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\x94\xD0\x9E\xD0\x9B\xD0\xAC\xD0\x9D\xD0\x90\xD0\xAF " AXIS_Z,   // ПРОДОЛЬНАЯ Y
                             "\xD0\x9F\xD0\x9E\xD0\x9F\xD0\x95\xD0\xA0\xD0\x95\xD0\xA7\xD0\x9D\xD0\x90\xD0\xAF " AXIS_X }; // ПОПЕРЕЧНАЯ X
    for (int c = 0; c < 2; c++) {
        lv_obj_t* h = lv_label_create(ov);
        lv_obj_set_style_text_font(h, &font_dejavu_14, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x6a9fb5), 0);
        lv_label_set_recolor(h, true);
        lv_label_set_text(h, heads[c]);
        lv_obj_set_pos(h, c ? X_X : X_Z, UY(40));
    }
    for (int r = 0; r < (int)(sizeof(CFG_FIELDS) / sizeof(CFG_FIELDS[0])); r++) {
        const CfgField& f = CFG_FIELDS[r];
        int y = ROW_Y0 + r * ROW_DY;
        lv_obj_t* n = lv_label_create(ov);
        lv_obj_set_style_text_font(n, &font_dejavu_16, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0x9ab8c8), 0);
        lv_label_set_text(n, f.name);
        lv_obj_set_pos(n, X_NAME, y + UY(6));
        for (int c = 0; c < 2; c++) {
            int idx = c ? f.idx_x : f.idx_z;
            if (idx < 0) continue;
            lv_obj_t* b = cfg_button(ov, "--", 0x10202c, 0xe0e0e0);
            lv_obj_set_size(b, COL_W, ROW_H);
            lv_obj_set_pos(b, c ? X_X : X_Z, y);
            g_cfg.val[idx] = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_font(g_cfg.val[idx], &font_tahoma_bold_22, 0);
            lv_obj_add_event_cb(b, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                if (!uart_protocol.getData().cfg_valid) return;
                const CfgField* f = nullptr;
                for (auto& x : CFG_FIELDS) if (x.idx_z == idx || x.idx_x == idx) f = &x;
                static char title[64];
                snprintf(title, sizeof(title), "%s%s", f->name,
                         f->idx_x < 0 ? "" : idx == f->idx_z ? " (" AXIS_Z ")" : " (" AXIS_X ")");
                char init[16];
                cfg_fmt(init, sizeof(init), idx, g_cfg.edit[idx]);
                numpad_show(title, f->unit, f->dec, f->digits, false, init, [](int32_t v, intptr_t i) {
                    g_cfg.edit[i] = v;
                    g_cfg.dirty = true;
                    cfg_set_status("\xD0\x9D\xD0\x95 \xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x95\xD0\x9D\xD0\x9E", 0xffcc00);   // НЕ СОХРАНЕНО
                    cfg_refresh();
                }, idx);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
        }
    }

    // СОХРАНИТЬ / ИЗ КОНФИГА / ЗАКРЫТЬ
    const int BY = UY(222), BH = UY(30), BW = UX(140);   // отступ от нижнего края: рамка подрезает
    lv_obj_t* save = cfg_button(ov, "\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\x98\xD0\xA2\xD0\xAC", 0x0a3a2a, 0x00ff88);   // СОХРАНИТЬ
    lv_obj_set_size(save, BW, BH); lv_obj_set_pos(save, UX(16), BY);
    lv_obj_add_event_cb(save, [](lv_event_t*) { cfg_send_edit(); }, LV_EVENT_CLICKED, nullptr);

    // КАЛИБРОВКА — справа от строки ЭНКОДЕР (BACKLOG-029)
    lv_obj_t* calb = cfg_button(ov, "\xD0\x9A\xD0\x90\xD0\x9B\xD0\x98\xD0\x91\xD0\xA0\xD0\x9E\xD0\x92\xD0\x9A\xD0\x90", 0x1a2a3a, 0x00d4ff);   // КАЛИБРОВКА
    lv_obj_set_size(calb, COL_W, ROW_H);
    lv_obj_set_pos(calb, X_X, ROW_Y0 + 4 * ROW_DY);
    lv_obj_add_event_cb(calb, [](lv_event_t*) { cal_show(); }, LV_EVENT_CLICKED, nullptr);
    lv_timer_create(cal_tick, 200, nullptr);

    lv_obj_t* def = cfg_button(ov, "\xD0\x98\xD0\x97 \xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA4\xD0\x98\xD0\x93\xD0\x90", 0x1a2a3a, 0x9ab8c8);   // ИЗ КОНФИГА
    lv_obj_set_size(def, BW, BH); lv_obj_set_pos(def, UX(170), BY);
    lv_obj_add_event_cb(def, [](lv_event_t*) {
        // долгое нажатие — чтобы не сбросить случайно
        uart_protocol.sendButtonPress("CFG_DEFAULT");
        g_cfg.dirty = false;
        cfg_set_status("\xD0\xA1\xD0\x9E\xD0\xA5\xD0\xA0\xD0\x90\xD0\x9D\xD0\xAF\xD0\x95\xD0\x9C...", 0x6a9fb5);   // СОХРАНЯЕМ...
    }, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(def, [](lv_event_t*) {
        cfg_set_status("\xD0\x94\xD0\x95\xD0\xA0\xD0\x96\xD0\x98\xD0\xA2\xD0\x95 \xD0\x9D\xD0\x90\xD0\x96\xD0\x90\xD0\xA2\xD0\x9E\xD0\x99", 0xffcc00);   // ДЕРЖИТЕ НАЖАТОЙ
    }, LV_EVENT_SHORT_CLICKED, nullptr);

    lv_obj_t* close_btn = cfg_button(ov, "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC", 0x2a1a1a, 0xa08080);   // ЗАКРЫТЬ
    lv_obj_set_size(close_btn, BW, BH); lv_obj_set_pos(close_btn, UX(324), BY);
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) {
        numpad_close();
        lv_obj_add_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    lv_timer_create(cfg_tick, 200, nullptr);

    // Тап по ⚠ — раздел НАСТРОЙКИ (механика, калибровка, Wi-Fi, линейки; адрес точки доступа)
    if (g_ui.warn_bg) {
        lv_obj_add_flag(g_ui.warn_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(g_ui.warn_bg, [](lv_event_t*) { hub_open(); }, LV_EVENT_CLICKED, nullptr);
    }
}

// ============================================================================
// Create UI - Design #8: Dark Theme Pro (480x272) - по DESIGN_8_SCREENS_480x272.html
// Layout: Statusbar(36px) | Content (варианты K или I) — полная ширина 480px
// ============================================================================

lv_obj_t* create_dark_pro_ui()
{
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0a0a0a), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // =========================================================
    // STATUSBAR — 480×36, bottom border 2px cyan
    // =========================================================
    lv_obj_t* statusbar = lv_obj_create(screen);
    lv_obj_set_size(statusbar, SCREEN_WIDTH, UY(36));
    lv_obj_set_pos(statusbar, 0, 0);
    lv_obj_set_style_bg_color(statusbar, lv_color_hex(0x0d2a3d), 0);
    lv_obj_set_style_radius(statusbar, 0, 0);
    lv_obj_set_style_pad_all(statusbar, 0, 0);
    lv_obj_set_style_border_side(statusbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(statusbar, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(statusbar, US(2), 0);
    lv_obj_set_style_shadow_width(statusbar, US(10), 0);
    lv_obj_set_style_shadow_color(statusbar, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_shadow_opa(statusbar, LV_OPA_30, 0);
    lv_obj_clear_flag(statusbar, LV_OBJ_FLAG_SCROLLABLE);

    // "M1 - ПОДАЧА" — Tahoma Bold 22  (Issue 1: was hardcoded M3)
    {
        lv_obj_t* mode_label = lv_label_create(statusbar);
        lv_obj_set_pos(mode_label, UX(15), UY(7));
        lv_obj_set_style_text_color(mode_label, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(mode_label, &font_tahoma_bold_22, 0);
        lv_label_set_text(mode_label, MODE_STRS[1]);  // M1 - ПОДАЧА
        g_ui.mode_lbl = mode_label;

        // Подрежим — справа от режима, мелким шрифтом
        lv_obj_t* submode_label = lv_label_create(statusbar);
        lv_obj_set_pos(submode_label, UX(210), UY(10));
        lv_obj_set_style_text_color(submode_label, lv_color_hex(0x4a7a8a), 0);
        lv_obj_set_style_text_font(submode_label, &font_dejavu_16, 0);
        lv_label_set_text(submode_label, "");  // обновится по UART
        g_ui.submode_lbl = submode_label;
    }

    // Иконка S2 (зелёная, активная)
    lv_obj_t* s2_ic = lv_obj_create(statusbar);
    lv_obj_set_size(s2_ic, US(24), US(24));
    lv_obj_set_pos(s2_ic, UX(396), UY(6));
    lv_obj_set_style_bg_color(s2_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_radius(s2_ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s2_ic, 0, 0);
    lv_obj_set_style_shadow_width(s2_ic, US(8), 0);
    lv_obj_set_style_shadow_color(s2_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_clear_flag(s2_ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* s2_lbl = lv_label_create(s2_ic);
    lv_obj_center(s2_lbl);
    lv_obj_set_style_text_color(s2_lbl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s2_lbl, &font_dejavu_12, 0);
    lv_label_set_text(s2_lbl, "S2");
    g_ui.s2_bg  = s2_ic;
    g_ui.s2_lbl = s2_lbl;

    // Issue 5: statusbar category indicator removed (category shown in M3 row1 instead)
    g_ui.thr_cat_lbl = nullptr;

    // Иконка ⚡ (зелёная, активная)
    lv_obj_t* pwr_ic = lv_obj_create(statusbar);
    lv_obj_set_size(pwr_ic, US(24), US(24));
    lv_obj_set_pos(pwr_ic, UX(424), UY(6));
    lv_obj_set_style_bg_color(pwr_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_radius(pwr_ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pwr_ic, 0, 0);
    lv_obj_set_style_shadow_width(pwr_ic, US(6), 0);
    lv_obj_set_style_shadow_color(pwr_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_clear_flag(pwr_ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* pwr_lbl = lv_label_create(pwr_ic);
    lv_obj_center(pwr_lbl);
    lv_obj_set_style_text_color(pwr_lbl, lv_color_hex(0x000000), 0);
    lv_label_set_text(pwr_lbl, LV_SYMBOL_CHARGE);  // LVGL symbol font (не DejaVu!)
    g_ui.pwr_bg = pwr_ic;
    // Тап по ⚡ — экранный джойстик (раньше — двойной тап по ПОЗИЦИЯ X, теперь там ноль оси; BACKLOG-023)
    lv_obj_add_flag(pwr_ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(pwr_ic, US(8));
    lv_obj_add_event_cb(pwr_ic, [](lv_event_t*) {
        if (g_joystick.active) hide_joystick_overlay();
        else                    show_joystick_overlay();
    }, LV_EVENT_CLICKED, nullptr);

    // Иконка ⚠ (серая, неактивная)
    lv_obj_t* warn_ic = lv_obj_create(statusbar);
    lv_obj_set_size(warn_ic, US(24), US(24));
    lv_obj_set_pos(warn_ic, UX(452), UY(6));
    lv_obj_set_style_bg_color(warn_ic, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(warn_ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(warn_ic, 0, 0);
    lv_obj_clear_flag(warn_ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* warn_lbl = lv_label_create(warn_ic);
    lv_obj_center(warn_lbl);
    lv_obj_set_style_text_color(warn_lbl, lv_color_hex(0x666666), 0);
    lv_label_set_text(warn_lbl, LV_SYMBOL_WARNING);  // LVGL symbol font (не DejaVu!)
    g_ui.warn_bg = warn_ic;

    // =========================================================
    // Индикаторы лимитов в statusbar — стрелки (← → ↑ ↓)
    // Z-ось (← Left, → Right): голубой; X-ось (↑ Front, ↓ Rear): зелёный
    // SET → яркий цвет, NOT SET → тёмный
    // =========================================================
    {
        struct LimInfo { int x; const char* sym; lv_obj_t** field; };
        LimInfo lims[] = {
            {UX(312), LV_SYMBOL_LEFT,  &g_ui.lim_L},  // Z- ←
            {UX(332), LV_SYMBOL_RIGHT, &g_ui.lim_R},  // Z+ →
            {UX(352), LV_SYMBOL_UP,   &g_ui.lim_F},  // X+ ↑
            {UX(372), LV_SYMBOL_DOWN, &g_ui.lim_B},  // X- ↓
        };
        for (auto& li : lims) {
            lv_obj_t* lbl = lv_label_create(statusbar);
            lv_obj_set_pos(lbl, li.x, UY(8));
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x1a3a4a), 0);  // NOT SET (тёмный)
            lv_label_set_text(lbl, li.sym);
            *li.field = lbl;
        }
    }

    // =========================================================
    // ALERT OVERLAY — всплывающее уведомление (скрыт по умолчанию)
    // Создаётся здесь на screen, показывается поверх всего
    // =========================================================
    {
        lv_obj_t* abox = lv_obj_create(screen);
        lv_obj_set_size(abox, UX(400), UY(110));
        lv_obj_align(abox, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(abox, lv_color_hex(0x1a0a00), 0);
        lv_obj_set_style_bg_opa(abox, LV_OPA_90, 0);
        lv_obj_set_style_border_color(abox, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_border_width(abox, US(2), 0);
        lv_obj_set_style_radius(abox, US(8), 0);
        lv_obj_set_style_shadow_width(abox, US(16), 0);
        lv_obj_set_style_shadow_color(abox, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_shadow_opa(abox, LV_OPA_50, 0);
        lv_obj_clear_flag(abox, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(abox, LV_OBJ_FLAG_HIDDEN);
        g_ui.alert_box = abox;

        // Иконка предупреждения
        lv_obj_t* aicon = lv_label_create(abox);
        lv_obj_set_pos(aicon, UX(10), UY(10));
        lv_obj_set_style_text_font(aicon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(aicon, lv_color_hex(0xff8800), 0);
        lv_label_set_text(aicon, LV_SYMBOL_WARNING);

        // Текст алерта (2 строки, кириллица)
        lv_obj_t* amsg = lv_label_create(abox);
        lv_obj_set_pos(amsg, UX(50), UY(12));
        lv_obj_set_width(amsg, UX(340));           // высота — по тексту, окно подстраивается в show_alert
        lv_obj_set_height(amsg, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(amsg, &font_tahoma_bold_22, 0);
        lv_obj_set_style_text_color(amsg, lv_color_hex(0xffcc44), 0);
        lv_label_set_long_mode(amsg, LV_LABEL_LONG_WRAP);
        lv_label_set_text(amsg, "");
        g_ui.alert_msg = amsg;

        // Кнопка OK для немедленного закрытия
        lv_obj_t* aok = lv_btn_create(abox);
        lv_obj_set_size(aok, UX(80), UY(28));
        lv_obj_align(aok, LV_ALIGN_BOTTOM_RIGHT, -UX(8), -UY(8));
        lv_obj_set_style_bg_color(aok, lv_color_hex(0x2a1800), 0);
        lv_obj_set_style_border_color(aok, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_border_width(aok, US(1), 0);
        lv_obj_set_style_radius(aok, US(4), 0);
        lv_obj_set_style_shadow_width(aok, 0, 0);
        lv_obj_t* aok_lbl = lv_label_create(aok);
        lv_obj_center(aok_lbl);
        lv_obj_set_style_text_font(aok_lbl, &font_dejavu_16, 0);
        lv_obj_set_style_text_color(aok_lbl, lv_color_hex(0xff8800), 0);
        lv_label_set_text(aok_lbl, "OK");
        lv_obj_add_event_cb(aok, [](lv_event_t*) {
            dismiss_alert();
            uart_protocol.sendButtonPress("ALERT_OK");  // Сообщить Arduino: закрыть алерт
        }, LV_EVENT_CLICKED, nullptr);
    }

    // =========================================================
    // PHASE-2 TEST BAR — тонкая полоска поверх statusbar
    // Показывает номер и название тестового сценария
    // Скрыта по умолчанию; тест-режим делает её видимой
    // =========================================================
    {
        lv_obj_t* tbar = lv_obj_create(screen);
        lv_obj_set_size(tbar, SCREEN_WIDTH, UY(20));
        lv_obj_set_pos(tbar, 0, 0);
        lv_obj_set_style_bg_color(tbar, lv_color_hex(0x500000), 0);
        lv_obj_set_style_bg_opa(tbar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tbar, 0, 0);
        lv_obj_set_style_radius(tbar, 0, 0);
        lv_obj_set_style_pad_all(tbar, 0, 0);
        lv_obj_clear_flag(tbar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tbar, LV_OBJ_FLAG_HIDDEN);
        g_ui.test_bar = tbar;

        lv_obj_t* tlbl = lv_label_create(tbar);
        lv_obj_align(tlbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(tlbl, &font_dejavu_14, 0);
        lv_obj_set_style_text_color(tlbl, lv_color_hex(0xff8888), 0);
        lv_label_set_text(tlbl, "TEST MODE");
        g_ui.test_lbl = tlbl;
    }

    // =========================================================
    // CONTENT PANEL (компоновка K — единственная; вариант I убран)
    // =========================================================
    create_k_content(screen);
    create_settings_hub(screen);     // раздел НАСТРОЙКИ — под окном механики
    create_settings_menu(screen);
    apply_layout_widgets(g_wk);


    // =========================================================
    // TAP ZONE на statusbar — открывает меню выбора режима
    // Прозрачная кнопка поверх текста режима (левые 200px)
    // =========================================================
    {
        lv_obj_t* tap = lv_btn_create(statusbar);
        lv_obj_set_size(tap, UX(200), UY(36));
        lv_obj_set_pos(tap, 0, 0);
        lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tap, 0, 0);
        lv_obj_set_style_shadow_width(tap, 0, 0);
        lv_obj_add_event_cb(tap, [](lv_event_t*) {
            lv_obj_clear_flag(g_ui.mode_menu, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // =========================================================
    // MODE OVERLAY — полноэкранный оверлей выбора режима
    // Показывается поверх всего при тапе на режим в статусбаре
    // =========================================================
    {
        lv_obj_t* ov = lv_obj_create(screen);
        lv_obj_set_size(ov, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_pos(ov, 0, 0);
        lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
        lv_obj_set_style_border_width(ov, 0, 0);
        lv_obj_set_style_radius(ov, 0, 0);
        lv_obj_set_style_pad_all(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        g_ui.mode_menu = ov;

        // Заголовок: ВЫБОР РЕЖИМА
        lv_obj_t* title = lv_label_create(ov);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UY(10));
        lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
        lv_label_set_text(title,
            "\xD0\x92\xD0\xAB\xD0\x91\xD0\x9E\xD0\xA0 "
            "\xD0\xA0\xD0\x95\xD0\x96\xD0\x98\xD0\x9C\xD0\x90");  // ВЫБОР РЕЖИМА

        // 8 кнопок режимов: 2 колонки × 4 строки
        for (int i = 0; i < 8; i++) {
            int col = i % 2;
            int row = i / 2;
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, UX(218), UY(38));
            lv_obj_set_pos(btn, UX(12) + col * UX(235), UY(44) + row * UY(47));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a4060), 0);
            lv_obj_set_style_border_width(btn, US(1), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            g_ui.mode_btns[i] = btn;

            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xc0d0e0), 0);
            lv_label_set_text(lbl, MODE_STRS[i + 1]);

            lv_obj_set_user_data(btn, (void*)(intptr_t)(i + 1));
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                int mode_num = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                lv_obj_add_flag(g_ui.mode_menu, LV_OBJ_FLAG_HIDDEN);
                // Сбросить режимы редактирования при смене режима
                if (g_roller.active)     close_roller(false);
                if (g_edit_param.active) exit_edit_mode();
                if (g_sub_edit.active)   exit_sub_edit();

                // Локальное мгновенное обновление UI (не ждём подтверждения от Arduino).
                // setModeOptimistic синхронизирует data_.mode, иначе каждый POS_Z update
                // откатывал бы s_last_mode к старому значению через update_ui_values.
                LatheMode new_mode = (LatheMode)mode_num;
                if (new_mode != s_last_mode) {
                    uart_protocol.setModeOptimistic(new_mode);
                    s_last_mode = new_mode;
                    apply_mode_layout(new_mode);
                    if (mode_num >= 1 && mode_num <= 8)
                        lv_label_set_text(g_ui.mode_lbl, MODE_STRS[mode_num]);
                }

                // Отправить на Arduino → он сменит режим и пришлёт обновлённые данные
                char buf[16];
                snprintf(buf, sizeof(buf), "M%d", mode_num);
                uart_protocol.sendButtonPress(buf);  // → <TOUCH:M1>..<TOUCH:M8>
            }, LV_EVENT_CLICKED, nullptr);
        }

        // Кнопка ОТМЕНА
        lv_obj_t* cancel = lv_btn_create(ov);
        lv_obj_set_size(cancel, UX(150), UY(32));
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -UY(8));
        lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2a1a1a), 0);
        lv_obj_set_style_border_color(cancel, lv_color_hex(0x604040), 0);
        lv_obj_set_style_border_width(cancel, US(1), 0);
        lv_obj_set_style_radius(cancel, US(6), 0);
        lv_obj_set_style_shadow_width(cancel, 0, 0);
        lv_obj_t* cancel_lbl = lv_label_create(cancel);
        lv_obj_center(cancel_lbl);
        lv_obj_set_style_text_font(cancel_lbl, &font_tahoma_bold_22, 0);
        lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xa08080), 0);
        lv_label_set_text(cancel_lbl,
            "\xD0\x9E\xD0\xA2\xD0\x9C\xD0\x95\xD0\x9D\xD0\x90");  // ОТМЕНА
        lv_obj_add_event_cb(cancel, [](lv_event_t*) {
            lv_obj_add_flag(g_ui.mode_menu, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // =========================================================
    // JOYSTICK OVERLAY (US-013) — управление осями через тачскрин
    // Открывается тапом по ⚡ в шапке, закрывается кнопкой ✕ или повторным тапом
    // Авто-закрытие через 30 секунд бездействия
    // =========================================================
    {
        lv_obj_t* ov = lv_obj_create(screen);
        lv_obj_set_size(ov, UX(450), UY(215));
        lv_obj_align(ov, LV_ALIGN_CENTER, 0, UY(5));
        lv_obj_set_style_bg_color(ov, lv_color_hex(0x00080f), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
        lv_obj_set_style_border_color(ov, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_border_width(ov, US(2), 0);
        lv_obj_set_style_radius(ov, US(8), 0);
        lv_obj_set_style_pad_all(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        g_ui.joystick_overlay = ov;

        // Заголовок: ДЖОЙСТИК
        lv_obj_t* title = lv_label_create(ov);
        lv_obj_set_pos(title, UX(10), UY(7));
        lv_obj_set_style_text_color(title, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
        // "ДЖОЙСТИК" = Д-Ж-О-Й-С-Т-И-К
        lv_label_set_text(title,
            "\xD0\x94\xD0\x96\xD0\x9E\xD0\x99\xD0\xA1\xD0\xA2\xD0\x98\xD0\x9A");

        // Кнопка ✕ закрыть (top-right)
        lv_obj_t* close_btn = lv_btn_create(ov);
        lv_obj_set_size(close_btn, UX(32), UY(28));
        lv_obj_set_pos(close_btn, UX(413), UY(5));
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2a1010), 0);
        lv_obj_set_style_border_color(close_btn, lv_color_hex(0x884444), 0);
        lv_obj_set_style_border_width(close_btn, US(1), 0);
        lv_obj_set_style_radius(close_btn, US(4), 0);
        lv_obj_set_style_shadow_width(close_btn, 0, 0);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_obj_center(close_lbl);
        lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_20, 0);  // LV_SYMBOL — только в Montserrat
        lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xff6666), 0);
        lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
        lv_obj_add_event_cb(close_btn, [](lv_event_t*) {
            hide_joystick_overlay();
        }, LV_EVENT_CLICKED, nullptr);

        // =====================
        // Крест управления осями (левая часть, X=10..240, Y=38..195)
        // ↑/↓ = ось X (поперечный суппорт), ←/→ = ось Z (продольная)
        // =====================
        const int cx = UX(10);   // начало X-области
        const int cy = UY(38);   // начало Y-области
        const int bw = UX(72);   // ширина кнопки
        const int bh = UY(50);   // высота кнопки
        const int bx = cx + bw + UX(5);   // центральная X позиция (для ↑ и ↓)
        const int by = cy + bh + UY(5);   // центральная Y позиция (для ← и →)

        // Общий стиль кнопок-стрелок
        auto make_dir_btn = [&](const char* txt, int x, int y,
                                 const char* cmd) -> lv_obj_t* {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, bw, bh);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00d4ff), 0);
            lv_obj_set_style_border_width(btn, US(2), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);  // стрелки LV_SYMBOL_* есть только в Montserrat
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x00d4ff), 0);
            lv_label_set_text(lbl, txt);
            lv_obj_set_user_data(btn, (void*)cmd);
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                const char* c = (const char*)lv_obj_get_user_data(lv_event_get_target(e));
                g_joystick.last_ms = millis();
                uart_protocol.sendButtonPress(c);
            }, LV_EVENT_CLICKED, nullptr);
            return btn;
        };

        // ↑ X+ (верх)
        make_dir_btn(LV_SYMBOL_UP,    bx,        cy,      "JOY:UP");
        // ← Z- (лево)
        make_dir_btn(LV_SYMBOL_LEFT,  cx,        by,      "JOY:LEFT");
        // → Z+ (право)
        make_dir_btn(LV_SYMBOL_RIGHT, bx + bw + UX(5), by,    "JOY:RIGHT");
        // ↓ X- (низ)
        make_dir_btn(LV_SYMBOL_DOWN,  bx,        by + bh + UY(5), "JOY:DOWN");

        // СТОП (центр креста)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, bw, bh);
            lv_obj_set_pos(btn, bx, by);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a0808), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xff4444), 0);
            lv_obj_set_style_border_width(btn, US(2), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xff4444), 0);
            // "СТОП" = С-Т-О-П
            lv_label_set_text(lbl,
                "\xD0\xA1\xD0\xA2\xD0\x9E\xD0\x9F");
            lv_obj_add_event_cb(btn, [](lv_event_t*) {
                g_joystick.last_ms = millis();
                uart_protocol.sendButtonPress("JOY:STOP");
            }, LV_EVENT_CLICKED, nullptr);
        }

        // =====================
        // Правая панель кнопок (X=250..440, Y=38..195)
        // Кнопки: БЫСТРО, UP, DOWN, OK
        // =====================
        const int rx = UX(255);   // начало правой панели
        const int rbw = UX(180);  // ширина правых кнопок
        const int rbh = UY(38);   // высота правых кнопок

        // БЫСТРО (toggle rapid mode)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, rbw, rbh);
            lv_obj_set_pos(btn, rx, UY(38));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1f0a), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00aa44), 0);
            lv_obj_set_style_border_width(btn, US(2), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x00cc55), 0);
            // "БЫСТРО" = Б-Ы-С-Т-Р-О
            lv_label_set_text(lbl,
                "\xD0\x91\xD1\x8B\xD1\x81\xD1\x82\xD1\x80\xD0\xBE");
            g_ui.joy_rapid_btn = btn;
            g_ui.joy_rapid_lbl = lbl;
            lv_obj_add_event_cb(btn, [](lv_event_t*) {
                g_joystick.last_ms = millis();
                g_joystick.rapid = !g_joystick.rapid;
                if (g_joystick.rapid) {
                    lv_obj_set_style_bg_color(g_ui.joy_rapid_btn, lv_color_hex(0x005500), 0);
                    lv_obj_set_style_border_color(g_ui.joy_rapid_btn, lv_color_hex(0x00ff44), 0);
                    uart_protocol.sendButtonPress("JOY:RAPID_ON");
                } else {
                    lv_obj_set_style_bg_color(g_ui.joy_rapid_btn, lv_color_hex(0x0a1f0a), 0);
                    lv_obj_set_style_border_color(g_ui.joy_rapid_btn, lv_color_hex(0x00aa44), 0);
                    uart_protocol.sendButtonPress("JOY:RAPID_OFF");
                }
            }, LV_EVENT_CLICKED, nullptr);
        }

        // UP (KEY:UP — навигация/увеличить параметр)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, rbw, rbh);
            lv_obj_set_pos(btn, rx, UY(84));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a6080), 0);
            lv_obj_set_style_border_width(btn, US(1), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x80c0e0), 0);
            lv_label_set_text(lbl, "UP");
            lv_obj_add_event_cb(btn, [](lv_event_t*) {
                g_joystick.last_ms = millis();
                uart_protocol.sendButtonPress("KEY:UP");
            }, LV_EVENT_CLICKED, nullptr);
        }

        // DOWN (KEY:DN — навигация/уменьшить параметр)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, rbw, rbh);
            lv_obj_set_pos(btn, rx, UY(130));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a6080), 0);
            lv_obj_set_style_border_width(btn, US(1), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x80c0e0), 0);
            lv_label_set_text(lbl, "DOWN");
            lv_obj_add_event_cb(btn, [](lv_event_t*) {
                g_joystick.last_ms = millis();
                uart_protocol.sendButtonPress("KEY:DN");
            }, LV_EVENT_CLICKED, nullptr);
        }

        // OK (PARAM_OK)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, rbw, rbh);
            lv_obj_set_pos(btn, rx, UY(170));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x00192a), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00d4ff), 0);
            lv_obj_set_style_border_width(btn, US(2), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x00d4ff), 0);
            lv_label_set_text(lbl, "OK");
            lv_obj_add_event_cb(btn, [](lv_event_t*) {
                g_joystick.last_ms = millis();
                uart_protocol.sendButtonPress("PARAM_OK");
            }, LV_EVENT_CLICKED, nullptr);
        }
    }

    // =========================================================
    // THREAD TYPE MENU — компактный оверлей быстрого перехода к типу резьбы
    // Показывается на double-tap по основному значению в M3
    // =========================================================
    {
        lv_obj_t* ov = lv_obj_create(screen);
        lv_obj_set_size(ov, UX(260), UY(220));
        lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
        lv_obj_set_style_border_color(ov, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_border_width(ov, US(2), 0);
        lv_obj_set_style_radius(ov, US(8), 0);
        lv_obj_set_style_pad_all(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        g_ui.thr_type_menu = ov;

        // Заголовок: ТИП РЕЗЬБЫ
        lv_obj_t* title = lv_label_create(ov);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UY(8));
        lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
        lv_label_set_text(title,
            "\xD0\xA2\xD0\x98\xD0\x9F \xD0\xA0\xD0\x95\xD0\x97\xD0\xAC\xD0\x91\xD0\xAB");  // ТИП РЕЗЬБЫ

        // 4 кнопки типов резьбы
        struct ThrTypeEntry { const char* label; int first_idx; };
        static const ThrTypeEntry thr_types[] = {
            {"\xD0\x9C\xD0\xB5\xD1\x82\xD1\x80\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB0\xD1\x8F", THREAD_IDX_METRIC},  // Метрическая
            {"\xD0\x94\xD1\x8E\xD0\xB9\xD0\xBC\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x8F",                          THREAD_IDX_INCH},    // Дюймовая
            {"G-\xD1\x82\xD1\x80\xD1\x83\xD0\xB1",                                                          THREAD_IDX_GPIPE},   // G-труб
            {"K-\xD1\x82\xD1\x80\xD1\x83\xD0\xB1",                                                          THREAD_IDX_KPIPE},   // K-труб
        };

        for (int i = 0; i < 4; i++) {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, UX(220), UY(32));
            lv_obj_set_pos(btn, UX(20), UY(38) + i * UY(36));   // последняя кончается выше кнопки ОТМЕНА
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a4060), 0);
            lv_obj_set_style_border_width(btn, US(1), 0);
            lv_obj_set_style_radius(btn, US(6), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_22, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xc0d0e0), 0);
            lv_label_set_text(lbl, thr_types[i].label);
            lv_obj_set_user_data(btn, (void*)(intptr_t)(thr_types[i].first_idx));
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                int idx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                lv_obj_add_flag(g_ui.thr_type_menu, LV_OBJ_FLAG_HIDDEN);
                jump_to_thread_type(idx);
            }, LV_EVENT_CLICKED, nullptr);
        }

        // Кнопка ОТМЕНА
        lv_obj_t* cancel = lv_btn_create(ov);
        lv_obj_set_size(cancel, UX(150), UY(28));
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -UY(6));
        lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2a1a1a), 0);
        lv_obj_set_style_border_color(cancel, lv_color_hex(0x604040), 0);
        lv_obj_set_style_border_width(cancel, US(1), 0);
        lv_obj_set_style_radius(cancel, US(6), 0);
        lv_obj_set_style_shadow_width(cancel, 0, 0);
        lv_obj_t* cancel_lbl = lv_label_create(cancel);
        lv_obj_center(cancel_lbl);
        lv_obj_set_style_text_font(cancel_lbl, &font_tahoma_bold_22, 0);
        lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(0xa08080), 0);
        lv_label_set_text(cancel_lbl,
            "\xD0\x9E\xD0\xA2\xD0\x9C\xD0\x95\xD0\x9D\xD0\x90");  // ОТМЕНА
        lv_obj_add_event_cb(cancel, [](lv_event_t*) {
            lv_obj_add_flag(g_ui.thr_type_menu, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Инициализировать layout для начального режима (M1 - ПОДАЧА, как и на Arduino)
    s_last_mode = MODE_FEED;
    apply_mode_layout(MODE_FEED);

    lv_scr_load(screen);
    return screen;
}

// ============================================================================
// M2 SelectMenu=2: Подэкран параметров делителя (Текущий угол / Делений / Метка / Угол сектора)
// Обновляет только заголовки строк и подпись основного значения.
// ============================================================================
static void apply_afeed_sm2_layout()
{
    // Основное значение → текущий угол шпинделя
    lv_label_set_text(g_ui.primary_unit,
        "\xD0\xA3\xD0\x93\xD0\x9E\xD0\x9B \xC2\xB0");              // УГОЛ °
    // row1: Делений (Total_Tooth) — read-only
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x94\xD0\x95\xD0\x9B\xD0\x95\xD0\x9D\xD0\x98\xD0\x99");  // ДЕЛЕНИЙ
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    // row2: Метка (Current_Tooth) — read-only
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9C\xD0\x95\xD0\xA2\xD0\x9A\xD0\x90");                   // МЕТКА
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    // row3: Угол сектора = 360*(current_tooth-1)/total_tooth — read-only
    lv_label_set_text(g_ui.row3_title,
        "\xD0\xA3\xD0\x93.\xD0\xA1\xD0\x95\xD0\x9A\xD0\xA2\xD0\x9E\xD0\xA0");  // УГ.СЕКТОР
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// M1 SelectMenu=2: Подэкран позиции (Диаметр / Ось X / Ось Z)
// ============================================================================
static void apply_feed_sm2_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC " AXIS_Z);   // ОСЬ Y (ДИАМЕТР — постоянная плитка рядом)
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC " AXIS_X);                                   // ОСЬ X
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title, "");
}

// ============================================================================
// M1 SelectMenu=3: Подэкран отскок/натяг (OTSKOK_Z / TENSION_Z)
// ============================================================================
static void apply_feed_sm3_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x9E\xD0\xA2\xD0\xA1\xD0\x9A\xD0\x9E\xD0\x9A " AXIS_Z);         // ОТСКОК Z
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9E\xD0\xA1\xD0\x9B\xD0\x90\xD0\x91\xD0\x9B. \xD0\x9D\xD0\x90\xD0\xA2\xD0\xAF\xD0\x93\xD0\x90 " AXIS_Z);  // ОСЛАБЛ. НАТЯГА Y (LCD «Ослабление натяга Y»)
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title, "---");
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x555555), 0);
}

// ============================================================================
// M3 SelectMenu=2: Подэкран параметров резьбы (чист.пр / заходов / ход мм)
// ============================================================================
static void apply_thread_sm2_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\xA7\xD0\x98\xD0\xA1\xD0\xA2.\xD0\x9F\xD0\xA0");           // ЧИСТ.ПР
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x97\xD0\x90\xD0\xA5\xD0\x9E\xD0\x94\xD0\x9E\xD0\x92");    // ЗАХОДОВ
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title,
        "\xD0\xA5\xD0\x9E\xD0\x94 \xD0\x9C\xD0\x9C");                    // ХОД ММ
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// M4/M5 SelectMenu=2: Подэкран конуса (тип конуса / коническая резьба)
// ============================================================================
static void apply_cone_sm2_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA3\xD0\xA1");                    // КОНУС
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9A.\xD0\xA0\xD0\x95\xD0\x97\xD0\xAC\xD0\x91\xD0\x90");   // К.РЕЗЬБА
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title, "---");
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x555555), 0);
}

// ============================================================================
// M6 SelectMenu=2: Подэкран шара (ширина резца / шаг оси Z)
// ============================================================================
static void apply_sphere_sm2_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\xA8\xD0\x98\xD0\xA0\xD0\x98\xD0\x9D\xD0\x90 \xD0\xA0\xD0\x95\xD0\x97\xD0\xA6\xD0\x90");  // ШИРИНА РЕЗЦА
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9E\xD0\xA1\xD0\x98 " AXIS_Z);             // ШАГ ОСИ Z
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title,
        "\xD0\x9D\xD0\x9E\xD0\x96\xD0\x9A\xD0\x90 " DIAM_SIGN);       // НОЖКА ⌀ (LCD «Ножка:Ø», диаметр)
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// SM=3 Ввод/Сброс Осей: общий layout для M2/M3/M4/M5/M6/M7 (не M1)
// ============================================================================
static void apply_common_sm3_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC " AXIS_Z);   // ОСЬ Y (ДИАМЕТР — постоянная плитка рядом)
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC " AXIS_X);                                        // ОСЬ X
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title, "");
}

// ============================================================================
// Меняет подписи/заголовки UI при смене режима.
// Вызывается из update_ui_values только когда mode изменился.
// ============================================================================
static void apply_mode_layout(LatheMode mode)
{
    // M8: имя файла — в узкой колонке слева (по центру подсказка о нуле); в остальных режимах — по тексту
    lv_obj_set_width(g_ui.primary_unit, mode == MODE_RESERVE ? UX(165) : LV_SIZE_CONTENT);
    lv_label_set_long_mode(g_ui.primary_unit, mode == MODE_RESERVE ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    // M8 (G-код) — в основном поле слова (ГОТОВ, ПАУЗА…): tahoma 40 содержит только цифры и пару букв
    lv_obj_set_style_text_font(g_ui.primary_val, mode == MODE_RESERVE ? &font_tahoma_bold_28 : &font_tahoma_bold_40, 0);
    // Выходим из редактирования если активно (скрывает стрелки и восстанавливает цвет)
    if (g_edit_param.active) exit_edit_mode();

    // Скрыть индикаторы типа резьбы при смене режима (показываются только в M3)
    if (g_ui.thread_tag)   lv_obj_add_flag(g_ui.thread_tag,   LV_OBJ_FLAG_HIDDEN);
    if (g_ui.thr_cat_lbl)  lv_obj_add_flag(g_ui.thr_cat_lbl,  LV_OBJ_FLAG_HIDDEN);
    if (g_ui.thr_type_box) lv_obj_add_flag(g_ui.thr_type_box, LV_OBJ_FLAG_HIDDEN);

    // ── Подпись под основным большим значением (cyan) ─────────────────────────
    switch (mode) {
        case MODE_THREAD:
            lv_label_set_text(g_ui.primary_unit,
                "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");              // ШАГ ММ
            break;
        case MODE_FEED:
        case MODE_AFEED:
        case MODE_CONE_L:
        case MODE_CONE_R:
            lv_label_set_text(g_ui.primary_unit,
                "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0");     // Подача
            break;
        case MODE_SPHERE:
            lv_label_set_text(g_ui.primary_unit,
                "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0");          // Подача
            break;
        case MODE_DIVIDER:
            lv_label_set_text(g_ui.primary_unit,
                "\xD0\xA3\xD0\x93\xD0\x9E\xD0\x9B \xC2\xB0");              // УГОЛ °
            break;
        case MODE_RESERVE:                                         // M8 — G-код (BACKLOG-001)
            lv_label_set_text(g_ui.primary_unit, "G-\xD0\x9A\xD0\x9E\xD0\x94");
            break;
        default:
            lv_label_set_text(g_ui.primary_unit, "---");
            break;
    }

    // ── Подсветка активного режима в меню ────────────────────────────────────
    for (int i = 0; i < 8; i++) {
        if (!g_ui.mode_btns[i]) continue;
        bool active = ((int)mode == i + 1);
        lv_obj_set_style_bg_color(g_ui.mode_btns[i],
            active ? lv_color_hex(0x003a5a) : lv_color_hex(0x0d2233), 0);
        lv_obj_set_style_border_color(g_ui.mode_btns[i],
            active ? lv_color_hex(0x00d4ff) : lv_color_hex(0x1a4060), 0);
        lv_obj_set_style_border_width(g_ui.mode_btns[i], active ? US(2) : US(1), 0);
    }

    // ── Подпись вторичного значения + заголовки строк правой панели ──────────
    // Правило: row1/row2 = ПОЗ Z/X для большинства режимов, для M7 = ДЕЛЕНИЙ/МЕТКА
    // row3 = редактируемый параметр режима (или read-only для ШАР/РЕЗЕРВ)
    // Сброс цвета secondary_unit до дефолтного (переопределяется в FEED/AFEED)
    lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
    switch (mode) {
        case MODE_FEED:
        case MODE_AFEED:
            // Вторичное = RPM (обороты)
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);  // read-only: dim blue
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);  // read-only: dim blue
            // row3: СЪЁМ (Ap) — редактируемый (жёлтое название)
            lv_label_set_text(g_ui.row3_title,
                mode == MODE_AFEED ? "\xD0\xA1\xD0\xAA\xD0\x81\xD0\x9C R" : "\xD0\xA1\xD0\xAA\xD0\x81\xD0\x9C " DIAM_SIGN);                            // СЪЁМ R (M2, LCD «Съём R», на радиус) / СЪЁМ ⌀ (M1, «Съём Ø», на диаметр)
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0xffaa44), 0);
            // row4: ПРОХОДЫ — read-only (синий)
            lv_label_set_text(g_ui.row4_title,
                "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");  // ПРОХОДЫ
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            break;

        case MODE_THREAD:
            // Вторичное = RPM (всегда для всех режимов)
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
            // row1 = ПОЗИЦИЯ Z (всегда; дабл-тап — ноль оси, категория резьбы — плиткой ТИП)
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            // row3: ОБ/МИН — read-only
            lv_label_set_text(g_ui.row3_title,
                "\xD0\x9E\xD0\x91/\xD0\x9C\xD0\x98\xD0\x9D");                   // ОБ/МИН
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
            // row4: ЦИКЛОВ — read-only (показываем теперь, было скрыто)
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_ui.row4_title,
                "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");            // ПРОХОДЫ (LCD «Циклов»; везде «проходы» — решение владельца)
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            // thr_type_box: ТИП резьбы — показываем в M3, сбрасываем заголовок на "ТИП"
            if (g_ui.thr_type_box) {
                lv_obj_clear_flag(g_ui.thr_type_box, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_ui.thr_type_title, "\xD0\xA2\xD0\x98\xD0\x9F"); // ТИП
                lv_obj_set_style_text_color(g_ui.thr_type_title, lv_color_hex(0xffaa44), 0);
                lv_obj_set_style_border_color(g_ui.thr_type_box, lv_color_hex(0xffaa44), 0);
            }
            break;

        case MODE_CONE_L:
        case MODE_CONE_R:
            // M4/M5: primary=ПОДАЧА, secondary=RPM
            // row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X (всегда), row3=КОНУС(редактируемый), row4=ПРОХОДЫ
            // thr_type_box=СЪЁМ (Ap, редактируемый)
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
            // row1: ПОЗИЦИЯ Z (всегда — синий, read-only)
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            // row2: ПОЗИЦИЯ X (всегда — синий, read-only)
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            // row3: КОНУС — редактируемый (оранжевый)
            lv_label_set_text(g_ui.row3_title,
                "\xD0\x9A\xD0\x9E\xD0\x9D\xD0\xA3\xD0\xA1");                    // КОНУС
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0xffaa44), 0);
            // row4: ПРОХОДЫ — read-only (синий)
            lv_label_set_text(g_ui.row4_title,
                "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");  // ПРОХОДЫ
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            // thr_type_box: СЪЁМ (Ap) — показываем с оранжевым заголовком
            if (g_ui.thr_type_box) {
                lv_obj_clear_flag(g_ui.thr_type_box, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_ui.thr_type_title,
                    "\xD0\xA1\xD0\xAA\xD0\x81\xD0\x9C " DIAM_SIGN);                         // СЪЁМ ⌀ (LCD «Съём Ø»)
                lv_obj_set_style_text_color(g_ui.thr_type_title, lv_color_hex(0xffaa44), 0);
                lv_obj_set_style_border_color(g_ui.thr_type_box, lv_color_hex(0xffaa44), 0);
            }
            break;

        case MODE_SPHERE:
            // M6: secondary=RPM, row1/row2=Z/X, row3=НОЖКА ММ(editable), row4=ЗАХОДОВ
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
            // row1: ПОЗИЦИЯ Z (всегда — синий, read-only)
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            // row2: ПОЗИЦИЯ X (всегда — синий, read-only)
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            // row3: НОЖКА ММ — редактируемый (оранжевый)
            lv_label_set_text(g_ui.row3_title,
                "\xD0\x9D\xD0\x9E\xD0\x96\xD0\x9A\xD0\x90 " DIAM_SIGN);  // НОЖКА ⌀ (LCD «Ножка:Ø», диаметр)
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0xffaa44), 0);
            // row4: ПРОХОДЫ (LCD «Заходов» шара — это проходы) — read-only (синий)
            lv_label_set_text(g_ui.row4_title,
                "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");  // ПРОХОДЫ
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            // thr_type_box: ШАР ММ (диаметр шара, редактируемый)
            if (g_ui.thr_type_box) {
                lv_obj_clear_flag(g_ui.thr_type_box, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_ui.thr_type_title,
                    "\xD0\xA8\xD0\x90\xD0\xA0 " DIAM_SIGN);           // ШАР ⌀ (LCD «ШАР Ø20.0мм», диаметр)
                lv_obj_set_style_text_color(g_ui.thr_type_title, lv_color_hex(0xffaa44), 0);
                lv_obj_set_style_border_color(g_ui.thr_type_box, lv_color_hex(0xffaa44), 0);
            }
            break;

        case MODE_DIVIDER:
            // Вторичное = RPM (всегда показываем)
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_obj_set_style_text_color(g_ui.secondary_unit, lv_color_hex(0x3a7a5a), 0);
            // row1: ДЕЛЕНИЙ (Total_Tooth) — редактируемый
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x94\xD0\x95\xD0\x9B\xD0\x95\xD0\x9D\xD0\x98\xD0\x99");  // ДЕЛЕНИЙ
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0xffaa44), 0);
            // row2: МЕТКА (Current_Tooth) — редактируемый
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9C\xD0\x95\xD0\xA2\xD0\x9A\xD0\x90");                   // МЕТКА
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0xffaa44), 0);
            // row3: УГОЛ° (sector angle) — read-only
            lv_label_set_text(g_ui.row3_title, "\xD0\xA3\xD0\x93\xD0\x9E\xD0\x9B\xC2\xB0");  // УГОЛ°
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
            // row4: скрыт для M7
            if (g_ui.row4_box) lv_obj_add_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            break;

        case MODE_RESERVE:                                         // M8 — G-код: позиции и строка программы
            lv_label_set_text(g_ui.secondary_unit, "");
            lv_label_set_text(g_ui.row1_title, "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);   // ПОЗИЦИЯ Y
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            lv_label_set_text(g_ui.row2_title, "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);   // ПОЗИЦИЯ X — как везде
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            lv_label_set_text(g_ui.row3_title, "\xD0\x92\xD0\xAB\xD0\x9F\xD0\x9E\xD0\x9B\xD0\x9D\xD0\x95\xD0\x9D\xD0\x9E");   // ВЫПОЛНЕНО
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
            lv_label_set_text(g_ui.row4_title, "\xD0\x9E\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9B\xD0\x9E\xD0\xA1\xD0\xAC");   // ОСТАЛОСЬ
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_Z);  // ПОЗИЦИЯ Z
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF " AXIS_X);  // ПОЗИЦИЯ X
            lv_label_set_text(g_ui.row3_title, "---");
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_add_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            break;
    }

    // ── Сброс отображаемых значений до дефолтных при смене режима ────────────
    // (Если Arduino подключён, update_ui_values() скоро перезапишет их UART-данными)
    lv_obj_set_style_text_color(g_ui.primary_val, lv_color_hex(0x00d4ff), 0);
    if (g_ui.primary_val_glow)
        lv_obj_set_style_text_color(g_ui.primary_val_glow, lv_color_hex(0x00d4ff), 0);
    switch (mode) {
        case MODE_FEED:
        case MODE_AFEED:
            lv_label_set_text(g_ui.primary_val, "0.00");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "0.00");
            lv_label_set_text(g_ui.secondary_val, "--");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "--");
            lv_label_set_text(g_ui.row1_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row2_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row3_val, "0.00");
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;
        case MODE_THREAD:
            lv_label_set_text(g_ui.primary_val, "1.50");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "1.50");
            lv_label_set_text(g_ui.secondary_val, "--");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "--");
            // row1 = ПОЗИЦИЯ Z (всегда), row2 = ПОЗИЦИЯ X (всегда)
            lv_label_set_text(g_ui.row1_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row2_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row3_val, "--");     // RPM/travel
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;
        case MODE_CONE_L:
        case MODE_CONE_R:
            lv_label_set_text(g_ui.primary_val, "0.00");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "0.00");
            lv_label_set_text(g_ui.secondary_val, "--");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "--");
            // row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X (всегда), row3=КОНУС (тип, ред.)
            lv_label_set_text(g_ui.row1_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row2_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row3_val, CONE_NAMES[0]);   // "45°" по умолчанию
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;
        case MODE_SPHERE:
            lv_label_set_text(g_ui.primary_val, "0.25");  // default feed 0.25mm/ob
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "0.25");
            lv_label_set_text(g_ui.secondary_val, "--");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "--");
            // row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X (всегда), row3=НОЖКА ММ
            lv_label_set_text(g_ui.row1_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row2_val, "+0.00");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x00ff88), 0);
            lv_label_set_text(g_ui.row3_val, "0.00");       // НОЖКА ММ
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;
        case MODE_DIVIDER:
            lv_label_set_text(g_ui.primary_val, "0.0");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "0.0");
            lv_label_set_text(g_ui.secondary_val, "--");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "--");
            lv_label_set_text(g_ui.row1_val, "1");     // total_tooth=1
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
            lv_label_set_text(g_ui.row2_val, "1");     // current_tooth=1
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
            lv_label_set_text(g_ui.row3_val, "360.0\xC2\xB0");  // sector=360/1
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;
        case MODE_RESERVE:
            lv_label_set_text(g_ui.primary_val, "---");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "---");
            lv_obj_set_style_text_color(g_ui.primary_val, lv_color_hex(0x555555), 0);
            if (g_ui.primary_val_glow) lv_obj_set_style_text_color(g_ui.primary_val_glow, lv_color_hex(0x555555), 0);
            lv_label_set_text(g_ui.secondary_val, "");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "");
            lv_label_set_text(g_ui.row1_val, "");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row2_val, "");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row3_val, "");
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            break;
        default:
            lv_label_set_text(g_ui.primary_val, "---");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "---");
            lv_label_set_text(g_ui.secondary_val, "---");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "---");
            lv_label_set_text(g_ui.row1_val, "---");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row2_val, "---");
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row3_val, "---");
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            break;
    }
    lv_bar_set_value(g_ui.rpm_bar, 0, LV_ANIM_OFF);

    // Сбросить sub-edit при смене режима (цвет заголовков восстановится выше)
    if (g_sub_edit.active) exit_sub_edit();
}

// ============================================================================
// Вход/выход из режима редактирования основного параметра
// ============================================================================

static void enter_edit_mode()
{
    if (!g_ui.primary_val) return;
    // Не входить если открыт оверлей смены режима
    if (g_ui.mode_menu && !lv_obj_has_flag(g_ui.mode_menu, LV_OBJ_FLAG_HIDDEN)) return;
    // Делилка и Резерв — редактирование основного значения не предусмотрено
    if (s_last_mode == MODE_DIVIDER || s_last_mode == MODE_RESERVE) return;
    // SM sub-screens are read-only (no primary-value editing)
    {
        uint8_t sm = uart_protocol.getData().select_menu;
        if (sm >= 2) {
            if (s_last_mode == MODE_AFEED || s_last_mode == MODE_FEED ||
                s_last_mode == MODE_THREAD ||
                s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) return;
        }
    }

    // Выйти из sub_edit если был активен (предотвращает одновременную активность)
    if (g_sub_edit.active) exit_sub_edit();

    g_edit_param.active  = true;
    g_edit_param.last_ms = millis();

    // Инициализация локального значения для мгновенной обратной связи
    const LatheData& d = uart_protocol.getData();
    switch (s_last_mode) {
        case MODE_THREAD: {
            int best = 13;  // default: 1.50mm
            bool found = false;
            // Priority: match by thread_name label (authoritative — avoids ambiguity
            // when multiple entries share the same mm_x100, e.g. "0.40mm" vs "64tpi ")
            if (d.thread_name[0] != '\0') {
                for (int i = 0; i < THREAD_TABLE_SIZE; i++) {
                    if (strncmp(d.thread_name, s_thread_table[i].label, 6) == 0) {
                        best = i;
                        found = true;
                        break;
                    }
                }
            }
            // Fallback: closest mm_x100 match (used in test mode / when thread_name absent)
            if (!found) {
                int16_t cur = d.thread_mm;
                int best_dist = 32767;
                for (int i = 0; i < THREAD_TABLE_SIZE; i++) {
                    int dist = abs((int)s_thread_table[i].mm_x100 - (int)cur);
                    if (dist < best_dist) { best_dist = dist; best = i; }
                }
            }
            g_edit_param.local_step = best;
            g_edit_param.local_val  = 0;
            break;
        }
        case MODE_FEED:
        case MODE_CONE_L:
        case MODE_CONE_R:
            g_edit_param.local_val = (d.feed_mm > 0) ? d.feed_mm : 25;  // 0.25 по умолчанию
            break;
        case MODE_AFEED:
            g_edit_param.local_val = (d.afeed_mm > 0) ? d.afeed_mm : 100;  // 100 мм/мин по умолчанию
            break;
        case MODE_SPHERE:
            g_edit_param.local_val = (d.feed_mm > 0) ? d.feed_mm : 25;  // подача как в M1
            break;
        default:
            g_edit_param.local_val = 0;
            break;
    }

    // Для режимов с числовым параметром — открыть roller вместо простой подсветки
    {
        RollerCfg rc = {};
        bool use_roller = true;
        switch (s_last_mode) {
            case MODE_FEED:
                rc = { RT_FEED, 5, 2500, 5,
                    "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0 \xD0\xBC\xD0\xBC/\xD0\xBE\xD0\xB1" };
                break;
            case MODE_AFEED:
                rc = { RT_AFEED, 10, 2500, 10,
                    "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0 \xD0\xBC\xD0\xBC/\xD0\xBC\xD0\xB8\xD0\xBD" };
                break;
            case MODE_THREAD:
                { int lo, hi; thread_cat_range(g_edit_param.local_step, lo, hi);
                  rc = { RT_THREAD, lo, hi, 1, "\xD0\xA0\xD0\xB5\xD0\xB7\xD1\x8C\xD0\xB1\xD0\xB0" }; }
                break;
            case MODE_CONE_L:
            case MODE_CONE_R:
                rc = { RT_FEED, 5, 2500, 5,
                    "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0 \xD0\xBC\xD0\xBC/\xD0\xBE\xD0\xB1" };
                break;
            case MODE_SPHERE:
                rc = { RT_FEED, 5, 2500, 5,
                    "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0 \xD0\xBC\xD0\xBC/\xD0\xBE\xD0\xB1" };
                break;
            default:
                use_roller = false;
                break;
        }
        if (use_roller) {
            int32_t init_val = (s_last_mode == MODE_THREAD)
                ? g_edit_param.local_step
                : g_edit_param.local_val;
            open_roller(init_val, rc);
            Serial.println("Edit mode: ON (roller)");
            return;
        }
    }

    // Основное значение → жёлтый
    lv_obj_set_style_text_color(g_ui.primary_val, lv_color_hex(0xffcc00), 0);
    if (g_ui.primary_val_glow)
        lv_obj_set_style_text_color(g_ui.primary_val_glow, lv_color_hex(0xffcc00), 0);

    // Жёлтая рамка вокруг ячейки редактирования
    if (g_ui.primary_edit_box) {
        lv_obj_set_style_border_color(g_ui.primary_edit_box, lv_color_hex(0xffcc00), 0);
        lv_obj_set_style_bg_color(g_ui.primary_edit_box, lv_color_hex(0x120a00), 0);
    } else {
        lv_obj_set_style_border_width(g_ui.primary_val, US(2), 0);
        lv_obj_set_style_border_color(g_ui.primary_val, lv_color_hex(0xffcc00), 0);
        lv_obj_set_style_pad_hor(g_ui.primary_val, US(8), 0);
    }

    Serial.println("Edit mode: ON  (UP=KEY:UP, OK=PARAM_OK, DN=KEY:DN)");
}

static void exit_edit_mode()
{
    if (!g_ui.primary_val) return;

    // Закрыть roller без подтверждения если он ещё открыт (например при auto-exit)
    if (g_roller.active) close_roller(false);

    // M3: немедленно зафиксировать локальный label до прихода подтверждения от Arduino
    // (иначе update_ui_values() покажет старый data.thread_name на долю секунды)
    if (s_last_mode == MODE_THREAD && g_edit_param.active) {
        int step = g_edit_param.local_step;
        if (step >= 0 && step < THREAD_TABLE_SIZE) {
            const char* lbl = s_thread_table[step].label;
            char fbuf[12] = {}; char* dst = fbuf;
            for (const char* src = lbl; *src && dst < fbuf + sizeof(fbuf) - 1; src++) {
                uint8_t c = (uint8_t)*src;
                if (c == 0x2B || (c >= 0x2D && c <= 0x3A)) *dst++ = *src;
            }
            if (fbuf[0] != '\0') {
                lv_label_set_text(g_ui.primary_val, fbuf);
                if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, fbuf);
            }
            if (g_ui.primary_unit) {
                if (strstr(lbl, "tpi"))
                    lv_label_set_text(g_ui.primary_unit, "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C"); // ДЮЙМ
                else if (lbl[0] == 'G')
                    lv_label_set_text(g_ui.primary_unit, "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // G-ТРУБ
                else if (lbl[0] == 'K')
                    lv_label_set_text(g_ui.primary_unit, "K-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // K-ТРУБ
                else
                    lv_label_set_text(g_ui.primary_unit, "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C"); // ШАГ ММ
            }
        }
    }

    // Оптимистично обновить data_ до прихода подтверждения от STM32
    if (g_edit_param.active) {
        switch (s_last_mode) {
            case MODE_FEED:
            case MODE_CONE_L: case MODE_CONE_R:
            case MODE_SPHERE:
                uart_protocol.setFeedOptimistic(g_edit_param.local_val);
                break;
            case MODE_AFEED:
                uart_protocol.setAfeedOptimistic(g_edit_param.local_val);
                break;
            case MODE_THREAD: {
                int step = g_edit_param.local_step;
                if (step >= 0 && step < THREAD_TABLE_SIZE) {
                    uart_protocol.setThreadOptimistic(
                        s_thread_table[step].mm_x100,
                        s_thread_table[step].label);
                }
                break;
            }
            default: break;
        }
    }

    g_edit_param.active = false;

    // Вернуть основному значению исходный голубой цвет
    lv_obj_set_style_text_color(g_ui.primary_val, lv_color_hex(0x00d4ff), 0);
    if (g_ui.primary_val_glow)
        lv_obj_set_style_text_color(g_ui.primary_val_glow, lv_color_hex(0x00d4ff), 0);

    // Убрать рамку редактирования
    if (g_ui.primary_edit_box) {
        lv_obj_set_style_border_color(g_ui.primary_edit_box, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_bg_color(g_ui.primary_edit_box, lv_color_hex(0x0a1828), 0);
    } else {
        lv_obj_set_style_border_width(g_ui.primary_val, 0, 0);
        lv_obj_set_style_pad_hor(g_ui.primary_val, 0, 0);
    }

    Serial.println("Edit mode: OFF");
}

// ============================================================================
// Универсальный барабан — реализация (кастомные метки + swipe + инерция)
// ============================================================================

// Форматировать метку для строки барабана
static void roller_label_text(char* buf, size_t len, int32_t val, RollerTarget tgt) {
    switch (tgt) {
        case RT_FEED:
            snprintf(buf, len, "%d.%02d", (int)(val / 100), (int)(val % 100));
            break;
        case RT_AFEED:
            snprintf(buf, len, "%d", (int)val);
            break;
        case RT_THREAD:
            if (val >= 0 && val < THREAD_TABLE_SIZE) {
                // Только ASCII-безопасные символы
                const char* lbl = s_thread_table[val].label;
                char* dst = buf;
                for (const char* src = lbl; *src && dst < buf + len - 1; src++) {
                    uint8_t fc = (uint8_t)*src;
                    if (fc == 0x2B || (fc >= 0x2D && fc <= 0x3A)) *dst++ = *src;
                }
                *dst = '\0';
                if (buf[0] == '\0') snprintf(buf, len, "%d", (int)val);
            } else {
                snprintf(buf, len, "%d", (int)val);
            }
            break;
        case RT_AP:
            snprintf(buf, len, "%d.%02d", (int)(val / 100), (int)(val % 100));
            break;
        case RT_CONE:
            if (val >= 0 && val < CONE_COUNT)
                snprintf(buf, len, "%s", CONE_NAMES[val]);
            else
                snprintf(buf, len, "%d", (int)val);
            break;
        case RT_SPH_R: {
            // sphere_r в мм*100 (радиус), показываем диаметр XX.X
            int32_t diam = val * 2;
            snprintf(buf, len, "%d.%01d", (int)(diam / 100), (int)(abs(diam % 100) / 10));
            break;
        }
        case RT_BAR_R: {
            // bar_r в мм*100 (радиус), показываем диаметр XX.XX
            int32_t diam = val * 2;
            snprintf(buf, len, "%d.%02d", (int)(diam / 100), (int)(diam % 100));
            break;
        }
        case RT_TOOTH_TOTAL:
        case RT_TOOTH_CUR:
            snprintf(buf, len, "%d", (int)val);
            break;
        default:
            snprintf(buf, len, "%d", (int)val);
            break;
    }
}

static void roller_update_labels() {
    const lv_color_t colors[] = {
        lv_color_hex(0x283848),
        lv_color_hex(0x607880),
        lv_color_hex(0x00d4ff),
        lv_color_hex(0x607880),
        lv_color_hex(0x283848),
    };
    const RollerCfg& c = g_roller.cfg;
    for (int i = 0; i < 5; i++) {
        if (!g_roller.labels[i]) continue;
        int32_t v = g_roller.pending_val + (i - 2) * c.val_step;
        char buf[24] = "";   // за границей диапазона — пустая строка, а не повтор крайнего значения
        if (v >= c.val_min && v <= c.val_max) roller_label_text(buf, sizeof(buf), v, c.target);
        lv_label_set_text(g_roller.labels[i], buf);
        lv_obj_set_style_text_color(g_roller.labels[i], colors[i], 0);
    }
    // Синхронизировать primary/sub значения на фоновом UI
    char cur[24];
    roller_label_text(cur, sizeof(cur), g_roller.pending_val, c.target);
    switch (c.target) {
        case RT_FEED:
            g_edit_param.local_val = g_roller.pending_val;
            g_edit_param.last_ms   = millis();
            if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, cur);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, cur);
            break;
        case RT_AFEED:
            g_edit_param.local_val = g_roller.pending_val;
            g_edit_param.last_ms   = millis();
            if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, cur);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, cur);
            break;
        case RT_THREAD:
            g_edit_param.local_step = (int)g_roller.pending_val;
            g_edit_param.last_ms    = millis();
            if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, cur);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, cur);
            break;
        case RT_AP:
            g_sub_edit.ap = (int16_t)g_roller.pending_val;
            if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, cur);
            break;
        case RT_CONE:
            g_sub_edit.cone_idx = (uint8_t)g_roller.pending_val;
            if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, cur);
            break;
        case RT_SPH_R:
            g_sub_edit.sphere_r = (int16_t)g_roller.pending_val;
            if (g_ui.thr_type_val) lv_label_set_text(g_ui.thr_type_val, cur);
            break;
        case RT_BAR_R:
            g_sub_edit.bar_r = (int16_t)g_roller.pending_val;
            if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, cur);
            break;
        case RT_TOOTH_TOTAL:
            g_sub_edit.total_tooth = (uint8_t)g_roller.pending_val;
            if (g_ui.row1_val) lv_label_set_text(g_ui.row1_val, cur);
            break;
        case RT_TOOTH_CUR:
            g_sub_edit.current_tooth = (uint8_t)g_roller.pending_val;
            if (g_ui.row2_val) lv_label_set_text(g_ui.row2_val, cur);
            break;
    }
}

static void close_roller(bool confirm) {
    if (!g_roller.active) return;
    g_roller.active = false;
    if (g_roller.inertia_timer) {
        lv_timer_del(g_roller.inertia_timer);
        g_roller.inertia_timer = nullptr;
    }
    if (g_roller.backdrop) { lv_obj_del(g_roller.backdrop); g_roller.backdrop = nullptr; }
    if (g_roller.panel)    { lv_obj_del(g_roller.panel);    g_roller.panel    = nullptr; }
    memset(g_roller.labels, 0, sizeof(g_roller.labels));

    const RollerCfg& c = g_roller.cfg;
    if (confirm) {
        int32_t val = g_roller.pending_val;
        char cmd[32];
        switch (c.target) {
            case RT_FEED:
                snprintf(cmd, sizeof(cmd), "FEED:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                break;
            case RT_AFEED:
                snprintf(cmd, sizeof(cmd), "AFEED:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                break;
            case RT_THREAD:
                if (val >= 0 && val < THREAD_TABLE_SIZE) {
                    snprintf(cmd, sizeof(cmd), "THREAD_STEP:%d", (int)val);
                    uart_protocol.sendButtonPress(cmd);
                }
                break;
            case RT_AP:
                snprintf(cmd, sizeof(cmd), "AP:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setApOptimistic((int16_t)val);
                break;
            case RT_CONE:
                snprintf(cmd, sizeof(cmd), "CONE:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setConeOptimistic((uint8_t)val);
                break;
            case RT_SPH_R:
                snprintf(cmd, sizeof(cmd), "SPHERE:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setSphereOptimistic((int16_t)val);
                break;
            case RT_BAR_R:
                snprintf(cmd, sizeof(cmd), "BAR:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setBarOptimistic((int16_t)val);
                break;
            case RT_TOOTH_TOTAL:
                snprintf(cmd, sizeof(cmd), "DIVN:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setTotalToothOptimistic((uint16_t)val);
                break;
            case RT_TOOTH_CUR:
                snprintf(cmd, sizeof(cmd), "DIVM:%d", (int)val);
                uart_protocol.sendButtonPress(cmd);
                uart_protocol.setCurrentToothOptimistic((uint16_t)val);
                break;
        }
        Serial.printf("Roller confirmed: target=%d val=%d\n", (int)c.target, (int)val);
    } else {
        // Отмена — восстановить исходное значение на фоновом UI
        int32_t ev = g_roller.entry_val;
        char buf[24];
        roller_label_text(buf, sizeof(buf), ev, c.target);
        switch (c.target) {
            case RT_FEED:
            case RT_AFEED:
                g_edit_param.local_val = ev;
                if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, buf);
                if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
                break;
            case RT_THREAD:
                g_edit_param.local_step = (int)ev;
                if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, buf);
                if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
                break;
            case RT_AP:
                g_sub_edit.ap = (int16_t)ev;
                if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, buf);
                break;
            case RT_CONE:
                g_sub_edit.cone_idx = (uint8_t)ev;
                if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, buf);
                break;
            case RT_SPH_R:
                g_sub_edit.sphere_r = (int16_t)ev;
                if (g_ui.thr_type_val) lv_label_set_text(g_ui.thr_type_val, buf);
                break;
            case RT_BAR_R:
                g_sub_edit.bar_r = (int16_t)ev;
                if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, buf);
                break;
            case RT_TOOTH_TOTAL:
                g_sub_edit.total_tooth = (uint8_t)ev;
                if (g_ui.row1_val) lv_label_set_text(g_ui.row1_val, buf);
                break;
            case RT_TOOTH_CUR:
                g_sub_edit.current_tooth = (uint8_t)ev;
                if (g_ui.row2_val) lv_label_set_text(g_ui.row2_val, buf);
                break;
        }
        Serial.printf("Roller cancelled: target=%d restored=%d\n", (int)c.target, (int)ev);
    }

    // Sub-edit state cleanup (primary targets управляются вызывающим кодом через exit_edit_mode)
    bool _is_primary = (c.target == RT_FEED || c.target == RT_AFEED || c.target == RT_THREAD);
    if (!_is_primary) {
        if (confirm && g_sub_edit.active) {
            // Grace period: визуально выходим сразу, g_sub_edit.active остаётся true на 800мс
            // Это защищает update_ui_values от перезаписи старыми данными STM32
            highlight_sub_edit_row(g_sub_edit.row, false);
            if (g_sub_edit.exit_timer) { lv_timer_del(g_sub_edit.exit_timer); g_sub_edit.exit_timer = nullptr; }
            g_sub_edit.exit_timer = lv_timer_create([](lv_timer_t* t) {
                g_sub_edit.active     = false;
                g_sub_edit.row        = -1;
                g_sub_edit.exit_timer = nullptr;
                lv_timer_del(t);
            }, 800, nullptr);
        } else {
            exit_sub_edit();  // Отмена: немедленная очистка
        }
    }
}

static void _roller_inertia_cb(lv_timer_t* t) {
    const float DECAY   = 0.92f;
    const float MIN_VEL = 0.35f;
    const int   STEP_PX = UY(15);

    g_roller.velocity *= DECAY;
    if (fabsf(g_roller.velocity) < MIN_VEL) {
        lv_timer_del(t);
        g_roller.inertia_timer = nullptr;
        g_roller.velocity      = 0.0f;
        return;
    }

    g_roller.vel_accum += g_roller.velocity;
    const RollerCfg& c = g_roller.cfg;
    while (g_roller.vel_accum >= STEP_PX) {
        g_roller.vel_accum -= STEP_PX;
        g_roller.pending_val = constrain(g_roller.pending_val + c.val_step, c.val_min, c.val_max);
        roller_update_labels();
    }
    while (g_roller.vel_accum <= -STEP_PX) {
        g_roller.vel_accum += STEP_PX;
        g_roller.pending_val = constrain(g_roller.pending_val - c.val_step, c.val_min, c.val_max);
        roller_update_labels();
    }
}

static void open_roller(int32_t cur_val, const RollerCfg& cfg) {
    if (g_roller.active) close_roller(false);

    g_roller.cfg = cfg;
    lv_obj_t* scr = lv_scr_act();

    // Полупрозрачный фон — левая область (правые кнопки x=418..474 остаются доступными)
    g_roller.backdrop = lv_obj_create(scr);
    lv_obj_set_size(g_roller.backdrop, SCREEN_WIDTH - UX(64), SCREEN_HEIGHT);
    lv_obj_set_pos(g_roller.backdrop, 0, 0);
    lv_obj_set_style_bg_color(g_roller.backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_roller.backdrop, 140, 0);
    lv_obj_set_style_border_width(g_roller.backdrop, 0, 0);
    lv_obj_clear_flag(g_roller.backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_roller.backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_roller.backdrop, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        bool changed = (g_roller.pending_val != g_roller.entry_val);
        RollerTarget tgt = g_roller.cfg.target;
        close_roller(changed);  // sub_edit grace period / cleanup handled inside close_roller
        bool is_primary = (tgt == RT_FEED || tgt == RT_AFEED || tgt == RT_THREAD);
        if (is_primary) exit_edit_mode();
    }, LV_EVENT_ALL, nullptr);

    // Центральная панель барабана
    const int PW = UX(260), PH = UY(260);
    const int PX = ((SCREEN_WIDTH - UX(64)) - PW) / 2, PY = (SCREEN_HEIGHT - PH) / 2;
    g_roller.panel = lv_obj_create(scr);
    lv_obj_set_size(g_roller.panel, PW, PH);
    lv_obj_set_pos(g_roller.panel, PX, PY);
    lv_obj_set_style_bg_color(g_roller.panel, lv_color_hex(0x0d1e2e), 0);
    lv_obj_set_style_bg_opa(g_roller.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_roller.panel, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(g_roller.panel, US(2), 0);
    lv_obj_set_style_radius(g_roller.panel, US(8), 0);
    lv_obj_set_style_pad_all(g_roller.panel, 0, 0);
    lv_obj_clear_flag(g_roller.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_roller.panel, LV_OBJ_FLAG_CLICKABLE);

    // Заголовок
    lv_obj_t* title = lv_label_create(g_roller.panel);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_label_set_text(title, cfg.title ? cfg.title : "");
    lv_obj_set_pos(title, 0, UY(8));
    lv_obj_set_width(title, PW);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    const int ROW_H = UY(40), ROWS_Y = UY(40);
    lv_obj_t* sel_rect = lv_obj_create(g_roller.panel);
    lv_obj_set_size(sel_rect, PW - UX(12), UY(42));
    lv_obj_set_pos(sel_rect, UX(4), ROWS_Y + 2 * ROW_H - UY(4));
    lv_obj_set_style_bg_color(sel_rect, lv_color_hex(0x003a5a), 0);
    lv_obj_set_style_bg_opa(sel_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sel_rect, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(sel_rect, US(1), 0);
    lv_obj_set_style_radius(sel_rect, US(4), 0);
    lv_obj_clear_flag(sel_rect, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Центральная строка — font_40, внешние — font_28 (текст) или font_22 (строковые)
    bool text_target = (cfg.target == RT_CONE);
    const lv_font_t* font_center = text_target ? &font_tahoma_bold_22 : &font_tahoma_bold_40;
    const lv_font_t* font_outer  = text_target ? &font_tahoma_bold_16 : &font_tahoma_bold_28;
    const lv_font_t* row_fonts[] = { font_outer, font_outer, font_center, font_outer, font_outer };

    for (int i = 0; i < 5; i++) {
        g_roller.labels[i] = lv_label_create(g_roller.panel);
        lv_obj_set_style_text_font(g_roller.labels[i], row_fonts[i], 0);
        int label_y = ROWS_Y + i * ROW_H;
        if (i != 2) label_y += UY(2);
        lv_obj_set_pos(g_roller.labels[i], 0, label_y);
        lv_obj_set_width(g_roller.labels[i], lv_pct(100));
        lv_obj_set_style_text_align(g_roller.labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(g_roller.labels[i], "");
        lv_obj_clear_flag(g_roller.labels[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Swipe + инерция
    lv_obj_add_event_cb(g_roller.panel, [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            if (g_roller.inertia_timer) {
                lv_timer_del(g_roller.inertia_timer);
                g_roller.inertia_timer = nullptr;
            }
            g_roller.velocity    = 0.0f;
            g_roller.vel_accum   = 0.0f;
            g_roller.touch_accum = 0;
        } else if (code == LV_EVENT_PRESSING) {
            lv_indev_t* indev = lv_indev_get_act();
            if (!indev) return;
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            const int STEP_PX = UY(15);
            g_roller.velocity = g_roller.velocity * 0.7f + (-vect.y) * 0.3f;
            g_roller.touch_accum -= vect.y;
            const RollerCfg& c = g_roller.cfg;
            while (g_roller.touch_accum >= STEP_PX) {
                g_roller.touch_accum -= STEP_PX;
                g_roller.pending_val = constrain(g_roller.pending_val + c.val_step, c.val_min, c.val_max);
                roller_update_labels();
            }
            while (g_roller.touch_accum <= -STEP_PX) {
                g_roller.touch_accum += STEP_PX;
                g_roller.pending_val = constrain(g_roller.pending_val - c.val_step, c.val_min, c.val_max);
                roller_update_labels();
            }
        } else if (code == LV_EVENT_RELEASED) {
            g_roller.touch_accum = 0;
            if (fabsf(g_roller.velocity) >= (float)UY(2) && !g_roller.inertia_timer) {
                g_roller.vel_accum    = 0.0f;
                g_roller.inertia_timer = lv_timer_create(_roller_inertia_cb, 16, nullptr);
            } else {
                g_roller.velocity = 0.0f;
            }
        } else if (code == LV_EVENT_PRESS_LOST) {
            g_roller.touch_accum = 0;
            g_roller.velocity    = 0.0f;
        }
    }, LV_EVENT_ALL, nullptr);

    // Начальное значение — выровнять по шагу
    int32_t v = constrain(cur_val, cfg.val_min, cfg.val_max);
    if (cfg.val_step > 1) v = ((v + cfg.val_step / 2) / cfg.val_step) * cfg.val_step;
    v = constrain(v, cfg.val_min, cfg.val_max);

    g_roller.entry_val     = v;
    g_roller.pending_val   = v;
    g_roller.touch_accum   = 0;
    g_roller.inertia_timer = nullptr;
    g_roller.velocity      = 0.0f;
    g_roller.vel_accum     = 0.0f;
    g_roller.active        = true;

    roller_update_labels();
    Serial.printf("Roller OPEN: target=%d val=%d\n", (int)cfg.target, (int)v);
}

// ============================================================================
// Меню быстрого выбора типа резьбы (double-tap в M3)
// ============================================================================

static void show_thr_type_menu()
{
    if (!g_ui.thr_type_menu) return;
    lv_obj_clear_flag(g_ui.thr_type_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ui.thr_type_menu);
    Serial.println("Thread type menu: OPEN");
}

// ============================================================================
// Джойстик-оверлей (US-013)
// ============================================================================

static void show_joystick_overlay()
{
    if (!g_ui.joystick_overlay) return;
    // Сбросить rapid при открытии
    g_joystick.rapid = false;
    if (g_ui.joy_rapid_btn) {
        lv_obj_set_style_bg_color(g_ui.joy_rapid_btn, lv_color_hex(0x0a1f0a), 0);
        lv_obj_set_style_border_color(g_ui.joy_rapid_btn, lv_color_hex(0x00aa44), 0);
    }
    g_joystick.active  = true;
    g_joystick.last_ms = millis();
    lv_obj_clear_flag(g_ui.joystick_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ui.joystick_overlay);
    Serial.println("Joystick overlay: OPEN");
}

static void hide_joystick_overlay()
{
    if (!g_ui.joystick_overlay) return;
    // Выключить rapid при закрытии
    if (g_joystick.rapid) {
        g_joystick.rapid = false;
        uart_protocol.sendButtonPress("JOY:RAPID_OFF");
    }
    g_joystick.active = false;
    lv_obj_add_flag(g_ui.joystick_overlay, LV_OBJ_FLAG_HIDDEN);
    Serial.println("Joystick overlay: CLOSE");
}

// Прыжок к первой записи типа резьбы: входит в edit mode, обновляет дисплей
static void jump_to_thread_type(int first_idx)
{
    if (first_idx < 0 || first_idx >= THREAD_TABLE_SIZE) return;

    // enter_edit_mode() берёт шаг из текущего состояния STM32 — поэтому выбранный
    // тип выставляется ПОСЛЕ входа в редактирование, иначе барабан открылся бы на старом шаге
    if (!g_edit_param.active && !g_roller.active) enter_edit_mode();
    g_edit_param.local_step = first_idx;
    g_edit_param.last_ms    = millis();
    if (g_roller.active) {
        int lo, hi; thread_cat_range(first_idx, lo, hi);   // барабан — на диапазон новой категории
        g_roller.cfg.val_min = lo;
        g_roller.cfg.val_max = hi;
        g_roller.pending_val = first_idx;
        roller_update_labels();
    }

    const char* lbl = s_thread_table[first_idx].label;

    // Фильтр: только символы из digit-font (как в UP/DN обработчиках)
    char fbuf[32]; char* fdst = fbuf;
    for (const char* fsrc = lbl; *fsrc && fdst < fbuf + sizeof(fbuf) - 1; fsrc++) {
        uint8_t fc = (uint8_t)*fsrc;
        if (fc == 0x2B || (fc >= 0x2D && fc <= 0x3A)) *fdst++ = *fsrc;
    }
    *fdst = '\0';
    if (fbuf[0] == '\0') snprintf(fbuf, sizeof(fbuf), "--");

    lv_label_set_text(g_ui.primary_val, fbuf);
    if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, fbuf);

    // Обновить подпись типа резьбы
    if (strstr(lbl, "tpi"))
        lv_label_set_text(g_ui.primary_unit, "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C");    // ДЮЙМ
    else if (lbl[0] == 'G')
        lv_label_set_text(g_ui.primary_unit, "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // G-ТРУБ
    else if (lbl[0] == 'K')
        lv_label_set_text(g_ui.primary_unit, "K-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // K-ТРУБ
    else
        lv_label_set_text(g_ui.primary_unit, "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C"); // ШАГ ММ

    update_thread_type_indicators(lbl);

    Serial.printf("Thread type jump: idx=%d label=%s\n", first_idx, lbl);
}

// ============================================================================
// Редактирование параметра правой панели (тап на строку)
// ============================================================================

// Какие строки (0=row1,1=row2,2=row3) редактируемы для данного режима (битмаска)
static uint8_t get_editable_rows(LatheMode mode) {
    switch (mode) {
        case MODE_FEED:
        case MODE_AFEED:   return 0b10100;  // row3: Ap (bit2), row4: Pass_Total (bit4)
        case MODE_THREAD:  return 0;        // row3: ОБ/МИН — read-only
        case MODE_CONE_L:
        case MODE_CONE_R:  return 0b11100;  // row3: КОНУС (bit2), thr_type=СЪЁМ (bit3), row4: Pass_Total (bit4)
        case MODE_SPHERE:  return 0b1100;   // row3: НОЖКА ММ (bit2), thr_type=ШАР ММ (bit3)
        case MODE_DIVIDER: return 0b011;    // row1: Total_Tooth, row2: Current_Tooth
        default:           return 0;
    }
}

static void highlight_sub_edit_row(int row, bool on)
{
    // Тёмный фон + жёлтая рамка при редактировании (как primary_edit_box)
    if (row == 3) {
        if (!g_ui.thr_type_box) return;
        lv_obj_set_style_border_color(g_ui.thr_type_box,
            lv_color_hex(on ? 0xffcc00 : 0xffaa44), 0);
        lv_obj_set_style_bg_color(g_ui.thr_type_box,
            lv_color_hex(on ? 0x120a00 : 0x060810), 0);
        return;
    }
    if (row == 4) {
        if (!g_ui.row4_box) return;
        lv_obj_set_style_border_color(g_ui.row4_box,
            lv_color_hex(on ? 0xffcc00 : 0x00d4ff), 0);
        lv_obj_set_style_bg_color(g_ui.row4_box,
            lv_color_hex(on ? 0x120a00 : 0x060810), 0);
        return;
    }
    lv_obj_t* boxes[] = {g_ui.row1_box, g_ui.row2_box, g_ui.row3_box};
    if (row < 0 || row > 2) return;
    lv_obj_t* box = boxes[row];
    if (!box) return;
    lv_obj_set_style_border_color(box, lv_color_hex(on ? 0xffcc00 : 0x00d4ff), 0);
    lv_obj_set_style_bg_color(box,     lv_color_hex(on ? 0x120a00 : 0x0a1828), 0);
}

static void enter_sub_edit(int row)
{
    // SM sub-screens are read-only (no editing)
    {
        uint8_t sm = uart_protocol.getData().select_menu;
        if (sm >= 2) {
            if (s_last_mode == MODE_AFEED || s_last_mode == MODE_FEED ||
                s_last_mode == MODE_THREAD ||
                s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) return;
        }
    }

    // Проверить что эта строка редактируема в текущем режиме
    uint8_t editable = get_editable_rows(s_last_mode);
    if (!(editable & (1 << row))) return;

    // Выйти из primary edit если активен
    if (g_edit_param.active) exit_edit_mode();
    // Выйти из предыдущего sub_edit если был другой ряд
    if (g_sub_edit.active && g_sub_edit.row != row) exit_sub_edit();

    g_sub_edit.active   = true;
    g_sub_edit.row      = row;
    g_sub_edit.last_ms  = millis();

    // Инициализировать локальные значения из текущих данных Arduino
    const LatheData& d = uart_protocol.getData();
    g_sub_edit.ap           = d.ap > 0 ? d.ap : 5;
    g_sub_edit.ph           = d.ph > 0 ? d.ph : 1;
    g_sub_edit.cone_idx     = d.cone_idx < (uint8_t)CONE_COUNT ? d.cone_idx : 0;
    g_sub_edit.total_tooth  = d.total_tooth > 0 ? d.total_tooth : 4;
    g_sub_edit.current_tooth = d.current_tooth > 0 ? d.current_tooth : 1;
    g_sub_edit.bar_r        = d.bar_r > 0 ? d.bar_r : 100;    // 1.00mm default
    g_sub_edit.sphere_r     = d.sphere_radius > 0 ? d.sphere_radius : 1000;  // 10mm radius = 20mm diam
    g_sub_edit.pass_total   = d.pass_total > 0 ? d.pass_total : 1;

    highlight_sub_edit_row(row, true);

    // Для числовых sub-параметров открыть roller
    {
        RollerCfg rc = {};
        int32_t init_val = 0;
        bool use_roller = true;
        switch (s_last_mode) {
            case MODE_FEED:
            case MODE_AFEED:
                // row2/3 = Ap (съём), диапазон 0..990, шаг 5
                rc = { RT_AP, 0, 990, 5,
                    "\xD0\xA1\xD1\x8A\xD0\xB5\xD0\xBC \xD0\xBC\xD0\xBC" };
                init_val = g_sub_edit.ap;
                break;
            case MODE_CONE_L:
            case MODE_CONE_R:
                if (row == 2) {
                    rc = { RT_CONE, 0, (int32_t)(CONE_COUNT - 1), 1,
                        "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x83\xD1\x81" };
                    init_val = g_sub_edit.cone_idx;
                } else if (row == 3) {
                    rc = { RT_AP, 0, 990, 5,
                        "\xD0\xA1\xD1\x8A\xD0\xB5\xD0\xBC \xD0\xBC\xD0\xBC" };
                    init_val = g_sub_edit.ap;
                } else { use_roller = false; }
                break;
            case MODE_SPHERE:
                if (row == 2) {
                    rc = { RT_BAR_R, 0, 9900, 25,
                        "\xD0\x9D\xD0\xBE\xD0\xB6\xD0\xBA\xD0\xB0 \xD0\xBC\xD0\xBC" };
                    init_val = g_sub_edit.bar_r;
                } else if (row == 3) {
                    rc = { RT_SPH_R, 50, 9999, 50,
                        "\xD0\xA8\xD0\xB0\xD1\x80 \xD0\xBC\xD0\xBC" };
                    init_val = g_sub_edit.sphere_r;
                } else { use_roller = false; }
                break;
            case MODE_DIVIDER:
                if (row == 0) {
                    rc = { RT_TOOTH_TOTAL, 2, 360, 1,
                        "\xD0\x94\xD0\xB5\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9" };
                    init_val = g_sub_edit.total_tooth;
                } else if (row == 1) {
                    rc = { RT_TOOTH_CUR, 1, (int32_t)g_sub_edit.total_tooth, 1,
                        "\xD0\x9C\xD0\xB5\xD1\x82\xD0\xBA\xD0\xB0" };
                    init_val = g_sub_edit.current_tooth;
                } else { use_roller = false; }
                break;
            default:
                use_roller = false;
                break;
        }
        if (use_roller) {
            open_roller(init_val, rc);
            Serial.printf("SubEdit roller: row=%d mode=%d\n", row, (int)s_last_mode);
        }
    }

    // Сообщить Arduino какой параметр редактируем
    // row==4 (Pass_Total) управляется KEY:LEFT/RIGHT — SUBSEL не нужен
    char uart_cmd[24];
    if (row == 4) {
        uart_cmd[0] = '\0';
    } else
    switch (s_last_mode) {
        case MODE_FEED:
        case MODE_AFEED:  snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:AP");   break;
        case MODE_THREAD: snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:PH");   break;
        case MODE_CONE_L:
        case MODE_CONE_R:
            if (row == 2) snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:CONE");
            else if (row == 3) snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:AP");
            break;
        case MODE_SPHERE:
            if (row == 2)      snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:BAR");
            else if (row == 3) snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:SPHERE");
            break;
        case MODE_DIVIDER:
            if (row == 0) snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:DIVN");
            else          snprintf(uart_cmd, sizeof(uart_cmd), "SUBSEL:DIVM");
            break;
        default: uart_cmd[0] = '\0'; break;
    }
    if (uart_cmd[0]) uart_protocol.sendButtonPress(uart_cmd);

    Serial.printf("SubEdit: row=%d mode=%d\n", row, (int)s_last_mode);
}

static void exit_sub_edit()
{
    // Отменить таймер grace period если активен
    if (g_sub_edit.exit_timer) {
        lv_timer_del(g_sub_edit.exit_timer);
        g_sub_edit.exit_timer = nullptr;
    }
    if (!g_sub_edit.active) return;
    highlight_sub_edit_row(g_sub_edit.row, false);
    g_sub_edit.active = false;
    g_sub_edit.row    = -1;
    Serial.println("SubEdit: OFF");
}

static void sub_edit_step(bool up)
{
    if (!g_sub_edit.active) return;
    g_sub_edit.last_ms = millis();
    int delta = up ? 1 : -1;
    char buf[32];

    // row4 = Pass_Total: UP → KEY:RIGHT (increase), DN → KEY:LEFT (decrease)
    if (g_sub_edit.row == 4) {
        uart_protocol.sendButtonPress(up ? "KEY:RIGHT" : "KEY:LEFT");
        return;
    }

    switch (s_last_mode) {
        case MODE_FEED:
        case MODE_AFEED:
            // row3: Ap — съём (мм*100), шаг 5 = 0.05мм, диапазон 0..990
            g_sub_edit.ap = (int16_t)constrain((int)g_sub_edit.ap + delta * 5, 0, 990);
            snprintf(buf, sizeof(buf), "%d.%02d",
                     g_sub_edit.ap / 100, g_sub_edit.ap % 100);
            lv_label_set_text(g_ui.row3_val, buf);
            uart_protocol.setApOptimistic(g_sub_edit.ap);
            snprintf(buf, sizeof(buf), "AP:%d", (int)g_sub_edit.ap);
            uart_protocol.sendButtonPress(buf);
            break;

        case MODE_THREAD:
            // row3 (ОБ/МИН или ХОД ММ) — read-only, не отправляем изменений
            break;

        case MODE_CONE_L:
        case MODE_CONE_R:
            if (g_sub_edit.row == 2) {
                // row3 = КОНУС (тип конуса), навигация по CONE_NAMES
                g_sub_edit.cone_idx = (uint8_t)constrain(
                    (int)g_sub_edit.cone_idx + delta, 0, CONE_COUNT - 1);
                lv_label_set_text(g_ui.row3_val, CONE_NAMES[g_sub_edit.cone_idx]);
                uart_protocol.setConeOptimistic(g_sub_edit.cone_idx);
                snprintf(buf, sizeof(buf), "CONE:%d", (int)g_sub_edit.cone_idx);
                uart_protocol.sendButtonPress(buf);
            } else if (g_sub_edit.row == 3) {
                // thr_type_box = СЪЁМ (Ap), шаг 5 = 0.05мм
                g_sub_edit.ap = (int16_t)constrain((int)g_sub_edit.ap + delta * 5, 0, 990);
                snprintf(buf, sizeof(buf), "%d.%02d",
                         g_sub_edit.ap / 100, g_sub_edit.ap % 100);
                if (g_ui.thr_type_val) lv_label_set_text(g_ui.thr_type_val, buf);
                snprintf(buf, sizeof(buf), "AP:%d", (int)g_sub_edit.ap);
                uart_protocol.sendButtonPress(buf);
            }
            break;

        case MODE_SPHERE:
            if (g_sub_edit.row == 2) {
                // row3 = НОЖКА ММ (bar_r в мм*100), шаг 25 = 0.25мм радиуса
                g_sub_edit.bar_r = (int16_t)constrain(
                    (int)g_sub_edit.bar_r + delta * 25, 0, 9900);
                // Показываем диаметр (bar_r * 2 / 100)
                int32_t bar_diam = (int32_t)g_sub_edit.bar_r * 2;
                snprintf(buf, sizeof(buf), "%ld.%02ld",
                         (long)(bar_diam / 100), (long)(bar_diam % 100));
                lv_label_set_text(g_ui.row3_val, buf);
                uart_protocol.setBarOptimistic(g_sub_edit.bar_r);
                snprintf(buf, sizeof(buf), "BAR:%d", (int)g_sub_edit.bar_r);
                uart_protocol.sendButtonPress(buf);
            } else if (g_sub_edit.row == 3) {
                // thr_type_box = ШАР ММ (sphere radius в мм*100), шаг 50 = 0.5мм радиуса = 1мм диаметра
                g_sub_edit.sphere_r = (int16_t)constrain(
                    (int)g_sub_edit.sphere_r + delta * 50, 50, 9999);
                // Показываем диаметр = radius*2, формат XX.X
                int32_t sphere_diam = (int32_t)g_sub_edit.sphere_r * 2;
                snprintf(buf, sizeof(buf), "%ld.%01ld",
                         (long)(sphere_diam / 100), (long)(abs(sphere_diam % 100) / 10));
                if (g_ui.thr_type_val) lv_label_set_text(g_ui.thr_type_val, buf);
                uart_protocol.setSphereOptimistic(g_sub_edit.sphere_r);
                snprintf(buf, sizeof(buf), "SPHERE:%d", (int)g_sub_edit.sphere_r);
                uart_protocol.sendButtonPress(buf);
            }
            break;

        case MODE_DIVIDER:
            if (g_sub_edit.row == 0) {
                // row1: Total_Tooth — на сколько частей делим (1..255)
                g_sub_edit.total_tooth = (uint16_t)constrain(
                    (int)g_sub_edit.total_tooth + delta, 1, 255);
                snprintf(buf, sizeof(buf), "%d", g_sub_edit.total_tooth);
                lv_label_set_text(g_ui.row1_val, buf);
                // Пересчитать угол сектора
                float sector = 360.0f / g_sub_edit.total_tooth;
                snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", sector);
                lv_label_set_text(g_ui.row3_val, buf);
                // Подправить current_tooth если вышло за пределы
                if (g_sub_edit.current_tooth > g_sub_edit.total_tooth)
                    g_sub_edit.current_tooth = g_sub_edit.total_tooth;
                snprintf(buf, sizeof(buf), "DIVN:%d", (int)g_sub_edit.total_tooth);
                uart_protocol.sendButtonPress(buf);
            } else if (g_sub_edit.row == 1) {
                // row2: Current_Tooth — текущая метка (1..total_tooth)
                uint16_t mx = g_sub_edit.total_tooth > 0 ? g_sub_edit.total_tooth : 1;
                g_sub_edit.current_tooth = (uint16_t)constrain(
                    (int)g_sub_edit.current_tooth + delta, 1, mx);
                snprintf(buf, sizeof(buf), "%d", g_sub_edit.current_tooth);
                lv_label_set_text(g_ui.row2_val, buf);
                snprintf(buf, sizeof(buf), "DIVM:%d", (int)g_sub_edit.current_tooth);
                uart_protocol.sendButtonPress(buf);
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// Алерт-уведомление — показывается поверх экрана 5 секунд или до нажатия OK
// ============================================================================

static void show_alert(const char* message)
{
    if (!g_ui.alert_box || !g_ui.alert_msg) return;
    lv_label_set_text(g_ui.alert_msg, message);
    // Длинное сообщение (ошибка G-кода) — окно выше и шрифт мельче, текст не наезжает на OK
    bool longmsg = strlen(message) > 160;
    const lv_font_t* afont = longmsg ? &font_tahoma_bold_16 : &font_tahoma_bold_22;
    lv_obj_set_style_text_font(g_ui.alert_msg, afont, 0);
    // высота текста — по ширине и шрифту сразу (lv_obj_update_layout ещё не знает о переносе строк:
    // третья строка уходила под кнопку OK)
    lv_point_t tsz;
    lv_txt_get_size(&tsz, message, afont, 0, lv_obj_get_style_text_line_space(g_ui.alert_msg, 0),
                    lv_obj_get_width(g_ui.alert_msg), LV_TEXT_FLAG_NONE);
    lv_coord_t h = UY(12) + tsz.y + UY(20) + UY(28) + UY(10);   // запас: строки шрифта выше расчётных
    if (h < UY(110)) h = UY(110);
    if (h > UY(250)) h = UY(250);
    lv_obj_set_height(g_ui.alert_box, h);
    lv_obj_clear_flag(g_ui.alert_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ui.alert_box);
    g_alert.active  = true;
    g_alert.show_ms = millis();
    g_alert.sticky  = uart_protocol.alertFromCore();
    // Кружок ⚠ → оранжевый
    if (g_ui.warn_bg) {
        lv_obj_set_style_bg_color(g_ui.warn_bg, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_shadow_width(g_ui.warn_bg, US(8), 0);
        lv_obj_set_style_shadow_color(g_ui.warn_bg, lv_color_hex(0xff8800), 0);
    }
    Serial.printf("Alert: %s\n", message);
}

static void dismiss_alert()
{
    if (!g_ui.alert_box) return;
    g_alert.active = false;
    g_alert.sticky = false;
    s_gcode_alert  = false;
    lv_obj_add_flag(g_ui.alert_box, LV_OBJ_FLAG_HIDDEN);
    // Кружок ⚠ → серый
    if (g_ui.warn_bg) {
        lv_obj_set_style_bg_color(g_ui.warn_bg, lv_color_hex(0x333333), 0);
        lv_obj_set_style_shadow_width(g_ui.warn_bg, 0, 0);
    }
}

// ============================================================================
// Обновление индикаторов лимитов в statusbar
// ============================================================================

static void update_limit_indicators(const LimitStatus& lim)
{
    // Z-ось (L/R): SET → голубой #00d4ff, NOT SET → тёмный #1a3a4a
    // X-ось (F/B): SET → зелёный #00ff88, NOT SET → тёмный #1a3a1a
    struct LimSet { lv_obj_t* lbl; bool on; uint32_t on_col; uint32_t off_col; };
    LimSet ls[] = {
        {g_ui.lim_L, lim.left  != 0, 0x00d4ff, 0x1a3a4a},  // Z- ←
        {g_ui.lim_R, lim.right != 0, 0x00d4ff, 0x1a3a4a},  // Z+ →
        {g_ui.lim_F, lim.front != 0, 0x00ff88, 0x1a3a1a},  // X+ ↑
        {g_ui.lim_B, lim.rear  != 0, 0x00ff88, 0x1a3a1a},  // X- ↓
    };
    for (auto& l : ls) {
        if (!l.lbl) continue;
        lv_obj_set_style_text_color(l.lbl,
            lv_color_hex(l.on ? l.on_col : l.off_col), 0);
    }
}

// ============================================================================
// SM-индикатор в Spare2 (row6) — показывает название текущего SelectMenu
// ============================================================================
static void update_sm_row6(LatheMode mode, uint8_t sm)
{
    if (!g_ui.sm_row6) return;

    // В M1: SM=2 — это "Ввод * Сброс Осей" (ДИАМЕТР/ОСЬ X), SM=3 — доп. параметры
    // В остальных: SM=2 — параметры режима, SM=3 — "Ввод * Сброс Осей"
    bool is_vvod = (mode == MODE_FEED) ? (sm == 2) : (sm == 3);
    bool is_param = (mode == MODE_FEED) ? (sm == 3) : (sm == 2);

    if (mode == MODE_RESERVE) {
        // G-код: как поставить ноль (подробно — окно «Перед стартом» на веб-странице)
        lv_label_set_text(g_ui.sm_row6,
            "\xD0\x9D\xD0\x9E\xD0\x9B\xD0\xAC Z: \xD0\xA2\xD0\x9E\xD0\xA0\xD0\x95\xD0\xA6\n"          // НОЛЬ Z: ТОРЕЦ
            DIAM_SIGN ": \xD0\x9A\xD0\x90\xD0\xA1\xD0\x90\xD0\x9D\xD0\x98\xD0\x95\n"                      // ⌀: КАСАНИЕ
            "\xD0\x98 \xD0\x92\xD0\x92\xD0\x9E\xD0\x94 " DIAM_SIGN);                                        // И ВВОД ⌀
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0x6a9fb5), 0);
    } else if (mode == MODE_DIVIDER) {
        // Делилка: подсказка LCD2004 «Сброс кнопкой Селект» (сброс угла)
        lv_label_set_text(g_ui.sm_row6,
            "\xD0\xA1\xD0\x91\xD0\xA0\xD0\x9E\xD0\xA1\n"
            "\xD0\x9A\xD0\x9D\xD0\x9E\xD0\x9F\xD0\x9A\xD0\x9E\xD0\x99\n"
            "\xD0\xA1\xD0\x95\xD0\x9B\xD0\x95\xD0\x9A\xD0\xA2");   // СБРОС / КНОПКОЙ / СЕЛЕКТ
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0x6a9fb5), 0);
    } else if (sm == 1) {
        // SM=1: индикация пропадает
        lv_label_set_text(g_ui.sm_row6, "--");
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0x2a3a4a), 0);
    } else if (is_vvod) {
        // ВВОД * СБРОС ОСЕЙ — жёлтый
        lv_label_set_text(g_ui.sm_row6,
            "\xD0\x92\xD0\x92\xD0\x9E\xD0\x94 " DIAM_SIGN "\n"
            "\xD0\xA1\xD0\x91\xD0\xA0\xD0\x9E\xD0\xA1\n"
            "\xD0\x9E\xD0\xA1\xD0\x95\xD0\x99");   // ВВОД ⌀ / СБРОС / ОСЕЙ (LCD «Ввод Ø  Сброс_осей»)
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0xffcc00), 0);
    } else if (is_param) {
        // ПАРАМЕТРЫ — оранжевый
        lv_label_set_text(g_ui.sm_row6,
            "\xD0\x9F\xD0\x90\xD0\xA0\xD0\x90\xD0\x9C\xD0\x95\xD0\xA2\xD0\xA0\xD0\xAB");  // ПАРАМЕТРЫ
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0xffaa44), 0);
    }
}

// ============================================================================
// Обновление всех виджетов UI по данным от станка
// Вызывается из onDataUpdate при каждом пришедшем пакете
// ============================================================================

// Helper: обновить thread_tag (tpi/G/K рядом с числами) и thr_cat_lbl (индикатор вверху)
// label — оригинальная строка из таблицы (до фильтрации), например "1.50mm", " 8tpi ", "G  1/8"
// Вызывается из update_ui_values и из UP/DN обработчиков
static void update_thread_type_indicators(const char* label)
{
    bool is_tpi = label && strstr(label, "tpi") != nullptr;
    bool is_g   = label && label[0] == 'G';
    bool is_k   = label && label[0] == 'K';

    // thread_tag: вызывает квадраты в K-layout (digit-font), всегда скрываем
    if (g_ui.thread_tag) lv_obj_add_flag(g_ui.thread_tag, LV_OBJ_FLAG_HIDDEN);

    // thr_type_val: отдельная жёлтая ячейка с полным названием типа резьбы
    if (g_ui.thr_type_val) {
        if (is_tpi)
            lv_label_set_text(g_ui.thr_type_val,
                "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C");   // ДЮЙМ
        else if (is_g)
            lv_label_set_text(g_ui.thr_type_val,
                "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // G-ТРУБ
        else if (is_k)
            lv_label_set_text(g_ui.thr_type_val,
                "\xD0\x9A-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91"); // К-ТРУБ
        else
            lv_label_set_text(g_ui.thr_type_val,
                "\xD0\x9C\xD0\x95\xD0\xA2\xD0\xA0\xD0\x98\xD0\xA7");  // МЕТРИЧ
    }
}

// Раскладка режима + подэкран SM. Сначала всегда раскладка режима: подэкраны меняют
// часть подписей (например «УГОЛ °» в M2 SM2), и без сброса они оставались на SM3.
static void apply_sm_layout(LatheMode mode, uint8_t sm)
{
    apply_mode_layout(mode);
    if (sm == 2) {
        if (mode == MODE_AFEED)  apply_afeed_sm2_layout();
        else if (mode == MODE_FEED)   apply_feed_sm2_layout();
        else if (mode == MODE_THREAD) apply_thread_sm2_layout();
        else if (mode == MODE_CONE_L || mode == MODE_CONE_R) apply_cone_sm2_layout();
        else if (mode == MODE_SPHERE) apply_sphere_sm2_layout();
    } else if (sm == 3) {
        if (mode == MODE_FEED) apply_feed_sm3_layout();
        else if (mode != MODE_DIVIDER) apply_common_sm3_layout();
    }
}

// M8 — G-код: состояние программы (исполнитель на дисплее, файлы — веб-страница), строка и процент
static void gcode_status_update()
{
    char b[48], l[32];
    GcExecSt st = GCodeExec_GetState();
    const char* t; uint32_t col = 0x00d4ff;
    switch (st) {
        case GCE_RUNNING: case GCE_WAITING_ACK: case GCE_DWELL:
            snprintf(b, sizeof(b), "N%d", GCodeExec_GetLine()); t = b; col = 0x00ff88; break;
        case GCE_PAUSED: t = "\xD0\x9F\xD0\x90\xD0\xA3\xD0\x97\xD0\x90"; col = 0xffcc00; break;
        case GCE_DONE:   t = "\xD0\x93\xD0\x9E\xD0\xA2\xD0\x9E\xD0\x92\xD0\x9E"; col = 0x00ff88; break;
        case GCE_ERROR:  t = "\xD0\x9E\xD0\xA8\xD0\x98\xD0\x91\xD0\x9A\xD0\x90"; col = 0xff5555; break;
        default:         t = "\xD0\x93\xD0\x9E\xD0\xA2\xD0\x9E\xD0\x92"; break;
    }
    lv_label_set_text(g_ui.primary_val, t);
    if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, t);
    lv_obj_set_style_text_color(g_ui.primary_val, lv_color_hex(col), 0);
    if (st == GCE_IDLE) snprintf(l, sizeof(l), "--");
    else snprintf(l, sizeof(l), "%d%%", GCodeExec_GetProgress());   // номер строки — крупно выше («N7»)
    // ячейка узкая — причина ошибки целиком в сообщении (один раз при переходе в ошибку)
    static GcExecSt s_prev = GCE_IDLE;
    if (st == GCE_ERROR && s_prev != GCE_ERROR) {
        show_alert(GCodeExec_GetError());
        g_alert.sticky = true;                 // висит до OK — причину надо успеть прочитать
        s_gcode_alert = true;
    }
    s_prev = st;
    if (st == GCE_ERROR) snprintf(l, sizeof(l), "N%d", GCodeExec_GetLine());   // «ОШИБКА» — крупно выше
    lv_label_set_text(g_ui.row3_val, l);
    {   // осталось времени (расчёт по модели станка с поправкой по факту): м:сс или ч:мм:сс
        int eta = GCodeExec_GetEta();
        char e[16];
        if (eta < 0) snprintf(e, sizeof(e), "--");
        else if (eta >= 3600) snprintf(e, sizeof(e), "%d:%02d:%02d", eta / 3600, eta % 3600 / 60, eta % 60);
        else snprintf(e, sizeof(e), "%d:%02d", eta / 60, eta % 60);
        lv_label_set_text(g_ui.row4_val, e);
        lv_obj_set_style_text_color(g_ui.row4_val, lv_color_hex(0xe0e0e0), 0);
    }
    lv_label_set_long_mode(g_ui.row3_val, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(st == GCE_ERROR ? 0xff5555 : 0xe0e0e0), 0);
    const char* f = GCodeExec_GetFilename();
    // имя файла (режим «G-КОД» — в шапке); длинное — с многоточием по ширине колонки
    snprintf(b, sizeof(b), "%s", f && *f ? (strrchr(f, '/') ? strrchr(f, '/') + 1 : f) : "\xD0\xA4\xD0\x90\xD0\x99\xD0\x9B \xD0\x9D\xD0\x95 \xD0\x92\xD0\xAB\xD0\x91\xD0\xA0\xD0\x90\xD0\x9D");   // ФАЙЛ НЕ ВЫБРАН
    lv_label_set_text(g_ui.primary_unit, b);
}

static void update_ui_values(const LatheData& data)
{
    if (!g_ui.primary_val) return;  // UI не инициализирован

    char buf[32];

    // ── Смена режима: обновить подписи ───────────────────────────────────────
    if (data.mode != s_last_mode) {
        s_last_mode = data.mode;
        s_last_select_menu = data.select_menu;
        if (g_alert.active && (!g_alert.sticky || s_gcode_alert)) dismiss_alert();  // флаги ядра переживают смену режима, как на LCD2004
        apply_sm_layout(data.mode, data.select_menu);
    } else if (data.select_menu != s_last_select_menu) {
        s_last_select_menu = data.select_menu;
        // SM изменился внутри текущего режима
        apply_sm_layout(data.mode, data.select_menu);
    }

    // ── Statusbar: режим, подрежим и лимиты ─────────────────────────────────
    {
        int mi = (int)data.mode;
        if (mi >= 1 && mi <= 8) lv_label_set_text(g_ui.mode_lbl, MODE_STRS[mi]);
        if (s_sphere_alert && data.mode != MODE_SPHERE) {   // ушли из M6 — алерт неактуален
            if (g_alert.active) dismiss_alert();
            s_sphere_alert = false;
        }

        int si = (int)data.submode;
        if (si >= 1 && si <= 3) lv_label_set_text(g_ui.submode_lbl, SUBMODE_STRS[si]);

        // M6 ШАР: override submode_lbl with sphere state (matching old LCD)
        if (data.mode == MODE_SPHERE && g_ui.submode_lbl) {
            if (data.submode == SUBMODE_INTERNAL) {
                lv_label_set_text(g_ui.submode_lbl,
                    "\xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC "
                    "\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB7\xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD!");  // Режим невозможен!
                lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0xff4444), 0);
            } else if (data.submode == SUBMODE_MANUAL) {
                lv_label_set_text(g_ui.submode_lbl,
                    "\xD0\x9E\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD");  // Отключен
                lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0x888888), 0);
            } else {
                lv_label_set_text(g_ui.submode_lbl,
                    "\xD0\x92\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD");  // Включен
                lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0x00ff88), 0);
            }
        } else {
            lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0x4a7a8a), 0);
        }

        // Кружок подрежима S1/S2/S3
        if (g_ui.s2_bg && g_ui.s2_lbl && si >= 1 && si <= 3) {
            static const char* snames[] = {"S1","S2","S3"};
            // S1=синий(внутренняя), S2=зелёный(ручная), S3=голубой(наружная)
            static const uint32_t scols[] = {0x0088ff, 0x00ff88, 0x00d4ff};
            lv_label_set_text(g_ui.s2_lbl, snames[si - 1]);
            lv_obj_set_style_bg_color(g_ui.s2_bg, lv_color_hex(scols[si - 1]), 0);
            lv_obj_set_style_shadow_color(g_ui.s2_bg, lv_color_hex(scols[si - 1]), 0);
        }

        // Кружок мотора Z / индикатор работы (STATE:run)
        if (g_ui.pwr_bg) {
            bool on = data.motor_z_enabled || data.is_running;
            lv_obj_set_style_bg_color(g_ui.pwr_bg,
                lv_color_hex(on ? 0x00ff88 : 0x333333), 0);
            lv_obj_set_style_shadow_color(g_ui.pwr_bg,
                lv_color_hex(on ? 0x00ff88 : 0x333333), 0);
        }

        update_limit_indicators(data.limits);
        update_sm_row6(data.mode, data.select_menu);
    }

    // ── Левая панель: основное значение (cyan) ───────────────────────────────
    // В режиме редактирования: не перезаписывать — пользователь навигирует локально
    if (!g_edit_param.active) {
        switch (data.mode) {
            case MODE_THREAD:
                if (data.thread_name[0] != '\0') {
                    // Обновляем подпись: тип резьбы (определяем до фильтрации)
                    if (strstr(data.thread_name, "tpi"))
                        lv_label_set_text(g_ui.primary_unit,
                            "\xD0\x94\xD0\xAE\xD0\x99\xD0\x9C");             // ДЮЙМ
                    else if (data.thread_name[0] == 'G')
                        lv_label_set_text(g_ui.primary_unit, "G-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");  // G-ТРУБ
                    else if (data.thread_name[0] == 'K')
                        lv_label_set_text(g_ui.primary_unit, "K-\xD0\xA2\xD0\xA0\xD0\xA3\xD0\x91");  // K-ТРУБ
                    else
                        lv_label_set_text(g_ui.primary_unit,
                            "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");   // ШАГ ММ
                    // Issues 2&3: font_tahoma_bold_72 имеет только символы 0x2B,0x2D-0x3A
                    // Фильтруем thread_name: оставляем +, -, ., /, 0-9, :
                    // "1.50mm" → "1.50", " 8tpi " → "8", "G  1/8" → "1/8"
                    char* dst = buf;
                    for (const char* src = data.thread_name; *src && dst < buf + sizeof(buf) - 1; src++) {
                        uint8_t c = (uint8_t)*src;
                        if (c == 0x2B || (c >= 0x2D && c <= 0x3A))
                            *dst++ = *src;
                    }
                    *dst = '\0';
                    if (buf[0] == '\0') {
                        // Резерв: ничего из шрифта нет — показываем числовой шаг
                        snprintf(buf, sizeof(buf), "%d.%02d",
                                 data.thread_mm / 100, abs(data.thread_mm % 100));
                    }
                } else {
                    snprintf(buf, sizeof(buf), "%d.%02d",
                             data.thread_mm / 100, abs(data.thread_mm % 100));
                    lv_label_set_text(g_ui.primary_unit,
                        "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");       // ШАГ ММ
                }
                break;
            case MODE_FEED:
            case MODE_CONE_L:
            case MODE_CONE_R:
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.feed_mm / 100, abs(data.feed_mm % 100));
                break;
            case MODE_AFEED:
                if (data.select_menu == 2) {
                    // SelectMenu=2: показываем текущий угол шпинделя (Текущий угол)
                    snprintf(buf, sizeof(buf), "%d.%d",
                             data.spindle_angle / 10, abs(data.spindle_angle % 10));
                } else {
                    snprintf(buf, sizeof(buf), "%d", data.afeed_mm);
                }
                break;
            case MODE_SPHERE:
                // Подача (feed_mm), как в M1/M4
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.feed_mm / 100, abs(data.feed_mm % 100));
                break;
            case MODE_DIVIDER:
                snprintf(buf, sizeof(buf), "%d.%d",
                         data.spindle_angle / 10, abs(data.spindle_angle % 10));
                break;
            default:
                snprintf(buf, sizeof(buf), "---");
                break;
        }
        lv_label_set_text(g_ui.primary_val, buf);
        if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        // Fix D+E: Update thread tag and statusbar category indicator when in M3
        if (data.mode == MODE_THREAD)
            update_thread_type_indicators(data.thread_name);
    }

    // ── Левая панель: вторичное значение — всегда RPM ───────────────────────
    snprintf(buf, sizeof(buf), "%d", data.rpm);
    lv_label_set_text(g_ui.secondary_val, buf);
    if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);

    // ── Правая панель: row4 (ПРОХОДЫ/ЗАХОДОВ/ЦИКЛОВ) ─────────────────────────
    // Как LCD2004: крупно — сколько проходов ОСТАЛОСЬ (total − nr + 1, в цикле убывает),
    // через дробь — всего. В «Ручном» nr = 1, поэтому видно «всего/всего».
    if (g_ui.row4_val) {
        int total = -1;
        switch (data.mode) {
            case MODE_THREAD:  total = data.thread_cycles; break;                 // таблица + чистовые
            case MODE_FEED:
            case MODE_AFEED:
            case MODE_CONE_L:
            case MODE_CONE_R:  total = data.pass_total; break;
            case MODE_SPHERE:  total = data.pass_total_sphr > 0 ? (int)data.pass_total_sphr + 1 : 0; break;  // Sphere.ino: до Pass_Total_Sphr+1
            default: break;
        }
        if (total > 0) {
            int left = total - (int)data.pass_nr + 1;
            if (left < 0) left = 0;
            snprintf(buf, sizeof(buf), "#00ff88 %d#/#ffcc00 %d#", left, total);
            lv_label_set_text(g_ui.row4_val, buf);
        } else if (total == 0) {
            lv_label_set_text(g_ui.row4_val, "--");
        }
    }

    // ── RPM bar ──────────────────────────────────────────────────────────────
    lv_bar_set_value(g_ui.rpm_bar, data.rpm, LV_ANIM_OFF);

    // ── Диаметр (как Ø на LCD2004): все режимы, кроме делилки; в G-КОД от него — X программы ─────
    if (g_ui.diam_val) {
        bool show = data.mode != MODE_DIVIDER;
        if (show) {
            if (g_ui.diam_box) lv_obj_clear_flag(g_ui.diam_box, LV_OBJ_FLAG_HIDDEN);
            DisplayFormatter::formatPosition(buf, data.diam_x);
            lv_label_set_text(g_ui.diam_val, buf);
            lv_obj_set_style_text_color(g_ui.diam_val,
                data.diam_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        } else {
            if (g_ui.diam_box) lv_obj_add_flag(g_ui.diam_box, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ── Правая панель: row1 и row2 ───────────────────────────────────────────
    // M7: row1=Делений, row2=Метка; остальные: row1=ПОЗ Z, row2=ПОЗ X
    if (data.mode == MODE_DIVIDER) {
        // row1: Total_Tooth (не обновляем если редактируется)
        if (!(g_sub_edit.active && g_sub_edit.row == 0)) {
            snprintf(buf, sizeof(buf), "%d", data.total_tooth);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
        }
        // row2: Current_Tooth
        if (!(g_sub_edit.active && g_sub_edit.row == 1)) {
            snprintf(buf, sizeof(buf), "%d", data.current_tooth);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
        }
    } else if (data.select_menu == 3 && data.mode != MODE_FEED && data.mode != MODE_DIVIDER) {
        // SM=3 Ввод/Сброс Осей (M2/M3/M4/M5/M6): row1=ОСЬ Y, row2=ОСЬ X (ДИАМЕТР — постоянная плитка)
        DisplayFormatter::formatPosition(buf, data.pos_z);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val,
            data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        DisplayFormatter::formatPosition(buf, data.pos_x);
        lv_label_set_text(g_ui.row2_val, buf);
        lv_obj_set_style_text_color(g_ui.row2_val,
            data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
    } else if (data.mode == MODE_SPHERE) {
        if (data.select_menu == 2) {
            // M6 SM=2: row1=ШИРИНА РЕЗЦА, row2=ШАГ ОСИ Z
            snprintf(buf, sizeof(buf), "%d.%02d",
                     data.cutter_w / 100, abs(data.cutter_w % 100));
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
            snprintf(buf, sizeof(buf), "%d.%02d",
                     data.cutting_w / 100, abs(data.cutting_w % 100));
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
        } else {
            // M6 SM=1: row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X
            DisplayFormatter::formatPosition(buf, data.pos_z);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val,
                data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
            DisplayFormatter::formatPosition(buf, data.pos_x);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val,
                data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        }
    } else if (data.mode == MODE_CONE_L || data.mode == MODE_CONE_R) {
        if (data.select_menu == 2) {
            // M4/M5 SM=2: КОНУС (тип) + К.РЕЗЬБА (флаг)
            int idx = constrain((int)data.cone_idx, 0, CONE_COUNT - 1);
            lv_label_set_text(g_ui.row1_val, CONE_NAMES[idx]);
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
            lv_label_set_text(g_ui.row2_val, data.cone_thr
                ? "\xD0\x92\xD0\x9A\xD0\x9B"    // ВКЛ
                : "\xD0\x92\xD0\xAB\xD0\x9A\xD0\x9B");  // ВЫКЛ
            lv_obj_set_style_text_color(g_ui.row2_val,
                data.cone_thr ? lv_color_hex(0x00ff88) : lv_color_hex(0x888888), 0);
        } else {
            // M4/M5 SM=1: row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X (всегда)
            DisplayFormatter::formatPosition(buf, data.pos_z);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val,
                data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
            DisplayFormatter::formatPosition(buf, data.pos_x);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val,
                data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        }
    } else if (data.mode == MODE_AFEED && data.select_menu == 2) {
        // M2 SelectMenu=2: подэкран делителя — Делений / Метка
        snprintf(buf, sizeof(buf), "%d", data.total_tooth);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);

        snprintf(buf, sizeof(buf), "%d", data.current_tooth);
        lv_label_set_text(g_ui.row2_val, buf);
        lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
    } else if (data.mode == MODE_THREAD) {
        if (data.select_menu == 2) {
            // M3 SM=2: ЧИСТ.ПР + ЗАХОДОВ
            snprintf(buf, sizeof(buf), "%d", data.pass_fin);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
            snprintf(buf, sizeof(buf), "%d", data.ph);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
        } else {
            // M3 SM=1: row1=ПОЗИЦИЯ Z, row2=ПОЗИЦИЯ X (всегда)
            DisplayFormatter::formatPosition(buf, data.pos_z);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val,
                data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
            DisplayFormatter::formatPosition(buf, data.pos_x);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val,
                data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        }
    } else if (data.mode == MODE_FEED && data.select_menu == 2) {
        // M1 SM=2: ОСЬ Y + ОСЬ X (ДИАМЕТР — постоянная плитка)
        DisplayFormatter::formatPosition(buf, data.pos_z);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val,
            data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
        DisplayFormatter::formatPosition(buf, data.pos_x);
        lv_label_set_text(g_ui.row2_val, buf);
        lv_obj_set_style_text_color(g_ui.row2_val,
            data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
    } else if (data.mode == MODE_FEED && data.select_menu == 3) {
        // M1 SM=3: ОТСКОК Z + НАТЯГ Z
        {
            int32_t ov = data.otskok_z;
            snprintf(buf, sizeof(buf), "%ld.%02ld",
                     (long)(abs(ov) / 1000), (long)(abs(ov) % 1000 / 10));
            lv_label_set_text(g_ui.row1_val, buf);
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
        }
        {
            int32_t tv = data.tension_z;
            snprintf(buf, sizeof(buf), "%ld.%02ld",
                     (long)(abs(tv) / 1000), (long)(abs(tv) % 1000 / 10));
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
        }
    } else {
        // M1 SM=1, M2 SM=1, others: row1=Позиция Z, row2=Позиция X
        DisplayFormatter::formatPosition(buf, data.pos_z);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val,
            data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);

        DisplayFormatter::formatPosition(buf, data.pos_x);
        lv_label_set_text(g_ui.row2_val, buf);
        lv_obj_set_style_text_color(g_ui.row2_val,
            data.pos_x >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
    }

    // ── Правая панель row3: зависит от режима, не перезаписывать при sub-edit ─
    if (g_sub_edit.active && g_sub_edit.row == 2) return;  // row3 редактируется
    // При редактировании Total_Tooth (row 0) в делилке sub_edit_step сам обновляет сектор
    if (data.mode == MODE_DIVIDER && g_sub_edit.active && g_sub_edit.row == 0) return;

    switch (data.mode) {
        case MODE_FEED:
            if (data.select_menu == 2) {
                // M1 SM=2: Ввод/Сброс осей — ось Y уже в row1
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            } else if (data.select_menu == 3) {
                // M1 SM=3: "---" (row1/row2 уже показывают отскок/натяг)
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            } else {
                // M1 SM=1: СЪЁМ (Ap): мм
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.ap / 100, abs(data.ap % 100));
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            }
            break;

        case MODE_AFEED:
            if (data.select_menu == 3) {
                // M2 SM=3: Ввод/Сброс осей — ось Y уже в row1
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
                break;
            }
            if (data.select_menu == 2) {
                // M2 SelectMenu=2: Угол сектора = 360*(current_tooth-1)/total_tooth
                if (data.total_tooth > 0) {
                    float req = 360.0f * (data.current_tooth > 0 ? data.current_tooth - 1 : 0)
                                / data.total_tooth;
                    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", req);
                } else {
                    snprintf(buf, sizeof(buf), "---");
                }
            } else {
                // СЪЁМ (Ap): мм
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.ap / 100, abs(data.ap % 100));
            }
            lv_label_set_text(g_ui.row3_val, buf);
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;

        case MODE_THREAD:
            if (data.select_menu == 3) {
                // M3 SM=3: Ввод/Сброс осей — ось Y уже в row1
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
                break;
            }
            if (data.select_menu == 2) {
                // M3 SM=2: ХОД ММ (thread_travel)
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.thread_travel / 100, abs(data.thread_travel % 100));
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            } else {
                // M3 SM=1: Ph==1 → ОБ/МИН (RPM лимит); Ph>1 → ХОД ММ (travel)
                if (data.ph > 1) {
                    lv_label_set_text(g_ui.row3_title,
                        "\xD0\xA5\xD0\x9E\xD0\x94 \xD0\x9C\xD0\x9C");      // ХОД ММ
                    snprintf(buf, sizeof(buf), "%d.%02d",
                             data.thread_travel / 100, abs(data.thread_travel % 100));
                } else {
                    lv_label_set_text(g_ui.row3_title,
                        "\xD0\x9E\xD0\x91/\xD0\x9C\xD0\x98\xD0\x9D");      // ОБ/МИН
                    snprintf(buf, sizeof(buf), "%d", data.rpm_limit);
                }
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            }
            break;

        case MODE_SPHERE: {
            if (data.select_menu == 3) {
                // M6 SM=3: Ввод/Сброс осей — ось Y уже в row1
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
                break;
            }
            // НОЖКА ММ = bar_r * 2 / 100 (диаметр, как на старом LCD: Bar_R_mm*2/100)
            int32_t bar_diam = (int32_t)data.bar_r * 2;
            snprintf(buf, sizeof(buf), "%ld.%02ld",
                     (long)(bar_diam / 100), (long)abs(bar_diam % 100));
            lv_label_set_text(g_ui.row3_val, buf);
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            // thr_type_box: ШАР ММ (диаметр шара) — обновляем если не в sub-edit
            if (g_ui.thr_type_val && !(g_sub_edit.active && g_sub_edit.row == 3)) {
                int32_t sphere_diam = (int32_t)data.sphere_radius * 2;
                snprintf(buf, sizeof(buf), "%ld.%01ld",
                         (long)(sphere_diam / 100), (long)(abs(sphere_diam % 100) / 10));
                lv_label_set_text(g_ui.thr_type_val, buf);
            }
            // Алерт "Режим невозможен!" при INTERNAL подрежиме; снимается, как только подрежим сменили
            if (data.submode == SUBMODE_INTERNAL && !g_alert.active) {
                show_alert("\xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC "
                           "\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB7\xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD!");
                g_alert.sticky = true;   // как на LCD2004: горит, пока подрежим Внутренний
                s_sphere_alert = true;
            } else if (data.submode != SUBMODE_INTERNAL && s_sphere_alert) {
                if (g_alert.active) dismiss_alert();
                s_sphere_alert = false;
            }
            break;
        }

        case MODE_DIVIDER:
            // УГОЛ° (угол сектора = 360/total_tooth)
            if (data.total_tooth > 0) {
                float sector = 360.0f / data.total_tooth;
                snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", sector);
            } else {
                snprintf(buf, sizeof(buf), "---");
            }
            lv_label_set_text(g_ui.row3_val, buf);
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            break;

        case MODE_CONE_L:
        case MODE_CONE_R:
            if (data.select_menu == 3) {
                // M4/M5 SM=3: Ввод/Сброс осей — ось Y уже в row1
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            } else if (data.select_menu == 2) {
                // M4/M5 SM=2: при включённой конической резьбе — подсказка LCD2004 «В режиме резьба»
                if (data.cone_thr) {
                    lv_label_set_text(g_ui.row3_title, "\xD0\x92 \xD0\xA0\xD0\x95\xD0\x96\xD0\x98\xD0\x9C\xD0\x95");  // В РЕЖИМЕ
                    lv_label_set_text(g_ui.row3_val, "\xD0\xA0\xD0\x95\xD0\x97\xD0\xAC\xD0\x91\xD0\x90");                // РЕЗЬБА
                    lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xffcc00), 0);
                } else {
                    lv_label_set_text(g_ui.row3_title, "");
                    lv_label_set_text(g_ui.row3_val, "---");
                    lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
                }
            } else if (!(g_sub_edit.active && g_sub_edit.row == 2)) {
                // M4/M5 SM=1: КОНУС (тип конуса, пропустить если редактируется)
                int idx = constrain((int)data.cone_idx, 0, CONE_COUNT - 1);
                lv_label_set_text(g_ui.row3_val, CONE_NAMES[idx]);
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0xe0e0e0), 0);
            }
            // thr_type_box: СЪЁМ (Ap) — обновляем только если не в sub-edit для этой ячейки
            if (g_ui.thr_type_val && !(g_sub_edit.active && g_sub_edit.row == 3)) {
                snprintf(buf, sizeof(buf), "%d.%02d",
                         data.ap / 100, abs(data.ap % 100));
                lv_label_set_text(g_ui.thr_type_val, buf);
            }
            break;

        default:
            lv_label_set_text(g_ui.row3_val, "---");
            lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
            break;
    }

    if (data.mode == MODE_RESERVE) gcode_status_update();   // M8 — G-код: поверх общих значений
}

// ============================================================================
// Демо-режим — 30 сек, 8 режимов × 3.75с, тройной тап OK
// ============================================================================

static const uint32_t DEMO_TOTAL_MS  = 30000;
static const uint32_t DEMO_MODE_MS   = 3750;
static const uint32_t DEMO_UPDATE_MS = 120;

static void demo_start()
{
    if (g_edit_param.active) exit_edit_mode();
    if (g_sub_edit.active)   exit_sub_edit();
    g_demo_active       = true;
    g_demo_start_ms     = millis();
    g_demo_last_step_ms = millis() - DEMO_UPDATE_MS;  // обновить сразу
    g_demo_mode_idx     = -1;
    Serial.println("DEMO: start (triple-tap OK to stop)");
}

static void demo_stop()
{
    g_demo_active = false;
    // Убрать алерт если был показан
    if (g_alert.active) dismiss_alert();
    // Восстановить лимиты (все выкл)
    LimitStatus no_lims = {0,0,0,0};
    update_limit_indicators(no_lims);
    // Кружки в дефолтное состояние
    if (g_ui.s2_bg)  lv_obj_set_style_bg_color(g_ui.s2_bg, lv_color_hex(0x00ff88), 0);
    if (g_ui.s2_bg)  lv_obj_set_style_shadow_color(g_ui.s2_bg, lv_color_hex(0x00ff88), 0);
    if (g_ui.s2_lbl) lv_label_set_text(g_ui.s2_lbl, "S2");
    if (g_ui.pwr_bg) lv_obj_set_style_bg_color(g_ui.pwr_bg, lv_color_hex(0x00ff88), 0);
    if (g_ui.pwr_bg) lv_obj_set_style_shadow_color(g_ui.pwr_bg, lv_color_hex(0x00ff88), 0);
    lv_label_set_text(g_ui.submode_lbl, "");
    lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0x4a7a8a), 0);
    // Вернуть к начальному состоянию M3 (стартовый)
    s_last_mode = MODE_THREAD;
    apply_mode_layout(MODE_THREAD);
    lv_label_set_text(g_ui.mode_lbl, MODE_STRS[3]);
    Serial.println("DEMO: stop");
}

static void demo_tick()
{
    uint32_t now     = millis();
    uint32_t elapsed = now - g_demo_start_ms;

    if (elapsed >= DEMO_TOTAL_MS) { demo_stop(); return; }
    if (now - g_demo_last_step_ms < DEMO_UPDATE_MS) return;
    g_demo_last_step_ms = now;

    // Текущий режим (0..7 = M1..M8)
    int m = (int)(elapsed / DEMO_MODE_MS);
    if (m > 7) m = 7;
    // Прогресс внутри режима 0..99
    int pct = (int)((elapsed % DEMO_MODE_MS) * 100 / DEMO_MODE_MS);

    // Смена режима
    if (m != g_demo_mode_idx) {
        g_demo_mode_idx = m;
        LatheMode nm = (LatheMode)(m + 1);
        s_last_mode = nm;
        apply_mode_layout(nm);
        lv_label_set_text(g_ui.mode_lbl, MODE_STRS[m + 1]);
        lv_label_set_text(g_ui.submode_lbl,
            "\xD0\x94\xD0\x95\xD0\x9C\xD0\x9E");  // ДЕМО
        lv_obj_set_style_text_color(g_ui.submode_lbl, lv_color_hex(0xff8800), 0);
        pct = 0;
    }

    char buf[32];
    LatheMode cm = (LatheMode)(g_demo_mode_idx + 1);

    // Позиции (общие для M1..M6, не ДЕЛИЛКА и не РЕЗЕРВ)
    if (cm != MODE_DIVIDER && cm != MODE_RESERVE) {
        snprintf(buf, sizeof(buf), "+%d.%02d", pct * 2, pct);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0x00ff88), 0);
        snprintf(buf, sizeof(buf), "-%d.%02d", pct / 2, (pct * 50) % 100);
        lv_label_set_text(g_ui.row2_val, buf);
        lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xff5555), 0);
    }

    switch (cm) {
        case MODE_FEED: {
            int feed = 5 + pct * 20 / 100;   // 0.05..0.25 mm/ob
            snprintf(buf, sizeof(buf), "0.%02d", feed);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            int pass = 1 + pct * 4 / 100;    // 1..5
            snprintf(buf, sizeof(buf), "%d/5", pass);
            lv_label_set_text(g_ui.secondary_val, buf);
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);
            int ap = 5 + pct * 45 / 100;     // 0.05..0.50 мм съём
            snprintf(buf, sizeof(buf), "0.%02d", ap);
            lv_label_set_text(g_ui.row3_val, buf);
            lv_bar_set_value(g_ui.rpm_bar, pct * 20, LV_ANIM_OFF);
            break;
        }
        case MODE_AFEED: {
            int af = 1000 + pct * 390;  // 10.00..400.00 mm/min
            snprintf(buf, sizeof(buf), "%d.%02d", af / 100, af % 100);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            int pass = 1 + pct * 4 / 100;
            snprintf(buf, sizeof(buf), "%d/5", pass);
            lv_label_set_text(g_ui.secondary_val, buf);
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);
            int ap = 5 + pct * 45 / 100;
            snprintf(buf, sizeof(buf), "0.%02d", ap);
            lv_label_set_text(g_ui.row3_val, buf);
            lv_bar_set_value(g_ui.rpm_bar, pct * 25, LV_ANIM_OFF);
            break;
        }
        case MODE_THREAD: {
            int tbl = pct * 19 / 100;   // 0..19 метрических шагов
            int16_t mm = s_thread_table[tbl].mm_x100;
            snprintf(buf, sizeof(buf), "%d.%02d", mm / 100, abs(mm % 100));
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            int pass = 1 + pct * 3 / 100;   // 1..4
            snprintf(buf, sizeof(buf), "%d/4", pass);
            lv_label_set_text(g_ui.secondary_val, buf);
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);
            int ph = 1 + pct * 3 / 100;     // 1..4 захода
            snprintf(buf, sizeof(buf), "%d", ph);
            lv_label_set_text(g_ui.row3_val, buf);
            lv_bar_set_value(g_ui.rpm_bar, 800, LV_ANIM_OFF);
            break;
        }
        case MODE_CONE_L:
        case MODE_CONE_R: {
            int cf = 50 + pct * 250 / 100;  // 0.50..3.00 mm/ob
            snprintf(buf, sizeof(buf), "%d.%02d", cf / 100, cf % 100);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            int rpm = 600 + pct * 10;        // 600..1600 RPM
            snprintf(buf, sizeof(buf), "%d", rpm);
            lv_label_set_text(g_ui.secondary_val, buf);
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);
            int ci = pct * (CONE_COUNT - 1) / 100;
            // BUG-07: row1=RPM, row2=cone type, row3=angle
            snprintf(buf, sizeof(buf), "%d", rpm);
            lv_label_set_text(g_ui.row1_val, buf);
            lv_label_set_text(g_ui.row2_val, CONE_NAMES[ci]);
            snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", (float)(ci < 2 ? (ci == 0 ? 450 : 300) : (ci - 1) * 10) / 10.0f);
            lv_label_set_text(g_ui.row3_val, buf);
            lv_bar_set_value(g_ui.rpm_bar, rpm, LV_ANIM_OFF);
            break;
        }
        case MODE_SPHERE: {
            int sr = 1000 + pct * 7000 / 100;  // radius 10..80mm (*100)
            int sd = sr * 2;
            snprintf(buf, sizeof(buf), "%d.%02d", sd / 100, sd % 100);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            int pass = 1 + pct * 2 / 100;   // 1..3
            snprintf(buf, sizeof(buf), "%d/3", pass);
            lv_label_set_text(g_ui.secondary_val, buf);
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, buf);
            snprintf(buf, sizeof(buf), "%d.%02d", sr / 100, sr % 100);
            lv_label_set_text(g_ui.row3_val, buf);
            lv_bar_set_value(g_ui.rpm_bar, 1200, LV_ANIM_OFF);
            break;
        }
        case MODE_DIVIDER: {
            int cur = 1 + pct * 11 / 100;   // 1..12 (при total=12)
            float ang = (float)(cur - 1) * 30.0f;
            snprintf(buf, sizeof(buf), "%.0f", ang);
            lv_label_set_text(g_ui.primary_val, buf);
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
            lv_label_set_text(g_ui.secondary_val, "30.0");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "30.0");
            lv_label_set_text(g_ui.row1_val, "12");
            lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
            snprintf(buf, sizeof(buf), "%d", cur);
            lv_label_set_text(g_ui.row2_val, buf);
            lv_obj_set_style_text_color(g_ui.row2_val, lv_color_hex(0xe0e0e0), 0);
            lv_label_set_text(g_ui.row3_val, "30.0\xC2\xB0");
            lv_bar_set_value(g_ui.rpm_bar, 0, LV_ANIM_OFF);
            break;
        }
        default:
            lv_label_set_text(g_ui.primary_val, "---");
            if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, "---");
            lv_label_set_text(g_ui.secondary_val, "---");
            if (g_ui.secondary_val_glow) lv_label_set_text(g_ui.secondary_val_glow, "---");
            break;
    }

    // ── Кружок подрежима: меняется с каждым режимом ─────────────────────────
    {
        // S1(синий) для чётных режимов, S2(зелёный) для нечётных, S3(голубой) для M5/M7
        static const uint32_t s_scols[3] = {0x0088ff, 0x00ff88, 0x00d4ff};
        static const char* s_snames[3] = {"S1","S2","S3"};
        int si = m % 3;
        if (g_ui.s2_bg)  lv_obj_set_style_bg_color(g_ui.s2_bg, lv_color_hex(s_scols[si]), 0);
        if (g_ui.s2_bg)  lv_obj_set_style_shadow_color(g_ui.s2_bg, lv_color_hex(s_scols[si]), 0);
        if (g_ui.s2_lbl) lv_label_set_text(g_ui.s2_lbl, s_snames[si]);
    }

    // ── Кружок мотора: мигает при нечётном pct/10 (виден "пульс") ───────────
    {
        bool motor_on = (pct / 10) % 2 == 0;
        if (g_ui.pwr_bg) {
            lv_obj_set_style_bg_color(g_ui.pwr_bg,
                lv_color_hex(motor_on ? 0x00ff88 : 0x226644), 0);
            lv_obj_set_style_shadow_color(g_ui.pwr_bg,
                lv_color_hex(motor_on ? 0x00ff88 : 0x226644), 0);
        }
    }

    // ── Лимиты: вращающийся паттерн (бегущий огонь по 4 стрелкам) ───────────
    {
        // Каждый квадрант (0..3) зажигает одну стрелку
        int lim_phase = (int)(elapsed / 600) % 5; // 0=L, 1=R, 2=F, 3=B, 4=выкл
        LimitStatus fake = {0,0,0,0};
        if (lim_phase == 0) fake.left  = 1;
        if (lim_phase == 1) fake.right = 1;
        if (lim_phase == 2) fake.front = 1;
        if (lim_phase == 3) fake.rear  = 1;
        // lim_phase==4: все выкл (пауза)
        update_limit_indicators(fake);
    }

    // ── Алерты: показываем один из 4 сообщений, закрываем через 2с ──────────
    {
        // Каждые 7.5 секунд (два режима) — показываем алерт на 2.5с
        static const char* demo_alerts[] = {
            "\xD0\xA3\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9D\xD0\x9E\xD0\x92\xD0\x98\xD0\xA2\xD0\x95\n\xD0\xA3\xD0\x9F\xD0\x9E\xD0\xA0\xD0\xAB!",   // УСТАНОВИТЕ УПОРЫ!
            "\xD0\x9E\xD0\x9F\xD0\x95\xD0\xA0\xD0\x90\xD0\xA6\xD0\x98\xD0\xAF\n\xD0\x97\xD0\x90\xD0\x92\xD0\x95\xD0\xA0\xD0\xA8\xD0\x95\xD0\x9D\xD0\x90!", // ОПЕРАЦИЯ ЗАВЕРШЕНА!
            "\xD0\x94\xD0\x96\xD0\x9E\xD0\x99\xD0\xA1\xD0\xA2\xD0\x98\xD0\x9A\n\xD0\x92 \xD0\x9D\xD0\x95\xD0\xAC\xD0\xA2\xD0\xA0\xD0\x90\xD0\x9B\xD0\xAC\xD0\x9D\xD0\x9E\xD0\x95", // ДЖОЙСТИК В НЕЙТРАЛЬНОЕ
            "\xD0\xA3\xD0\xA1\xD0\xA2\xD0\x90\xD0\x9D\xD0\x9E\xD0\x92\xD0\x98\xD0\xA2\xD0\x95\n\xD0\xA1\xD0\xA3\xD0\x9F\xD0\x9F\xD0\x9E\xD0\xA0\xD0\xA2 \xD0\x92\n\xD0\x98\xD0\xA1\xD0\x9F\xD0\x9E\xD0\x94\xD0\x9D\xD0\x9E\xD0\x95",  // УСТАНОВИТЕ СУППОРТ В ИСХОДНОЕ
        };
        // Показываем алерт когда pct входит в диапазон 60..85
        bool want_alert = (pct >= 60 && pct < 85);
        if (want_alert && !g_alert.active) {
            int ai = m % 4;
            show_alert(demo_alerts[ai]);
        } else if (!want_alert && g_alert.active) {
            dismiss_alert();
        }
    }
}

// ============================================================================
// Phase-2: тест-режим — 23 предустановленных сценария всех режимов/подрежимов
// Вход: 5× быстрый DN вне edit-режима
// Навигация: UP/DN меняют сценарий, OK выходит
// ============================================================================

struct TestScenario {
    const char* name;   // Отображается в тест-баре
    LatheData   data;
};

// Вспомогательная функция — заполнить LatheData с разумными дефолтами
static LatheData s_mk(LatheMode m, LatheSubMode sub,
                       int16_t  feed   = 25,
                       int16_t  thread = 100,
                       int16_t  afeed  = 20,
                       int32_t  pz     = 12350,
                       int32_t  px     = -3456,
                       uint16_t rpm    = 800,
                       uint16_t pnr    = 0,
                       uint16_t ptot   = 0,
                       int16_t  ap     = 25,
                       uint8_t  ph     = 1,
                       int16_t  angle  = 0,
                       uint8_t  cone   = 0,
                       int16_t  cangle = 0,
                       int16_t  sphere = 3000,
                       uint16_t tn     = 6,
                       uint16_t tc     = 1,
                       int16_t  bar    = 500,
                       bool     mz     = true,
                       const char* tname = "",
                       int16_t  tcycl  = 0,
                       int16_t  rpmLim = 0,
                       int16_t  ttravel = 0,
                       uint8_t  lims   = 0)  // 4-bit: bit0=L, bit1=R, bit2=F, bit3=B
{
    LatheData d;
    memset(&d, 0, sizeof(d));
    d.mode          = m;
    d.submode       = sub;
    d.feed_mm       = feed;
    d.thread_mm     = thread;
    d.afeed_mm      = afeed;
    d.pos_z         = pz;
    d.pos_x         = px;
    d.rpm           = rpm;
    d.pass_nr       = pnr;
    d.pass_total    = ptot;
    d.ap            = ap;
    d.ph            = ph;
    d.spindle_angle = angle;
    d.cone_idx      = cone;
    d.cone_angle    = cangle;
    d.sphere_radius = sphere;
    d.total_tooth   = tn;
    d.current_tooth = tc;
    d.bar_r         = bar;
    d.motor_z_enabled = mz;
    if (tname[0]) {
        strncpy(d.thread_name, tname, sizeof(d.thread_name) - 1);
        d.thread_name[sizeof(d.thread_name) - 1] = '\0';
    }
    d.thread_cycles = tcycl;
    d.rpm_limit     = rpmLim;
    d.thread_travel = ttravel;
    d.limits.left  = (lims >> 0) & 1;
    d.limits.right = (lims >> 1) & 1;
    d.limits.front = (lims >> 2) & 1;
    d.limits.rear  = (lims >> 3) & 1;
    return d;
}

// clang-format off
// pz/px в мкм (10 = 0.01мм), feed в 0.01мм, ap в 0.01мм
// pz= 123450 → Z=+123.45мм   px=-18500 → X=-18.50мм
static const TestScenario s_test_scenarios[TEST_SCENARIO_COUNT] = {
    // ─── M1 ПОДАЧА синхронная (5 сценариев) ──────────────────────────
    { "M1/S1 Подача 0.10 Пр1/6 Съём 0.10",
      s_mk(MODE_FEED, SUBMODE_INTERNAL, /*feed*/10,  100, 20, /*pz*/56780,  /*px*/-8500, 800, 1, 6,/*ap*/10, 1) },
    { "M1/S2 Подача 0.20 Ручной  Z+87.50 X-12.30",
      s_mk(MODE_FEED, SUBMODE_MANUAL,   /*feed*/20,  100, 20, /*pz*/87500,  /*px*/-12300, 1200, 0, 0,/*ap*/20, 1) },
    { "M1/S3 Подача 0.30 Пр3/8  Съём 0.15",
      s_mk(MODE_FEED, SUBMODE_EXTERNAL, /*feed*/30,  100, 20, /*pz*/34120,  /*px*/-5200,  600, 3, 8,/*ap*/15, 1) },
    { "M1/S1 Подача 0.50 Пр5/10 Съём 0.25",
      s_mk(MODE_FEED, SUBMODE_INTERNAL, /*feed*/50,  100, 20, /*pz*/112340, /*px*/-21000, 400, 5,10,/*ap*/25, 1) },
    { "M1/S3 Подача 0.15 Пр8/10 Z-47.80 X+3.60",
      s_mk(MODE_FEED, SUBMODE_EXTERNAL, /*feed*/15,  100, 20, /*pz*/-47800, /*px*/3600,   800, 8,10,/*ap*/15, 1) },

    // ─── M2 ПОДАЧА асинхронная (5 сценариев) ─────────────────────────
    { "M2/S1 аПодача 0.10 Пр2/8 Съём 0.10",
      s_mk(MODE_AFEED, SUBMODE_INTERNAL, 25, 100,/*afeed*/10, /*pz*/23400,  /*px*/-7200, 1500, 2, 8,/*ap*/10, 1) },
    { "M2/S2 аПодача 0.30 Ручной  Z+63.20 X-9.40",
      s_mk(MODE_AFEED, SUBMODE_MANUAL,   25, 100,/*afeed*/30, /*pz*/63200,  /*px*/-9400,  800, 0, 0,/*ap*/20, 1) },
    { "M2/S3 аПодача 0.50 Пр7/8  Съём 0.25",
      s_mk(MODE_AFEED, SUBMODE_EXTERNAL, 25, 100,/*afeed*/50, /*pz*/35000,  /*px*/-8000,  400, 7, 8,/*ap*/25, 1) },
    { "M2/S1 аПодача 0.20 Пр4/12 Z+18.90 X-3.50",
      s_mk(MODE_AFEED, SUBMODE_INTERNAL, 25, 100,/*afeed*/20, /*pz*/18900,  /*px*/-3500, 1200, 4,12,/*ap*/15, 1) },
    { "M2/S3 аПодача 0.40 Пр0/6  Z-22.50 X+1.80",
      s_mk(MODE_AFEED, SUBMODE_EXTERNAL, 25, 100,/*afeed*/40, /*pz*/-22500, /*px*/1800,   600, 0, 6,/*ap*/20, 1) },

    // ─── M3 РЕЗЬБА (7 сценариев) ─────────────────────────────────────
    // thread_cycles = Thread_Info[step].Pass + PASS_FINISH(2) - pass_nr + 1  (Внутр/Нар)
    // thread_cycles = Thread_Info[step].Pass + PASS_FINISH(2)                (Ручной)
    { "M3/S1 Резьба 1.00мм Пр5/10 Z+45.00",
      // Thread_Info[11].Pass=10; cycles=(10+2)-5+1=8
      s_mk(MODE_THREAD, SUBMODE_INTERNAL, 25,/*thread*/100, 20, /*pz*/45000,  /*px*/-5000, 200, 5,10,/*ap*/10, 1, 0,0,0,3000,6,1,500,true,"",/*tcycl*/8) },
    { "M3/S2 Резьба 1.50мм Ph=2  Ручной",
      // Thread_Info[13].Pass=14; cycles=14+2=16
      s_mk(MODE_THREAD, SUBMODE_MANUAL,   25,/*thread*/150, 20, /*pz*/32000,  /*px*/-4500, 150, 0, 0,/*ap*/15, 2, 0,0,0,3000,6,1,500,true,"",/*tcycl*/16) },
    { "M3/S3 Резьба 2.00мм Пр8/10 Z+78.50",
      // Thread_Info[15].Pass=18; cycles=(18+2)-8+1=13
      s_mk(MODE_THREAD, SUBMODE_EXTERNAL, 25,/*thread*/200, 20, /*pz*/78500,  /*px*/-6000, 100, 8,10,/*ap*/20, 1, 0,0,0,3000,6,1,500,true,"",/*tcycl*/13) },
    { "M3/S2 Резьба 3.175мм 8tpi Ph=3",
      // Thread_Info[22].Pass=27; cycles=27+2=29
      s_mk(MODE_THREAD, SUBMODE_MANUAL,   25,/*thread*/318, 20, /*pz*/52000,  /*px*/-8000, 150, 0, 0,/*ap*/15, 3, 0,0,0,3000,6,1,500,true,"",/*tcycl*/29) },
    { "M3/S1 Резьба 0.907мм G1/8 Пр3/6",
      // Thread_Info[48].Pass=9; cycles=(9+2)-3+1=9
      s_mk(MODE_THREAD, SUBMODE_INTERNAL, 25,/*thread*/ 91, 20, /*pz*/28000,  /*px*/-3500, 100, 3, 6,/*ap*/8,  1, 0,0,0,3000,6,1,500,true,"",/*tcycl*/9) },
    { "M3/S3 Резьба 2.50мм Пр2/8  Ph=4",
      // Thread_Info[16].Pass=22; cycles=(22+2)-2+1=23
      s_mk(MODE_THREAD, SUBMODE_EXTERNAL, 25,/*thread*/250, 20, /*pz*/60000,  /*px*/-7000, 100, 2, 8,/*ap*/20, 4, 0,0,0,3000,6,1,500,true,"",/*tcycl*/23) },
    { "M3/S2 Резьба 0.75мм M10x0.75 Пр0",
      // Thread_Info[9].Pass=8; cycles=8+2=10
      s_mk(MODE_THREAD, SUBMODE_MANUAL,   25,/*thread*/ 75, 20, /*pz*/15500,  /*px*/-2000, 200, 0, 0,/*ap*/8,  1, 0,0,0,3000,6,1,500,true,"",/*tcycl*/10) },

    // ─── M4 КОНУС влево (4 сценария) ─────────────────────────────────
    { "M4/S1 Конус< Морзе0 Пр2/4 Z+18.30 X-4.20",
      s_mk(MODE_CONE_L, SUBMODE_INTERNAL,/*feed*/15, 100, 20, /*pz*/18300, /*px*/-4200, 600, 2, 4,/*ap*/15, 1, 0,/*cone*/0) },
    { "M4/S2 Конус< Морзе2 Ручной Z+42.70 X-9.80",
      s_mk(MODE_CONE_L, SUBMODE_MANUAL,  /*feed*/25, 100, 20, /*pz*/42700, /*px*/-9800, 600, 0, 0,/*ap*/25, 1, 0,/*cone*/2) },
    { "M4/S3 Конус< МТ2 Пр1/3 Z+55.00 X-6.50",
      s_mk(MODE_CONE_L, SUBMODE_EXTERNAL,/*feed*/30, 100, 20, /*pz*/55000, /*px*/-6500, 600, 1, 3,/*ap*/30, 1, 0,/*cone*/8) },
    { "M4/S1 Конус< Морзе4 Пр3/6 Съём 0.20",
      s_mk(MODE_CONE_L, SUBMODE_INTERNAL,/*feed*/20, 100, 20, /*pz*/33800, /*px*/-11200,500, 3, 6,/*ap*/20, 1, 0,/*cone*/4) },

    // ─── M5 КОНУС вправо (4 сценария) ────────────────────────────────
    { "M5/S1 Конус> Морзе0 Пр2/4 Z-18.30 X-4.20",
      s_mk(MODE_CONE_R, SUBMODE_INTERNAL,/*feed*/15, 100, 20, /*pz*/-18300,/*px*/-4200, 600, 2, 4,/*ap*/15, 1, 0,/*cone*/0) },
    { "M5/S2 Конус> Морзе3 Ручной Z-42.70 X-9.80",
      s_mk(MODE_CONE_R, SUBMODE_MANUAL,  /*feed*/20, 100, 20, /*pz*/-42700,/*px*/-9800, 600, 0, 0,/*ap*/20, 1, 0,/*cone*/3) },
    { "M5/S3 Конус> МТ1 Пр1/3 Z-55.00 X-6.50",
      s_mk(MODE_CONE_R, SUBMODE_EXTERNAL,/*feed*/30, 100, 20, /*pz*/-55000,/*px*/-6500, 600, 1, 3,/*ap*/30, 1, 0,/*cone*/7) },
    { "M5/S3 Конус> Морзе5 Пр4/6 Съём 0.25",
      s_mk(MODE_CONE_R, SUBMODE_EXTERNAL,/*feed*/25, 100, 20, /*pz*/-68200,/*px*/-14500,450, 4, 6,/*ap*/25, 1, 0,/*cone*/5) },

    // ─── M6 ШАР (4 сценария) ─────────────────────────────────────────
    { "M6/S2 Шар ø40мм Пр3/8 Z+12.50 X-8.00",
      s_mk(MODE_SPHERE, SUBMODE_MANUAL,   25, 100, 20, /*pz*/12500, /*px*/-8000,  400, 3, 8,/*ap*/15, 1, 0, 0,
           /*sphere*/2000, 6, 1,/*bar*/500) },
    { "M6/S3 Шар ø100мм Пр0/15 Z+50.00 X-25.00",
      s_mk(MODE_SPHERE, SUBMODE_EXTERNAL, 25, 100, 20, /*pz*/50000, /*px*/-25000, 400, 0,15,/*ap*/25, 1, 0, 0,
           /*sphere*/5000, 6, 1,/*bar*/1000) },
    { "M6/S1 Шар ø60мм Пр7/12 Z+30.00 X-15.00",
      s_mk(MODE_SPHERE, SUBMODE_INTERNAL, 25, 100, 20, /*pz*/30000, /*px*/-15000, 350, 7,12,/*ap*/20, 1, 0, 0,
           /*sphere*/3000, 6, 1,/*bar*/750) },
    { "M6/S2 Шар ø25мм Пр5/10 Ножка 8.50",
      s_mk(MODE_SPHERE, SUBMODE_MANUAL,   25, 100, 20, /*pz*/8500,  /*px*/-6200,  500, 5,10,/*ap*/10, 1, 0, 0,
           /*sphere*/1250, 6, 1,/*bar*/850) },

    // ─── M7 ДЕЛИТЕЛЬ (4 сценария) ────────────────────────────────────
    { "M7 Делений=6   Метка=1   (60.0°)",
      s_mk(MODE_DIVIDER, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 0, 0, 0, 25, 1,
           /*angle*/600,  0, 0, /*tn*/6,   /*tc*/1) },
    { "M7 Делений=12  Метка=7   (210.0°)",
      s_mk(MODE_DIVIDER, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 0, 0, 0, 25, 1,
           /*angle*/2100, 0, 0, /*tn*/12,  /*tc*/7) },
    { "M7 Делений=36  Метка=25  (250.0°)",
      s_mk(MODE_DIVIDER, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 0, 0, 0, 25, 1,
           /*angle*/2500, 0, 0, /*tn*/36,  /*tc*/25) },
    { "M7 Делений=360 Метка=180 (180.0°)",
      s_mk(MODE_DIVIDER, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 0, 0, 0, 25, 1,
           /*angle*/1800, 0, 0, /*tn*/360, /*tc*/180) },

    // ─── M8 РЕЗЕРВ (2 сценария) ───────────────────────────────────────
    { "M8 Резерв — начало",
      s_mk(MODE_RESERVE, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 0, 0, 0, 25, 1) },
    { "M8 Резерв — позиция Z+95 X-3",
      s_mk(MODE_RESERVE, SUBMODE_MANUAL, 25, 100, 20, /*pz*/95000, /*px*/-3000, 600, 0, 0, 25, 1) },

    // ─── BUG-04: M3 резьбы с одинаковым mm_x100 (проверка синхронизации edit mode) ───
    // D05: 64tpi (mm_x100=40) совпадает с "0.40mm" — без thread_name синк шёл бы на 0.40mm
    { "BUG04/D05 M3 64tpi (mm=40, name='64tpi ')",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25,/*thread*/40, 20, /*pz*/20000, /*px*/-3000, 300, 0, 0,/*ap*/10, 1,
           0, 0, 3000, 6, 1, 500, true, "64tpi ") },
    // D06: G  1/8 (mm_x100=91) совпадает с "G 1/16" — без thread_name синк шёл бы на G 1/16
    { "BUG04/D06 M3 G  1/8 (mm=91, name='G  1/8')",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25,/*thread*/91, 20, /*pz*/28000, /*px*/-3500, 200, 2, 6,/*ap*/8, 1,
           0, 0, 3000, 6, 1, 500, true, "G  1/8") },
    // E04: K  1/8 (mm_x100=94) совпадает с "K 1/16" — без thread_name синк шёл бы на K 1/16
    { "BUG04/E04 M3 K  1/8 (mm=94, name='K  1/8')",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25,/*thread*/94, 20, /*pz*/22000, /*px*/-2500, 200, 1, 4,/*ap*/8, 1,
           0, 0, 3000, 6, 1, 500, true, "K  1/8") },

    // ─── BUG-06: Проверка формулы Циклов для M3 ──────────────────────
    // E02: M3/S1 Внутренний, 0.20mm (Thread_Info[0].Pass=4), Pass_Nr=3
    // Правильно: (4+2)-3+1=4; Без +1 было бы: (4+2)-3=3 → вот откуда "3 vs 4" в описании бага
    { "BUG06/E02 M3/S1 Внутр 0.20mm Пр3/4 Циклов=4",
      // Thread_Info[0].Pass=4; PASS_FINISH=2; cycles=(4+2)-3+1=4
      s_mk(MODE_THREAD, SUBMODE_INTERNAL, 25,/*thread*/20, 20, /*pz*/12000, /*px*/-2000, 980, 3, 4,/*ap*/5, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "",/*tcycl*/4) },

    // ─── US-018: Edge case tests ──────────────────────────────────────
    // M01: Большое положительное значение Z (99999mm) — без усечения
    { "US018/M01 M1 Z+99999mm (edge: large pos)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, /*pz*/99999000, /*px*/-1000, 800, 0, 0, 25, 1) },
    // M02: Большое отрицательное значение Z (-99999mm) — отображается со знаком минус
    { "US018/M02 M1 Z-99999mm (edge: large neg)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, /*pz*/-99999000, /*px*/1000, 800, 0, 0, 25, 1) },
    // M03: M3 с пустым thread_name — без артефактов (числовой fallback)
    { "US018/M03 M3 empty thread_name fallback",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 200, 0, 0, 15, 1,
           0, 0, 0, 3000, 6, 1, 500, true, /*tname*/"", /*tcycl*/10) },
    // M04: M3 с Ph=5 — заголовок row3 должен быть "ХОД ММ"
    { "US018/M04 M3 Ph=5 row3=ХОД ММ",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 150, 0, 0, 15, /*ph*/5,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10) },
    // M05: M5 после быстрого переключения режимов M1->M7->M3->M5 — без зависания
    { "US018/M05 M5 after M1->M7->M3->M5 switch",
      s_mk(MODE_CONE_R, SUBMODE_INTERNAL, 20, 100, 20, /*pz*/-42700, /*px*/-9800, 600, 2, 4, 15, 1, 0, /*cone*/3) },

    // ─── US-013: Проверка мн. заходной резьбы (Ph=1..8) ──────────────
    // E01: Ph=1 → row3 title="ОБ/МИН", value=rpm_limit (read-only)
    { "US013/E01 M3 Ph=1 row3=ОБ/МИН 300об",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/1,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/300, /*ttravel*/0) },
    // E02: Ph=2 → row3 title="ХОД ММ", value=150×2=300 → "3.00"
    { "US013/E02 M3 Ph=2 row3=ХОД ММ 3.00",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/2,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/300) },
    // E03: Ph=3 → row3 title="ХОД ММ", value=150×3=450 → "4.50"
    { "US013/E03 M3 Ph=3 row3=ХОД ММ 4.50",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/3,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/450) },
    // E04: Ph=4 → row3 title="ХОД ММ", value=150×4=600 → "6.00"
    { "US013/E04 M3 Ph=4 row3=ХОД ММ 6.00",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/4,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/600) },
    // E05: Ph=5 → row3 title="ХОД ММ", value=150×5=750 → "7.50"
    { "US013/E05 M3 Ph=5 row3=ХОД ММ 7.50",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/5,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/750) },
    // E06: Ph=6 → row3 title="ХОД ММ", value=150×6=900 → "9.00"
    { "US013/E06 M3 Ph=6 row3=ХОД ММ 9.00",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/6,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/900) },
    // E07: Ph=7 → row3 title="ХОД ММ", value=150×7=1050 → "10.50"
    { "US013/E07 M3 Ph=7 row3=ХОД ММ 10.50",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/7,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/1050) },
    // E08: Ph=8 → row3 title="ХОД ММ", value=150×8=1200 → "12.00"
    { "US013/E08 M3 Ph=8 row3=ХОД ММ 12.00",
      s_mk(MODE_THREAD, SUBMODE_MANUAL, 25, /*thread*/150, 20, /*pz*/30000, /*px*/-5000, 300, 0, 0, 15, /*ph*/8,
           0, 0, 0, 3000, 6, 1, 500, true, "1.50mm", /*tcycl*/10, /*rpmLim*/0, /*ttravel*/1200) },

    // ─── K-GROUP: Limit indicators (US-019) — 8 сценариев ────────────
    // lims bitmask: bit0=L(←), bit1=R(→), bit2=F(↑), bit3=B(↓)
    // Z-ось (L/R): SET=голубой #00d4ff, CLEAR=тёмный #1a3a4a
    // X-ось (F/B): SET=зелёный #00ff88, CLEAR=тёмный #1a3a1a
    // K1: LL first → ← arrow cyan
    { "US019/K1 LL: стрелка← голубая #00d4ff",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x01) },
    // K2: LL again → ← arrow dim (all off)
    { "US019/K2 LL2: стрелка← тёмная #1a3a4a (все выкл)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x00) },
    // K3: LR first → → arrow cyan
    { "US019/K3 LR: стрелка→ голубая #00d4ff",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x02) },
    // K4: LF first → ↑ arrow green
    { "US019/K4 LF: стрелка↑ зелёная #00ff88",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x04) },
    // K5: LB first → ↓ arrow green
    { "US019/K5 LB: стрелка↓ зелёная #00ff88",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x08) },
    // K6: LR again → → arrow dim (all off)
    { "US019/K6 LR2: стрелка→ тёмная (все выкл)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x00) },
    // K7: LF again → ↑ arrow dim (all off)
    { "US019/K7 LF2: стрелка↑ тёмная (все выкл)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x00) },
    // K8: LB again → ↓ arrow dim (all off)
    { "US019/K8 LB2: стрелка↓ тёмная (все выкл)",
      s_mk(MODE_FEED, SUBMODE_MANUAL, 25, 100, 20, 0, 0, 800, 0, 0, 25, 1,
           0, 0, 0, 3000, 6, 1, 500, true, "", 0, 0, 0, /*lims*/0x00) },
};
// clang-format on

static void test_apply(int idx)
{
    if (idx < 0 || idx >= TEST_SCENARIO_COUNT) return;
    const TestScenario& sc = s_test_scenarios[idx];
    update_ui_values(sc.data);
    if (g_ui.test_lbl) {
        lv_label_set_text_fmt(g_ui.test_lbl, "[%d/%d] %s",
                              idx + 1, TEST_SCENARIO_COUNT, sc.name);
    }
    Serial.printf("TEST [%d/%d]: %s\n", idx + 1, TEST_SCENARIO_COUNT, sc.name);
}

static void test_start()
{
    if (g_edit_param.active) exit_edit_mode();
    if (g_sub_edit.active)   exit_sub_edit();
    if (g_demo_active)       demo_stop();
    g_test_active = true;
    g_test_idx    = 0;
    Serial.println("TEST MODE: start. UP/DN = сценарий, OK = выход");
    if (g_ui.test_bar) {
        lv_obj_clear_flag(g_ui.test_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_ui.test_bar);
    }
    test_apply(0);
}

static void test_stop()
{
    g_test_active = false;
    if (g_ui.test_bar) lv_obj_add_flag(g_ui.test_bar, LV_OBJ_FLAG_HIDDEN);
    Serial.println("TEST MODE: stop");
    // Восстановить реальные данные от Arduino
    update_ui_values(uart_protocol.getData());
}

// ============================================================================
// Callback for UART data
// ============================================================================

void onDataUpdate(const LatheData& data)
{
    // оценка времени G-кода: обороты (подача на оборот) и шагов на мм из механики STM32
    GCodeExec_SetRpm(data.rpm);
    if (data.cfg_valid && data.cfg[3] > 0 && data.cfg[6] > 0)
        GCodeExec_SetMachine(data.cfg[1] * data.cfg[2] * 100.0 / data.cfg[3], data.cfg[4] * data.cfg[5] * 100.0 / data.cfg[6]);
    update_ui_values(data);
}

// ============================================================================
// Автотесты: текущие тексты виджетов одной строкой (<UIDUMP> по USB)
// Формат: [UI] key="text" ... (перевод строки в тексте → \n), цвета — RRGGBB
// ============================================================================
static void ui_dump()
{
    const struct { const char* key; lv_obj_t* obj; } labels[] = {
        {"mode_lbl", g_ui.mode_lbl},         {"submode_lbl", g_ui.submode_lbl},
        {"primary_unit", g_ui.primary_unit}, {"primary_val", g_ui.primary_val},
        {"thread_tag", g_ui.thread_tag},
        {"secondary_unit", g_ui.secondary_unit}, {"secondary_val", g_ui.secondary_val},
        {"row1_title", g_ui.row1_title}, {"row1_val", g_ui.row1_val},
        {"row2_title", g_ui.row2_title}, {"row2_val", g_ui.row2_val},
        {"row3_title", g_ui.row3_title}, {"row3_val", g_ui.row3_val},
        {"row4_title", g_ui.row4_title}, {"row4_val", g_ui.row4_val},
        {"thr_type_title", g_ui.thr_type_title}, {"thr_type_val", g_ui.thr_type_val},
        {"diam_title", g_ui.diam_unit}, {"diam_val", g_ui.diam_val},
        {"sm_row6", g_ui.sm_row6}, {"s2_lbl", g_ui.s2_lbl}, {"alert_msg", g_ui.alert_msg},
    };
    Serial.print("[UI]");
    for (const auto& l : labels) {
        if (!l.obj) continue;
        Serial.printf(" %s=\"", l.key);
        for (const char* p = lv_label_get_text(l.obj); *p; p++) {
            if (*p == '\n') Serial.print("\\n");
            else if (*p == '"') Serial.print("\\\"");
            else Serial.print(*p);
        }
        Serial.print("\"");
    }
    auto hidden = [](lv_obj_t* o) { return !o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); };
    auto color  = [](lv_obj_t* o, lv_style_selector_t part) {
        return (unsigned)(lv_color_to32(lv_obj_get_style_text_color(o, part)) & 0xFFFFFF);
    };
    Serial.printf(" alert=%d mode_menu=%d thr_type_menu=%d joystick=%d settings=%d roller=%d"
                  " edit=%d sub_edit=%d thr_type_box=%d axis_kb=%d axis_edit=%d",
                  !hidden(g_ui.alert_box), !hidden(g_ui.mode_menu), !hidden(g_ui.thr_type_menu),
                  !hidden(g_ui.joystick_overlay), !hidden(g_ui.settings_menu), (int)g_roller.active,
                  (int)g_edit_param.active, (int)g_sub_edit.active, !hidden(g_ui.thr_type_box),
                  g_numpad.back != nullptr, (int)uart_protocol.getData().axis_edit);
    Serial.printf(" cal=%d", g_cal.back != nullptr);
    if (g_ui.lim_L)
        Serial.printf(" lim=%06X,%06X,%06X,%06X", color(g_ui.lim_L, 0), color(g_ui.lim_R, 0),
                      color(g_ui.lim_F, 0), color(g_ui.lim_B, 0));
    if (g_ui.pwr_bg)
        Serial.printf(" pwr_bg=%06X",
                      (unsigned)(lv_color_to32(lv_obj_get_style_bg_color(g_ui.pwr_bg, 0)) & 0xFFFFFF));
    Serial.printf(" diam_box=%d row1_box=%d row2_box=%d row3_box=%d row4_box=%d",
                  !hidden(g_ui.diam_box), !hidden(g_ui.row1_box), !hidden(g_ui.row2_box),
                  !hidden(g_ui.row3_box), !hidden(g_ui.row4_box));
    if (g_ui.row1_val) Serial.printf(" row1_color=%06X", color(g_ui.row1_val, 0));
    if (g_ui.row2_val) Serial.printf(" row2_color=%06X", color(g_ui.row2_val, 0));
    Serial.println();
}

// ============================================================================
// Setup
// ============================================================================

void setup()
{
    Serial.setRxBufferSize(4096);   // команды автотестов по USB не теряются при занятом loop()
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n===========================================");
    Serial.println("ELS Display - Starting...");
    Serial.printf("Display: %s (%dx%d)\n", DISPLAY_NAME, SCREEN_WIDTH, SCREEN_HEIGHT);
    Serial.println("===========================================");

#ifdef DISPLAY_JC4827W543
    // === JC4827W543 Initialization ===
    Serial.println("Initializing JC4827W543 (NV3041A + GT911)...");

    // Backlight ON (PWM capable, but use simple HIGH for now)
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);

    // Canvas init (includes panel initialization)
    if (!gfx->begin()) {
        Serial.println("GFX init FAILED!");
    }

    gfx->fillScreen(0x0000);  // BLACK
    gfx->setTextColor(0xFFFF); // WHITE
    gfx->setTextSize(2);
    gfx->setCursor(10, 10);
    gfx->println("ELS Display");
    gfx->println("JC4827W543 Init...");

    Serial.println("Display initialized OK");

    // Touch init
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    if (touch.init()) {
        Serial.println("GT911 Touch initialized OK");
    } else {
        Serial.println("GT911 Touch init FAILED!");
    }

    delay(1000);
    gfx->fillScreen(0x0000);

    // Выделяем PSRAM framebuffer
    screen_fb = (uint16_t*)ps_malloc((uint32_t)SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    if (screen_fb) {
        memset(screen_fb, 0, (uint32_t)SCREEN_WIDTH * SCREEN_HEIGHT * 2);
        Serial.println("PSRAM framebuffer allocated OK");
    } else {
        Serial.println("PSRAM framebuffer FAILED (no PSRAM?)");
    }

    // Семафоры: httpd task ↔ capture task
    sem_requested = xSemaphoreCreateBinary();
    sem_done      = xSemaphoreCreateBinary();
    if (sem_requested && sem_done) {
        Serial.println("Semaphores created OK");
    } else {
        Serial.println("Semaphore creation FAILED");
    }

    // JPEG буфер в PSRAM (128KB — достаточно для 480×272 при качестве 80)
    jpeg_cap = 131072;
    jpeg_buf = (uint8_t*)ps_malloc(jpeg_cap);
    if (jpeg_buf) {
        Serial.printf("JPEG buffer: %u bytes in PSRAM OK\n", jpeg_cap);
    } else {
        Serial.println("JPEG buffer FAILED");
    }

    // Задача кодирования JPEG с большим стеком (стек loop() = 8KB, stb нужно ~12KB)
    xTaskCreate(capture_task_fn, "jpeg_capture", 32768, nullptr, 1, nullptr);
    Serial.println("JPEG capture task started (32KB stack)");

    // Screenshot server запускается позже (после GCodeWiFi_Init) на порту 8081

#elif defined(DISPLAY_JC8012P4A1C)
    // === JC8012P4A1C Initialization (ESP32-P4, MIPI-DSI) ===
    Serial.println("Initializing JC8012P4A1C (JD9365 DSI + GSL3680)...");
    p4_lcd.begin();
    Serial.println("JD9365 LCD init OK");
    p4_touch.begin();
    Serial.println("GSL3680 Touch init OK");

    // screen_fb = framebuffer самой панели (800×1280): flush пишет в него,
    // скриншот читает из него же
    screen_fb = p4_lcd.frame_buffer();
    if (screen_fb) {
        memset(screen_fb, 0, (uint32_t)P4_LCD_H_RES * P4_LCD_V_RES * 2);
        p4_lcd.lcd_draw_bitmap(0, 0, P4_LCD_H_RES, P4_LCD_V_RES, screen_fb);
        Serial.println("P4 panel framebuffer OK");
    } else {
        Serial.println("P4 panel framebuffer FAILED");
    }
    sem_requested = xSemaphoreCreateBinary();
    sem_done      = xSemaphoreCreateBinary();
    jpeg_cap = (uint32_t)P4_LCD_H_RES * P4_LCD_V_RES * 2;  // 2MB — запас для 800×1280 JPEG
    jpeg_buf = (uint8_t*)heap_caps_malloc(jpeg_cap, MALLOC_CAP_SPIRAM);
    if (jpeg_buf) Serial.printf("P4 JPEG buffer: %u bytes OK\n", jpeg_cap);
    xTaskCreate(capture_task_fn, "jpeg_capture", 32768, nullptr, 1, nullptr);
    Serial.println("P4 JPEG capture task started");

#else
    // === ESP32-2432S024 Initialization ===
    Serial.println("Initializing ESP32-2432S024 (ILI9341)...");
    tft.begin();
    tft.setRotation(SCREEN_ROTATION);
    tft.fillScreen(TFT_BLACK);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("ELS Display");
    tft.println("Initializing...");
    delay(1000);
    tft.fillScreen(TFT_BLACK);
#endif

    // === LVGL Initialization ===
    Serial.println("Initializing LVGL...");
    lv_init();

#ifdef DISPLAY_JC8012P4A1C
    // P4: LVGL 1280×800, частичная перерисовка (только изменённые области).
    // Буфер отрисовки во внутренней SRAM — рендер в PSRAM в разы медленнее.
    const uint32_t p4_buf_px = (uint32_t)SCREEN_WIDTH * P4_DRAW_BUF_LINES;
    p4_buf0 = (lv_color_t*)heap_caps_malloc(p4_buf_px * sizeof(lv_color_t),
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p4_buf0) {
        Serial.println("P4 LVGL buf: internal RAM FAILED → PSRAM");
        p4_buf0 = (lv_color_t*)heap_caps_malloc(p4_buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    lv_disp_draw_buf_init(&draw_buf, p4_buf0, nullptr, p4_buf_px);
    Serial.printf("P4 LVGL buf: %u lines, free internal=%u\n", (unsigned)P4_DRAW_BUF_LINES,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_WIDTH;   // логических 1280
    disp_drv.ver_res      = SCREEN_HEIGHT;  // логических 800
    disp_drv.flush_cb     = my_disp_flush;
    disp_drv.draw_buf     = &draw_buf;
    lv_disp_drv_register(&disp_drv);
#else
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 32);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_WIDTH;
    disp_drv.ver_res  = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
#endif

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("LVGL initialized OK");

    // === UART Initialization ===
#ifdef DISPLAY_JC8012P4A1C
    // ESP32-P4: явно задаём GPIO пины (см. display_config_p4.h)
    Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    uart_protocol.begin(Serial2, UART_BAUD_RATE);
#else
    uart_protocol.begin(Serial2, UART_BAUD_RATE);
#endif
    uart_protocol.setDataUpdateCallback(onDataUpdate);
    uart_protocol.setAlertCallback(show_alert);
    // ALERT:0 от STM32 снимает флаги ядра, но не «Режим невозможен!» шара — его держит сам дисплей,
    // пока шар во Внутреннем подрежиме (как LCD2004)
    uart_protocol.setAlertDismissCallback([]() {
        const LatheData& d = uart_protocol.getData();
        if (s_sphere_alert && d.mode == MODE_SPHERE && d.submode == SUBMODE_INTERNAL) return;
        if (s_gcode_alert) return;
        dismiss_alert();
    });

    // === Create UI ===
    create_dark_pro_ui();

    // GCode WiFi: AP/STA + LittleFS + HTTP файловый менеджер (порт 80)
    GCodeWiFi_SetConnectedCallback([](const char* ip, bool is_ap) {
        char msg[48];
        snprintf(msg, sizeof(msg), is_ap ? "AP: %s/" : "http://els.local/ (%s)", ip);
        show_alert(msg);
    });
    GCodeWiFi_Init();

    // GCode executor: отправляет строки в STM32 через UART
    GCodeExec_Init([](const char* line) {
        uart_protocol.sendGCodeLine(line);
    });
    uart_protocol.setGCodeResponseCallback([](bool ok, const char* err) {
        Serial.printf("[GC] %s%s\n", ok ? "OK" : "ERR:", ok ? "" : (err ? err : "?"));   // стенд читает ответ
        GCodeExec_OnAck(ok, err);
    });

    Serial.println("===========================================");
    Serial.println("Setup complete! System ready.");
    Serial.println("===========================================\n");
}

// ============================================================================
// Loop
// ============================================================================

void loop()
{
    // Process UART commands
    uart_protocol.process();
    GCodeWiFi_Process();
    GCodeExec_Process();

    // Авто-выход из режима редактирования через 5 секунд бездействия
    if (g_edit_param.active && (millis() - g_edit_param.last_ms > 5000)) {
        exit_edit_mode();
    }
    // Авто-выход из sub-edit (редактирование строки правой панели) через 5 секунд
    if (g_sub_edit.active && (millis() - g_sub_edit.last_ms > 5000)) {
        exit_sub_edit();
    }

    // Авто-скрытие алерта через 5 секунд (кроме флагов ошибки ядра — их закрывает STM32)
    if (g_alert.active && !g_alert.sticky && (millis() - g_alert.show_ms > 5000)) {
        dismiss_alert();
    }

    // Авто-закрытие джойстик-оверлея через 30 секунд бездействия (US-013)
    if (g_joystick.active && (millis() - g_joystick.last_ms > 30000)) {
        hide_joystick_overlay();
    }

    // Демо-режим (тройное нажатие OK)
    if (g_demo_active) demo_tick();

    // Update LVGL tick — use actual elapsed time so LVGL refresh runs at real-time rate.
    // Fixed lv_tick_inc(5) caused ~1-2 sec display lag: if lv_timer_handler() takes
    // ~200ms, LVGL only advanced 5ms per 200ms real time → 30ms refresh needed 6 iters
    // × 200ms = 1.2 s between renders.
    {
        static uint32_t lv_last_tick_ms = 0;
        uint32_t lv_now = millis();
        lv_tick_inc(lv_now - lv_last_tick_ms);
        lv_last_tick_ms = lv_now;
    }

    // Process LVGL (timing measured for diagnosis)
    {
        static uint32_t dbg_last = 0, dbg_max = 0, dbg_loops = 0;
        uint32_t t0 = millis();
        lv_timer_handler();
        uint32_t dt = millis() - t0;
        if (dt > dbg_max) dbg_max = dt;
        dbg_loops++;
        if (t0 - dbg_last >= 2000) {
            // lv_mem_free — только для встроенного пула LVGL (4.3"); на 10.1" память LVGL в PSRAM
            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);
            Serial.printf("[DBG] loops/2s=%lu lv_max=%lums lv_mem_free=%u heap=%u psram=%u\n",
                          (unsigned long)dbg_loops, (unsigned long)dbg_max,
                          (unsigned)mon.free_size, (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getFreePsram());
            dbg_last = t0; dbg_max = 0; dbg_loops = 0;
        }
    }

#if defined(DISPLAY_JC4827W543) || defined(DISPLAY_JC8012P4A1C)
#ifdef DISPLAY_JC4827W543
    // Проверяем HTTP screenshot сервер
    handle_screenshot_server();
#endif

    // USB CDC Screenshot state machine
    // Протокол: PC шлёт 'S' → ESP32 отвечает {0xDE,0xAD,0xBE,0xEF} + 4-byte LE size + JPEG
    {
        static uint8_t  usb_ss_state = 0;

        if (usb_ss_state == 0) {
            // USB CDC (закрой Serial Monitor перед использованием!):
            //   'S'          → скриншот
            //   <BTN:name>   → <TOUCH:name> в STM32 (как нажатие на экране)
            //   <TAP:x,y[,мс]> → имитация касания, x/y в координатах базы 480×272
            //   <UIDUMP>     → тексты виджетов в [UI]-строку (автотесты)
            //   <RXMUTE:0|1> → игнорировать данные от STM32 (автотесты)
            //   <CMD:val>    → обработать как команду от STM32
            static char    usb_inj_buf[128];
            static uint8_t usb_inj_len = 0;
            while (Serial.available() >= 1) {
                char c = (char)Serial.read();
                if (usb_inj_len > 0) {
                    if (usb_inj_len < sizeof(usb_inj_buf) - 1) usb_inj_buf[usb_inj_len++] = c;
                    if (c == '>') {
                        usb_inj_buf[usb_inj_len - 1] = '\0';  // без '>'
                        int tx, ty, tms = 120;
                        if (strncmp(usb_inj_buf, "<GCLINE:", 8) == 0) {      // стенд: строка G-кода в STM32
                            uart_protocol.sendGCodeLine(usb_inj_buf + 8);
                            Serial.printf("[GCLINE→STM32] %s\n", usb_inj_buf + 8);
                        } else if (strncmp(usb_inj_buf, "<BTN:", 5) == 0) {
                            uart_protocol.sendButtonPress(usb_inj_buf + 5);
                            Serial.printf("[BTN→STM32] TOUCH:%s\n", usb_inj_buf + 5);
                        } else if (sscanf(usb_inj_buf, "<TAP:%d,%d,%d", &tx, &ty, &tms) >= 2) {
                            sim_tap(UX(tx), UY(ty), tms);
                            Serial.printf("[TAP] %d,%d %d мс\n", tx, ty, tms);
                        } else if (strcmp(usb_inj_buf, "<UIDUMP") == 0) {
                            ui_dump();
                        } else if (sscanf(usb_inj_buf, "<RXMUTE:%d", &tx) == 1) {
                            uart_protocol.setRxMute(tx != 0);
                            Serial.printf("[RXMUTE] %d\n", tx);
                        } else {
                            usb_inj_buf[usb_inj_len - 1] = '>';
                            usb_inj_buf[usb_inj_len] = '\0';
                            uart_protocol.injectCmd(usb_inj_buf);
                            Serial.printf("[INJ] %s\n", usb_inj_buf);
                        }
                        usb_inj_len = 0;
                    } else if (c == '\n' || c == '\r') {
                        usb_inj_len = 0;
                    }
                } else if (c == 'S') {
                    if (sem_requested) { xSemaphoreGive(sem_requested); usb_ss_state = 1; }
                    break;
                } else if (c == '<') {
                    usb_inj_buf[0] = c; usb_inj_len = 1;
                }
            }
        } else if (usb_ss_state == 1) {
            // Ждём завершения захвата (неблокирующе)
            if (sem_done && xSemaphoreTake(sem_done, 0) == pdTRUE) {
                if (jpeg_size > 0) {
                    uint8_t hdr[8] = {
                        0xDE, 0xAD, 0xBE, 0xEF,
                        (uint8_t)(jpeg_size),
                        (uint8_t)(jpeg_size >> 8),
                        (uint8_t)(jpeg_size >> 16),
                        (uint8_t)(jpeg_size >> 24)
                    };
                    // Весь кадр одной операцией: иначе между кусками в поток USB
                    // вклиниваются [STATE]/[DBG] из следующих итераций loop() и JPEG ломается
                    Serial.write(hdr, 8);
                    Serial.write(jpeg_buf, jpeg_size);
                    Serial.flush();  // сбросить USB CDC буфер (иначе последние ~64 байта теряются)
                }
                usb_ss_state = 0;
            }
        }
    }
#endif  // DISPLAY_JC4827W543 || DISPLAY_JC8012P4A1C

#ifndef DISPLAY_JC8012P4A1C
    delay(5);
#endif
}
