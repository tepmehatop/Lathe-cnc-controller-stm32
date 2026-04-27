/**
 * @file ui_common.h
 * @brief Общие функции и классы для UI
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <lvgl.h>
#include "uart_protocol.h"
#include "display_config.h"

// ============================================================================
// Базовый класс для всех UI дизайнов
// ============================================================================

class UIDesign {
public:
    virtual ~UIDesign() {}

    // Инициализация UI (создание объектов LVGL)
    virtual void init() = 0;

    // Обновление данных на экране
    virtual void update(const LatheData& data) = 0;

    // Очистка/удаление UI
    virtual void deinit() {}

    // Получить имя дизайна
    virtual const char* getName() = 0;
};

// ============================================================================
// Дизайн #3: Industrial HMI Style
// ============================================================================

class UIDesign3_Industrial : public UIDesign {
public:
    UIDesign3_Industrial();
    virtual ~UIDesign3_Industrial();

    void init() override;
    void update(const LatheData& data) override;
    void deinit() override;
    const char* getName() override { return "Industrial HMI"; }

private:
    lv_obj_t* screen_;
    lv_obj_t* status_bar_;
    lv_obj_t* mode_label_;
    lv_obj_t* submode_label_;
    lv_obj_t* rpm_label_;

    lv_obj_t* gauge1_;  // Главный параметр
    lv_obj_t* gauge2_;  // Проходов
    lv_obj_t* gauge3_;  // Заходов

    lv_obj_t* gauge1_value_;
    lv_obj_t* gauge1_label_;
    lv_obj_t* gauge2_value_;
    lv_obj_t* gauge3_value_;

    lv_obj_t* axis_z_;
    lv_obj_t* axis_x_;
    lv_obj_t* diameter_;
    lv_obj_t* extra_;

    lv_obj_t* status_indicator_;

    void createStatusBar();
    void createGauges();
    void createAxisDisplay();
    void updateGaugeValue(lv_obj_t* gauge_value, const char* text);
};

// ============================================================================
// Дизайн #8: Dark Theme Pro
// ============================================================================

class UIDesign8_DarkPro : public UIDesign {
public:
    UIDesign8_DarkPro();
    virtual ~UIDesign8_DarkPro();

    void init() override;
    void update(const LatheData& data) override;
    void deinit() override;
    const char* getName() override { return "Dark Theme Pro"; }

private:
    lv_obj_t* screen_;
    lv_obj_t* toolbar_;
    lv_obj_t* mode_toolbar_label_;
    lv_obj_t* submode_toolbar_label_;
    lv_obj_t* rpm_toolbar_label_;

    lv_obj_t* left_panel_;
    lv_obj_t* right_panel_;

    lv_obj_t* main_value_label_;
    lv_obj_t* main_title_label_;

    lv_obj_t* passes_row_;
    lv_obj_t* starts_row_;
    lv_obj_t* pitch_row_;

    lv_obj_t* pos_z_row_;
    lv_obj_t* pos_x_row_;
    lv_obj_t* diameter_row_;
    lv_obj_t* limits_row1_;
    lv_obj_t* limits_row2_;

    void createToolbar();
    void createLeftPanel();
    void createRightPanel();
    void createMetricRow(lv_obj_t* parent, const char* label, lv_obj_t** row_obj);
    void updateMetricRow(lv_obj_t* row, const char* value);
};

// ============================================================================
// UI Manager - управление дизайнами
// ============================================================================

class UIManager {
public:
    UIManager();
    ~UIManager();

    // Инициализация (создать все доступные дизайны)
    void init();

    // Переключить на другой дизайн
    void switchDesign(uint8_t design_number);

    // Обновить текущий дизайн
    void update(const LatheData& data);

    // Получить текущий номер дизайна
    uint8_t getCurrentDesign() const { return current_design_; }

private:
    UIDesign* designs_[2];  // Массив дизайнов
    uint8_t current_design_;
};

// ============================================================================
// Цветовая палитра
// ============================================================================

namespace Colors {
    // Industrial HMI (Design #3)
    namespace Industrial {
        constexpr lv_color_t BG_DARK = LV_COLOR_MAKE(0x2c, 0x3e, 0x50);
        constexpr lv_color_t BG_PANEL = LV_COLOR_MAKE(0x34, 0x49, 0x5e);
        constexpr lv_color_t BORDER = LV_COLOR_MAKE(0x34, 0x49, 0x5e);
        constexpr lv_color_t STATUS_GREEN = LV_COLOR_MAKE(0x27, 0xae, 0x60);
        constexpr lv_color_t VALUE_BLUE = LV_COLOR_MAKE(0x34, 0x98, 0xdb);
        constexpr lv_color_t TEXT_GRAY = LV_COLOR_MAKE(0x95, 0xa5, 0xa6);
        constexpr lv_color_t TEXT_WHITE = LV_COLOR_MAKE(0xec, 0xf0, 0xf1);
        constexpr lv_color_t AXIS_RED = LV_COLOR_MAKE(0xe7, 0x4c, 0x3c);
        constexpr lv_color_t AXIS_BLUE = LV_COLOR_MAKE(0x34, 0x98, 0xdb);
        constexpr lv_color_t AXIS_GREEN = LV_COLOR_MAKE(0x27, 0xae, 0x60);
        constexpr lv_color_t AXIS_ORANGE = LV_COLOR_MAKE(0xf3, 0x9c, 0x12);
    }

    // Dark Theme Pro (Design #8)
    namespace DarkPro {
        constexpr lv_color_t BG_MAIN = LV_COLOR_MAKE(0x0d, 0x11, 0x17);
        constexpr lv_color_t BG_PANEL = LV_COLOR_MAKE(0x16, 0x1b, 0x22);
        constexpr lv_color_t BORDER = LV_COLOR_MAKE(0x30, 0x36, 0x3d);
        constexpr lv_color_t ACCENT_BLUE = LV_COLOR_MAKE(0x58, 0xa6, 0xff);
        constexpr lv_color_t TEXT_PRIMARY = LV_COLOR_MAKE(0xc9, 0xd1, 0xd9);
        constexpr lv_color_t TEXT_SECONDARY = LV_COLOR_MAKE(0x8b, 0x94, 0x9e);
        constexpr lv_color_t SEPARATOR = LV_COLOR_MAKE(0x21, 0x26, 0x2d);
    }
}

#endif // UI_COMMON_H
