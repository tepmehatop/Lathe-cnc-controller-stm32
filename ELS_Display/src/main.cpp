/**
 * @file main.cpp
 * @brief ELS Display - Main firmware for ESP32/ESP32-S3
 *
 * Supports:
 * - ESP32-2432S024 (2.4" 320x240) - ILI9341 via TFT_eSPI
 * - JC4827W543 (4.3" 480x272) - NV3041A via Arduino_GFX
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "display_config.h"
#include "uart_protocol.h"

#ifdef DISPLAY_JC4827W543
#include <WiFi.h>
#include <esp_http_server.h>

// stb JPEG encoder (single-header)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// WiFi подключение к роутеру
static const char* WIFI_SSID = "VIRUS932";
static const char* WIFI_PASS = "111111111";

// PSRAM framebuffer — RGB565 (заполняется в my_disp_flush)
static uint16_t* screen_fb = nullptr;

// JPEG буфер в PSRAM (~128KB достаточно для 480×272 при качестве 80)
static uint8_t*  jpeg_buf  = nullptr;
static uint32_t  jpeg_size = 0;
static uint32_t  jpeg_cap  = 0;

// Семафоры: httpd task (core 0) ↔ loop() task (core 1)
static SemaphoreHandle_t sem_requested = nullptr;  // httpd → loop: "сделай скриншот"
static SemaphoreHandle_t sem_done      = nullptr;  // loop → httpd: "готово"

// stb callback — дописывает данные в jpeg_buf
static void jpeg_write_cb(void* /*ctx*/, void* data, int size) {
    if (!jpeg_buf || jpeg_size + (uint32_t)size > jpeg_cap) return;
    memcpy(jpeg_buf + jpeg_size, data, (size_t)size);
    jpeg_size += (uint32_t)size;
}

// Вызывается из loop() — конвертирует screen_fb RGB565 → JPEG в jpeg_buf
static void do_capture() {
    if (!screen_fb || !jpeg_buf) { jpeg_size = 0; return; }
    const int W = SCREEN_WIDTH, H = SCREEN_HEIGHT;

    // Временный RGB888 буфер в PSRAM (391KB — слишком большой для стека)
    uint8_t* rgb = (uint8_t*)ps_malloc((size_t)W * H * 3);
    if (!rgb) { jpeg_size = 0; Serial.println("JPEG: ps_malloc rgb failed"); return; }

    // RGB565 (LV_COLOR_16_SWAP=0) → RGB888
    for (int y = 0; y < H; y++) {
        const uint16_t* src = screen_fb + y * W;
        uint8_t*        dst = rgb + y * W * 3;
        for (int x = 0; x < W; x++) {
            uint16_t px = src[x];
            dst[x*3+0] = (uint8_t)(((px >> 11) & 0x1F) << 3);  // R
            dst[x*3+1] = (uint8_t)(((px >>  5) & 0x3F) << 2);  // G
            dst[x*3+2] = (uint8_t)( (px        & 0x1F) << 3);  // B
        }
    }

    jpeg_size = 0;
    stbi_write_jpg_to_func(jpeg_write_cb, nullptr, W, H, 3, rgb, 80);
    free(rgb);
    Serial.printf("JPEG ready: %u bytes\n", jpeg_size);
}

// HTTP handler — работает в httpd task (core 0)
// Сигналит loop() сделать скриншот, ждёт результата, шлёт JPEG
static esp_err_t screenshot_handler(httpd_req_t* req) {
    if (!screen_fb || !jpeg_buf || !sem_requested || !sem_done) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not ready");
        return ESP_FAIL;
    }

    // Запрашиваем захват у loop() task
    xSemaphoreGive(sem_requested);

    // Ждём завершения (max 5 секунд)
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

    // Chunked transfer по 4KB
    const size_t CHUNK = 4096;
    esp_err_t ret = ESP_OK;
    size_t sent = 0;
    while (sent < jpeg_size && ret == ESP_OK) {
        size_t chunk = jpeg_size - sent;
        if (chunk > CHUNK) chunk = CHUNK;
        ret = httpd_resp_send_chunk(req, (const char*)jpeg_buf + sent, (ssize_t)chunk);
        if (ret == ESP_OK) sent += chunk;
    }
    Serial.printf("Screenshot: sent %u/%u bytes, ret=%d\n",
                  (unsigned)sent, (unsigned)jpeg_size, ret);
    if (ret == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
    return ret;
}

// FreeRTOS задача с большим стеком (32KB) — ждёт запрос, кодирует JPEG
// (stb JPEG нужно ~10-16KB стека, Arduino loop() имеет 8KB — мало!)
static void capture_task_fn(void* /*pvParameters*/) {
    while (true) {
        if (sem_requested && xSemaphoreTake(sem_requested, portMAX_DELAY) == pdTRUE) {
            do_capture();
            if (sem_done) xSemaphoreGive(sem_done);
        }
    }
}

// Noop — всё делает capture_task_fn
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

    // NV3041A QSPI Bus
    Arduino_DataBus *bus = new Arduino_ESP32QSPI(
        45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */
    );

    // NV3041A Panel (480x272) - used directly for LVGL (no Canvas needed)
    Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

    // Touch GT911
    TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, GT911_SLAVE_ADDRESS1, TOUCH_RST);

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

// Layout variant: 0 = K (Featured+Grid+VertButtons), 1 = I (3×2 Grid+HorizButtons)
static uint8_t g_layout = 0;

// ============================================================================
// LVGL Display Driver
// ============================================================================

void IRAM_ATTR my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

#ifdef DISPLAY_JC4827W543
    // Direct write to display
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);

    // Копируем в PSRAM framebuffer для скриншотов
    if (screen_fb) {
        uint16_t* src = (uint16_t*)&color_p->full;
        for (uint32_t row = 0; row < h; row++) {
            uint16_t* dst = screen_fb + (area->y1 + row) * SCREEN_WIDTH + area->x1;
            memcpy(dst, src + row * w, w * 2);
        }
    }
#else
    // TFT_eSPI write
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

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
#ifdef DISPLAY_JC4827W543
    // GT911 Touch
    if (touch.read()) {
        TP_Point p = touch.getPoint(0);
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
#else
    // XPT2046 - TODO: implement
    data->state = LV_INDEV_STATE_REL;
#endif
}

// ============================================================================
// FONT DEMO SCREEN — показывает 4 жирных шрифта для выбора
// Строки: 1=Arial Bold, 2=DIN Alternate, 3=DIN Condensed, 4=Tahoma Bold
// ============================================================================

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
    lv_obj_set_pos(hdr, 8, 3);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(hdr, &font_dejavu_12, 0);
    lv_label_set_text(hdr, "FONT DEMO  [cyan=1.50 @ 56px]  [green=1650 @ 40px]");

    uint32_t bg_clr[] = {0x0d1a2a, 0x080e18, 0x0d1a2a, 0x080e18};
    int row_h = 62;

    for (int i = 0; i < 4; i++) {
        int y = 19 + i * row_h;

        // Фон строки
        lv_obj_t* row_bg = lv_obj_create(screen);
        lv_obj_set_size(row_bg, 480, row_h);
        lv_obj_set_pos(row_bg, 0, y);
        lv_obj_set_style_bg_color(row_bg, lv_color_hex(bg_clr[i]), 0);
        lv_obj_set_style_radius(row_bg, 0, 0);
        lv_obj_set_style_border_side(row_bg, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row_bg, lv_color_hex(0x1a3040), 0);
        lv_obj_set_style_border_width(row_bg, 1, 0);
        lv_obj_set_style_pad_all(row_bg, 0, 0);
        lv_obj_clear_flag(row_bg, LV_OBJ_FLAG_SCROLLABLE);

        // Название шрифта
        lv_obj_t* name_lbl = lv_label_create(row_bg);
        lv_obj_set_pos(name_lbl, 5, 8);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x5a8a9a), 0);
        lv_obj_set_style_text_font(name_lbl, &font_dejavu_12, 0);
        lv_label_set_text(name_lbl, rows[i].name);

        // "1.50" — большой шрифт 56px
        lv_obj_t* val_56 = lv_label_create(row_bg);
        lv_obj_set_pos(val_56, 115, 2);
        lv_obj_set_style_text_color(val_56, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(val_56, rows[i].font56, 0);
        lv_label_set_text(val_56, "1.50");

        // "1650" — средний шрифт 40px
        lv_obj_t* val_40 = lv_label_create(row_bg);
        lv_obj_set_pos(val_40, 310, 10);
        lv_obj_set_style_text_color(val_40, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_font(val_40, rows[i].font40, 0);
        lv_label_set_text(val_40, "1650");
    }

    lv_scr_load(screen);
    return screen;
}

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
    lv_obj_t* k_content;           // Панель варианта K (показана когда g_layout==0)
    lv_obj_t* i_content;           // Панель варианта I (показана когда g_layout==1)

    // Настройки
    lv_obj_t* settings_menu;       // Меню настроек (оверлей)

    // Блок для жёлтой рамки в режиме редактирования primary value
    // В K — featured cell box; в I — cell[0] box; nullptr → border на самом label (compat)
    lv_obj_t* primary_edit_box;
};

static UIHandles g_ui = {};

