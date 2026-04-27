/**
 * @file display_config.h
 * @brief Display configuration - легко меняется для разных дисплеев
 *
 * ТЕКУЩИЙ: ESP32-2432S024 (2.4" 240x320)
 * БУДУЩИЙ: JC4827W543 (4.3" 480x272)
 *
 * При смене дисплея нужно изменить только #define ниже
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

// ============================================================================
// ВЫБОР ДИСПЛЕЯ (автоматически из platformio.ini или вручную)
// ============================================================================

#ifdef JC4827W543_DISPLAY
    #define DISPLAY_JC4827W543
#else
    #define DISPLAY_ESP32_2432S024
#endif

// ============================================================================
// АВТОМАТИЧЕСКАЯ КОНФИГУРАЦИЯ
// ============================================================================

#ifdef DISPLAY_ESP32_2432S024
    // ESP32-2432S024: 2.4" 320x240 (ландшафтная ориентация)
    #define SCREEN_WIDTH  320
    #define SCREEN_HEIGHT 240
    #define SCREEN_ROTATION 1  // 0=портрет, 1=пейзаж, 2=портрет перевернутый, 3=пейзаж перевернутый

    #define DISPLAY_NAME "ESP32-2432S024"
    #define DISPLAY_SIZE "2.4\""

    // Коэффициенты масштабирования для адаптации UI
    #define SCALE_FACTOR_X 1.0f
    #define SCALE_FACTOR_Y 1.0f

#elif defined(DISPLAY_JC4827W543)
    // JC4827W543: 4.3" 480x272 (ландшафтная ориентация)
    #define SCREEN_WIDTH  480
    #define SCREEN_HEIGHT 272
    #define SCREEN_ROTATION 1  // Ландшафт

    #define DISPLAY_NAME "JC4827W543"
    #define DISPLAY_SIZE "4.3\""

    // Коэффициенты для масштабирования UI с 240x320 на 480x272
    #define SCALE_FACTOR_X 2.0f  // 480/240 = 2.0
    #define SCALE_FACTOR_Y 0.85f // 272/320 = 0.85

#else
    #error "No display selected! Define DISPLAY_ESP32_2432S024 or DISPLAY_JC4827W543"
#endif

// ============================================================================
// МАКРОСЫ ДЛЯ МАСШТАБИРОВАНИЯ (автоматически адаптируют размеры)
// ============================================================================

// Масштабировать координату X
#define SCALE_X(x) ((int)((x) * SCALE_FACTOR_X))

// Масштабировать координату Y
#define SCALE_Y(y) ((int)((y) * SCALE_FACTOR_Y))

// Масштабировать ширину
#define SCALE_W(w) ((int)((w) * SCALE_FACTOR_X))

// Масштабировать высоту
#define SCALE_H(h) ((int)((h) * SCALE_FACTOR_Y))

// Масштабировать размер шрифта
#define SCALE_FONT(size) ((int)((size) * ((SCALE_FACTOR_X + SCALE_FACTOR_Y) / 2.0f)))

// ============================================================================
// ЗОНЫ ЭКРАНА (базовые для 240x320, автоматически масштабируются)
// ============================================================================

// Шапка (заголовок)
#define HEADER_HEIGHT SCALE_H(30)
#define HEADER_Y 0

// Основная область (главные данные)
#define MAIN_AREA_Y SCALE_H(30)
#define MAIN_AREA_HEIGHT SCALE_H(140)

// Область информации (вторичные данные)
#define INFO_AREA_Y SCALE_H(170)
#define INFO_AREA_HEIGHT SCALE_H(80)

// Футер (кнопки управления)
#define FOOTER_HEIGHT SCALE_H(70)
#define FOOTER_Y (SCREEN_HEIGHT - FOOTER_HEIGHT)

// Ширина кнопок
#define BUTTON_WIDTH SCALE_W(70)
#define BUTTON_HEIGHT SCALE_H(60)
#define BUTTON_PADDING SCALE_W(5)

// ============================================================================
// РАЗМЕРЫ ШРИФТОВ (автоматически масштабируются)
// ============================================================================

#define FONT_SIZE_HUGE   SCALE_FONT(48)  // Главное значение
#define FONT_SIZE_LARGE  SCALE_FONT(32)  // Важные данные
#define FONT_SIZE_MEDIUM SCALE_FONT(24)  // Обычный текст
#define FONT_SIZE_SMALL  SCALE_FONT(16)  // Мелкий текст
#define FONT_SIZE_TINY   SCALE_FONT(12)  // Очень мелкий

// ============================================================================
// ЦВЕТОВЫЕ СХЕМЫ (не зависят от размера дисплея)
// ============================================================================

// Определяем цветовые схемы для разных дизайнов
typedef enum {
    COLOR_SCHEME_DARK_GREEN = 0,      // Темный фон, зеленые цифры (классика CNC)
    COLOR_SCHEME_DARK_BLUE,           // Темно-синий фон, белые/желтые цифры
    COLOR_SCHEME_LIGHT,               // Светлый фон, темные цифры
    COLOR_SCHEME_HIGH_CONTRAST,       // Черный фон, белые цифры
    COLOR_SCHEME_AUTO_DARK,           // Темный фон, синие акценты
    COLOR_SCHEME_RETRO_ORANGE,        // Черный фон, оранжевые цифры
    COLOR_SCHEME_COUNT                // Количество схем
} ColorScheme;

// Текущая цветовая схема (по умолчанию)
extern ColorScheme current_color_scheme;

// ============================================================================
// UART КОНФИГУРАЦИЯ (связь с Arduino Mega)
// ============================================================================

// 57600 — надёжный потолок SoftwareSerial на Arduino Mega (16МГц)
// ESP32 аппаратный UART поддерживает любую скорость
#define UART_BAUD_RATE  57600
#define UART_BUFFER_SIZE 256

#ifdef DISPLAY_JC4827W543
    // ESP32-S3 UART пины через P1 разъём платы JC4827W543 — НЕ МЕНЯЮТСЯ
    // P1 TX = GPIO43 (UART0 TX по умолчанию, выходит на P1 разъём)
    // P1 RX = GPIO44 (UART0 RX по умолчанию, выходит на P1 разъём)
    // Подключение: Arduino Pin51(SS_TX) → P1_RX(GPIO44), Arduino Pin52(SS_RX) → P1_TX(GPIO43)
    // (Пины 18/19 освобождены → возвращены ручному энкодеру Hand Encoder 100Lines)
    #define UART_RX_PIN     44  // P1 RX = GPIO44
    #define UART_TX_PIN     43  // P1 TX = GPIO43
#else
    // ESP32 UART2 пины
    #define UART_RX_PIN     16  // RX2 на ESP32
    #define UART_TX_PIN     17  // TX2 на ESP32
#endif

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ МАКРОСЫ
// ============================================================================

// Центрировать по X
#define CENTER_X(width) ((SCREEN_WIDTH - (width)) / 2)

// Центрировать по Y
#define CENTER_Y(height) ((SCREEN_HEIGHT - (height)) / 2)

// Проверка попадания точки в прямоугольник
#define POINT_IN_RECT(px, py, rx, ry, rw, rh) \
    ((px) >= (rx) && (px) < ((rx) + (rw)) && \
     (py) >= (ry) && (py) < ((ry) + (rh)))

#endif // DISPLAY_CONFIG_H
