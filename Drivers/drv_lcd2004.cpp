/**
 * @file  drv_lcd2004.cpp
 * @brief Драйвер LCD2004 через I2C1 / PCF8574 — Этап 4
 *
 * Реализация 4-битного протокола HD44780 поверх PCF8574 (I2C expander).
 * Пины PCF8574 → LCD:
 *   P0=RS, P1=RW, P2=EN, P3=BL(подсветка), P4=D4, P5=D5, P6=D6, P7=D7
 *
 * Используем STM32duino Wire (I2C1, PB6=SCL, PB7=SDA).
 * Кастомные символы идентичны Arduino-версии (Print.ino совместимость).
 */

#include "drv_lcd2004.h"
#include "../Core/els_config.h"
#include "../Core/els_tables.h"
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if USE_LCD2004

// ============================================================
// PCF8574 битовые маски
// ============================================================
#define PCF_RS   0x01  // Register Select: 0=команда, 1=данные
#define PCF_RW   0x02  // Read/Write: всегда 0 (запись)
#define PCF_EN   0x04  // Enable strobe
#define PCF_BL   0x08  // Backlight
#define PCF_D4   0x10
#define PCF_D5   0x20
#define PCF_D6   0x40
#define PCF_D7   0x80

// HD44780 команды
#define LCD_CLEARDISPLAY   0x01
#define LCD_RETURNHOME     0x02
#define LCD_ENTRYMODESET   0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET    0x20
#define LCD_SETCGRAMADDR   0x40
#define LCD_SETDDRAMADDR   0x80

// Флаги
#define LCD_ENTRYLEFT      0x02
#define LCD_4BITMODE       0x00
#define LCD_2LINE          0x08
#define LCD_5x8DOTS        0x00
#define LCD_DISPLAYON      0x04
#define LCD_CURSOROFF      0x00
#define LCD_BLINKOFF       0x00

// Адреса строк HD44780
static const uint8_t s_row_offsets[4] = {0x00, 0x40, 0x14, 0x54};

static uint8_t s_backlight = PCF_BL;
static uint8_t s_addr      = LCD_I2C_ADDR;

// ============================================================
// Кастомные символы (совместимо с Arduino-версией)
// ============================================================
static const uint8_t s_char_arrow_left[8]  = {0x00,0x04,0x08,0x1F,0x08,0x04,0x00,0x00};
static const uint8_t s_char_arrow_right[8] = {0x00,0x04,0x02,0x1F,0x02,0x04,0x00,0x00};
static const uint8_t s_char_arrow_up[8]    = {0x00,0x04,0x0E,0x15,0x04,0x04,0x00,0x00};
static const uint8_t s_char_arrow_down[8]  = {0x00,0x04,0x04,0x15,0x0E,0x04,0x00,0x00};
static const uint8_t s_char_degree[8]      = {0x0C,0x12,0x12,0x0C,0x00,0x00,0x00,0x00};
static const uint8_t s_char_diameter[8]    = {0x01,0x0E,0x13,0x15,0x19,0x0E,0x10,0x00};

// ============================================================
// Низкоуровневая запись в PCF8574
// ============================================================
// Счётчик ошибок I2C — для диагностики
volatile uint32_t g_i2c_err_cnt = 0;
volatile uint8_t  g_i2c_last_err = 0;

static void _pcf_write(uint8_t val) {
    Wire.beginTransmission(s_addr);
    Wire.write(val | s_backlight);
    uint8_t err = Wire.endTransmission();
    if (err) {
        g_i2c_err_cnt++;
        g_i2c_last_err = err;
    }
}

// Строб EN: выставить → пауза → снять
static void _pulse_en(uint8_t val) {
    _pcf_write(val | PCF_EN);
    delayMicroseconds(1);
    _pcf_write(val & ~PCF_EN);
    delayMicroseconds(50);
}

// Отправить 4 бита (старший ниббл)
static void _write4bits(uint8_t val, uint8_t rs) {
    uint8_t data = rs;
    if (val & 0x10) data |= PCF_D4;
    if (val & 0x20) data |= PCF_D5;
    if (val & 0x40) data |= PCF_D6;
    if (val & 0x80) data |= PCF_D7;
    _pulse_en(data);
}

// Отправить байт команды или данных
static void _send(uint8_t val, uint8_t rs) {
    _write4bits(val & 0xF0, rs);        // Старший ниббл
    _write4bits((val << 4) & 0xF0, rs); // Младший ниббл
}

static void _cmd(uint8_t cmd)  { _send(cmd, 0); }
static void _data(uint8_t dat) { _send(dat, PCF_RS); }

// ============================================================
// Запись кастомного символа в CGRAM
// ============================================================
static void _create_char(uint8_t loc, const uint8_t* bitmap) {
    _cmd(LCD_SETCGRAMADDR | ((loc & 0x07) << 3));
    for (uint8_t i = 0; i < 8; i++) {
        _data(bitmap[i]);
    }
}

