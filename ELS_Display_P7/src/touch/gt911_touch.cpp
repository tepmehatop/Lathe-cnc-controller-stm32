// GT911: регистры по даташиту Goodix — 0x814E статус (бит 7 «данные готовы», биты 0..3 — число точек),
// 0x8150.. первая точка (X мл/ст, Y мл/ст). После чтения статус сбрасывается записью 0.
#include "Arduino.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "gt911_touch.h"

#define GT_PORT I2C_NUM_0

bool gt911_touch::rd(uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_write_read_device(GT_PORT, _addr, r, 2, buf, n, pdMS_TO_TICKS(20)) == ESP_OK;
}

bool gt911_touch::wr(uint16_t reg, uint8_t val)
{
    uint8_t b[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, val};
    return i2c_master_write_to_device(GT_PORT, _addr, b, 3, pdMS_TO_TICKS(20)) == ESP_OK;
}

void gt911_touch::begin()
{
    i2c_config_t c = {};
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = (gpio_num_t)_sda;
    c.scl_io_num = (gpio_num_t)_scl;
    c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = 400000;
    i2c_param_config(GT_PORT, &c);
    i2c_driver_install(GT_PORT, c.mode, 0, 0, 0);

    // Сброс с выбором адреса 0x5D: INT держим в 0, пока RST поднимается
    if (_rst >= 0) {
        pinMode(_rst, OUTPUT);
        if (_int >= 0) { pinMode(_int, OUTPUT); digitalWrite(_int, LOW); }
        digitalWrite(_rst, LOW);  delay(10);
        digitalWrite(_rst, HIGH); delay(10);
        if (_int >= 0) pinMode(_int, INPUT);
        delay(60);
    }
    uint8_t id[4] = {};
    _addr = 0x5D;
    if (!rd(0x8140, id, 4)) { _addr = 0x14; rd(0x8140, id, 4); }   // если сброс не сработал — второй адрес
    Serial.printf("GT911: адрес 0x%02X, ID '%c%c%c%c'\n", _addr, id[0] ? id[0] : '?', id[1] ? id[1] : '?',
                  id[2] ? id[2] : '?', id[3] ? id[3] : '?');
}

bool gt911_touch::getTouch(uint16_t *x, uint16_t *y)
{
    static bool last = false;
    static uint16_t lx = 0, ly = 0;
    uint8_t st = 0;
    if (!rd(0x814E, &st, 1)) return false;
    if (!(st & 0x80)) { *x = lx; *y = ly; return last; }   // новых данных нет — прежнее состояние
    uint8_t n = st & 0x0F;
    if (n > 0 && n <= 5) {
        uint8_t p[4];
        if (rd(0x8150, p, 4)) {
            lx = p[0] | (p[1] << 8);
            ly = p[2] | (p[3] << 8);
            last = true;
        }
    } else {
        last = false;
    }
    wr(0x814E, 0);
    *x = lx; *y = ly;
    return last;
}
