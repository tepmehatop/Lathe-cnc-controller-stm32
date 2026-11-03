#include <Arduino.h>

/**
 * @file  main.cpp
 * @brief Симулятор DRO для тестирования STM32 ELS без станка и без линеек.
 *        Arduino Mega 2560.
 *
 * Что умеет:
 *   1. Блок DRO HS800-2: 29-байтовые пакеты по UART 57600 (Serial1 TX pin 18 → PA3).
 *      Работает всегда, в любом режиме.
 *   2. Эмуляция линеек, подключённых к STM32 напрямую (ELS_POS_SOURCE = SCALES):
 *        q — квадратура A/B (стекло/магнит), 5 мкм на импульс
 *        s — Sylvac 48 бит (ёмкостные китайские), такт от «линейки»
 *        b — BIN6 24 бит, такт от «линейки»
 *        i — iGaging 21 бит, такт от STM32
 *        d — Mitutoyo Digimatic, запрос REQ от STM32
 *        0 — эмуляция линеек выключена (пины — входы)
 *
 * Подключение к STM32F4DISCOVERY (уровни Mega 5 В — пины STM32 толерантны к 5 В,
 * для надёжности через резисторы 1 кОм):
 *   Mega D22 → PA15 (ось Z, A/такт)     Mega D24 → PB4 (ось X, A/такт)
 *   Mega D23 → PB3  (ось Z, B/данные)   Mega D25 → PB5 (ось X, B/данные)
 *   Mega D26 ← PC6  (ось Z, REQ)        Mega D27 ← PC7 (ось X, REQ)
 *   Mega TX1 (18) → PA3 (HS800 по UART), GND → GND
 *   Без схемы сопряжения: в els_config.h SCALE_*_SIGNAL_INVERT = 0, а REQ
 *   Digimatic подаётся напрямую (активный уровень STM32 — высокий).
 *
 * Команды по USB (115200, строка + Enter):
 *   m<буква>      — режим эмуляции линеек (mq, ms, mb, mi, md, m0)
 *   z<мкм>        — позиция оси Z (каретка), например z-12345 = −12.345 мм
 *   x<мкм>        — позиция оси X (суппорт)
 *   w<0|1>        — 1 = «качание» позиций (треугольник), 0 = стоят на месте
 *   ?             — текущее состояние
 * В ответ печатается строка [SIM] mode=… z=… x=… — её читают автотесты.
 */

// ============================================================
// Пины эмуляции линеек
// ============================================================
struct AxisPins { uint8_t a, b, req; };
static const AxisPins PINS[2] = { {22, 23, 26}, {24, 25, 27} };   // 0 = Z, 1 = X

// ============================================================
// Состояние
// ============================================================
static char     s_mode  = '0';          // режим эмуляции линеек
// Позиции, мкм (0 = Z, 1 = X) — ровно то, что покажет дисплей при конфиге по умолчанию
// (SCALE_*_INVERT = 0): ядро 7e2 выводит показания блока DRO / линеек как есть.
// HS800: Y = z, X = x.
static int32_t  s_pos_um[2] = {0, 0};
static int32_t scale_um(uint8_t ax) { return s_pos_um[ax]; }
static bool     s_sweep = true;    // по умолчанию позиции «качаются», как в старом симуляторе
static int32_t  s_quad_count[2] = {0, 0};
static const int32_t QUAD_NM_PER_COUNT = 5000;   // 5 мкм

// ============================================================
// HS800-2 (UART)
// ============================================================
#define PACKET_LEN  29
#define PACKET_INTERVAL_MS 20

