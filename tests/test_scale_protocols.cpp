// Юнит-тесты разбора протоколов линеек (запуск на ПК): tools/run_unit_tests.sh
#include "scale_protocols.h"
#include <cstdio>
#include <cstdlib>

static int g_fail = 0, g_total = 0;

#define CHECK_EQ(got, exp) do { g_total++; long long _g = (long long)(got), _e = (long long)(exp); \
    if (_g != _e) { g_fail++; std::printf("FAIL %s:%d  %s = %lld, ожидалось %lld\n", \
                                          __FILE__, __LINE__, #got, _g, _e); } } while (0)
#define CHECK(cond) do { g_total++; if (!(cond)) { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ── Генераторы кадров — так линейки формируют данные (используются и в симуляторе) ──
static uint64_t sylvac48(int32_t counts) {
    uint64_t w = (uint32_t)counts & 0xFFFFFFu;
    return w | (w << 24);
}
static uint32_t bin6(int32_t value, bool inch) {
    uint32_t v = (uint32_t)(value < 0 ? -value : value) & 0xFFFFFu;
    return v | (value < 0 ? (1u << 20) : 0) | (inch ? (1u << 23) : 0);
}
static uint32_t igaging21(int32_t counts) { return (uint32_t)counts & 0x1FFFFFu; }
static void digimatic(uint8_t n[13], bool neg, const char* digits6, int dp, int unit) {
    for (int i = 0; i < 4; i++) n[i] = 0xF;
    n[4] = neg ? 8 : 0;
    for (int i = 0; i < 6; i++) n[5 + i] = (uint8_t)(digits6[i] - '0');
    n[11] = (uint8_t)dp;
    n[12] = (uint8_t)unit;
}

int main() {
    // Квадратура
    CHECK_EQ(ScaleProto_QuadToUm(0, 5000), 0);
    CHECK_EQ(ScaleProto_QuadToUm(200, 5000), 1000);          // 200 × 5 мкм = 1 мм
    CHECK_EQ(ScaleProto_QuadToUm(-200, 5000), -1000);
    CHECK_EQ(ScaleProto_QuadToUm(1, 1000), 1);
    CHECK_EQ(ScaleProto_QuadToUm(400000000LL, 5000), 2000000000);   // 2 км не переполняется
    CHECK_EQ(ScaleProto_QuadToUm(3, 2500), 8);               // 7.5 мкм → 8

    // Sylvac 48: 20480 имп/дюйм
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(0)), 0);
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(20480)), 25400);      // 1"
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(-20480)), -25400);
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(806)), 1000);         // ≈1 мм (999.6)
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(-8388608)), -10403840); // мин. 24-бит
    CHECK_EQ(ScaleProto_Sylvac48ToUm(sylvac48(8388607)), 10403839);

    // BIN6
    int32_t um = 0;
    CHECK(ScaleProto_Bin6ToUm(bin6(12345, false), &um)); CHECK_EQ(um, 123450);  // 123.45 мм
    CHECK(ScaleProto_Bin6ToUm(bin6(-50, false), &um));   CHECK_EQ(um, -500);    // −0.50 мм
    CHECK(ScaleProto_Bin6ToUm(bin6(2000, true), &um));   CHECK_EQ(um, 25400);   // 1.0000"
    CHECK(ScaleProto_Bin6ToUm(bin6(-1, true), &um));     CHECK_EQ(um, -13);     // −0.0005"
    CHECK(!ScaleProto_Bin6ToUm(0xFFFFFu, &um));                                 // мусор

    // iGaging 21: 2560 имп/дюйм
    CHECK_EQ(ScaleProto_IGaging21ToUm(igaging21(0)), 0);
    CHECK_EQ(ScaleProto_IGaging21ToUm(igaging21(2560)), 25400);
    CHECK_EQ(ScaleProto_IGaging21ToUm(igaging21(-2560)), -25400);
    CHECK_EQ(ScaleProto_IGaging21ToUm(igaging21(-1)), -10);         // −9.92 мкм
    CHECK_EQ(ScaleProto_IGaging21ToUm(igaging21(-1048576)), -10403840);

    // Digimatic
    uint8_t n[13];
    digimatic(n, false, "012345", 2, 0); CHECK(ScaleProto_DigimaticToUm(n, &um)); CHECK_EQ(um, 123450);
    digimatic(n, true,  "000500", 3, 0); CHECK(ScaleProto_DigimaticToUm(n, &um)); CHECK_EQ(um, -500);
    digimatic(n, false, "010000", 4, 1); CHECK(ScaleProto_DigimaticToUm(n, &um)); CHECK_EQ(um, 25400);
    digimatic(n, false, "000001", 5, 1); CHECK(ScaleProto_DigimaticToUm(n, &um)); CHECK_EQ(um, 0);   // 0.25 мкм
    digimatic(n, false, "012345", 2, 0); n[0] = 0xE; CHECK(!ScaleProto_DigimaticToUm(n, &um));      // заголовок
    digimatic(n, false, "01A345", 2, 0); CHECK(!ScaleProto_DigimaticToUm(n, &um));                 // не BCD

    // Разбор 52-битного кадра на полубайты (бит 0 — первый принятый)
    uint64_t frame = 0;
    digimatic(n, true, "987654", 3, 0);
    for (int i = 0; i < 13; i++) frame |= (uint64_t)n[i] << (4 * i);
    uint8_t m[13];
    ScaleProto_SplitNibbles(frame, m);
    CHECK(ScaleProto_DigimaticToUm(m, &um)); CHECK_EQ(um, -987654);

    std::printf("%s: %d проверок, ошибок %d\n", g_fail ? "FAIL" : "OK", g_total, g_fail);
    return g_fail ? 1 : 0;
}
