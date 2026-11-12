#pragma once
// Тач GT911 по I2C (7" JC1060P470). Опрос без прерывания: одна точка, координаты панели 1024×600.
#include <stdint.h>
#include <stddef.h>

class gt911_touch
{
public:
    gt911_touch(int8_t sda, int8_t scl, int8_t rst, int8_t intr) : _sda(sda), _scl(scl), _rst(rst), _int(intr) {}
    void begin();
    bool getTouch(uint16_t *x, uint16_t *y);   // true — нажато, x/y — в координатах панели
    uint8_t address() const { return _addr; }

private:
    bool rd(uint16_t reg, uint8_t *buf, size_t n);
    bool wr(uint16_t reg, uint8_t val);
    int8_t _sda, _scl, _rst, _int;
    uint8_t _addr = 0x5D;
};
