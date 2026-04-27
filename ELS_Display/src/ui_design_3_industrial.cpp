/**
 * @file ui_design_3_industrial.cpp
 * @brief Дизайн #3: Industrial HMI Style
 */

#include "ui_common.h"

UIDesign3_Industrial::UIDesign3_Industrial()
    : screen_(nullptr)
    , status_bar_(nullptr)
    , mode_label_(nullptr)
    , submode_label_(nullptr)
    , rpm_label_(nullptr)
    , gauge1_(nullptr)
    , gauge2_(nullptr)
    , gauge3_(nullptr)
    , gauge1_value_(nullptr)
    , gauge1_label_(nullptr)
    , gauge2_value_(nullptr)
    , gauge3_value_(nullptr)
    , axis_z_(nullptr)
    , axis_x_(nullptr)
    , diameter_(nullptr)
    , extra_(nullptr)
    , status_indicator_(nullptr)
{
}

UIDesign3_Industrial::~UIDesign3_Industrial()
{
    deinit();
}

void UIDesign3_Industrial::init()
{
    // Создать экран
    screen_ = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_, Colors::Industrial::BG_DARK, 0);

    createStatusBar();
    createGauges();
    createAxisDisplay();

    lv_scr_load(screen_);
}

void UIDesign3_Industrial::createStatusBar()
{
    // Status bar (верхняя полоска)
    status_bar_ = lv_obj_create(screen_);
    lv_obj_set_size(status_bar_, SCREEN_WIDTH - 20, 35);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(status_bar_, Colors::Industrial::BG_PANEL, 0);
    lv_obj_set_style_border_color(status_bar_, Colors::Industrial::BORDER, 0);
    lv_obj_set_style_border_width(status_bar_, 2, 0);
    lv_obj_set_style_radius(status_bar_, 6, 0);
    lv_obj_set_style_pad_all(status_bar_, 8, 0);

    // Status indicator (пульсирующая точка)
    status_indicator_ = lv_obj_create(status_bar_);
    lv_obj_set_size(status_indicator_, 12, 12);
    lv_obj_align(status_indicator_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(status_indicator_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_indicator_, Colors::Industrial::STATUS_GREEN, 0);
    lv_obj_set_style_border_width(status_indicator_, 0, 0);

    // Mode label
    mode_label_ = lv_label_create(status_bar_);
    lv_obj_align(mode_label_, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_font(mode_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mode_label_, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(mode_label_, "M3 - РЕЗЬБА");

    // Submode label
    submode_label_ = lv_label_create(status_bar_);
    lv_obj_align(submode_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(submode_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(submode_label_, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(submode_label_, "S2 - Ручная");

    // RPM label
    rpm_label_ = lv_label_create(status_bar_);
    lv_obj_align(rpm_label_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(rpm_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rpm_label_, Colors::Industrial::VALUE_BLUE, 0);
    lv_label_set_text(rpm_label_, "RPM: 1500");
}

void UIDesign3_Industrial::createGauges()
{
    int gauge_width = 90;
    int gauge_height = 90;
    int start_x = 15;
    int start_y = 55;

    // Gauge 1 - Главный параметр (большой)
    gauge1_ = lv_obj_create(screen_);
    lv_obj_set_size(gauge1_, gauge_width, gauge_height);
    lv_obj_set_pos(gauge1_, start_x, start_y);
    lv_obj_set_style_bg_color(gauge1_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(gauge1_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(gauge1_, Colors::Industrial::VALUE_BLUE, 0);
    lv_obj_set_style_border_width(gauge1_, 3, 0);
    lv_obj_set_style_radius(gauge1_, 8, 0);

    gauge1_label_ = lv_label_create(gauge1_);
    lv_obj_align(gauge1_label_, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_font(gauge1_label_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(gauge1_label_, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge1_label_, "ШАГ РЕЗЬБЫ");

    gauge1_value_ = lv_label_create(gauge1_);
    lv_obj_align(gauge1_value_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(gauge1_value_, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(gauge1_value_, Colors::Industrial::VALUE_BLUE, 0);
    lv_label_set_text(gauge1_value_, "1.50");

    lv_obj_t* gauge1_unit = lv_label_create(gauge1_);
    lv_obj_align(gauge1_unit, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_font(gauge1_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gauge1_unit, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge1_unit, "мм");

    // Gauge 2 - Проходов
    gauge2_ = lv_obj_create(screen_);
    lv_obj_set_size(gauge2_, gauge_width, gauge_height);
    lv_obj_set_pos(gauge2_, start_x + gauge_width + 10, start_y);
    lv_obj_set_style_bg_color(gauge2_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(gauge2_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(gauge2_, Colors::Industrial::VALUE_BLUE, 0);
    lv_obj_set_style_border_width(gauge2_, 3, 0);
    lv_obj_set_style_radius(gauge2_, 8, 0);

    lv_obj_t* gauge2_label = lv_label_create(gauge2_);
    lv_obj_align(gauge2_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_font(gauge2_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(gauge2_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge2_label, "ПРОХОДОВ");

    gauge2_value_ = lv_label_create(gauge2_);
    lv_obj_align(gauge2_value_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(gauge2_value_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gauge2_value_, Colors::Industrial::VALUE_BLUE, 0);
    lv_label_set_text(gauge2_value_, "12");

    lv_obj_t* gauge2_unit = lv_label_create(gauge2_);
    lv_obj_align(gauge2_unit, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_font(gauge2_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gauge2_unit, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge2_unit, "циклов");

    // Gauge 3 - Заходов
    gauge3_ = lv_obj_create(screen_);
    lv_obj_set_size(gauge3_, gauge_width, gauge_height);
    lv_obj_set_pos(gauge3_, start_x + (gauge_width + 10) * 2, start_y);
    lv_obj_set_style_bg_color(gauge3_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(gauge3_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(gauge3_, Colors::Industrial::VALUE_BLUE, 0);
    lv_obj_set_style_border_width(gauge3_, 3, 0);
    lv_obj_set_style_radius(gauge3_, 8, 0);

    lv_obj_t* gauge3_label = lv_label_create(gauge3_);
    lv_obj_align(gauge3_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_font(gauge3_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(gauge3_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge3_label, "ЗАХОДОВ");

    gauge3_value_ = lv_label_create(gauge3_);
    lv_obj_align(gauge3_value_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(gauge3_value_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gauge3_value_, Colors::Industrial::VALUE_BLUE, 0);
    lv_label_set_text(gauge3_value_, "2");

    lv_obj_t* gauge3_unit = lv_label_create(gauge3_);
    lv_obj_align(gauge3_unit, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_font(gauge3_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gauge3_unit, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(gauge3_unit, "шт");
}

void UIDesign3_Industrial::createAxisDisplay()
{
    int box_width = 140;
    int box_height = 35;
    int start_y = 155;

    // Z axis
    axis_z_ = lv_obj_create(screen_);
    lv_obj_set_size(axis_z_, box_width, box_height);
    lv_obj_set_pos(axis_z_, 15, start_y);
    lv_obj_set_style_bg_color(axis_z_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(axis_z_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(axis_z_, Colors::Industrial::AXIS_RED, 0);
    lv_obj_set_style_border_width(axis_z_, 3, 0);
    lv_obj_set_style_border_side(axis_z_, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(axis_z_, 6, 0);
    lv_obj_set_style_pad_all(axis_z_, 8, 0);

    lv_obj_t* z_label = lv_label_create(axis_z_);
    lv_obj_align(z_label, LV_ALIGN_LEFT_MID, 0, -8);
    lv_obj_set_style_text_font(z_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(z_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(z_label, "ОСЬ Z");

    lv_obj_t* z_value = lv_label_create(axis_z_);
    lv_obj_align(z_value, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_set_style_text_font(z_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(z_value, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(z_value, "+123.45 мм");

    // X axis
    axis_x_ = lv_obj_create(screen_);
    lv_obj_set_size(axis_x_, box_width, box_height);
    lv_obj_set_pos(axis_x_, 165, start_y);
    lv_obj_set_style_bg_color(axis_x_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(axis_x_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(axis_x_, Colors::Industrial::AXIS_BLUE, 0);
    lv_obj_set_style_border_width(axis_x_, 3, 0);
    lv_obj_set_style_border_side(axis_x_, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(axis_x_, 6, 0);
    lv_obj_set_style_pad_all(axis_x_, 8, 0);

    lv_obj_t* x_label = lv_label_create(axis_x_);
    lv_obj_align(x_label, LV_ALIGN_LEFT_MID, 0, -8);
    lv_obj_set_style_text_font(x_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(x_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(x_label, "ОСЬ X");

    lv_obj_t* x_value = lv_label_create(axis_x_);
    lv_obj_align(x_value, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_set_style_text_font(x_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(x_value, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(x_value, "-67.89 мм");

    // Diameter
    diameter_ = lv_obj_create(screen_);
    lv_obj_set_size(diameter_, box_width, box_height);
    lv_obj_set_pos(diameter_, 15, start_y + box_height + 8);
    lv_obj_set_style_bg_color(diameter_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(diameter_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(diameter_, Colors::Industrial::AXIS_GREEN, 0);
    lv_obj_set_style_border_width(diameter_, 3, 0);
    lv_obj_set_style_border_side(diameter_, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(diameter_, 6, 0);
    lv_obj_set_style_pad_all(diameter_, 8, 0);

    lv_obj_t* d_label = lv_label_create(diameter_);
    lv_obj_align(d_label, LV_ALIGN_LEFT_MID, 0, -8);
    lv_obj_set_style_text_font(d_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(d_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(d_label, "ДИАМЕТР");

    lv_obj_t* d_value = lv_label_create(diameter_);
    lv_obj_align(d_value, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_set_style_text_font(d_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(d_value, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(d_value, "45.20 мм");

    // Extra (Ход)
    extra_ = lv_obj_create(screen_);
    lv_obj_set_size(extra_, box_width, box_height);
    lv_obj_set_pos(extra_, 165, start_y + box_height + 8);
    lv_obj_set_style_bg_color(extra_, LV_COLOR_MAKE(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(extra_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(extra_, Colors::Industrial::AXIS_ORANGE, 0);
    lv_obj_set_style_border_width(extra_, 3, 0);
    lv_obj_set_style_border_side(extra_, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(extra_, 6, 0);
    lv_obj_set_style_pad_all(extra_, 8, 0);

    lv_obj_t* e_label = lv_label_create(extra_);
    lv_obj_align(e_label, LV_ALIGN_LEFT_MID, 0, -8);
    lv_obj_set_style_text_font(e_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(e_label, Colors::Industrial::TEXT_GRAY, 0);
    lv_label_set_text(e_label, "ХОД");

    lv_obj_t* e_value = lv_label_create(extra_);
    lv_obj_align(e_value, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_set_style_text_font(e_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(e_value, Colors::Industrial::TEXT_WHITE, 0);
    lv_label_set_text(e_value, "3.00 мм");
}

void UIDesign3_Industrial::update(const LatheData& data)
{
    char buffer[32];

    // Обновить mode
    snprintf(buffer, sizeof(buffer), "%s - %s",
             DisplayFormatter::getModeShortName(data.mode),
             DisplayFormatter::getModeName(data.mode));
    lv_label_set_text(mode_label_, buffer);

    // Обновить submode
    snprintf(buffer, sizeof(buffer), "%s - %s",
             DisplayFormatter::getSubModeShortName(data.submode),
             DisplayFormatter::getSubModeName(data.submode));
    lv_label_set_text(submode_label_, buffer);

    // Обновить RPM
    snprintf(buffer, sizeof(buffer), "RPM: %d", data.rpm);
    lv_label_set_text(rpm_label_, buffer);

    // Обновить главное значение (зависит от режима)
    if (data.mode == MODE_THREAD) {
        DisplayFormatter::formatThread(buffer, data.thread_mm);
        lv_label_set_text(gauge1_value_, buffer);
        lv_label_set_text(gauge1_label_, "ШАГ РЕЗЬБЫ");
    }
    else if (data.mode == MODE_FEED) {
        DisplayFormatter::formatFeed(buffer, data.feed_mm);
        lv_label_set_text(gauge1_value_, buffer);
        lv_label_set_text(gauge1_label_, "ПОДАЧА");
    }

    // Обновить позиции
    // (Нужно найти дочерние label объекты и обновить их текст)
    // Это упрощенная версия, в полной нужно сохранять указатели на label'ы
}

void UIDesign3_Industrial::deinit()
{
    if (screen_) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }
}

void UIDesign3_Industrial::updateGaugeValue(lv_obj_t* gauge_value, const char* text)
{
    lv_label_set_text(gauge_value, text);
}