// ============================================================
// Публичный API
// ============================================================
void DRV_LCD2004_Init(void) {
    // STM32duino Wire по умолчанию использует I2C1 (PB6=SCL, PB7=SDA)
    // Явно указываем пины для надёжности
    Wire.setSCL(LCD_I2C_PIN_SCL == GPIO_PIN_6 ? PB6 : PB6);
    Wire.setSDA(LCD_I2C_PIN_SDA == GPIO_PIN_7 ? PB7 : PB7);
    Wire.begin();
    Wire.setClock(400000); // 400 кГц

    s_addr      = LCD_I2C_ADDR;
    s_backlight = PCF_BL;

    delay(50); // Ждём питания LCD

    // Инициализация в 4-bit mode по протоколу HD44780
    // Три раза отправляем 0x30 (8-bit init sequence)
    _pcf_write(0x00); // PCF в начальное состояние
    delay(20);

    // Форсированный переход в 4-bit mode
    _write4bits(0x30, 0); delay(5);
    _write4bits(0x30, 0); delay(1);
    _write4bits(0x30, 0); delay(1);
    _write4bits(0x20, 0); delay(1); // Переключаем в 4-bit

    // Function Set: 4-bit, 2 строки, 5x8
    _cmd(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);
    // Display: ON, cursor OFF, blink OFF
    _cmd(LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF);
    // Entry mode: left to right
    _cmd(LCD_ENTRYMODESET | LCD_ENTRYLEFT);
    // Clear
    _cmd(LCD_CLEARDISPLAY); delay(2);

    // Загрузка кастомных символов (как в Arduino-версии)
    _create_char(1, s_char_arrow_left);
    _create_char(2, s_char_arrow_right);
    _create_char(3, s_char_arrow_up);
    _create_char(4, s_char_arrow_down);
    _create_char(5, s_char_degree);
    _create_char(6, s_char_diameter);

    // Вернуться в DDRAM (иначе вывод будет в CGRAM)
    _cmd(LCD_SETDDRAMADDR);
}

void DRV_LCD2004_Backlight(uint8_t on) {
    s_backlight = on ? PCF_BL : 0;
    _pcf_write(0);
}

void DRV_LCD2004_Clear(void) {
    _cmd(LCD_CLEARDISPLAY);
    delay(2);
}

void DRV_LCD2004_SetCursor(uint8_t col, uint8_t row) {
    if (row >= 4) row = 3;
    _cmd(LCD_SETDDRAMADDR | (col + s_row_offsets[row]));
}

void DRV_LCD2004_Print(uint8_t row, uint8_t col, const char* str) {
    DRV_LCD2004_SetCursor(col, row);
    while (*str) {
        _data((uint8_t)*str++);
    }
}

// Вывести строку ровно 20 символов (дополнить пробелами справа)
void DRV_LCD2004_PrintRow(uint8_t row, const char* str20) {
    DRV_LCD2004_SetCursor(0, row);
    uint8_t i = 0;
    while (i < LCD_COLS) {
        char c = str20[i];
        _data(c ? (uint8_t)c : ' ');
        i++;
    }
}

// ============================================================
// Отображение позиций
// pos_y, pos_x в единицах 0.001 мм
// Строка 2: "X: +XXX.XXX mm      "  (20 символов)
// Строка 3: "Y: +XXX.XXX mm      "
// ============================================================
void DRV_LCD2004_UpdatePosition(int32_t pos_y, int32_t pos_x) {
    char buf[24];

    // Строка 2: ось X (поперечная)
    int32_t ax = (pos_x < 0) ? -pos_x : pos_x;
    snprintf(buf, sizeof(buf), "X:%c%4ld.%03ld mm     ",
        (pos_x < 0) ? '-' : '+',
        (long)(ax / 1000L),
        (long)(ax % 1000L));
    buf[20] = '\0';
    DRV_LCD2004_PrintRow(2, buf);

    // Строка 3: ось Y (продольная)
    int32_t ay = (pos_y < 0) ? -pos_y : pos_y;
    snprintf(buf, sizeof(buf), "Y:%c%4ld.%03ld mm     ",
        (pos_y < 0) ? '-' : '+',
        (long)(ay / 1000L),
        (long)(ay % 1000L));
    buf[20] = '\0';
    DRV_LCD2004_PrintRow(3, buf);
}

// ============================================================
// Полное меню ELS — точный порт Print.ino
// ============================================================