static uint16_t crc16_modbus(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

static void put_le32(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// HS800: byte[6..9] = X, byte[10..13] = Y
static void send_hs800(int32_t x_um, int32_t y_um) {
    uint8_t pkt[PACKET_LEN] = {0};
    pkt[0] = 0xFE;
    pkt[1] = 0x01;
    put_le32(&pkt[6],  x_um);
    put_le32(&pkt[10], y_um);
    uint16_t crc = crc16_modbus(&pkt[1], 25);
    pkt[26] = (uint8_t)crc;
    pkt[27] = (uint8_t)(crc >> 8);
    pkt[28] = 0xEF;
    Serial1.write(pkt, PACKET_LEN);
}

// ============================================================
// Кадры протоколов (те же форматы, что разбирает Drivers/scale_protocols.cpp)
// ============================================================
static uint64_t frame_sylvac48(int32_t um) {
    int32_t counts = (int32_t)(((int64_t)um * 20480 + (um >= 0 ? 12700 : -12700)) / 25400);
    uint64_t w = (uint32_t)counts & 0xFFFFFFUL;
    return w | (w << 24);
}

static uint32_t frame_bin6(int32_t um) {
    int32_t v = (um >= 0 ? um + 5 : um - 5) / 10;          // 0.01 мм
    uint32_t a = (uint32_t)(v < 0 ? -v : v) & 0xFFFFFUL;
    return a | (v < 0 ? (1UL << 20) : 0);
}

static uint32_t frame_igaging21(int32_t um) {
    int32_t counts = (int32_t)(((int64_t)um * 2560 + (um >= 0 ? 12700 : -12700)) / 25400);
    return (uint32_t)counts & 0x1FFFFFUL;
}

static uint64_t frame_digimatic(int32_t um) {
    // мм, 3 знака после точки: 6 цифр BCD = |um|
    uint8_t n[13];
    for (uint8_t i = 0; i < 4; i++) n[i] = 0xF;
    n[4] = um < 0 ? 8 : 0;
    uint32_t v = (uint32_t)(um < 0 ? -um : um);
    for (int8_t i = 10; i >= 5; i--) { n[i] = v % 10; v /= 10; }
    n[11] = 3;
    n[12] = 0;
    uint64_t f = 0;
    for (uint8_t i = 0; i < 13; i++) f |= (uint64_t)n[i] << (4 * i);
    return f;
}

// Выдать кадр с тактом от «линейки»: такт в покое высокий, данные выставляются
// до спада, STM32 читает на спаде (без инверсии в схеме сопряжения).
static void clock_out_frame(uint8_t axis, uint64_t frame, uint8_t bits) {
    const AxisPins& p = PINS[axis];
    for (uint8_t i = 0; i < bits; i++) {
        digitalWrite(p.b, (frame >> i) & 1 ? HIGH : LOW);
        delayMicroseconds(4);
        digitalWrite(p.a, LOW);          // спад — STM32 читает бит
        delayMicroseconds(6);
        digitalWrite(p.a, HIGH);
        delayMicroseconds(6);
    }
}

// ============================================================
// Режимы
// ============================================================
static void apply_mode(char m) {
    for (uint8_t ax = 0; ax < 2; ax++) {
        const AxisPins& p = PINS[ax];
        pinMode(p.a, INPUT); pinMode(p.b, INPUT); pinMode(p.req, INPUT);
        switch (m) {
            case 'q':
                pinMode(p.a, OUTPUT); pinMode(p.b, OUTPUT);
                s_quad_count[ax] = 0;            // STM32 считает от нуля при включении
                digitalWrite(p.a, LOW); digitalWrite(p.b, LOW);
                break;
            case 's': case 'b':
                pinMode(p.a, OUTPUT); pinMode(p.b, OUTPUT);
                digitalWrite(p.a, HIGH);
                break;
            case 'i':                            // такт — вход от STM32
                pinMode(p.b, OUTPUT);
                break;
            case 'd':                            // REQ — вход от STM32
                pinMode(p.a, OUTPUT); pinMode(p.b, OUTPUT);
                digitalWrite(p.a, HIGH);
                break;
        }
    }
    s_mode = m;
}

// Квадратура: один шаг кода Грея к цели. Рост счёта — A опережает B (AB: 00 → 10 → 11 → 01),
// как у линеек и энкодеров; так же считает таймер STM32 в режиме энкодера
static void quad_step(uint8_t ax) {
    int32_t target = (int32_t)(((int64_t)scale_um(ax) * 1000) / QUAD_NM_PER_COUNT);
    if (s_quad_count[ax] == target) return;
    s_quad_count[ax] += (target > s_quad_count[ax]) ? 1 : -1;
    static const uint8_t GRAY[4] = {0b00, 0b10, 0b11, 0b01};   // бит 1 — A, бит 0 — B
    uint8_t g = GRAY[(uint8_t)(s_quad_count[ax] & 3)];
    digitalWrite(PINS[ax].a, (g >> 1) & 1);
    digitalWrite(PINS[ax].b, g & 1);
}

static void print_state() {
    Serial.print("[SIM] mode=");  Serial.print(s_mode);
    Serial.print(" z=");          Serial.print(s_pos_um[0]);
    Serial.print(" x=");          Serial.print(s_pos_um[1]);
    Serial.print(" sweep=");      Serial.println(s_sweep ? 1 : 0);
}

static void handle_command(const char* c) {
    switch (c[0]) {
        case 'm':
            apply_mode(c[1] ? c[1] : '0');
            break;
        case 'z': s_pos_um[0] = atol(c + 1); break;
        case 'x': s_pos_um[1] = atol(c + 1); break;
        case 'w': s_sweep = (c[1] == '1'); break;
    }
    print_state();
}

// ============================================================
// Setup / loop
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial1.begin(57600);
    apply_mode('0');
    Serial.println("[SIM] DRO simulator: HS800 UART + эмуляция линеек (команды: m<q|s|b|i|d|0>, z<мкм>, x<мкм>, w<0|1>, ?)");
    print_state();
}

void loop() {
    // Команды по USB
    static char buf[24];
    static uint8_t len = 0;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (len) { buf[len] = 0; handle_command(buf); len = 0; }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = ch;
        }
    }

    uint32_t now = millis();

    // Качание позиций через ноль, чтобы на дисплее были и плюс, и минус:
    // Z −50..+50 мм за 4 с, X −5..+5 мм
    if (s_sweep) {
        uint32_t ph = now % 4000;
        int32_t tri = (int32_t)(ph < 2000 ? ph : 4000 - ph);    // 0..2000
        s_pos_um[0] = tri * 50 - 50000;   // −50000..+50000 мкм
        s_pos_um[1] = tri * 5 - 5000;     // −5000..+5000 мкм
    }

    // HS800 — всегда
    static uint32_t t_pkt = 0;
    if (now - t_pkt >= PACKET_INTERVAL_MS) {
        t_pkt = now;
        send_hs800(s_pos_um[1], s_pos_um[0]);
    }

    // Эмуляция линеек
    static uint32_t t_frame = 0;
    switch (s_mode) {
        case 'q':
            quad_step(0);
            quad_step(1);
            break;
        case 's':
        case 'b':
            if (now - t_frame >= 20) {      // 50 Гц, как «быстрый» режим линеек
                t_frame = now;
                for (uint8_t ax = 0; ax < 2; ax++) {
                    if (s_mode == 's') clock_out_frame(ax, frame_sylvac48(scale_um(ax)), 48);
                    else               clock_out_frame(ax, frame_bin6(scale_um(ax)), 24);
                }
            }
            break;
        case 'i': {
            // Такт от STM32 (высокий 20 мкс, бит 100 мкс, чтение на спаде, кадр раз в 20 мс) — D22/D24
            // не прерывания на Mega, поэтому кадр ловим целиком в коротком цикле на прямом чтении порта
            // (D22 = PA0, D24 = PA2): ждём паузу ≥ 1.2 мс (граница кадров), затем 21 бит каждой оси.
            // Кадры считаются ДО цикла: 64-битная арифметика на AVR дольше 20 мкс.
            const uint8_t CLK = _BV(0) | _BV(2);
            uint32_t fr[2] = { frame_igaging21(scale_um(0)), frame_igaging21(scale_um(1)) };
            uint8_t prev = PINA;
            uint32_t t_start = micros(), t_idle = t_start;
            while (micros() - t_idle < 1200 && micros() - t_start < 40000) {
                uint8_t p = PINA;
                if (p & ~prev & CLK) t_idle = micros();
                prev = p;
            }
            uint8_t nbit[2] = {0, 0};
            t_start = micros();
            while ((nbit[0] < 21 || nbit[1] < 21) && micros() - t_start < 30000) {
                uint8_t p = PINA, rise = p & ~prev & CLK;
                prev = p;
                if (!rise) continue;
                for (uint8_t ax = 0; ax < 2; ax++) {
                    if (!(rise & _BV(ax * 2))) continue;
                    uint8_t k = nbit[ax]++;
                    uint8_t bit = _BV(ax * 2 + 1);                    // D23 = PA1, D25 = PA3
                    if (k < 21 && ((fr[ax] >> k) & 1)) PORTA |= bit; else PORTA &= ~bit;
                }
            }
            break;
        }
        case 'd':
            for (uint8_t ax = 0; ax < 2; ax++) {
                if (digitalRead(PINS[ax].req) == HIGH) {   // запрос от STM32
                    clock_out_frame(ax, frame_digimatic(scale_um(ax)), 52);
                    while (digitalRead(PINS[ax].req) == HIGH && millis() - now < 50) {}
                }
            }
            break;
    }
}
