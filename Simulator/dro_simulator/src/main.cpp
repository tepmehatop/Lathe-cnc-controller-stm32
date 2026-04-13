#include <Arduino.h>

/**
 * @file  dro_simulator.ino
 * @brief Симулятор DRO HS800-2 для тестирования STM32 ELS
 *
 * Запускается на Arduino Mega 2560 или любой другой плате с Serial1.
 * Генерирует корректные 29-байтовые пакеты HS800-2 на 57600/8N1.
 *
 * Подключение к STM32F4DISCOVERY:
 *   Arduino TX1 (pin 18)  →  PA3 (STM32 USART2 RX)
 *   GND                  →  GND
 *   (уровень 3.3В: добавить делитель 1кОм/2кОм если Arduino 5В!)
 *
 * Режимы симуляции (выбираются константой MODE):
 *   SIM_STATIC   — неподвижные координаты (проверка парсера)
 *   SIM_SWEEP_Y  — ось Y плавно движется туда/обратно
 *   SIM_SWEEP_XY — обе оси синусоидально
 *   SIM_BUTTON   — периодически нажимает кнопку BTN_A
 *
 * Протокол HS800-2:
 *   byte[0]    = 0xFE (SOF)
 *   byte[1..4] = Header (0x01, 0x00, 0x00, 0x00 или любые)
 *   byte[5]    = 0x00 (axis_id, обычно 0)
 *   byte[6..9] = X позиция, int32 Little-Endian, 0.001мм
 *   byte[10..13]= Y позиция, int32 Little-Endian, 0.001мм
 *   byte[14..17]= Z позиция (не используется ELS)
 *   byte[18]   = кнопки BTN_A (bit 0)
 *   byte[19]   = кнопки BTN_B (bit 0)
 *   byte[20..25]= зарезервированы (0x00)
 *   byte[26..27]= CRC16/MODBUS bytes[1..25]
 *   byte[28]   = 0xEF (EOF)
 */

#define PACKET_LEN  29
#define SIM_STATIC   0
#define SIM_SWEEP_Y  1
#define SIM_SWEEP_XY 2
#define SIM_BUTTON   3

// ---- ВЫБЕРИ РЕЖИМ ----
#define MODE  SIM_SWEEP_Y

// Forward declarations
static void send_packet(int32_t x, int32_t y, uint8_t btn_a, uint8_t btn_b);
static void debug_print(int32_t x, int32_t y);

// Интервал между пакетами (мс). HS800-2 отправляет ~50 пакетов/сек
#define PACKET_INTERVAL_MS  20

// ============================================================
// CRC16/MODBUS
// ============================================================
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

// ============================================================
// Построить и отправить пакет
// ============================================================
static void send_packet(int32_t x_001mm, int32_t y_001mm,
                        uint8_t btn_a, uint8_t btn_b) {
    uint8_t pkt[PACKET_LEN] = {0};

    pkt[0] = 0xFE;  // SOF
    pkt[1] = 0x01;
    pkt[2] = 0x00;
    pkt[3] = 0x00;
    pkt[4] = 0x00;
    pkt[5] = 0x00;  // axis_id

    // X позиция (byte 6..9, LE)
    pkt[6]  = (uint8_t)(x_001mm      );
    pkt[7]  = (uint8_t)(x_001mm >>  8);
    pkt[8]  = (uint8_t)(x_001mm >> 16);
    pkt[9]  = (uint8_t)(x_001mm >> 24);

    // Y позиция (byte 10..13, LE)
    pkt[10] = (uint8_t)(y_001mm      );
    pkt[11] = (uint8_t)(y_001mm >>  8);
    pkt[12] = (uint8_t)(y_001mm >> 16);
    pkt[13] = (uint8_t)(y_001mm >> 24);

    // Z = 0 (byte 14..17)
    // Кнопки
    pkt[18] = btn_a & 0x01;
    pkt[19] = btn_b & 0x01;
    // byte 20..25 = 0x00

    // CRC16/MODBUS bytes[1..25] → записываем в [26..27]
    uint16_t crc = crc16_modbus(&pkt[1], 25);
    pkt[26] = (uint8_t)(crc & 0xFF);
    pkt[27] = (uint8_t)(crc >> 8);

    pkt[28] = 0xEF;  // EOF

    Serial1.write(pkt, PACKET_LEN);
}