// ============================================================
// Хелперы для порта Print.ino (имитируют lcd.setCursor/lcd.print)
// Поддержка кириллицы UTF-8 → HD44780 Russian ROM (AIP31066W3/Surenoo)
// Таблица из библиотеки LCDI2C_Multilingual ROM/Russian.h
// ============================================================
static uint8_t _cyr_to_hd44780(uint16_t cp) {
    switch (cp) {
        case 0x0401: return 0xA2; // Ё
        case 0x0410: return 0x41; // А
        case 0x0411: return 0xA0; // Б
        case 0x0412: return 0x42; // В
        case 0x0413: return 0xA1; // Г
        case 0x0414: return 0xE0; // Д
        case 0x0415: return 0x45; // Е
        case 0x0416: return 0xA3; // Ж
        case 0x0417: return 0xA4; // З
        case 0x0418: return 0xA5; // И
        case 0x0419: return 0xA6; // Й
        case 0x041A: return 0x4B; // К
        case 0x041B: return 0xA7; // Л
        case 0x041C: return 0x4D; // М
        case 0x041D: return 0x48; // Н
        case 0x041E: return 0x4F; // О
        case 0x041F: return 0xA8; // П
        case 0x0420: return 0x50; // Р
        case 0x0421: return 0x43; // С
        case 0x0422: return 0x54; // Т
        case 0x0423: return 0xA9; // У
        case 0x0424: return 0xAA; // Ф
        case 0x0425: return 0x58; // Х
        case 0x0426: return 0xE1; // Ц
        case 0x0427: return 0xAB; // Ч
        case 0x0428: return 0xAC; // Ш
        case 0x0429: return 0xE2; // Щ
        case 0x042A: return 0xAD; // Ъ
        case 0x042B: return 0xAE; // Ы
        case 0x042C: return 0x62; // Ь
        case 0x042D: return 0xAF; // Э
        case 0x042E: return 0xB0; // Ю
        case 0x042F: return 0xB1; // Я
        case 0x0430: return 0x61; // а
        case 0x0431: return 0xB2; // б
        case 0x0432: return 0xB3; // в
        case 0x0433: return 0xB4; // г
        case 0x0434: return 0xE3; // д
        case 0x0435: return 0x65; // е
        case 0x0436: return 0xB6; // ж
        case 0x0437: return 0xB7; // з
        case 0x0438: return 0xB8; // и
        case 0x0439: return 0xB9; // й
        case 0x043A: return 0xBA; // к
        case 0x043B: return 0xBB; // л
        case 0x043C: return 0xBC; // м
        case 0x043D: return 0xBD; // н
        case 0x043E: return 0x6F; // о
        case 0x043F: return 0xBE; // п
        case 0x0440: return 0x70; // р
        case 0x0441: return 0x63; // с
        case 0x0442: return 0xBF; // т
        case 0x0443: return 0x79; // у
        case 0x0444: return 0xE4; // ф
        case 0x0445: return 0x78; // х
        case 0x0446: return 0xE5; // ц
        case 0x0447: return 0xC0; // ч
        case 0x0448: return 0xC1; // ш
        case 0x0449: return 0xE6; // щ
        case 0x044A: return 0xC2; // ъ
        case 0x044B: return 0xC3; // ы
        case 0x044C: return 0xC4; // ь
        case 0x044D: return 0xC5; // э
        case 0x044E: return 0xC6; // ю
        case 0x044F: return 0xC7; // я
        case 0x0451: return 0xB5; // ё
        default:     return '?';
    }
}

static void _lcd_set(uint8_t col, uint8_t row) {
    DRV_LCD2004_SetCursor(col, row);
}
static void _lcd_str(const char* s) {
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        uint8_t b = *p++;
        if (b < 0x80) {
            _data(b);  // ASCII + кастомные 0x01-0x06
        } else if (b == 0xD0 && *p) {
            uint8_t b2 = *p++;
            // 0xD0 0x80..0xBF → U+0400..U+043F
            _data(_cyr_to_hd44780((uint16_t)(0x0400 | (b2 & 0x3F))));
        } else if (b == 0xD1 && *p) {
            uint8_t b2 = *p++;
            // 0xD1 0x80..0xBF → U+0440..U+047F
            _data(_cyr_to_hd44780((uint16_t)(0x0440 | (b2 & 0x3F))));
        } else {
            while (*p && (*p & 0xC0) == 0x80) p++;
        }
    }
}
static char _lcd_buf[24];
#define LCD_SET(col, row) _lcd_set(col, row)
#define LCD_P(s)          _lcd_str(s)
#define LCD_BUF           _lcd_buf

// Приветственный экран — как в Arduino setup()
void DRV_LCD2004_PrintWelcome(void) {
    LCD_SET(0,0); LCD_P("  www.chipmaker.ru  ");
    LCD_SET(0,1); LCD_P(" Система управления ");
    LCD_SET(0,2); LCD_P("  Токарным станком  ");
    LCD_SET(0,3); LCD_P("   STM32  7e2 V4.0  ");
    delay(2000);
    LCD_SET(0,1); LCD_P(" Инициализация...   ");
}

