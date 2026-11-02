#pragma once
/**
 * Минимальная имитация Arduino/STM32duino для проверки ядра 7e2 на компьютере.
 * Регистры GPIO и таймеров — обычные структуры в памяти; время — виртуальное
 * (sim_now_us), его двигает симулятор (sim.cpp).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <deque>
#include <algorithm>
#include "binary.h"

typedef uint8_t byte;
typedef bool boolean;

#define F(s) (s)
#define HEX 16
#define DEC 10
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define INPUT_ANALOG 3
#define CHANGE 1
#define TICK_FORMAT 0

template <class T, class L, class H> static inline T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ── Время и задержки ────────────────────────────────────────────────────────
extern uint64_t sim_now_us;
void sim_delay_us(uint64_t us);     // двигает время, прерывания продолжают срабатывать
static inline uint32_t millis() { return (uint32_t)(sim_now_us / 1000); }
static inline uint32_t micros() { return (uint32_t)sim_now_us; }
static inline void delay(uint32_t ms) { sim_delay_us((uint64_t)ms * 1000); }
static inline void delayMicroseconds(uint32_t us) { sim_delay_us(us); }

// ── GPIO ─────────────────────────────────────────────────────────────────────
struct GPIO_TypeDef;
struct BsrrReg {
    void operator=(uint32_t v);
};
struct GPIO_TypeDef {
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    BsrrReg BSRR;
};
inline void BsrrReg::operator=(uint32_t v) {
    GPIO_TypeDef* g = reinterpret_cast<GPIO_TypeDef*>(reinterpret_cast<char*>(this) - offsetof(GPIO_TypeDef, BSRR));
    g->ODR = (g->ODR | (v & 0xFFFF)) & ~(v >> 16);
}
extern GPIO_TypeDef g_gpio[5];
#define GPIOA (&g_gpio[0])
#define GPIOB (&g_gpio[1])
#define GPIOC (&g_gpio[2])
#define GPIOD (&g_gpio[3])
#define GPIOE (&g_gpio[4])

// Номера выводов: порт*16 + бит
#define MKPIN(p, n) ((p) * 16 + (n))
enum {
    PA0 = MKPIN(0,0), PA1, PA2, PA3, PA4, PA5, PA6, PA7, PA8, PA9, PA10, PA11, PA12, PA13, PA14, PA15,
    PB0, PB1, PB2, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PB10, PB11, PB12, PB13, PB14, PB15,
    PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PC8, PC9, PC10, PC11, PC12, PC13, PC14, PC15,
    PD0, PD1, PD2, PD3, PD4, PD5, PD6, PD7, PD8, PD9, PD10, PD11, PD12, PD13, PD14, PD15,
    PE0, PE1, PE2, PE3, PE4, PE5, PE6, PE7, PE8, PE9, PE10, PE11, PE12, PE13, PE14, PE15,
};
#define A7 PC1

void pinMode(int pin, int mode);
int  analogRead(int pin);
static inline int digitalPinToInterrupt(int p) { return p; }
void attachInterrupt(int irq, void (*cb)(void), int mode);

// ── Таймеры ──────────────────────────────────────────────────────────────────
struct TIM_TypeDef { volatile uint32_t ARR, CNT, SR, CR1; };
extern TIM_TypeDef g_tim6, g_tim7, g_tim12;
#define TIM6  (&g_tim6)
#define TIM7  (&g_tim7)
#define TIM12 (&g_tim12)
#define TIM_CR1_ARPE 0x80u
#define TIM_SR_UIF   0x01u
enum { EXTI1_IRQn = 7, EXTI9_5_IRQn = 23 };
static inline void HAL_NVIC_SetPriority(int, int, int) {}

// ── String ───────────────────────────────────────────────────────────────────
class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& x) : s(x) {}
    String(char c) : s(1, c) {}
    String(int v) : s(std::to_string(v)) {}
    String(long v) : s(std::to_string(v)) {}
    unsigned length() const { return (unsigned)s.size(); }
    char charAt(unsigned i) const { return i < s.size() ? s[i] : 0; }
    int indexOf(char c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const char* c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    String substring(unsigned a) const { return a >= s.size() ? String() : String(s.substr(a)); }
    String substring(unsigned a, unsigned b) const { if (a > s.size()) return String(); if (b > s.size()) b = (unsigned)s.size(); return b > a ? String(s.substr(a, b - a)) : String(); }
    long toInt() const { return atol(s.c_str()); }
    bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
    bool startsWith(const String& p) const { return s.rfind(p.s, 0) == 0; }
    void toUpperCase() { for (auto& c : s) c = (char)toupper((unsigned char)c); }
    void trim() { size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n"); s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1); }
    const char* c_str() const { return s.c_str(); }
    String& operator+=(char c) { s += c; return *this; }
    String& operator+=(const char* c) { s += c; return *this; }
    String& operator+=(const String& c) { s += c.s; return *this; }
    bool operator==(const char* c) const { return s == c; }
    bool operator==(const String& c) const { return s == c.s; }
    bool operator!=(const char* c) const { return s != c; }
};

// ── Последовательные порты ───────────────────────────────────────────────────
class HardwareSerial {
public:
    HardwareSerial() {}
    HardwareSerial(int, int) {}
    std::string out;               // всё, что прошивка отправила
    std::deque<char> in;           // что прошивке предстоит прочитать
    void begin(unsigned long) {}
    int  available() { return (int)in.size(); }
    int  read() { if (in.empty()) return -1; char c = in.front(); in.pop_front(); return (uint8_t)c; }
    int  availableForWrite() { return 4096; }
    void inject(const std::string& s) { for (char c : s) in.push_back(c); }

    size_t write(uint8_t c) { out += (char)c; return 1; }
    size_t print(const char* x) { out += x; return strlen(x); }
    size_t print(const String& x) { out += x.s; return x.length(); }
    size_t print(char c) { out += c; return 1; }
    size_t print(long v, int base = DEC) { char b[24]; snprintf(b, sizeof b, base == HEX ? "%lX" : "%ld", v); out += b; return strlen(b); }
    size_t print(int v, int base = DEC) { return print((long)v, base); }
    size_t print(unsigned long v, int base = DEC) { return print((long)v, base); }
    size_t print(unsigned v, int base = DEC) { return print((long)v, base); }
    size_t print(uint8_t v, int base = DEC) { return print((long)v, base); }
    size_t print(double v, int digits = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", digits, v); out += b; return strlen(b); }
    size_t println() { out += "\r\n"; return 2; }
    template <class T> size_t println(T v) { size_t n = print(v); return n + println(); }
    template <class T> size_t println(T v, int b) { size_t n = print(v, b); return n + println(); }
    size_t printf(const char* fmt, ...) { char b[256]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); out += b; return strlen(b); }
};
extern HardwareSerial Serial;
