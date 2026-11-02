/**
 * @file  scale_protocols.cpp
 * @brief Разбор кадров цифровых линеек — см. scale_protocols.h
 */
#include "scale_protocols.h"

// Деление с округлением к ближайшему (для отрицательных — симметрично)
static int64_t div_round(int64_t num, int64_t den) {
    return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

// Знаковое расширение n-битного числа
static int32_t sign_extend(uint32_t v, unsigned bits) {
    uint32_t m = 1u << (bits - 1);
    v &= (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return (int32_t)((v ^ m) - m);
}

int32_t ScaleProto_QuadToUm(int64_t count, int32_t nm_per_count) {
    return (int32_t)div_round(count * (int64_t)nm_per_count, 1000);
}

int32_t ScaleProto_Sylvac48ToUm(uint64_t frame) {
    int32_t counts = sign_extend((uint32_t)(frame & 0xFFFFFFu), 24);
    // 20480 имп/дюйм: 1 имп = 25400/20480 мкм
    return (int32_t)div_round((int64_t)counts * 25400, 20480);
}

bool ScaleProto_Bin6ToUm(uint32_t frame, int32_t* um) {
    uint32_t value = frame & 0xFFFFFu;          // биты 0..19
    bool negative  = (frame >> 20) & 1u;
    bool inch      = (frame >> 23) & 1u;
    // Разумный предел: 2000 мм в мм-режиме или 80" в дюймовом
    if ((!inch && value > 200000u) || (inch && value > 160000u)) return false;
    int64_t v = inch ? div_round((int64_t)value * 127, 10)   // 0.0005" = 12.7 мкм
                     : (int64_t)value * 10;                  // 0.01 мм = 10 мкм
    *um = (int32_t)(negative ? -v : v);
    return true;
}

int32_t ScaleProto_IGaging21ToUm(uint32_t frame) {
    int32_t counts = sign_extend(frame, 21);
    // 2560 имп/дюйм: 1 имп = 25400/2560 мкм
    return (int32_t)div_round((int64_t)counts * 25400, 2560);
}

void ScaleProto_SplitNibbles(uint64_t frame, uint8_t nibbles[13]) {
    for (int i = 0; i < 13; i++) nibbles[i] = (uint8_t)((frame >> (4 * i)) & 0xFu);
}

bool ScaleProto_DigimaticToUm(const uint8_t n[13], int32_t* um) {
    for (int i = 0; i < 4; i++)
        if (n[i] != 0xF) return false;
    if (n[4] != 0 && n[4] != 8) return false;
    int64_t digits = 0;
    for (int i = 5; i <= 10; i++) {
        if (n[i] > 9) return false;
        digits = digits * 10 + n[i];
    }
    uint8_t dp = n[11];
    if (dp > 5 || n[12] > 1) return false;
    int64_t pow10 = 1;
    for (uint8_t i = 0; i < dp; i++) pow10 *= 10;
    // значение = digits / 10^dp  [мм или дюймы]
    int64_t v = (n[12] == 0) ? div_round(digits * 1000, pow10)      // мм → мкм
                             : div_round(digits * 25400, pow10);    // дюйм → мкм
    *um = (int32_t)(n[4] == 8 ? -v : v);
    return true;
}