void DRV_LCD2004_PrintELS(const ELS_State_t* s) {

    // ── Экраны ошибок и завершения ──────────────────────────
    if (s->err_0_flag) {
        LCD_SET(0,0); LCD_P("      ВНИМАНИЕ      ");
        LCD_SET(0,1); LCD_P("Установите джойстик ");
        LCD_SET(0,2); LCD_P("   в нейтральное    ");
        LCD_SET(0,3); LCD_P("     положение      ");
        return;
    }
    if (s->err_1_flag) {
        LCD_SET(0,0); LCD_P(" ВНИМАНИЕ:          ");
        LCD_SET(0,1); LCD_P("                    ");
        LCD_SET(0,2); LCD_P("   УСТАНОВИТЕ УПОРЫ!");
        LCD_SET(0,3); LCD_P("                    ");
        return;
    }
    if (s->err_2_flag) {
        LCD_SET(0,0); LCD_P(" ВНИМАНИЕ:          ");
        LCD_SET(0,1); LCD_P("                    ");
        LCD_SET(0,2); LCD_P("УСТАНОВИТЕ СУППОРТ  ");
        LCD_SET(0,3); LCD_P(" В ИСХОДНУЮ ПОЗИЦИЮ!");
        return;
    }
    if (s->Complete_flag) {
        LCD_SET(0,0); LCD_P("                    ");
        LCD_SET(0,1); LCD_P("ОПЕРАЦИЯ ЗАВЕРШЕНА! ");
        LCD_SET(0,2); LCD_P("                    ");
        LCD_SET(0,3); LCD_P("                    ");
        return;
    }

    // ── SelectMenu == 2: Ввод диаметра / сброс осей (Feed/Thread/Cone/aFeed/Sphere) ──
    if ((s->mode == MODE_FEED   && s->select_menu == 2) ||
        (s->mode == MODE_CONE_L && s->select_menu == 3) ||
        (s->mode == MODE_CONE_R && s->select_menu == 3) ||
        (s->mode == MODE_AFEED  && s->select_menu == 3) ||
        (s->mode == MODE_SPHERE && s->select_menu == 3) ||
        (s->mode == MODE_THREAD && s->select_menu == 3))
    {
        LCD_SET(0,0); LCD_P(" Ввод \x06  Сброс_осей ");
        LCD_SET(0,1);
        if (s->MSize_X_mm <= 0) LCD_P("Диаметр \x01\x02  ");
        else                     LCD_P("Диаметр \x01\x02 -");
        snprintf(LCD_BUF, 7, "%3ld.%02ld", (long)abs((int32_t)(s->MSize_X_mm/100)),
                                            (long)abs((int32_t)(s->MSize_X_mm%100)));
        LCD_P(LCD_BUF); LCD_P("мм");
        LCD_SET(0,2);
        if (s->Size_X_mm <= 0) LCD_P("Ось X:  \x03   ");
        else                    LCD_P("Ось X:  \x03  -");
        snprintf(LCD_BUF, 7, "%3ld.%02ld", (long)abs((int32_t)(s->Size_X_mm/100)),
                                            (long)abs((int32_t)(s->Size_X_mm%100)));
        LCD_P(LCD_BUF); LCD_P("мм");
        LCD_SET(0,3);
        if (s->Size_Z_mm <= 0) LCD_P("Ось Y:  \x04   ");
        else                    LCD_P("Ось Y:  \x04  -");
        snprintf(LCD_BUF, 7, "%3ld.%02ld", (long)abs((int32_t)(s->Size_Z_mm/100)),
                                            (long)abs((int32_t)(s->Size_Z_mm%100)));
        LCD_P(LCD_BUF); LCD_P("мм");
        return;  // только этот экран
    }

    // ── SelectMenu == 1: общая часть — линейка (строки 1-3, левая часть) ──
    if ((s->mode == MODE_FEED   && s->select_menu == 1) ||
        (s->mode == MODE_CONE_L && s->select_menu == 1) ||
        (s->mode == MODE_CONE_R && s->select_menu == 1) ||
        (s->mode == MODE_AFEED  && s->select_menu == 1) ||
        (s->mode == MODE_SPHERE && s->select_menu == 1) ||
        (s->mode == MODE_THREAD && s->select_menu == 1))
    {
        // Строка 1: диаметр (левые 8 символов)
        LCD_SET(0,1);
        if (s->MSize_X_mm <= 0)
            snprintf(LCD_BUF, 9, "\x06 %2ld.%02ld", (long)abs((int32_t)(s->MSize_X_mm/100)),
                                                      (long)abs((int32_t)(s->MSize_X_mm%100)));
        else
            snprintf(LCD_BUF, 9, "\x06-%2ld.%02ld", (long)abs((int32_t)(s->MSize_X_mm/100)),
                                                      (long)abs((int32_t)(s->MSize_X_mm%100)));
        LCD_P(LCD_BUF); LCD_P(" ");
        // Строка 2: X (левые 8 символов)
        LCD_SET(0,2);
        if (s->Size_X_mm <= 0)
            snprintf(LCD_BUF, 9, "X %2ld.%02ld", (long)abs((int32_t)(s->Size_X_mm/100)),
                                                   (long)abs((int32_t)(s->Size_X_mm%100)));
        else
            snprintf(LCD_BUF, 9, "X-%2ld.%02ld", (long)abs((int32_t)(s->Size_X_mm/100)),
                                                   (long)abs((int32_t)(s->Size_X_mm%100)));
        LCD_P(LCD_BUF); LCD_P(" ");
        // Строка 3: Y (левые 8 символов)
        LCD_SET(0,3);
        if (s->Size_Z_mm <= 0)
            snprintf(LCD_BUF, 9, "Y %2ld.%02ld", (long)abs((int32_t)(s->Size_Z_mm/100)),
                                                   (long)abs((int32_t)(s->Size_Z_mm%100)));
        else
            snprintf(LCD_BUF, 9, "Y-%2ld.%02ld", (long)abs((int32_t)(s->Size_Z_mm/100)),
                                                   (long)abs((int32_t)(s->Size_Z_mm%100)));
        LCD_P(LCD_BUF); LCD_P(" ");
    }

    // ── Режим РЕЗЬБА ────────────────────────────────────────
    if (s->mode == MODE_THREAD) {
        if (s->select_menu == 1) {
            // Строка 0, правая часть (cols 8-19): режим
            if ((s->Ph > 1) && (!s->ConL_Thr_flag || !s->ConR_Thr_flag)) {
                LCD_SET(8,0); LCD_P("\x02  Резьба");
                snprintf(LCD_BUF, 5, " %2d", (int)s->Gewinde_flag);
                LCD_P(LCD_BUF);
            } else if (s->ConL_Thr_flag && s->Ph == 1) {
                LCD_SET(8,0);
                snprintf(LCD_BUF, 8, "<%s ", Cone_Info[s->Cone_Step].Cone_Print);
                LCD_P(LCD_BUF); LCD_P("Резьба");
            } else if (s->ConR_Thr_flag && s->Ph == 1) {
                LCD_SET(8,0);
                snprintf(LCD_BUF, 8, ">%s ", Cone_Info[s->Cone_Step].Cone_Print);
                LCD_P(LCD_BUF); LCD_P("Резьба");
            } else {
                LCD_SET(8,0); LCD_P("\x02  Резьба   ");
            }
            LCD_SET(8,2); LCD_P(" Циклов:  ");
            LCD_SET(8,1); LCD_P(" Шаг: ");
            snprintf(LCD_BUF, 7, "%s", Thread_Info[s->Thread_Step].Thread_Print);
            LCD_P(LCD_BUF);
            // Строка 0, левая часть: подрежим
            switch (s->sub_thread) {
                case Sub_Mode_Thread_Int:
                    LCD_SET(0,0); LCD_P(" Внутр  ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2d",
                        (int)((Thread_Info[s->Thread_Step].Pass - s->Pass_Nr + 1
                               + PASS_FINISH + s->Pass_Fin) + s->Thr_Pass_Summ));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Thread_Man:
                    LCD_SET(0,0); LCD_P(" Ручная ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2d",
                        (int)((Thread_Info[s->Thread_Step].Pass
                               + PASS_FINISH + s->Pass_Fin) + s->Thr_Pass_Summ));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Thread_Ext:
                    LCD_SET(0,0); LCD_P(" Наружн ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2d",
                        (int)((Thread_Info[s->Thread_Step].Pass - s->Pass_Nr + 1
                               + PASS_FINISH + s->Pass_Fin) + s->Thr_Pass_Summ));
                    LCD_P(LCD_BUF); break;
            }
            // Строка 3 правая: ход резьбы или об/мин
            if (s->Ph > 1) {
                int ph_s1 = (s->Ph > 0) ? (int)s->Ph : 1;
                int thstep_mm = (int)(Thread_Info[s->Thread_Step].Step * 100.0f * ph_s1);
                LCD_SET(8,3); LCD_P(" Ход: ");
                snprintf(LCD_BUF, 5, "%1d.%02d", thstep_mm/100, thstep_mm%100);
                LCD_P(LCD_BUF); LCD_P("мм");
            } else {
                LCD_SET(8,3); LCD_P(" Об/мин: ");
                snprintf(LCD_BUF, 4, "%3d", (int)Thread_Info[s->Thread_Step].Limit_Print);
                LCD_P(LCD_BUF);
            }
        } else if (s->select_menu == 2) {
            int ph_safe = (s->Ph > 0) ? (int)s->Ph : 1;
            int thstep_mm = (int)(Thread_Info[s->Thread_Step].Step * 100.0f * ph_safe);
            LCD_SET(0,0); LCD_P("Чист проходов: \x01\x02 ");
            snprintf(LCD_BUF, 3, "%2d", (int)(PASS_FINISH + s->Pass_Fin));
            LCD_P(LCD_BUF);
            LCD_SET(0,1); LCD_P("Колич заходов: \x03\x04 ");
            snprintf(LCD_BUF, 3, "%2d", (int)s->Ph);
            LCD_P(LCD_BUF);
            LCD_SET(0,2); LCD_P("Ход резьбы:   ");
            snprintf(LCD_BUF, 5, "%1d.%02d", thstep_mm/100, thstep_mm%100);
            LCD_P(LCD_BUF); LCD_P("мм");
            LCD_SET(0,3); LCD_P("Шаг резьбы:   ");
            snprintf(LCD_BUF, 7, "%s", Thread_Info[s->Thread_Step].Thread_Print);
            LCD_P(LCD_BUF);
        }
    }

    // ── Режим ПОДАЧА СИНХРОННАЯ ──────────────────────────────
    else if (s->mode == MODE_FEED) {
        if (s->select_menu == 1) {
            // Строка 0 правая: режим + маркер модификации
            LCD_SET(8,0);
            if (s->OTSKOK_Z < REBOUND_Z || s->TENSION_Z > 0)
                LCD_P("\x02  Синхрон *");
            else
                LCD_P("\x02  Синхрон  ");
            LCD_SET(8,1); LCD_P(" Подача:");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Feed_mm/100), (int)(s->Feed_mm%100));
            LCD_P(LCD_BUF);
            LCD_SET(8,2); LCD_P(" Циклов:  ");
            LCD_SET(8,3); LCD_P(" Съём \x06:");
            snprintf(LCD_BUF, 5, "%1d.%02d", (int)(s->Ap/100), (int)(s->Ap%100));
            LCD_P(LCD_BUF);
            switch (s->sub_feed) {
                case Sub_Mode_Feed_Int:
                    LCD_SET(0,0); LCD_P(" Внутре ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Feed_Man:
                    LCD_SET(0,0); LCD_P(" Ручной ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)s->Pass_Total);
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Feed_Ext:
                    LCD_SET(0,0); LCD_P(" Наружн ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
            }
        } else if (s->select_menu == 3) {
            LCD_SET(0,0); LCD_P("Отскок Y: \x01\x02");
            snprintf(LCD_BUF, 16, " %2ld.%02ldmm",
                (long)abs((int32_t)(s->OTSKOK_Z_mm/100)),
                (long)abs((int32_t)(s->OTSKOK_Z_mm%100)));
            LCD_P(LCD_BUF);
            LCD_SET(0,1); LCD_P("                    ");
            LCD_SET(0,2); LCD_P("Ослабление \x03        ");
            LCD_SET(0,3); LCD_P("натяга Y:  \x04");
            snprintf(LCD_BUF, 16, " %2ld.%02ldmm",
                (long)abs((int32_t)(s->TENSION_Z_mm/100)),
                (long)abs((int32_t)(s->TENSION_Z_mm%100)));
            LCD_P(LCD_BUF);
        }
    }

    // ── Режим ПОДАЧА АСИНХРОННАЯ ─────────────────────────────
    else if (s->mode == MODE_AFEED) {
        if (s->select_menu == 1) {
            LCD_SET(8,0); LCD_P("\x02  Асинхрон ");
            LCD_SET(8,1); LCD_P(" Подача: ");
            snprintf(LCD_BUF, 4, "%3d", (int)s->aFeed_mm);
            LCD_P(LCD_BUF);
            LCD_SET(8,2); LCD_P(" Циклов:  ");
            LCD_SET(8,3); LCD_P(" Съём R:");
            snprintf(LCD_BUF, 5, "%1d.%02d", (int)(s->Ap/100), (int)(s->Ap%100));
            LCD_P(LCD_BUF);
            switch (s->sub_afeed) {
                case Sub_Mode_aFeed_Int:
                    LCD_SET(0,0); LCD_P(" Внутре ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_aFeed_Man:
                    LCD_SET(0,0); LCD_P(" Ручной ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)s->Pass_Total);
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_aFeed_Ext:
                    LCD_SET(0,0); LCD_P(" Наружн ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
            }
        } else if (s->select_menu == 2) {
            LCD_SET(0,0); LCD_P("Текущий  угол:");
            snprintf(LCD_BUF, 7, "%3ld.%01ld",
                (long)(s->Spindle_Angle/1000),
                (long)(s->Spindle_Angle%1000/100));
            LCD_P(LCD_BUF); LCD_P("\x05");
            LCD_SET(0,1); LCD_P("Делим круг на:");
            snprintf(LCD_BUF, 5, "%3d ", (int)s->Total_Tooth);
            LCD_P(LCD_BUF); LCD_P("\x03\x04");
            LCD_SET(0,2); LCD_P("Выбор отметки:");
            snprintf(LCD_BUF, 5, "%3d ", (int)s->Current_Tooth);
            LCD_P(LCD_BUF); LCD_P("\x01\x02");
            LCD_SET(0,3); LCD_P("Угол  сектора:");
            snprintf(LCD_BUF, 7, "%3ld.%01ld",
                (long)(s->Required_Angle/1000),
                (long)(s->Required_Angle%1000/100));
            LCD_P(LCD_BUF); LCD_P("\x05");
        }
    }

    // ── Режим КОНУС ЛЕВЫЙ < ──────────────────────────────────
    else if (s->mode == MODE_CONE_L) {
        if (s->select_menu == 1) {
            LCD_SET(8,0);
            snprintf(LCD_BUF, 8, "<%s ", Cone_Info[s->Cone_Step].Cone_Print);
            LCD_P(LCD_BUF);
            LCD_SET(13,0); LCD_P(" Конус ");
            LCD_SET(8,1); LCD_P(" Подача:");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Feed_mm/100), (int)(s->Feed_mm%100));
            LCD_P(LCD_BUF);
            LCD_SET(8,2); LCD_P(" Циклов:  ");
            LCD_SET(8,3); LCD_P(" Съём \x06:");
            snprintf(LCD_BUF, 5, "%1d.%02d", (int)(s->Ap/100), (int)(s->Ap%100));
            LCD_P(LCD_BUF);
            switch (s->sub_cone) {
                case Sub_Mode_Cone_Int:
                    LCD_SET(0,0); LCD_P(" Внутре ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Cone_Man:
                    LCD_SET(0,0); LCD_P(" Ручной ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)s->Pass_Total);
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Cone_Ext:
                    LCD_SET(0,0); LCD_P(" Наружн ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
            }
        } else if (s->select_menu == 2) {
            LCD_SET(0,0); LCD_P("Конус  < ");
            snprintf(LCD_BUF, 5, "%s", Cone_Info[s->Cone_Step].Cone_Print);
            LCD_P(LCD_BUF);
            LCD_SET(13,0); LCD_P("    \x01 \x02");
            LCD_SET(0,2); LCD_P("Коническая        \x03 ");
            if (s->ConL_Thr_flag) {
                LCD_SET(0,1); LCD_P("  В режиме резьба   ");
                LCD_SET(0,3); LCD_P("резьба:    Вкл    \x04 ");
            } else {
                LCD_SET(0,1); LCD_P("                    ");
                LCD_SET(0,3); LCD_P("резьба:    Выкл   \x04 ");
            }
        }
    }

    // ── Режим КОНУС ПРАВЫЙ > ─────────────────────────────────
    else if (s->mode == MODE_CONE_R) {
        if (s->select_menu == 1) {
            LCD_SET(8,0);
            snprintf(LCD_BUF, 8, ">%s ", Cone_Info[s->Cone_Step].Cone_Print);
            LCD_P(LCD_BUF);
            LCD_SET(13,0); LCD_P(" Конус ");
            LCD_SET(8,1); LCD_P(" Подача:");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Feed_mm/100), (int)(s->Feed_mm%100));
            LCD_P(LCD_BUF);
            LCD_SET(8,2); LCD_P(" Циклов:  ");
            LCD_SET(8,3); LCD_P(" Съём \x06:");
            snprintf(LCD_BUF, 5, "%1d.%02d", (int)(s->Ap/100), (int)(s->Ap%100));
            LCD_P(LCD_BUF);
            switch (s->sub_cone) {
                case Sub_Mode_Cone_Int:
                    LCD_SET(0,0); LCD_P(" Внутре ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Cone_Man:
                    LCD_SET(0,0); LCD_P(" Ручной ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)s->Pass_Total);
                    LCD_P(LCD_BUF); break;
                case Sub_Mode_Cone_Ext:
                    LCD_SET(0,0); LCD_P(" Наружн ");
                    LCD_SET(18,2);
                    snprintf(LCD_BUF, 3, "%2ld", (long)(s->Pass_Total - s->Pass_Nr + 1));
                    LCD_P(LCD_BUF); break;
            }
        } else if (s->select_menu == 2) {
            LCD_SET(0,0); LCD_P("Конус  > ");
            snprintf(LCD_BUF, 5, "%s", Cone_Info[s->Cone_Step].Cone_Print);
            LCD_P(LCD_BUF);
            LCD_SET(13,0); LCD_P("    \x01 \x02");
            LCD_SET(0,2); LCD_P("Коническая        \x03 ");
            if (s->ConR_Thr_flag) {
                LCD_SET(0,1); LCD_P("  В режиме резьба   ");
                LCD_SET(0,3); LCD_P("резьба:    Вкл    \x04 ");
            } else {
                LCD_SET(0,1); LCD_P("                    ");
                LCD_SET(0,3); LCD_P("резьба:    Выкл   \x04 ");
            }
        }
    }

    // ── Режим СФЕРА (шароточка) ──────────────────────────────
    else if (s->mode == MODE_SPHERE) {
        if (s->select_menu == 1 && s->sub_sphere != Sub_Mode_Sphere_Int) {
            LCD_SET(0,0); LCD_P("ШАР \x06");
            snprintf(LCD_BUF, 5, "%2ld.%01ld",
                (long)(s->Sph_R_mm * 2 / 100),
                (long)(s->Sph_R_mm * 2 / 10 % 10));
            LCD_P(LCD_BUF); LCD_P("мм");
            LCD_SET(8,1); LCD_P(" Подача:");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Feed_mm/100), (int)(s->Feed_mm%100));
            LCD_P(LCD_BUF);
            if (s->sub_sphere == Sub_Mode_Sphere_Man)
                { LCD_SET(11,0); LCD_P(" Отключен"); }
            else if (s->sub_sphere == Sub_Mode_Sphere_Ext)
                { LCD_SET(11,0); LCD_P("  Включен"); }
            LCD_SET(8,2); LCD_P(" Ножка:\x06");
            snprintf(LCD_BUF, 5, "%ld.%02ld",
                (long)(s->Bar_R_mm*2/100),
                (long)(s->Bar_R_mm*2%100));
            LCD_P(LCD_BUF);
            LCD_SET(8,3); LCD_P(" Заходов:");
            snprintf(LCD_BUF, 4, "%3ld", (long)(s->Pass_Total_Sphr + 2 - s->Pass_Nr));
            LCD_P(LCD_BUF);
        } else if (s->select_menu == 2) {
            LCD_SET(0,0); LCD_P("Ширина резца: ");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Cutter_Width/100), (int)(s->Cutter_Width%100));
            LCD_P(LCD_BUF); LCD_P("мм");
            LCD_SET(0,1); LCD_P("Шаг по оси Y: ");
            snprintf(LCD_BUF, 5, "%1d.%02d",
                (int)(s->Cutting_Width/100), (int)(s->Cutting_Width%100));
            LCD_P(LCD_BUF); LCD_P("мм");
            LCD_SET(0,2); LCD_P("                    ");
            LCD_SET(0,3); LCD_P("                    ");
        }
        if (s->sub_sphere == Sub_Mode_Sphere_Int) {
            LCD_SET(0,0); LCD_P("                    ");
            LCD_SET(0,1); LCD_P(" Режим невозможен!  ");
            LCD_SET(0,2); LCD_P("                    ");
            LCD_SET(0,3); LCD_P("                    ");
        }
    }

    // ── Режим ДЕЛИТЕЛЬ ───────────────────────────────────────
    else if (s->mode == MODE_DIVIDER) {
        LCD_SET(0,0); LCD_P("    Текущий угол    ");
        LCD_SET(0,1); LCD_P("       ");
        snprintf(LCD_BUF, 9, "%3ld.%01ld",
            (long)(s->Spindle_Angle/1000),
            (long)(s->Spindle_Angle%1000/100));
        LCD_P(LCD_BUF); LCD_P("\x05       ");
        LCD_SET(0,2); LCD_P("                    ");
        LCD_SET(0,3); LCD_P("Сброс кнопкой Селект");
    }

    // ── Режим РЕЗЕРВ ─────────────────────────────────────────
    else if (s->mode == MODE_RESERVE) {
        LCD_SET(0,0); LCD_P("                    ");
        LCD_SET(0,1); LCD_P("                    ");
        LCD_SET(0,2); LCD_P("                    ");
        LCD_SET(0,3); LCD_P("              РЕЗЕРВ");
    }
}

#else  // USE_LCD2004 = 0

void DRV_LCD2004_Init(void) {}
void DRV_LCD2004_Clear(void) {}
void DRV_LCD2004_SetCursor(uint8_t c, uint8_t r) { (void)c;(void)r; }
void DRV_LCD2004_Print(uint8_t r, uint8_t c, const char* s) { (void)r;(void)c;(void)s; }
void DRV_LCD2004_PrintRow(uint8_t r, const char* s) { (void)r;(void)s; }
void DRV_LCD2004_UpdatePosition(int32_t y, int32_t x) { (void)y;(void)x; }
void DRV_LCD2004_Backlight(uint8_t o) { (void)o; }
void DRV_LCD2004_PrintWelcome(void) {}
void DRV_LCD2004_PrintELS(const ELS_State_t* s) { (void)s; }

#endif
