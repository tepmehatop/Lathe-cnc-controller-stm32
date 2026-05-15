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
// Управление сборкой: установить нужный флаг в platformio.ini build_flags:
//   -DJC4827W543_DISPLAY=1   → 4.3" ESP32-S3 (env:jc4827w543)
//   -DJC8012P4A1C_DISPLAY=1  → 10.1" ESP32-P4 (env:jc8012p4a1c)
// ============================================================================

#ifdef JC8012P4A1C_DISPLAY
    #define DISPLAY_JC8012P4A1C
#elif defined(JC4827W543_DISPLAY)
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
    #define SCREEN_ROTATION 1

    #define DISPLAY_NAME "ESP32-2432S024"
    #define DISPLAY_SIZE "2.4\""

    #define SCALE_FACTOR_X 1.0f
    #define SCALE_FACTOR_Y 1.0f

#elif defined(DISPLAY_JC4827W543)
    // JC4827W543: 4.3" 480x272 (ландшафтная ориентация)
    #define SCREEN_WIDTH  480
    #define SCREEN_HEIGHT 272
    #define SCREEN_ROTATION 1

    #define DISPLAY_NAME "JC4827W543"
    #define DISPLAY_SIZE "4.3\""

    // Масштабирование UI с базы 240x320 → 480x272
    #define SCALE_FACTOR_X 2.0f   // 480/240 = 2.0
    #define SCALE_FACTOR_Y 0.85f  // 272/320 = 0.85

#elif defined(DISPLAY_JC8012P4A1C)
    // JC8012P4A1C: 10.1" ESP32-P4, физ. 800x1280 (портрет)
    // Логически 1280x800 через LVGL поворот 90° → ландшафт, совместимо с UI
    #define SCREEN_WIDTH  1280
    #define SCREEN_HEIGHT 800

    #define DISPLAY_NAME "JC8012P4A1C"
    #define DISPLAY_SIZE "10.1\""

    // Масштабирование UI с базы 480x272 (jc4827w543) → 1280x800
    #define SCALE_FACTOR_X 2.67f  // 1280/480 = 2.667
    #define SCALE_FACTOR_Y 2.94f  // 800/272 = 2.941

#else
    #error "No display selected! Set -DJC4827W543_DISPLAY=1 or -DJC8012P4A1C_DISPLAY=1 in platformio.ini"
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
    // ESP32-S3: UART через P1 разъём JC4827W543
    #define UART_RX_PIN     44  // P1 RX = GPIO44
    #define UART_TX_PIN     43  // P1 TX = GPIO43
#elif defined(DISPLAY_JC8012P4A1C)
    // ESP32-P4: UART1 через разъём JC8012P4A1C
    // TODO: уточнить по схеме модуля (docs/JC8012P4A1C_I_W_Y/5-Schematic/)
    #define UART_RX_PIN     16
    #define UART_TX_PIN     17
#else
    // ESP32 UART2 пины (ESP32-2432S024)
    #define UART_RX_PIN     16
    #define UART_TX_PIN     17
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
