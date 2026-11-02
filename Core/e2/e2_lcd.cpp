/**
 * @file  e2_lcd.cpp
 * @brief LCD2004 по I2C — см. e2_lcd.h
 */
#include "e2_lcd.h"
#include "e2_pins.h"
#include "e2_hal.h"
#include <Wire.h>
#include <string.h>

// PCF8574 → HD44780
#define PCF_RS   0x01
#define PCF_EN   0x04
#define PCF_BL   0x08

static const uint8_t s_row_offsets[4] = {0x00, 0x40, 0x14, 0x54};

E2Lcd::E2Lcd(uint8_t addr, uint8_t cols, uint8_t rows)
    : _addr(addr), _cols(cols), _rows(rows), _bl(PCF_BL), _present(false),
      _col(0), _row(0), _hw_pos(0xFF) {
    memset(_shadow, ' ', sizeof(_shadow));
    memset(_shown, ' ', sizeof(_shown));
}

// Байт за одну передачу I2C: старший ниббл со стробом EN, без него, младший — так же
void E2Lcd::_write_raw(uint8_t val, uint8_t rs) {
    uint8_t hi = (val & 0xF0) | rs | _bl;
    uint8_t lo = (uint8_t)(val << 4) | rs | _bl;
    Wire.beginTransmission(_addr);
    Wire.write(hi | PCF_EN); Wire.write(hi);
    Wire.write(lo | PCF_EN); Wire.write(lo);
    Wire.endTransmission();
}

void E2Lcd::init() {
    Wire.setSCL(E2_APIN(E2_PIN_LCD_SCL));
    Wire.setSDA(E2_APIN(E2_PIN_LCD_SDA));
    Wire.begin();
    Wire.setClock(400000);

    Wire.beginTransmission(_addr);
    _present = (Wire.endTransmission() == 0);
    if (!_present) return;   // дисплей не подключён — работаем без него

    delay(50);
    // Переход в 4-битный режим (HD44780, рис. 24)
    const uint8_t seq[4] = {0x30, 0x30, 0x30, 0x20};
    for (uint8_t i = 0; i < 4; i++) {
        Wire.beginTransmission(_addr);
        Wire.write(seq[i] | PCF_EN | _bl); Wire.write(seq[i] | _bl);
        Wire.endTransmission();
        delay(i == 0 ? 5 : 1);
    }
    _write_raw(0x28, 0);              // 4 бита, 2 строки, 5×8
    _write_raw(0x0C, 0);              // дисплей вкл, курсор выкл
    _write_raw(0x06, 0);              // адрес растёт слева направо
    _write_raw(0x01, 0); delay(2);    // очистка
    memset(_shown, ' ', sizeof(_shown));
    _hw_pos = 0;
}

void E2Lcd::backlight()   { _bl = PCF_BL; if (_present) { Wire.beginTransmission(_addr); Wire.write(_bl); Wire.endTransmission(); } }
void E2Lcd::noBacklight() { _bl = 0;      if (_present) { Wire.beginTransmission(_addr); Wire.write(_bl); Wire.endTransmission(); } }

void E2Lcd::clear() {
    memset(_shadow, ' ', sizeof(_shadow));
    _col = 0; _row = 0;
}

void E2Lcd::setCursor(uint8_t col, uint8_t row) {
    _col = col;
    _row = row < _rows ? row : _rows - 1;
}

void E2Lcd::createChar(uint8_t location, const uint8_t charmap[8]) {
    if (!_present) return;
    _write_raw(0x40 | ((location & 7) << 3), 0);
    for (uint8_t i = 0; i < 8; i++) _write_raw(charmap[i], PCF_RS);
    _hw_pos = 0xFF;
}

void E2Lcd::_put(uint8_t code) {
    if (_col < _cols) _shadow[_row][_col] = code;
    _col++;
}

// UTF-8 → коды HD44780 (русская таблица знакогенератора, как в LCDI2C_Russian)
static uint8_t _cyr(uint16_t cp) {
    static const uint8_t upper[32] = {   // А..Я
        0x41, 0xA0, 0x42, 0xA1, 0xE0, 0x45, 0xA3, 0xA4, 0xA5, 0xA6, 0x4B, 0xA7, 0x4D, 0x48, 0x4F, 0xA8,
        0x50, 0x43, 0x54, 0xA9, 0xAA, 0x58, 0xE1, 0xAB, 0xAC, 0xE2, 0xAD, 0xAE, 0x62, 0xAF, 0xB0, 0xB1 };
    static const uint8_t lower[32] = {   // а..я
        0x61, 0xB2, 0xB3, 0xB4, 0xE3, 0x65, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0x6F, 0xBE,
        0x70, 0x63, 0xBF, 0x79, 0xE4, 0x78, 0xE5, 0xC0, 0xC1, 0xE6, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7 };
    if (cp >= 0x0410 && cp <= 0x042F) return upper[cp - 0x0410];
    if (cp >= 0x0430 && cp <= 0x044F) return lower[cp - 0x0430];
    if (cp == 0x0401) return 0xA2;   // Ё
    if (cp == 0x0451) return 0xB5;   // ё
    return '?';
}

size_t E2Lcd::print(const char* s) {
    const uint8_t* p = (const uint8_t*)s;
    size_t n = 0;
    while (*p) {
        uint8_t b = *p++;
        if (b < 0x80) {
            _put(b);
        } else if ((b & 0xE0) == 0xC0 && (*p & 0xC0) == 0x80) {
            uint16_t cp = (uint16_t)((b & 0x1F) << 6) | (*p++ & 0x3F);
            _put(_cyr(cp));
        } else {
            while ((*p & 0xC0) == 0x80) p++;   // прочие многобайтовые — пропуск
            _put('?');
        }
        n++;
    }
    return n;
}

void E2Lcd::flush(uint32_t budget_us) {
    if (!_present) return;
    uint32_t t0 = micros();
    for (uint8_t r = 0; r < _rows; r++) {
        for (uint8_t c = 0; c < _cols; c++) {
            if (_shadow[r][c] == _shown[r][c]) continue;
            uint8_t addr = s_row_offsets[r] + c;
            if (_hw_pos != addr) _write_raw(0x80 | addr, 0);
            _write_raw(_shadow[r][c], PCF_RS);
            _shown[r][c] = _shadow[r][c];
            _hw_pos = addr + 1;
            if (micros() - t0 >= budget_us) return;
        }
    }
}
