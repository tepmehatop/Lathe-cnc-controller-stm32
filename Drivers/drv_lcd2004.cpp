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
static void _pcf_write(uint8_t val) {
    Wire.beginTransmission(s_addr);
    Wire.write(val | s_backlight);
    Wire.endTransmission();
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
// Отображение позиций — аналог фрагмента Print.ino для M1 SM=2
// Строка 2: "Ocь X:  \3  ±XXX.XXмм"
// Строка 3: "Ocь Y:  \4  ±XXX.XXмм"
// ============================================================
void DRV_LCD2004_UpdatePosition(int32_t pos_y, int32_t pos_x) {
    char buf[21];

    // Строка 2: ось X (поперечная)
    DRV_LCD2004_SetCursor(0, 2);
    if (pos_x <= 0) {
        snprintf(buf, sizeof(buf), "Oc\xFC X:  \x03   ");
    } else {
        snprintf(buf, sizeof(buf), "Oc\xFC X:  \x03  -");
    }
    // Вывести метку
    const char* p = buf;
    while (*p) _data((uint8_t)*p++);
    // Значение
    char val[12];
    snprintf(val, sizeof(val), "%3ld.%02d",
        (long)abs((int)(pos_x / 100L)),
        (int)abs((int)(pos_x % 100L)));
    p = val;
    while (*p) _data((uint8_t)*p++);
    _data('\xEC'); _data('\xBC'); // "мм" в KOI8-R / или просто "mm"

    // Строка 3: ось Y (продольная)
    DRV_LCD2004_SetCursor(0, 3);
    if (pos_y <= 0) {
        snprintf(buf, sizeof(buf), "Oc\xFC Y:  \x04   ");
    } else {
        snprintf(buf, sizeof(buf), "Oc\xFC Y:  \x04  -");
    }
    p = buf;
    while (*p) _data((uint8_t)*p++);
    snprintf(val, sizeof(val), "%3ld.%02d",
        (long)abs((int)(pos_y / 100L)),
        (int)abs((int)(pos_y % 100L)));
    p = val;
    while (*p) _data((uint8_t)*p++);
    _data('\xEC'); _data('\xBC');
}

#else  // USE_LCD2004 = 0

void DRV_LCD2004_Init(void) {}
void DRV_LCD2004_Clear(void) {}
void DRV_LCD2004_SetCursor(uint8_t c, uint8_t r) { (void)c;(void)r; }
void DRV_LCD2004_Print(uint8_t r, uint8_t c, const char* s) { (void)r;(void)c;(void)s; }
void DRV_LCD2004_PrintRow(uint8_t r, const char* s) { (void)r;(void)s; }
void DRV_LCD2004_UpdatePosition(int32_t y, int32_t x) { (void)y;(void)x; }
void DRV_LCD2004_Backlight(uint8_t o) { (void)o; }

#endif