// ============================================================================
// Хранит виджеты, специфичные для каждого варианта компоновки (K или I).
// Два экземпляра g_wk / g_wi заполняются при создании UI.
// switch_layout() копирует нужный набор в поля g_ui.*
// ============================================================================
struct LayoutWidgets {
    lv_obj_t* primary_val;
    lv_obj_t* primary_val_glow;
    lv_obj_t* primary_unit;
    lv_obj_t* thread_tag;
    lv_obj_t* secondary_val;
    lv_obj_t* secondary_val_glow;
    lv_obj_t* secondary_unit;
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
static LayoutWidgets g_wk = {};   // K layout widgets
static LayoutWidgets g_wi = {};   // I layout widgets

// Копирует виджеты из src в поля g_ui.*
static void apply_layout_widgets(const LayoutWidgets& w) {
    g_ui.primary_val       = w.primary_val;
    g_ui.primary_val_glow  = w.primary_val_glow;
    g_ui.primary_unit      = w.primary_unit;
    g_ui.thread_tag        = w.thread_tag;
    g_ui.secondary_val     = w.secondary_val;
    g_ui.secondary_val_glow= w.secondary_val_glow;
    g_ui.secondary_unit    = w.secondary_unit;
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
    "\xD0\xA0\xD0\x95\xD0\x97\xD0\x95\xD0\xA0\xD0\x92",                  // РЕЗЕРВ
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
};
static SubEdit g_sub_edit = {false, -1, 0, 0, 1, 0, 1, 1, 0, 1000, 1};

// ============================================================================
// Состояние алерта — всплывающее уведомление от Arduino
// ============================================================================
struct AlertState {
    bool     active;
    uint32_t show_ms;  // millis() когда показан → авто-скрытие через 5с
};
static AlertState g_alert = {false, 0};

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
// Барабан выбора подачи (pilot: MODE_FEED)
// 5 меток + swipe gesture вместо lv_roller — высокая производительность.
// UART отправляется только при OK. Правые кнопки всегда доступны.
// ============================================================================
struct FeedRollerState {
    bool      active;
    lv_obj_t* backdrop;       // полупрозрачный фон (только левая область)
    lv_obj_t* panel;          // центральная панель
    lv_obj_t* labels[5];      // 5 строк барабана
    int32_t   pending_val;    // выбранное значение мм*100
    int32_t   entry_val;      // значение при входе (для отмены)
    int32_t   touch_accum;    // накопленный сдвиг px
};
static FeedRollerState g_feed_roller = {};

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
static void create_i_content(lv_obj_t* screen);
static void switch_layout(uint8_t new_layout);
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
static void open_feed_roller(int32_t cur_val);
static void close_feed_roller(bool confirm);
static void feed_roller_update_labels();
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
    if (g_sub_edit.active) {
        sub_edit_step(true);
    } else if (g_edit_param.active) {
        g_edit_param.last_ms = millis();
        char buf[32];
        if (s_last_mode == MODE_THREAD) {
            if (g_edit_param.local_step < THREAD_TABLE_SIZE - 1) g_edit_param.local_step++;
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
            // aFeed_mm — целое мм/мин, шаг 5
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
            if (g_feed_roller.active && s_last_mode == MODE_FEED) {
                g_feed_roller.pending_val = constrain(g_edit_param.local_val, 5, 2500);
                feed_roller_update_labels();
            }
        }
        // Отправить целевую команду по режиму
        if (s_last_mode == MODE_THREAD) {
            char ts[24]; snprintf(ts, sizeof(ts), "THREAD_STEP:%d", (int)g_edit_param.local_step);
            uart_protocol.sendButtonPress(ts);
        } else if (s_last_mode == MODE_FEED || s_last_mode == MODE_SPHERE ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) {
            if (!g_feed_roller.active) {
                char fs[24]; snprintf(fs, sizeof(fs), "FEED:%d", (int)g_edit_param.local_val);
                uart_protocol.sendButtonPress(fs);
            }
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
    if (g_sub_edit.active) {
        sub_edit_step(false);
    } else if (g_edit_param.active) {
        g_edit_param.last_ms = millis();
        char buf[32];
        if (s_last_mode == MODE_THREAD) {
            if (g_edit_param.local_step > 0) g_edit_param.local_step--;
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
            // aFeed_mm — целое мм/мин, шаг 5
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
            if (g_feed_roller.active && s_last_mode == MODE_FEED) {
                g_feed_roller.pending_val = constrain(g_edit_param.local_val, 5, 2500);
                feed_roller_update_labels();
            }
        }
        // Отправить целевую команду по режиму
        if (s_last_mode == MODE_THREAD) {
            char ts[24]; snprintf(ts, sizeof(ts), "THREAD_STEP:%d", (int)g_edit_param.local_step);
            uart_protocol.sendButtonPress(ts);
        } else if (s_last_mode == MODE_FEED || s_last_mode == MODE_SPHERE ||
                   s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R) {
            if (!g_feed_roller.active) {
                char fs[24]; snprintf(fs, sizeof(fs), "FEED:%d", (int)g_edit_param.local_val);
                uart_protocol.sendButtonPress(fs);
            }
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

    if (g_sub_edit.active) {
        exit_sub_edit();
    } else if (g_edit_param.active) {
        if (s_last_mode == MODE_FEED && g_feed_roller.active) {
            close_feed_roller(true);  // отправляет FEED:val, обновляет local_val
        }
        exit_edit_mode();
    } else if (s_last_mode == MODE_DIVIDER) {
        uart_protocol.sendButtonPress("PARAM_OK");
    }
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
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // title: small label at top
    lv_obj_t* title = lv_label_create(box);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_text_font(title, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(editable ? 0xffaa44 : 0x6a9fb5), 0);
    lv_label_set_text(title, title_text);

    // val: vertically centered in remaining space below title
    // title_h≈16, val_h≈30, content=(h-8), remaining=(h-8-16-30)/2
    int val_y = 16 + ((h - 8 - 16 - 30) >> 1);
    if (val_y < 16) val_y = 16;
    lv_obj_t* val = lv_label_create(box);
    lv_obj_set_pos(val, 0, val_y);
    lv_obj_set_style_text_font(val, &font_tahoma_bold_28, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xe0e0e0), 0);
    lv_label_set_text(val, "--");
    if (use_recolor) lv_label_set_recolor(val, true);

    return {box, title, val};
}

// ============================================================================
// Создание панели ВАРИАНТ K: Featured cell + 3×2 grid + вертикальные кнопки
// ============================================================================
static void create_k_content(lv_obj_t* screen)
{
    lv_obj_t* panel = lv_obj_create(screen);
    lv_obj_set_size(panel, 480, 236);
    lv_obj_set_pos(panel, 0, 36);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    g_ui.k_content = panel;

    const int PAD = 6, GAP = 4;
    const int RIGHT_W = 56;
    const int LEFT_W  = 480 - 2*PAD - RIGHT_W - GAP;   // 408
    const int INNER_H = 236 - 2*PAD;                   // 224
    const int FEAT_H  = 72;
    const int GRID_Y  = PAD + FEAT_H + GAP;            // 82
    const int GRID_H  = INNER_H - FEAT_H - GAP;        // 148
    const int ROW_H   = (GRID_H - GAP) / 2;            // 72
    const int COL_W0  = (LEFT_W - 2*GAP) / 3;          // 133
    const int COL_W2  = LEFT_W - 2*COL_W0 - 2*GAP;    // 134
    const int RIGHT_X = PAD + LEFT_W + GAP;            // 418
    int col_x[3] = { PAD, PAD + COL_W0 + GAP, PAD + 2*COL_W0 + 2*GAP };

    // ── Featured cell ────────────────────────────────────────────────────────
    lv_obj_t* feat = lv_obj_create(panel);
    lv_obj_set_size(feat, LEFT_W, FEAT_H);
    lv_obj_set_pos(feat, PAD, PAD);
    lv_obj_set_style_bg_color(feat, lv_color_hex(0x0a1828), 0);
    lv_obj_set_style_bg_opa(feat, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(feat, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(feat, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(feat, 4, 0);
    lv_obj_set_style_radius(feat, 4, 0);
    lv_obj_set_style_pad_all(feat, 0, 0);
    lv_obj_clear_flag(feat, LV_OBJ_FLAG_SCROLLABLE);
    g_wk.primary_edit_box = feat;

    // Primary title/unit (tappable in M3 for THR_CAT)
    lv_obj_t* p_unit = lv_label_create(feat);
    lv_obj_set_pos(p_unit, 8, 5);
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
    lv_obj_set_pos(p_val, 8, 28);
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
    lv_obj_set_pos(tag, 200, 18);
    lv_obj_add_flag(tag, LV_OBJ_FLAG_HIDDEN);
    g_wk.thread_tag = tag;

    // Secondary unit label (right side) — same size as param cell titles
    lv_obj_t* s_unit = lv_label_create(feat);
    lv_obj_set_style_text_font(s_unit, &font_dejavu_14, 0);
    lv_obj_set_style_text_color(s_unit, lv_color_hex(0x3a7a5a), 0);
    lv_label_set_text(s_unit, "RPM");
    lv_obj_align(s_unit, LV_ALIGN_TOP_RIGHT, -8, 5);
    g_wk.secondary_unit = s_unit;

    // Secondary value (right side) — same font as param cell values, with gap below unit
    lv_obj_t* s_val = lv_label_create(feat);
    lv_obj_set_style_text_font(s_val, &font_tahoma_bold_28, 0);
    lv_obj_set_style_text_color(s_val, lv_color_hex(0x00ff88), 0);
    lv_label_set_text(s_val, "1650");
    lv_label_set_recolor(s_val, true);
    lv_obj_align(s_val, LV_ALIGN_TOP_RIGHT, -8, 24);
    g_wk.secondary_val      = s_val;
    g_wk.secondary_val_glow = nullptr;

    // RPM bar — invisible stub (removed per user request; kept for API compat)
    lv_obj_t* rbar = lv_bar_create(feat);
    lv_obj_set_size(rbar, 1, 1);
    lv_obj_set_style_opa(rbar, LV_OPA_TRANSP, 0);
    lv_bar_set_range(rbar, 0, 3000);
    lv_bar_set_value(rbar, 0, LV_ANIM_OFF);
    g_wk.rpm_bar = rbar;

    // Edit arrows (hidden)
    lv_obj_t* arL = lv_label_create(feat);
    lv_obj_set_style_text_color(arL, lv_color_hex(0xffcc00), 0);
    lv_obj_set_style_text_font(arL, &lv_font_montserrat_20, 0);
    lv_label_set_text(arL, LV_SYMBOL_UP);
    lv_obj_set_pos(arL, 8, 20);
    lv_obj_add_flag(arL, LV_OBJ_FLAG_HIDDEN);
    g_wk.edit_arrow_left = arL;

    lv_obj_t* arR = lv_label_create(feat);
    lv_obj_set_style_text_color(arR, lv_color_hex(0xffcc00), 0);
    lv_obj_set_style_text_font(arR, &lv_font_montserrat_20, 0);
    lv_label_set_text(arR, LV_SYMBOL_DOWN);
    lv_obj_align(arR, LV_ALIGN_TOP_RIGHT, -8, 20);
    lv_obj_add_flag(arR, LV_OBJ_FLAG_HIDDEN);
    g_wk.edit_arrow_right = arR;

    // Transparent tap zone — enter/exit primary edit + double-tap = thread type menu
    lv_obj_t* tap = lv_btn_create(feat);
    lv_obj_set_size(tap, LEFT_W - 100, FEAT_H - 8);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_shadow_width(tap, 0, 0);
    lv_obj_add_event_cb(tap, [](lv_event_t*) {
        static uint32_t s_ftap = 0; uint32_t now = millis();
        bool dbl = (now - s_ftap) < 400; s_ftap = now;
        if (dbl && s_last_mode == MODE_THREAD) {
            if (g_edit_param.active) exit_edit_mode();
            show_thr_type_menu(); return;
        }
        if (g_edit_param.active) exit_edit_mode(); else enter_edit_mode();
    }, LV_EVENT_CLICKED, nullptr);

    // ── Grid cells 3×2 ───────────────────────────────────────────────────────
    // Row 1: ПОЗИЦИЯ Z
    {
        auto r = make_param_cell(panel, col_x[0], GRID_Y, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
        g_wk.row1_box = r.box; g_wk.row1_title = r.title; g_wk.row1_val = r.val;
        lv_label_set_text(r.val, "+125.45");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0x00ff88), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)0);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_ztap = 0; uint32_t now = millis();
            bool dbl = (now - s_ztap) < 400; s_ztap = now;
            if (dbl && s_last_mode == MODE_THREAD) {
                if (g_edit_param.active) exit_edit_mode();
                uart_protocol.sendButtonPress("THR_CAT"); return;
            }
            if (dbl && (s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R)) {
                // Прыжок между категориями конусов: углы[0-44] → KM[45-51] → соотношения[52-62] → углы
                uint8_t ci = uart_protocol.getData().cone_idx;
                uint8_t next = (ci < 45) ? 45 : (ci < 52) ? 52 : 0;
                if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, CONE_NAMES[next]);
                uart_protocol.setConeOptimistic(next);
                char cstr[16]; snprintf(cstr, sizeof(cstr), "CONE:%d", (int)next);
                uart_protocol.sendButtonPress(cstr);
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Row 2: ПОЗИЦИЯ X
    {
        auto r = make_param_cell(panel, col_x[1], GRID_Y, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
        g_wk.row2_box = r.box; g_wk.row2_title = r.title; g_wk.row2_val = r.val;
        lv_label_set_text(r.val, "-42.78");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0xff5555), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)1);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_xtap = 0; uint32_t now = millis();
            bool dbl = (now - s_xtap) < 400; s_xtap = now;
            if (dbl) {
                if (g_joystick.active) hide_joystick_overlay();
                else                    show_joystick_overlay();
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Row 3: mode-specific param
    {
        auto r = make_param_cell(panel, col_x[2], GRID_Y, COL_W2, ROW_H,
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
        auto r = make_param_cell(panel, col_x[0], GRID_Y + ROW_H + GAP, COL_W0, ROW_H,
                                  "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB", false, true);  // ПРОХОДЫ
        g_wk.row4_box = r.box; g_wk.row4_title = r.title; g_wk.row4_val = r.val;
        lv_label_set_recolor(r.val, true);
    }
    // Spare1 (col1 bottom): tracked thread-type cell — shown in M3 only
    {
        auto r = make_param_cell(panel, col_x[1], GRID_Y + ROW_H + GAP, COL_W0, ROW_H,
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
            if (g_sub_edit.active && g_sub_edit.row == 3) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(3); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Spare2 (col2 bottom): SM-индикатор + двойной тап → PARAM_OK (SM1→SM2→SM3)
    {
        lv_obj_t* dim = lv_obj_create(panel);
        lv_obj_set_size(dim, COL_W2, ROW_H);
        lv_obj_set_pos(dim, col_x[2], GRID_Y + ROW_H + GAP);
        lv_obj_set_style_bg_color(dim, lv_color_hex(0x060810), 0);
        lv_obj_set_style_bg_opa(dim, LV_OPA_COVER, 0);
        lv_obj_set_style_border_side(dim, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(dim, lv_color_hex(0x1a3a4a), 0);
        lv_obj_set_style_border_width(dim, 2, 0);
        lv_obj_set_style_radius(dim, 4, 0);
        lv_obj_set_style_pad_all(dim, 8, 0);
        lv_obj_clear_flag(dim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dim, LV_OBJ_FLAG_CLICKABLE);
        // Значение: название текущего SM (обновляется через update_sm_row6)
        lv_obj_t* dlbl = lv_label_create(dim);
        lv_obj_set_style_text_font(dlbl, &font_tahoma_bold_16, 0);
        lv_obj_set_style_text_color(dlbl, lv_color_hex(0x2a3a4a), 0);
        lv_obj_align(dlbl, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_long_mode(dlbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(dlbl, COL_W2 - 20);
        lv_obj_set_style_text_align(dlbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(dlbl, "--");
        g_ui.sm_row6 = dlbl;
        // Двойной тап → PARAM_OK (SM1→SM2→SM3→SM1)
        lv_obj_add_event_cb(dim, [](lv_event_t*) {
            static uint32_t s_sm6tap = 0; uint32_t now = millis();
            bool dbl = (now - s_sm6tap) < 400; s_sm6tap = now;
            if (dbl) { exit_sub_edit(); uart_protocol.sendButtonPress("PARAM_OK"); }
        }, LV_EVENT_CLICKED, nullptr);
    }

    // ── Right vertical buttons ────────────────────────────────────────────────
    const int UP_H = 64, OK_H = 88, DN_H = 64;
    lv_obj_t* up_btn = lv_btn_create(panel);
    lv_obj_set_size(up_btn, RIGHT_W, UP_H);
    lv_obj_set_pos(up_btn, RIGHT_X, PAD);
    lv_obj_set_style_bg_color(up_btn, lv_color_hex(0x2a4a5a), 0);
    lv_obj_set_style_border_color(up_btn, lv_color_hex(0x3a6a8a), 0);
    lv_obj_set_style_border_width(up_btn, 1, 0);
    lv_obj_set_style_radius(up_btn, 4, 0);
    lv_obj_set_style_shadow_width(up_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(up_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_UP); }
    lv_obj_add_event_cb(up_btn, [](lv_event_t*) { handle_up_btn_click(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* ok_btn = lv_btn_create(panel);
    lv_obj_set_size(ok_btn, RIGHT_W, OK_H);
    lv_obj_set_pos(ok_btn, RIGHT_X, PAD + UP_H + GAP);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(ok_btn, 1, 0);
    lv_obj_set_style_radius(ok_btn, 4, 0);
    lv_obj_set_style_shadow_width(ok_btn, 10, 0);
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
    lv_obj_set_style_border_width(dn_btn, 1, 0);
    lv_obj_set_style_radius(dn_btn, 4, 0);
    lv_obj_set_style_shadow_width(dn_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(dn_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_DOWN); }
    lv_obj_add_event_cb(dn_btn, [](lv_event_t*) { handle_dn_btn_click(); }, LV_EVENT_CLICKED, nullptr);
}

// ============================================================================
// Создание панели ВАРИАНТ I: Симметричная сетка 3×2 + горизонтальные кнопки
// ============================================================================
static void create_i_content(lv_obj_t* screen)
{
    lv_obj_t* panel = lv_obj_create(screen);
    lv_obj_set_size(panel, 480, 236);
    lv_obj_set_pos(panel, 0, 36);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);  // hidden by default
    g_ui.i_content = panel;

    const int PAD = 6, GAP = 4;
    const int BTN_H   = 36;
    const int INNER_W = 480 - 2*PAD;       // 468
    const int INNER_H = 236 - 2*PAD;       // 224
    const int GRID_H  = INNER_H - BTN_H - GAP;   // 182
    const int ROW_H   = (GRID_H - GAP) / 2;      // 89
    const int COL_W   = (INNER_W - 2*GAP) / 3;   // 153
    int col_x[3] = { PAD, PAD + COL_W + GAP, PAD + 2*COL_W + 2*GAP };
    int row_y[2] = { PAD, PAD + ROW_H + GAP };

    // Cell [0]: row1 — ПОЗИЦИЯ Z
    {
        auto r = make_param_cell(panel, col_x[0], row_y[0], COL_W, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
        g_wi.row1_box = r.box; g_wi.row1_title = r.title; g_wi.row1_val = r.val;
        lv_label_set_text(r.val, "+125.45");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0x00ff88), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)0);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_ztap = 0; uint32_t now = millis();
            bool dbl = (now - s_ztap) < 400; s_ztap = now;
            if (dbl && s_last_mode == MODE_THREAD) {
                if (g_edit_param.active) exit_edit_mode();
                uart_protocol.sendButtonPress("THR_CAT"); return;
            }
            if (dbl && (s_last_mode == MODE_CONE_L || s_last_mode == MODE_CONE_R)) {
                // Прыжок между категориями конусов: углы[0-44] → KM[45-51] → соотношения[52-62] → углы
                uint8_t ci = uart_protocol.getData().cone_idx;
                uint8_t next = (ci < 45) ? 45 : (ci < 52) ? 52 : 0;
                if (g_ui.row3_val) lv_label_set_text(g_ui.row3_val, CONE_NAMES[next]);
                uart_protocol.setConeOptimistic(next);
                char cstr[16]; snprintf(cstr, sizeof(cstr), "CONE:%d", (int)next);
                uart_protocol.sendButtonPress(cstr);
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Cell [1]: row2 — ПОЗИЦИЯ X
    {
        auto r = make_param_cell(panel, col_x[1], row_y[0], COL_W, ROW_H,
                                  "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
        g_wi.row2_box = r.box; g_wi.row2_title = r.title; g_wi.row2_val = r.val;
        lv_label_set_text(r.val, "-42.78");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0xff5555), 0);
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)1);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            static uint32_t s_xtap = 0; uint32_t now = millis();
            bool dbl = (now - s_xtap) < 400; s_xtap = now;
            if (dbl) {
                if (g_joystick.active) hide_joystick_overlay();
                else                    show_joystick_overlay();
                return;
            }
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Cell [2]: primary_val — main operating value (pitch/feed/etc.)
    {
        auto r = make_param_cell(panel, col_x[2], row_y[0], COL_W, ROW_H,
                                  "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9C\xD0\x9C");  // ШАГ ММ
        g_wi.primary_edit_box = r.box;
        g_wi.primary_val = r.val; g_wi.primary_val_glow = nullptr;
        lv_label_set_text(r.val, "1.50");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0x00d4ff), 0);
        g_wi.primary_unit = r.title;
        lv_obj_add_flag(r.title, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r.title, [](lv_event_t*) {
            static uint32_t s_ttap = 0; uint32_t now = millis();
            if ((now - s_ttap) < 400 && s_last_mode == MODE_THREAD) {
                if (g_edit_param.active) exit_edit_mode();
                uart_protocol.sendButtonPress("THR_CAT");
            }
            s_ttap = now;
        }, LV_EVENT_CLICKED, nullptr);
        // Thread tag inside cell[2]
        lv_obj_t* itag = lv_label_create(r.box);
        lv_obj_set_style_text_color(itag, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_text_font(itag, &font_tahoma_bold_22, 0);
        lv_label_set_text(itag, "");
        lv_obj_align(itag, LV_ALIGN_TOP_RIGHT, -4, 0);
        lv_obj_add_flag(itag, LV_OBJ_FLAG_HIDDEN);
        g_wi.thread_tag = itag;
        // Edit tap zone
        lv_obj_t* itap = lv_btn_create(r.box);
        lv_obj_set_size(itap, COL_W, ROW_H - 6);
        lv_obj_set_pos(itap, 0, 0);
        lv_obj_set_style_bg_opa(itap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(itap, 0, 0);
        lv_obj_set_style_shadow_width(itap, 0, 0);
        lv_obj_add_event_cb(itap, [](lv_event_t*) {
            static uint32_t s_ptap = 0; uint32_t now = millis();
            bool dbl = (now - s_ptap) < 400; s_ptap = now;
            if (dbl && s_last_mode == MODE_THREAD) {
                if (g_edit_param.active) exit_edit_mode();
                show_thr_type_menu(); return;
            }
            if (g_edit_param.active) exit_edit_mode(); else enter_edit_mode();
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Cell [3]: secondary_val — RPM / cycles / etc.
    {
        auto r = make_param_cell(panel, col_x[0], row_y[1], COL_W, ROW_H,
                                  "RPM", false, true);
        g_wi.secondary_val = r.val; g_wi.secondary_val_glow = nullptr;
        g_wi.secondary_unit = r.title;
        lv_label_set_text(r.val, "1650");
        lv_obj_set_style_text_color(r.val, lv_color_hex(0x00ff88), 0);
        // Invisible RPM bar (functionally required)
        lv_obj_t* ibar = lv_bar_create(r.box);
        lv_obj_set_size(ibar, 1, 1);
        lv_obj_set_style_opa(ibar, LV_OPA_TRANSP, 0);
        lv_bar_set_range(ibar, 0, 3000);
        lv_bar_set_value(ibar, 0, LV_ANIM_OFF);
        g_wi.rpm_bar = ibar;
    }
    // Cell [4]: row3 — mode-specific sub-param
    {
        auto r = make_param_cell(panel, col_x[1], row_y[1], COL_W, ROW_H,
                                  "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB");  // ПРОХОДЫ
        g_wi.row3_box = r.box; g_wi.row3_title = r.title; g_wi.row3_val = r.val;
        lv_obj_add_flag(r.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(r.box, (void*)(intptr_t)2);
        lv_obj_add_event_cb(r.box, [](lv_event_t* e) {
            int row = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (g_sub_edit.active && g_sub_edit.row == row) exit_sub_edit();
            else { exit_sub_edit(); enter_sub_edit(row); }
        }, LV_EVENT_CLICKED, nullptr);
    }
    // Cell [5]: row4 — secondary count — только отображение (переключение SM перенесено на row6)
    {
        auto r = make_param_cell(panel, col_x[2], row_y[1], COL_W, ROW_H,
                                  "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA5\xD0\x9E\xD0\x94\xD0\xAB", false, true);  // ПРОХОДЫ
        g_wi.row4_box = r.box; g_wi.row4_title = r.title; g_wi.row4_val = r.val;
        lv_label_set_recolor(r.val, true);
    }
    // Edit arrows (hidden stubs for I layout)
    lv_obj_t* iArL = lv_label_create(panel);
    lv_obj_add_flag(iArL, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(iArL, "");
    g_wi.edit_arrow_left = iArL;
    lv_obj_t* iArR = lv_label_create(panel);
    lv_obj_add_flag(iArR, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(iArR, "");
    g_wi.edit_arrow_right = iArR;

    // ── Bottom button row ─────────────────────────────────────────────────────
    const int BTN_Y = PAD + GRID_H + GAP;
    const int UP_W  = (INNER_W - 2*GAP - 30) / 3;  // ~140
    const int OK_W  = INNER_W - 2*UP_W - 2*GAP;    // ~158
    int btn_x[3] = { PAD, PAD + UP_W + GAP, PAD + UP_W + GAP + OK_W + GAP };

    lv_obj_t* up_btn = lv_btn_create(panel);
    lv_obj_set_size(up_btn, UP_W, BTN_H);
    lv_obj_set_pos(up_btn, btn_x[0], BTN_Y);
    lv_obj_set_style_bg_color(up_btn, lv_color_hex(0x2a4a5a), 0);
    lv_obj_set_style_border_color(up_btn, lv_color_hex(0x3a6a8a), 0);
    lv_obj_set_style_border_width(up_btn, 1, 0);
    lv_obj_set_style_radius(up_btn, 4, 0);
    lv_obj_set_style_shadow_width(up_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(up_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_UP); }
    lv_obj_add_event_cb(up_btn, [](lv_event_t*) { handle_up_btn_click(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* ok_btn = lv_btn_create(panel);
    lv_obj_set_size(ok_btn, OK_W, BTN_H);
    lv_obj_set_pos(ok_btn, btn_x[1], BTN_Y);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(ok_btn, 1, 0);
    lv_obj_set_style_radius(ok_btn, 4, 0);
    lv_obj_set_style_shadow_width(ok_btn, 10, 0);
    lv_obj_set_style_shadow_color(ok_btn, lv_color_hex(0x00d4ff), 0);
    { lv_obj_t* l = lv_label_create(ok_btn); lv_obj_center(l);
      lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
      lv_obj_set_style_text_font(l, &font_dejavu_14, 0);
      lv_label_set_text(l, "OK"); }
    lv_obj_add_event_cb(ok_btn, [](lv_event_t*) { handle_ok_btn_click(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* dn_btn = lv_btn_create(panel);
    lv_obj_set_size(dn_btn, UP_W, BTN_H);
    lv_obj_set_pos(dn_btn, btn_x[2], BTN_Y);
    lv_obj_set_style_bg_color(dn_btn, lv_color_hex(0x2a4a5a), 0);
    lv_obj_set_style_border_color(dn_btn, lv_color_hex(0x3a6a8a), 0);
    lv_obj_set_style_border_width(dn_btn, 1, 0);
    lv_obj_set_style_radius(dn_btn, 4, 0);
    lv_obj_set_style_shadow_width(dn_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(dn_btn); lv_obj_center(l); lv_label_set_text(l, LV_SYMBOL_DOWN); }
    lv_obj_add_event_cb(dn_btn, [](lv_event_t*) { handle_dn_btn_click(); }, LV_EVENT_CLICKED, nullptr);
}

// ============================================================================
// Переключение компоновки K↔I: обновляет видимость панелей и указатели g_ui.*
// ============================================================================
static void switch_layout(uint8_t new_layout)
{
    g_layout = new_layout;
    if (new_layout == 0) {
        if (g_ui.k_content) lv_obj_clear_flag(g_ui.k_content, LV_OBJ_FLAG_HIDDEN);
        if (g_ui.i_content) lv_obj_add_flag  (g_ui.i_content, LV_OBJ_FLAG_HIDDEN);
        apply_layout_widgets(g_wk);
    } else {
        if (g_ui.k_content) lv_obj_add_flag  (g_ui.k_content, LV_OBJ_FLAG_HIDDEN);
        if (g_ui.i_content) lv_obj_clear_flag(g_ui.i_content, LV_OBJ_FLAG_HIDDEN);
        apply_layout_widgets(g_wi);
    }
    apply_mode_layout(s_last_mode);
    update_ui_values(uart_protocol.getData());
    Preferences prefs;
    prefs.begin("els_disp", false);
    prefs.putUChar("layout", new_layout);
    prefs.end();
}

// ============================================================================
// Меню настроек — оверлей (double-tap на иконку ⚠ в statusbar)
// ============================================================================
static void create_settings_menu(lv_obj_t* screen)
{
    lv_obj_t* ov = lv_obj_create(screen);
    lv_obj_set_size(ov, 480, 272);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_radius(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    g_ui.settings_menu = ov;

    lv_obj_t* title = lv_label_create(ov);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_label_set_text(title,
        "\xD0\x9D\xD0\x90\xD0\xA1\xD0\xA2\xD0\xA0\xD0\x9E\xD0\x99\xD0\x9A\xD0\x98");  // НАСТРОЙКИ

    lv_obj_t* lay_lbl = lv_label_create(ov);
    lv_obj_set_pos(lay_lbl, 20, 50);
    lv_obj_set_style_text_color(lay_lbl, lv_color_hex(0x6a9fb5), 0);
    lv_obj_set_style_text_font(lay_lbl, &font_dejavu_12, 0);
    lv_label_set_text(lay_lbl,
        "\xD0\x9A\xD0\x9E\xD0\x9C\xD0\x9F\xD0\x9E\xD0\x9D\xD0\x9E\xD0\x92\xD0\x9A\xD0\x90:");  // КОМПОНОВКА:

    lv_obj_t* btn_k = lv_btn_create(ov);
    lv_obj_set_size(btn_k, 190, 44);
    lv_obj_set_pos(btn_k, 20, 66);
    lv_obj_set_style_bg_color(btn_k, lv_color_hex(g_layout == 0 ? 0x003a5a : 0x0d2233), 0);
    lv_obj_set_style_border_color(btn_k, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(btn_k, g_layout == 0 ? 2 : 1, 0);
    lv_obj_set_style_radius(btn_k, 6, 0);
    lv_obj_set_style_shadow_width(btn_k, 0, 0);
    { lv_obj_t* l = lv_label_create(btn_k); lv_obj_center(l);
      lv_obj_set_style_text_font(l, &font_tahoma_bold_22, 0);
      lv_obj_set_style_text_color(l, lv_color_hex(0xc0d0e0), 0);
      lv_label_set_text(l,
          "\xD0\x92\xD0\x90\xD0\xA0\xD0\x98\xD0\x90\xD0\x9D\xD0\xA2 K"); }  // ВАРИАНТ K
    lv_obj_add_event_cb(btn_k, [](lv_event_t*) {
        if (g_layout != 0) switch_layout(0);
        lv_obj_add_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_i = lv_btn_create(ov);
    lv_obj_set_size(btn_i, 190, 44);
    lv_obj_set_pos(btn_i, 220, 66);
    lv_obj_set_style_bg_color(btn_i, lv_color_hex(g_layout == 1 ? 0x003a5a : 0x0d2233), 0);
    lv_obj_set_style_border_color(btn_i, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(btn_i, g_layout == 1 ? 2 : 1, 0);
    lv_obj_set_style_radius(btn_i, 6, 0);
    lv_obj_set_style_shadow_width(btn_i, 0, 0);
    { lv_obj_t* l = lv_label_create(btn_i); lv_obj_center(l);
      lv_obj_set_style_text_font(l, &font_tahoma_bold_22, 0);
      lv_obj_set_style_text_color(l, lv_color_hex(0xc0d0e0), 0);
      lv_label_set_text(l,
          "\xD0\x92\xD0\x90\xD0\xA0\xD0\x98\xD0\x90\xD0\x9D\xD0\xA2 I"); }  // ВАРИАНТ I
    lv_obj_add_event_cb(btn_i, [](lv_event_t*) {
        if (g_layout != 1) switch_layout(1);
        lv_obj_add_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lang_lbl = lv_label_create(ov);
    lv_obj_set_pos(lang_lbl, 20, 126);
    lv_obj_set_style_text_color(lang_lbl, lv_color_hex(0x6a9fb5), 0);
    lv_obj_set_style_text_font(lang_lbl, &font_dejavu_12, 0);
    lv_label_set_text(lang_lbl,
        "\xD0\xAF\xD0\x97\xD0\xAB\xD0\x9A: RU/EN \xe2\x80\x94 "
        "\xD0\x92 \xD0\xA0\xD0\x90\xD0\x97\xD0\xA0\xD0\x90\xD0\x91\xD0\x9E\xD0\xA2\xD0\x9A\xD0\x95");  // ЯЗЫК: RU/EN — В РАЗРАБОТКЕ

    lv_obj_t* other_lbl = lv_label_create(ov);
    lv_obj_set_pos(other_lbl, 20, 148);
    lv_obj_set_style_text_color(other_lbl, lv_color_hex(0x6a9fb5), 0);
    lv_obj_set_style_text_font(other_lbl, &font_dejavu_12, 0);
    lv_label_set_text(other_lbl,
        "\xD0\x9F\xD0\xA0\xD0\x9E\xD0\xA7\xD0\x95\xD0\x95: "
        "\xD0\x92 \xD0\xA0\xD0\x90\xD0\x97\xD0\xA0\xD0\x90\xD0\x91\xD0\x9E\xD0\xA2\xD0\x9A\xD0\x95");  // ПРОЧЕЕ: В РАЗРАБОТКЕ

    lv_obj_t* close_btn = lv_btn_create(ov);
    lv_obj_set_size(close_btn, 160, 36);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2a1a1a), 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(0x604040), 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    { lv_obj_t* l = lv_label_create(close_btn); lv_obj_center(l);
      lv_obj_set_style_text_font(l, &font_tahoma_bold_22, 0);
      lv_obj_set_style_text_color(l, lv_color_hex(0xa08080), 0);
      lv_label_set_text(l,
          "\xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2\xD0\xAC"); }  // ЗАКРЫТЬ
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) {
        lv_obj_add_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);

    // Double-tap on ⚠ warn icon → open settings
    if (g_ui.warn_bg) {
        lv_obj_add_flag(g_ui.warn_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(g_ui.warn_bg, [](lv_event_t*) {
            static uint32_t s_wtap = 0; uint32_t now = millis();
            if ((now - s_wtap) < 400)
                lv_obj_clear_flag(g_ui.settings_menu, LV_OBJ_FLAG_HIDDEN);
            s_wtap = now;
        }, LV_EVENT_CLICKED, nullptr);
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
    lv_obj_set_size(statusbar, 480, 36);
    lv_obj_set_pos(statusbar, 0, 0);
    lv_obj_set_style_bg_color(statusbar, lv_color_hex(0x0d2a3d), 0);
    lv_obj_set_style_radius(statusbar, 0, 0);
    lv_obj_set_style_pad_all(statusbar, 0, 0);
    lv_obj_set_style_border_side(statusbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(statusbar, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(statusbar, 2, 0);
    lv_obj_set_style_shadow_width(statusbar, 10, 0);
    lv_obj_set_style_shadow_color(statusbar, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_shadow_opa(statusbar, LV_OPA_30, 0);
    lv_obj_clear_flag(statusbar, LV_OBJ_FLAG_SCROLLABLE);

    // "M1 - ПОДАЧА" — Tahoma Bold 22  (Issue 1: was hardcoded M3)
    {
        lv_obj_t* mode_label = lv_label_create(statusbar);
        lv_obj_set_pos(mode_label, 15, 7);
        lv_obj_set_style_text_color(mode_label, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_text_font(mode_label, &font_tahoma_bold_22, 0);
        lv_label_set_text(mode_label, MODE_STRS[1]);  // M1 - ПОДАЧА
        g_ui.mode_lbl = mode_label;

        // Подрежим — справа от режима, мелким шрифтом
        lv_obj_t* submode_label = lv_label_create(statusbar);
        lv_obj_set_pos(submode_label, 210, 10);
        lv_obj_set_style_text_color(submode_label, lv_color_hex(0x4a7a8a), 0);
        lv_obj_set_style_text_font(submode_label, &font_dejavu_16, 0);
        lv_label_set_text(submode_label, "");  // обновится по UART
        g_ui.submode_lbl = submode_label;
    }

    // Иконка S2 (зелёная, активная)
    lv_obj_t* s2_ic = lv_obj_create(statusbar);
    lv_obj_set_size(s2_ic, 24, 24);
    lv_obj_set_pos(s2_ic, 396, 6);
    lv_obj_set_style_bg_color(s2_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_radius(s2_ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s2_ic, 0, 0);
    lv_obj_set_style_shadow_width(s2_ic, 8, 0);
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
    lv_obj_set_size(pwr_ic, 24, 24);
    lv_obj_set_pos(pwr_ic, 424, 6);
    lv_obj_set_style_bg_color(pwr_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_radius(pwr_ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pwr_ic, 0, 0);
    lv_obj_set_style_shadow_width(pwr_ic, 6, 0);
    lv_obj_set_style_shadow_color(pwr_ic, lv_color_hex(0x00ff88), 0);
    lv_obj_clear_flag(pwr_ic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* pwr_lbl = lv_label_create(pwr_ic);
    lv_obj_center(pwr_lbl);
    lv_obj_set_style_text_color(pwr_lbl, lv_color_hex(0x000000), 0);
    lv_label_set_text(pwr_lbl, LV_SYMBOL_CHARGE);  // LVGL symbol font (не DejaVu!)
    g_ui.pwr_bg = pwr_ic;

    // Иконка ⚠ (серая, неактивная)
    lv_obj_t* warn_ic = lv_obj_create(statusbar);
    lv_obj_set_size(warn_ic, 24, 24);
    lv_obj_set_pos(warn_ic, 452, 6);
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
            {312, LV_SYMBOL_LEFT,  &g_ui.lim_L},  // Z- ←
            {332, LV_SYMBOL_RIGHT, &g_ui.lim_R},  // Z+ →
            {352, LV_SYMBOL_UP,   &g_ui.lim_F},  // X+ ↑
            {372, LV_SYMBOL_DOWN, &g_ui.lim_B},  // X- ↓
        };
        for (auto& li : lims) {
            lv_obj_t* lbl = lv_label_create(statusbar);
            lv_obj_set_pos(lbl, li.x, 8);
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
        lv_obj_set_size(abox, 400, 110);
        lv_obj_align(abox, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(abox, lv_color_hex(0x1a0a00), 0);
        lv_obj_set_style_bg_opa(abox, LV_OPA_90, 0);
        lv_obj_set_style_border_color(abox, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_border_width(abox, 2, 0);
        lv_obj_set_style_radius(abox, 8, 0);
        lv_obj_set_style_shadow_width(abox, 16, 0);
        lv_obj_set_style_shadow_color(abox, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_shadow_opa(abox, LV_OPA_50, 0);
        lv_obj_clear_flag(abox, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(abox, LV_OBJ_FLAG_HIDDEN);
        g_ui.alert_box = abox;

        // Иконка предупреждения
        lv_obj_t* aicon = lv_label_create(abox);
        lv_obj_set_pos(aicon, 10, 10);
        lv_obj_set_style_text_font(aicon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(aicon, lv_color_hex(0xff8800), 0);
        lv_label_set_text(aicon, LV_SYMBOL_WARNING);

        // Текст алерта (2 строки, кириллица)
        lv_obj_t* amsg = lv_label_create(abox);
        lv_obj_set_pos(amsg, 50, 12);
        lv_obj_set_size(amsg, 300, 60);
        lv_obj_set_style_text_font(amsg, &font_tahoma_bold_22, 0);
        lv_obj_set_style_text_color(amsg, lv_color_hex(0xffcc44), 0);
        lv_label_set_long_mode(amsg, LV_LABEL_LONG_WRAP);
        lv_label_set_text(amsg, "");
        g_ui.alert_msg = amsg;

        // Кнопка OK для немедленного закрытия
        lv_obj_t* aok = lv_btn_create(abox);
        lv_obj_set_size(aok, 80, 28);
        lv_obj_align(aok, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_bg_color(aok, lv_color_hex(0x2a1800), 0);
        lv_obj_set_style_border_color(aok, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_border_width(aok, 1, 0);
        lv_obj_set_style_radius(aok, 4, 0);
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
        lv_obj_set_size(tbar, 480, 20);
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
    // CONTENT PANELS — варианты компоновки K и I
    // =========================================================
    create_k_content(screen);
    create_i_content(screen);
    create_settings_menu(screen);
    // Активировать виджеты сохранённого варианта компоновки
    if (g_layout == 0) {
        apply_layout_widgets(g_wk);
    } else {
        apply_layout_widgets(g_wi);
    }


    // =========================================================
    // TAP ZONE на statusbar — открывает меню выбора режима
    // Прозрачная кнопка поверх текста режима (левые 200px)
    // =========================================================
    {
        lv_obj_t* tap = lv_btn_create(statusbar);
        lv_obj_set_size(tap, 200, 36);
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
        lv_obj_set_size(ov, 480, 272);
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
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
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
            lv_obj_set_size(btn, 218, 38);
            lv_obj_set_pos(btn, 12 + col * 235, 44 + row * 47);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a4060), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
        lv_obj_set_size(cancel, 150, 32);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2a1a1a), 0);
        lv_obj_set_style_border_color(cancel, lv_color_hex(0x604040), 0);
        lv_obj_set_style_border_width(cancel, 1, 0);
        lv_obj_set_style_radius(cancel, 6, 0);
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
    // Открывается double-tap на ПОЗИЦИЯ X, закрывается кнопкой ✕ или double-tap
    // Авто-закрытие через 30 секунд бездействия
    // =========================================================
    {
        lv_obj_t* ov = lv_obj_create(screen);
        lv_obj_set_size(ov, 450, 215);
        lv_obj_align(ov, LV_ALIGN_CENTER, 0, 5);
        lv_obj_set_style_bg_color(ov, lv_color_hex(0x00080f), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
        lv_obj_set_style_border_color(ov, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_border_width(ov, 2, 0);
        lv_obj_set_style_radius(ov, 8, 0);
        lv_obj_set_style_pad_all(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        g_ui.joystick_overlay = ov;

        // Заголовок: ДЖОЙСТИК
        lv_obj_t* title = lv_label_create(ov);
        lv_obj_set_pos(title, 10, 7);
        lv_obj_set_style_text_color(title, lv_color_hex(0x00ff88), 0);
        lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
        // "ДЖОЙСТИК" = Д-Ж-О-Й-С-Т-И-К
        lv_label_set_text(title,
            "\xD0\x94\xD0\x96\xD0\x9E\xD0\x99\xD0\xA1\xD0\xA2\xD0\x98\xD0\x9A");

        // Кнопка ✕ закрыть (top-right)
        lv_obj_t* close_btn = lv_btn_create(ov);
        lv_obj_set_size(close_btn, 32, 28);
        lv_obj_set_pos(close_btn, 413, 5);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2a1010), 0);
        lv_obj_set_style_border_color(close_btn, lv_color_hex(0x884444), 0);
        lv_obj_set_style_border_width(close_btn, 1, 0);
        lv_obj_set_style_radius(close_btn, 4, 0);
        lv_obj_set_style_shadow_width(close_btn, 0, 0);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_obj_center(close_lbl);
        lv_obj_set_style_text_font(close_lbl, &font_tahoma_bold_22, 0);
        lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xff6666), 0);
        lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
        lv_obj_add_event_cb(close_btn, [](lv_event_t*) {
            hide_joystick_overlay();
        }, LV_EVENT_CLICKED, nullptr);

        // =====================
        // Крест управления осями (левая часть, X=10..240, Y=38..195)
        // ↑/↓ = ось X (поперечный суппорт), ←/→ = ось Z (продольная)
        // =====================
        const int cx = 10;   // начало X-области
        const int cy = 38;   // начало Y-области
        const int bw = 72;   // ширина кнопки
        const int bh = 50;   // высота кнопки
        const int bx = cx + bw + 5;   // центральная X позиция (для ↑ и ↓)
        const int by = cy + bh + 5;   // центральная Y позиция (для ← и →)

        // Общий стиль кнопок-стрелок
        auto make_dir_btn = [&](const char* txt, int x, int y,
                                 const char* cmd) -> lv_obj_t* {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, bw, bh);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00d4ff), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &font_tahoma_bold_28, 0);
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
        make_dir_btn(LV_SYMBOL_RIGHT, bx + bw + 5, by,    "JOY:RIGHT");
        // ↓ X- (низ)
        make_dir_btn(LV_SYMBOL_DOWN,  bx,        by + bh + 5, "JOY:DOWN");

        // СТОП (центр креста)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, bw, bh);
            lv_obj_set_pos(btn, bx, by);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a0808), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xff4444), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
        const int rx = 255;   // начало правой панели
        const int rbw = 180;  // ширина правых кнопок
        const int rbh = 38;   // высота правых кнопок

        // БЫСТРО (toggle rapid mode)
        {
            lv_obj_t* btn = lv_btn_create(ov);
            lv_obj_set_size(btn, rbw, rbh);
            lv_obj_set_pos(btn, rx, 38);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a1f0a), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00aa44), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
            lv_obj_set_pos(btn, rx, 84);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a6080), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
            lv_obj_set_pos(btn, rx, 130);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a6080), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
            lv_obj_set_pos(btn, rx, 170);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x00192a), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x00d4ff), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
        lv_obj_set_size(ov, 260, 220);
        lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(ov, lv_color_hex(0x000814), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
        lv_obj_set_style_border_color(ov, lv_color_hex(0x00d4ff), 0);
        lv_obj_set_style_border_width(ov, 2, 0);
        lv_obj_set_style_radius(ov, 8, 0);
        lv_obj_set_style_pad_all(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        g_ui.thr_type_menu = ov;

        // Заголовок: ТИП РЕЗЬБЫ
        lv_obj_t* title = lv_label_create(ov);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
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
            lv_obj_set_size(btn, 220, 34);
            lv_obj_set_pos(btn, 20, 40 + i * 38);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x0d2233), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x1a4060), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_radius(btn, 6, 0);
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
        lv_obj_set_size(cancel, 150, 28);
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2a1a1a), 0);
        lv_obj_set_style_border_color(cancel, lv_color_hex(0x604040), 0);
        lv_obj_set_style_border_width(cancel, 1, 0);
        lv_obj_set_style_radius(cancel, 6, 0);
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
        "\xD0\x94\xD0\x98\xD0\x90\xD0\x9C\xD0\x95\xD0\xA2\xD0\xA0");  // ДИАМЕТР
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC X");                                   // ОСЬ X
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC Y");                                   // ОСЬ Z
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// M1 SelectMenu=3: Подэкран отскок/натяг (OTSKOK_Z / TENSION_Z)
// ============================================================================
static void apply_feed_sm3_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x9E\xD0\xA2\xD0\xA1\xD0\x9A\xD0\x9E\xD0\x9A Y");         // ОТСКОК Z
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9D\xD0\x90\xD0\xA2\xD0\xAF\xD0\x93 Y");                  // НАТЯГ Z
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
        "\xD0\xA8\xD0\x98\xD0\xA0\xD0\x98\xD0\x9D\xD0\x90 \xD0\xA0\xD0\x95\xD0\xA1\xD0\xA6\xD0\x90");  // ШИРИНА РЕЗЦА
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\xA8\xD0\x90\xD0\x93 \xD0\x9E\xD0\xA1\xD0\x98 Y");             // ШАГ ОСИ Z
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title,
        "\xD0\x9D\xD0\x9E\xD0\x96\xD0\x9A\xD0\x90 \xD0\x9C\xD0\x9C");       // НОЖКА ММ
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// SM=3 Ввод/Сброс Осей: общий layout для M2/M3/M4/M5/M6/M7 (не M1)
// ============================================================================
static void apply_common_sm3_layout()
{
    if (g_edit_param.active) exit_edit_mode();
    lv_label_set_text(g_ui.row1_title,
        "\xD0\x94\xD0\x98\xD0\x90\xD0\x9C\xD0\x95\xD0\xA2\xD0\xA0");       // ДИАМЕТР
    lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row2_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC X");                                        // ОСЬ X
    lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
    lv_label_set_text(g_ui.row3_title,
        "\xD0\x9E\xD0\xA1\xD0\xAC Y");                                        // ОСЬ Z
    lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
}

// ============================================================================
// Меняет подписи/заголовки UI при смене режима.
// Вызывается из update_ui_values только когда mode изменился.
// ============================================================================
static void apply_mode_layout(LatheMode mode)
{
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
        case MODE_RESERVE:
            lv_label_set_text(g_ui.primary_unit,
                "\xD0\xA0\xD0\x95\xD0\x96\xD0\x98\xD0\x9C "
                "\xD0\x9D\xD0\x95\xD0\x94\xD0\x9E\xD0\xA1\xD0\xA2\xD0\xA3\xD0\x9F\xD0\x95\xD0\x9D");  // РЕЖИМ НЕДОСТУПЕН
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
        lv_obj_set_style_border_width(g_ui.mode_btns[i], active ? 2 : 1, 0);
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
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);  // read-only: dim blue
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);  // read-only: dim blue
            // row3: СЪЁМ (Ap) — редактируемый (жёлтое название)
            lv_label_set_text(g_ui.row3_title,
                "\xD0\xA1\xD0\xAA\xD0\x81\xD0\x9C");                            // СЪЁМ
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
            // row1 = ПОЗИЦИЯ Z (всегда; дабл-тап → THR_CAT)
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            // row3: ОБ/МИН — read-only
            lv_label_set_text(g_ui.row3_title,
                "\xD0\x9E\xD0\x91/\xD0\x9C\xD0\x98\xD0\x9D");                   // ОБ/МИН
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x6a9fb5), 0);
            // row4: ЦИКЛОВ — read-only (показываем теперь, было скрыто)
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_ui.row4_title,
                "\xD0\xA6\xD0\x98\xD0\x9A\xD0\x9B\xD0\x9E\xD0\x92");            // ЦИКЛОВ
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
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            // row2: ПОЗИЦИЯ X (всегда — синий, read-only)
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
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
                    "\xD0\xA1\xD0\xAA\xD0\x81\xD0\x9C");                         // СЪЁМ
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
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x6a9fb5), 0);
            // row2: ПОЗИЦИЯ X (всегда — синий, read-only)
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x6a9fb5), 0);
            // row3: НОЖКА ММ — редактируемый (оранжевый)
            lv_label_set_text(g_ui.row3_title,
                "\xD0\x9D\xD0\x9E\xD0\x96\xD0\x9A\xD0\x90 \xD0\x9C\xD0\x9C");  // НОЖКА ММ
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0xffaa44), 0);
            // row4: ЗАХОДОВ — read-only (синий)
            lv_label_set_text(g_ui.row4_title,
                "\xD0\x97\xD0\x90\xD0\xA5\xD0\x9E\xD0\x94\xD0\x9E\xD0\x92");  // ЗАХОДОВ
            lv_obj_set_style_text_color(g_ui.row4_title, lv_color_hex(0x6a9fb5), 0);
            if (g_ui.row4_box) lv_obj_clear_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            // thr_type_box: ШАР ММ (диаметр шара, редактируемый)
            if (g_ui.thr_type_box) {
                lv_obj_clear_flag(g_ui.thr_type_box, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_ui.thr_type_title,
                    "\xD0\xA8\xD0\x90\xD0\xA0 \xD0\x9C\xD0\x9C");           // ШАР ММ
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

        case MODE_RESERVE:
            lv_label_set_text(g_ui.secondary_unit, "");
            lv_label_set_text(g_ui.row1_title, "");
            lv_obj_set_style_text_color(g_ui.row1_title, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row2_title, "");
            lv_obj_set_style_text_color(g_ui.row2_title, lv_color_hex(0x4a6a7a), 0);
            lv_label_set_text(g_ui.row3_title, "");
            lv_obj_set_style_text_color(g_ui.row3_title, lv_color_hex(0x4a6a7a), 0);
            if (g_ui.row4_box) lv_obj_add_flag(g_ui.row4_box, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_label_set_text(g_ui.secondary_unit, "RPM");
            lv_label_set_text(g_ui.row1_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF Y");  // ПОЗИЦИЯ Z
            lv_label_set_text(g_ui.row2_title,
                "\xD0\x9F\xD0\x9E\xD0\x97\xD0\x98\xD0\xA6\xD0\x98\xD0\xAF X");  // ПОЗИЦИЯ X
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

    // Для MODE_FEED — открыть roller вместо простой подсветки
    if (s_last_mode == MODE_FEED) {
        open_feed_roller(g_edit_param.local_val);
        Serial.println("Edit mode: ON (feed roller)");
        return;
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
        lv_obj_set_style_border_width(g_ui.primary_val, 2, 0);
        lv_obj_set_style_border_color(g_ui.primary_val, lv_color_hex(0xffcc00), 0);
        lv_obj_set_style_pad_hor(g_ui.primary_val, 8, 0);
    }

    Serial.println("Edit mode: ON  (UP=KEY:UP, OK=PARAM_OK, DN=KEY:DN)");
}

static void exit_edit_mode()
{
    if (!g_ui.primary_val) return;

    // Закрыть roller без подтверждения если он ещё открыт (например при auto-exit)
    if (g_feed_roller.active) close_feed_roller(false);

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
// Барабан выбора подачи — реализация (кастомные метки + swipe gesture)
// ============================================================================

static void feed_roller_update_labels() {
    const int32_t STEP = 5;
    const lv_color_t colors[] = {
        lv_color_hex(0x283848),  // val-2: очень тёмный
        lv_color_hex(0x607880),  // val-1: серый
        lv_color_hex(0x00d4ff),  // текущий: ярко-голубой
        lv_color_hex(0x607880),  // val+1: серый
        lv_color_hex(0x283848),  // val+2: очень тёмный
    };
    for (int i = 0; i < 5; i++) {
        if (!g_feed_roller.labels[i]) continue;
        int32_t v = g_feed_roller.pending_val + (i - 2) * STEP;
        v = constrain(v, 5, 2500);
        char buf[10];
        snprintf(buf, sizeof(buf), "%d.%02d", (int)(v / 100), (int)(v % 100));
        lv_label_set_text(g_feed_roller.labels[i], buf);
        lv_obj_set_style_text_color(g_feed_roller.labels[i], colors[i], 0);
    }
    g_edit_param.local_val = g_feed_roller.pending_val;
    g_edit_param.last_ms   = millis();
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld.%02ld",
             (long)(g_feed_roller.pending_val / 100), (long)(g_feed_roller.pending_val % 100));
    if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, buf);
    if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
}

static void close_feed_roller(bool confirm) {
    if (!g_feed_roller.active) return;
    g_feed_roller.active = false;
    if (g_feed_roller.backdrop) { lv_obj_del(g_feed_roller.backdrop); g_feed_roller.backdrop = nullptr; }
    if (g_feed_roller.panel)    { lv_obj_del(g_feed_roller.panel);    g_feed_roller.panel    = nullptr; }
    memset(g_feed_roller.labels, 0, sizeof(g_feed_roller.labels));

    if (confirm) {
        g_edit_param.local_val = g_feed_roller.pending_val;
        char fs[24]; snprintf(fs, sizeof(fs), "FEED:%d", (int)g_feed_roller.pending_val);
        uart_protocol.sendButtonPress(fs);
        Serial.printf("Feed roller: confirmed FEED=%d\n", (int)g_feed_roller.pending_val);
    } else {
        // Отмена — восстановить исходное значение на дисплее
        g_edit_param.local_val = g_feed_roller.entry_val;
        char buf[16];
        snprintf(buf, sizeof(buf), "%ld.%02ld",
                 (long)(g_feed_roller.entry_val / 100), (long)(g_feed_roller.entry_val % 100));
        if (g_ui.primary_val)      lv_label_set_text(g_ui.primary_val, buf);
        if (g_ui.primary_val_glow) lv_label_set_text(g_ui.primary_val_glow, buf);
        Serial.printf("Feed roller: cancelled, restored to %d\n", (int)g_feed_roller.entry_val);
    }
}

static void open_feed_roller(int32_t cur_val) {
    if (g_feed_roller.active) return;

    lv_obj_t* scr = lv_scr_act();

    // Полупрозрачный фон — только левая область (правые кнопки x=418..474 остаются доступными)
    // CLICKABLE: тап мимо панели — подтверждение если значение изменено, иначе отмена
    g_feed_roller.backdrop = lv_obj_create(scr);
    lv_obj_set_size(g_feed_roller.backdrop, 416, 272);
    lv_obj_set_pos(g_feed_roller.backdrop, 0, 0);
    lv_obj_set_style_bg_color(g_feed_roller.backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_feed_roller.backdrop, 140, 0);
    lv_obj_set_style_border_width(g_feed_roller.backdrop, 0, 0);
    lv_obj_clear_flag(g_feed_roller.backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_feed_roller.backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_feed_roller.backdrop, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        bool changed = (g_feed_roller.pending_val != g_feed_roller.entry_val);
        close_feed_roller(changed);   // confirm если скроллили, иначе cancel
        exit_edit_mode();
    }, LV_EVENT_ALL, nullptr);

    // Центральная панель барабана — 30% крупнее (200→260)
    const int PW = 260, PH = 260;
    const int PX = (416 - PW) / 2, PY = (272 - PH) / 2;
    g_feed_roller.panel = lv_obj_create(scr);
    lv_obj_set_size(g_feed_roller.panel, PW, PH);
    lv_obj_set_pos(g_feed_roller.panel, PX, PY);
    lv_obj_set_style_bg_color(g_feed_roller.panel, lv_color_hex(0x0d1e2e), 0);
    lv_obj_set_style_bg_opa(g_feed_roller.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_feed_roller.panel, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(g_feed_roller.panel, 2, 0);
    lv_obj_set_style_radius(g_feed_roller.panel, 8, 0);
    lv_obj_set_style_pad_all(g_feed_roller.panel, 0, 0);
    lv_obj_clear_flag(g_feed_roller.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_feed_roller.panel, LV_OBJ_FLAG_CLICKABLE);

    // Заголовок "Подача мм/об"
    lv_obj_t* title = lv_label_create(g_feed_roller.panel);
    lv_obj_set_style_text_font(title, &font_tahoma_bold_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_label_set_text(title,
        "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0");  // Подача
    lv_obj_set_pos(title, 0, 8);
    lv_obj_set_width(title, PW);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    // 5 строк барабана: ROW_H=40, ROWS_Y=40
    // Выделение центральной строки (i=2): точно на сетке строк
    const int ROW_H = 40, ROWS_Y = 40;
    const int OUTER_FONT_H = 28;  // реальная высота font_tahoma_bold_28
    lv_obj_t* sel_rect = lv_obj_create(g_feed_roller.panel);
    lv_obj_set_size(sel_rect, PW - 8, ROW_H + 2);
    lv_obj_set_pos(sel_rect, 4, ROWS_Y + 2 * ROW_H - 1);
    lv_obj_set_style_bg_color(sel_rect, lv_color_hex(0x003a5a), 0);
    lv_obj_set_style_bg_opa(sel_rect, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sel_rect, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(sel_rect, 1, 0);
    lv_obj_set_style_radius(sel_rect, 4, 0);
    lv_obj_clear_flag(sel_rect, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // 5 строк барабана (сверху вниз: val-2, val-1, CURRENT, val+1, val+2)
    // Внешние строки: tahoma_bold_28, центральная: tahoma_bold_40
    const lv_font_t* row_fonts[] = {
        &font_tahoma_bold_28, &font_tahoma_bold_28,
        &font_tahoma_bold_40,
        &font_tahoma_bold_28, &font_tahoma_bold_28,
    };
    for (int i = 0; i < 5; i++) {
        g_feed_roller.labels[i] = lv_label_create(g_feed_roller.panel);
        lv_obj_set_style_text_font(g_feed_roller.labels[i], row_fonts[i], 0);
        // Центральная строка (i=2) — шрифт 40px заполняет ROW_H, без смещения
        // Внешние строки (28px) — центрируем вертикально внутри слота ROW_H
        int label_y = ROWS_Y + i * ROW_H;
        if (i != 2) label_y += (ROW_H - OUTER_FONT_H) / 2;
        lv_obj_set_pos(g_feed_roller.labels[i], 0, label_y);
        lv_obj_set_width(g_feed_roller.labels[i], PW);
        lv_obj_set_style_text_align(g_feed_roller.labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(g_feed_roller.labels[i], "0.00");
        lv_obj_clear_flag(g_feed_roller.labels[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Swipe gesture: PRESSING на панели → прокрутка без UART
    lv_obj_add_event_cb(g_feed_roller.panel, [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSING) {
            lv_indev_t* indev = lv_indev_get_act();
            if (!indev) return;
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            const int STEP_PX = 15;
            g_feed_roller.touch_accum -= vect.y; // вверх (vect.y<0) = больше значение
            while (g_feed_roller.touch_accum >= STEP_PX) {
                g_feed_roller.touch_accum -= STEP_PX;
                g_feed_roller.pending_val = constrain(g_feed_roller.pending_val + 5, 5, 2500);
                feed_roller_update_labels();
            }
            while (g_feed_roller.touch_accum <= -STEP_PX) {
                g_feed_roller.touch_accum += STEP_PX;
                g_feed_roller.pending_val = constrain(g_feed_roller.pending_val - 5, 5, 2500);
                feed_roller_update_labels();
            }
        } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_RELEASED) {
            g_feed_roller.touch_accum = 0;
        }
    }, LV_EVENT_ALL, nullptr);

    // Начальное значение
    int32_t v = constrain(cur_val, 5, 2500);
    v = ((v + 2) / 5) * 5;
    if (v < 5) v = 5;
    g_feed_roller.entry_val   = v;
    g_feed_roller.pending_val = v;
    g_feed_roller.touch_accum = 0;

    g_feed_roller.active = true;
    feed_roller_update_labels();
    Serial.printf("Feed roller: OPEN at %d.%02d mm/ob\n", (int)(v / 100), (int)(v % 100));
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

    // Войти в edit mode (если не активен)
    if (!g_edit_param.active) enter_edit_mode();

    g_edit_param.local_step = first_idx;
    g_edit_param.last_ms    = millis();

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
    lv_obj_clear_flag(g_ui.alert_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ui.alert_box);
    g_alert.active  = true;
    g_alert.show_ms = millis();
    // Кружок ⚠ → оранжевый
    if (g_ui.warn_bg) {
        lv_obj_set_style_bg_color(g_ui.warn_bg, lv_color_hex(0xff8800), 0);
        lv_obj_set_style_shadow_width(g_ui.warn_bg, 8, 0);
        lv_obj_set_style_shadow_color(g_ui.warn_bg, lv_color_hex(0xff8800), 0);
    }
    Serial.printf("Alert: %s\n", message);
}

static void dismiss_alert()
{
    if (!g_ui.alert_box) return;
    g_alert.active = false;
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

    if (sm == 1) {
        // SM=1: индикация пропадает
        lv_label_set_text(g_ui.sm_row6, "--");
        lv_obj_set_style_text_color(g_ui.sm_row6, lv_color_hex(0x2a3a4a), 0);
    } else if (is_vvod) {
        // ВВОД * СБРОС ОСЕЙ — жёлтый
        lv_label_set_text(g_ui.sm_row6,
            "\xD0\x92\xD0\x92\xD0\x9E\xD0\x94 *\n"
            "\xD0\xA1\xD0\x91\xD0\xA0\xD0\x9E\xD0\xA1\n"
            "\xD0\x9E\xD0\xA1\xD0\x95\xD0\x99");   // ВВОД * / СБРОС / ОСЕЙ
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

static void update_ui_values(const LatheData& data)
{
    if (!g_ui.primary_val) return;  // UI не инициализирован

    char buf[32];

    // ── Смена режима: обновить подписи ───────────────────────────────────────
    if (data.mode != s_last_mode) {
        s_last_mode = data.mode;
        s_last_select_menu = data.select_menu;
        if (g_alert.active) dismiss_alert();  // Сбросить алерт при смене режима
        apply_mode_layout(data.mode);
        // При смене режима применить SM-подэкран если нужно
        if (data.select_menu == 2) {
            if (data.mode == MODE_AFEED)  apply_afeed_sm2_layout();
            else if (data.mode == MODE_FEED)   apply_feed_sm2_layout();
            else if (data.mode == MODE_THREAD) apply_thread_sm2_layout();
            else if (data.mode == MODE_CONE_L || data.mode == MODE_CONE_R) apply_cone_sm2_layout();
            else if (data.mode == MODE_SPHERE) apply_sphere_sm2_layout();
        } else if (data.select_menu == 3) {
            if (data.mode == MODE_FEED) apply_feed_sm3_layout();
            else if (data.mode != MODE_DIVIDER) apply_common_sm3_layout();
        }
    } else if (data.select_menu != s_last_select_menu) {
        s_last_select_menu = data.select_menu;
        // SM изменился внутри текущего режима
        if (data.select_menu == 1) {
            apply_mode_layout(data.mode);  // Восстановить нормальный layout
        } else if (data.select_menu == 2) {
            if (data.mode == MODE_AFEED)  apply_afeed_sm2_layout();
            else if (data.mode == MODE_FEED)   apply_feed_sm2_layout();
            else if (data.mode == MODE_THREAD) apply_thread_sm2_layout();
            else if (data.mode == MODE_CONE_L || data.mode == MODE_CONE_R) apply_cone_sm2_layout();
            else if (data.mode == MODE_SPHERE) apply_sphere_sm2_layout();
            else apply_mode_layout(data.mode);
        } else if (data.select_menu == 3) {
            if (data.mode == MODE_FEED) apply_feed_sm3_layout();
            else if (data.mode != MODE_DIVIDER) apply_common_sm3_layout();
            else apply_mode_layout(data.mode);
        }
    }

    // ── Statusbar: режим, подрежим и лимиты ─────────────────────────────────
    {
        int mi = (int)data.mode;
        if (mi >= 1 && mi <= 8) lv_label_set_text(g_ui.mode_lbl, MODE_STRS[mi]);

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
    if (g_ui.row4_val) {
        switch (data.mode) {
            case MODE_THREAD:
                // M3: ЦИКЛОВ — total/pass_nr (как M1/M2), для Man — только total
                if (data.thread_cycles > 0) {
                    if (data.submode != SUBMODE_MANUAL) {
                        snprintf(buf, sizeof(buf), "#00ff88 %d#/#ffcc00 %d#",
                                 (int)data.thread_cycles, (int)data.pass_nr);
                    } else {
                        snprintf(buf, sizeof(buf), "%d", (int)data.thread_cycles);
                    }
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                lv_label_set_text(g_ui.row4_val, buf);
                break;
            case MODE_FEED:
            case MODE_AFEED:
            case MODE_CONE_L:
            case MODE_CONE_R:
                if (data.pass_total > 0)
                    snprintf(buf, sizeof(buf), "#00ff88 %d#/#ffcc00 %d#",
                             data.pass_total, data.pass_nr);
                else
                    snprintf(buf, sizeof(buf), "--/--");
                lv_label_set_text(g_ui.row4_val, buf);
                break;
            case MODE_SPHERE:
                // M6: ЗАХОДОВ — total/pass_nr (как M1/M2); total = Pass_Total_Sphr + 2
                if (data.pass_total_sphr > 0) {
                    int sphr_total = (int)data.pass_total_sphr + 2;
                    snprintf(buf, sizeof(buf), "#00ff88 %d#/#ffcc00 %d#",
                             sphr_total, (int)data.pass_nr);
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                lv_label_set_text(g_ui.row4_val, buf);
                break;
            default:
                break;
        }
    }

    // ── RPM bar ──────────────────────────────────────────────────────────────
    lv_bar_set_value(g_ui.rpm_bar, data.rpm, LV_ANIM_OFF);

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
        // SM=3 Ввод/Сброс Осей (M2/M3/M4/M5/M6): row1=ДИАМЕТР, row2=ОСЬ X
        DisplayFormatter::formatPosition(buf, data.diam_x);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
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
        // M1 SM=2: ДИАМЕТР + ОСЬ X
        DisplayFormatter::formatPosition(buf, data.diam_x);
        lv_label_set_text(g_ui.row1_val, buf);
        lv_obj_set_style_text_color(g_ui.row1_val, lv_color_hex(0xe0e0e0), 0);
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
                // M1 SM=2: ОСЬ Z
                DisplayFormatter::formatPosition(buf, data.pos_z);
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val,
                    data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
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
                // M2 SM=3: ОСЬ Z
                DisplayFormatter::formatPosition(buf, data.pos_z);
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val,
                    data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
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
                // M3 SM=3: ОСЬ Z
                DisplayFormatter::formatPosition(buf, data.pos_z);
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val,
                    data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
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
                // M6 SM=3: ОСЬ Z
                DisplayFormatter::formatPosition(buf, data.pos_z);
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val,
                    data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
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
            // Алерт "Режим невозможен!" при INTERNAL подрежиме
            if (data.submode == SUBMODE_INTERNAL && !g_alert.active)
                show_alert("\xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC "
                           "\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB7\xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD!");
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
                // M4/M5 SM=3: ОСЬ Z
                DisplayFormatter::formatPosition(buf, data.pos_z);
                lv_label_set_text(g_ui.row3_val, buf);
                lv_obj_set_style_text_color(g_ui.row3_val,
                    data.pos_z >= 0 ? lv_color_hex(0x00ff88) : lv_color_hex(0xff5555), 0);
            } else if (data.select_menu == 2) {
                // M4/M5 SM=2: "---"
                lv_label_set_text(g_ui.row3_val, "---");
                lv_obj_set_style_text_color(g_ui.row3_val, lv_color_hex(0x555555), 0);
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
    update_ui_values(data);
}

// ============================================================================
// Setup
// ============================================================================

void setup()
{
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

    // Подключение к WiFi роутеру
    Serial.printf("Connecting to WiFi: %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);  // КРИТИЧНО: отключаем modem-sleep → без этого пинг 100ms+!
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Screenshot URL: http://%s/screenshot\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection FAILED - screenshot server disabled");
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

    // Запускаем ESP-IDF HTTP сервер
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.server_port      = 80;
    httpd_cfg.stack_size       = 8192;
    httpd_cfg.send_wait_timeout = 60;  // 60 секунд (по умолчанию 5 — слишком мало)
    httpd_handle_t httpd_server = nullptr;
    if (httpd_start(&httpd_server, &httpd_cfg) == ESP_OK) {
        httpd_uri_t uri = {
            .uri      = "/screenshot",
            .method   = HTTP_GET,
            .handler  = screenshot_handler,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(httpd_server, &uri);
        Serial.printf("Screenshot server: http://%s/screenshot\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("httpd_start FAILED");
    }

#else
    // === ESP32-2432S024 Initialization ===
    Serial.println("Initializing ESP32-2432S024 (ILI9341)...");

    tft.begin();
    tft.setRotation(SCREEN_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // Backlight ON
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

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 32);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("LVGL initialized OK");

    // === UART Initialization ===
    uart_protocol.begin(Serial2, UART_BAUD_RATE);
    uart_protocol.setDataUpdateCallback(onDataUpdate);
    uart_protocol.setAlertCallback(show_alert);
    uart_protocol.setAlertDismissCallback(dismiss_alert);

    // === Load saved preferences ===
    {
        Preferences prefs;
        prefs.begin("els_disp", true);
        g_layout = prefs.getUChar("layout", 0);
        prefs.end();
    }

    // === Create UI ===
    create_dark_pro_ui();

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

    // Авто-выход из режима редактирования через 5 секунд бездействия
    if (g_edit_param.active && (millis() - g_edit_param.last_ms > 5000)) {
        exit_edit_mode();
    }
    // Авто-выход из sub-edit (редактирование строки правой панели) через 5 секунд
    if (g_sub_edit.active && (millis() - g_sub_edit.last_ms > 5000)) {
        exit_sub_edit();
    }

    // Авто-скрытие алерта через 5 секунд
    if (g_alert.active && (millis() - g_alert.show_ms > 5000)) {
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
            Serial.printf("[DBG] loops/2s=%lu lv_max=%lums\n",
                          (unsigned long)dbg_loops, (unsigned long)dbg_max);
            dbg_last = t0; dbg_max = 0; dbg_loops = 0;
        }
    }

#ifdef DISPLAY_JC4827W543
    // Проверяем HTTP screenshot сервер
    handle_screenshot_server();

    // USB CDC Screenshot state machine
    // Протокол: PC шлёт 'S' → ESP32 отвечает {0xDE,0xAD,0xBE,0xEF} + 4-byte LE size + JPEG
    {
        static uint8_t  usb_ss_state = 0;
        static uint32_t usb_ss_sent  = 0;

        if (usb_ss_state == 0) {
            // Ждём байт 'S' от PC (закрой Serial Monitor перед использованием!)
            if (Serial.available() >= 1 && Serial.peek() == 'S') {
                Serial.read();
                if (sem_requested) {
                    xSemaphoreGive(sem_requested);
                    usb_ss_state = 1;
                }
            } else if (Serial.available() >= 1) {
                Serial.read();  // выбрасываем мусор
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
                    Serial.write(hdr, 8);
                    usb_ss_sent = 0;
                    usb_ss_state = 2;
                } else {
                    usb_ss_state = 0;  // ошибка захвата
                }
            }
        } else if (usb_ss_state == 2) {
            // Отправляем JPEG кусками по 512 байт за итерацию
            if (usb_ss_sent < jpeg_size) {
                uint32_t chunk = jpeg_size - usb_ss_sent;
                if (chunk > 512) chunk = 512;
                Serial.write(jpeg_buf + usb_ss_sent, (size_t)chunk);
                usb_ss_sent += chunk;
            }
            if (usb_ss_sent >= jpeg_size) {
                usb_ss_state = 0;
            }
        }
    }
#endif

    delay(5);
}
