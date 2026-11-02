#pragma once
/**
 * @file  e2_lcd.h
 * @brief LCD2004 по I2C (PCF8574) с API LCDI2C_Russian, которое использует Print.ino.
 *
 * Отличие от библиотеки оригинала: print()/setCursor() пишут в теневой буфер
 * 20×4, а на дисплей уходят только изменившиеся символы — из flush(), понемногу
 * за вызов. Print() оригинала перерисовывает весь экран каждые 40 мс, и прямой
 * вывод по I2C занимал бы почти всё время главного цикла.
 *
 * Кириллица UTF-8 переводится в коды знакогенератора HD44780 с русской таблицей.
 */
#include <Arduino.h>

class E2Lcd {
public:
    E2Lcd(uint8_t addr, uint8_t cols, uint8_t rows);

    void   init();
    void   backlight();
    void   noBacklight();
    void   clear();
    void   setCursor(uint8_t col, uint8_t row);
    size_t print(const char* s);
    void   createChar(uint8_t location, const uint8_t charmap[8]);

    // Отправить изменения на дисплей, не дольше budget_us микросекунд
    void   flush(uint32_t budget_us = 2000);

    // Текущее содержимое экрана (коды HD44780), строка row, 20 символов
    const uint8_t* row(uint8_t r) const { return _shadow[r < 4 ? r : 3]; }
    bool   present() const { return _present; }

private:
    void   _write_raw(uint8_t val, uint8_t rs);
    void   _put(uint8_t code);

    uint8_t _addr, _cols, _rows;
    uint8_t _bl;
    bool    _present;
    uint8_t _col, _row;
    uint8_t _shadow[4][20];   // что должно быть на экране
    uint8_t _shown[4][20];    // что уже отправлено
    uint8_t _hw_pos;          // адрес DDRAM дисплея после последней записи (0xFF — неизвестен)
};
