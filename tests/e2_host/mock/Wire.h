#pragma once
// Имитация Wire: LCD2004 «отвечает» на любой адрес, байты отбрасываются.
#include "Arduino.h"

class TwoWire {
public:
    void setSCL(int) {}
    void setSDA(int) {}
    void begin() {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t) {}
    size_t write(uint8_t) { return 1; }
    uint8_t endTransmission() { return 0; }
};
extern TwoWire Wire;