// ============================================================
// Вывод в Serial Monitor (для отладки самого симулятора)
// ============================================================
static void debug_print(int32_t x, int32_t y) {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 500) return;
    last = now;

    Serial.print("SIM X=");
    Serial.print(x / 1000L);
    Serial.print(".");
    int32_t ax = abs(x % 1000L);
    if (ax < 100) Serial.print("0");
    if (ax < 10)  Serial.print("0");
    Serial.print(ax);
    Serial.print("mm  Y=");
    Serial.print(y / 1000L);
    Serial.print(".");
    int32_t ay = abs(y % 1000L);
    if (ay < 100) Serial.print("0");
    if (ay < 10)  Serial.print("0");
    Serial.print(ay);
    Serial.println("mm");
}

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);   // Debug output
    Serial1.begin(57600);   // DRO output → STM32 PA3

    Serial.println("[SIM] DRO HS800-2 Simulator started");
    Serial.print("[SIM] Mode: ");
    Serial.println(MODE);
    Serial.println("[SIM] Connect Serial1 TX (pin 18) -> STM32 PA3");
}

// ============================================================
// Loop
// ============================================================
static int32_t s_phase = 0;    // фаза анимации (× 100 для целочисленного sin)

void loop() {
    static uint32_t last_pkt = 0;
    uint32_t now = millis();
    if ((now - last_pkt) < PACKET_INTERVAL_MS) return;
    last_pkt = now;

    int32_t x_pos = 0;
    int32_t y_pos = 0;
    uint8_t btn_a = 0;
    uint8_t btn_b = 0;

    s_phase += 1;  // Инкремент каждые PACKET_INTERVAL_MS мс

#if MODE == SIM_STATIC
    // Статические координаты для проверки парсера
    x_pos = 12345;    //  12.345 мм
    y_pos = -67890;   // -67.890 мм

#elif MODE == SIM_SWEEP_Y
    // Ось Y: движение 0..100мм и обратно за ~4 секунды
    // period = 4000ms / 20ms = 200 шагов
    int32_t period = 200;
    int32_t half   = period / 2;
    int32_t phase  = (int32_t)(s_phase % period);
    if (phase < half) {
        y_pos = (int32_t)((int64_t)phase * 100000L / half); // 0..100.000 мм
    } else {
        y_pos = (int32_t)((int64_t)(period - phase) * 100000L / half);
    }
    x_pos = 5000;  // X фиксировано 5.000 мм

#elif MODE == SIM_SWEEP_XY
    // Обе оси синусоидально (аппроксимация через triangle wave)
    int32_t period = 300; // ~6 секунд
    int32_t hp = period / 2;
    int32_t qp = period / 4;
    int32_t ph_y = (int32_t)(s_phase % period);
    int32_t ph_x = (int32_t)((s_phase + qp) % period); // сдвиг 90°

    // Triangle wave для Y
    if (ph_y < hp)
        y_pos = (int32_t)((int64_t)ph_y * 50000L / hp);
    else
        y_pos = (int32_t)((int64_t)(period - ph_y) * 50000L / hp);

    // Triangle wave для X
    if (ph_x < hp)
        x_pos = (int32_t)((int64_t)ph_x * 30000L / hp);
    else
        x_pos = (int32_t)((int64_t)(period - ph_x) * 30000L / hp);

    // Добавляем отрицательные значения для проверки знака
    y_pos -= 25000;   // -25..+25 мм
    x_pos -= 15000;   // -15..+15 мм

#elif MODE == SIM_BUTTON
    // Каждые 2 секунды нажимаем кнопку на 200мс
    y_pos = 0;
    x_pos = 0;
    uint32_t t_mod = (uint32_t)(now % 2000);
    btn_a = (t_mod < 200) ? 1 : 0;

#endif

    send_packet(x_pos, y_pos, btn_a, btn_b);
    debug_print(x_pos, y_pos);
}
