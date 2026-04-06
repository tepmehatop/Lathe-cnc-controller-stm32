/**
 * @file  drv_dro.cpp
 * @brief Драйвер DRO HS800-2 — Этап 2
 *
 * USART2, PA3=RX (только приём), 57600/8N1
 * Прерывание-driven (HardwareSerial STM32duino) — CPU не блокируется.
 *
 * Логика парсинга:
 *   1. Читаем байты из UART в кольцевой rx-буфер
 *   2. Ищем SOF (0xFE) — начало пакета
 *   3. Накапливаем 29 байт
 *   4. Проверяем EOF (byte[28]) и TYPE (byte[2])
 *   5. Считаем CRC16/MODBUS от bytes[1..25], сравниваем с bytes[26..27]
 *   6. Извлекаем X/Y (int32 LE) и кнопки
 */

#include "drv_dro.h"
#include "../Core/els_config.h"
#include <Arduino.h>

#if USE_DRO_HS800

// ============================================================
// USART2 через STM32duino HardwareSerial
// PA3 = RX, PA2 = TX (TX не используется, но нужен для объекта)
// ============================================================
static HardwareSerial DROSerial(PA3, PA2);

// ============================================================
// Буфер и парсер
// ============================================================
#define RX_BUF_SIZE  64   // Должен быть кратен 2 и > DRO_PACKET_LEN

static uint8_t  s_rxbuf[RX_BUF_SIZE];
static uint8_t  s_rxlen = 0;         // Сколько байт накоплено
static uint8_t  s_synced = 0;        // 1 = нашли SOF, ждём пакет

// Результаты последнего валидного пакета
static int32_t  s_pos_x = 0;
static int32_t  s_pos_y = 0;
static uint8_t  s_btn   = 0;
static uint8_t  s_btn_b = 0;
static uint8_t  s_new   = 0;

static uint32_t s_pkt_ok  = 0;
static uint32_t s_pkt_err = 0;

// ============================================================
// CRC16/MODBUS
// ============================================================
static uint16_t crc16_modbus(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ============================================================
// Парсер одного полного пакета
// ============================================================
static void parse_packet(const uint8_t* p) {
    // Проверяем маркеры
    if (p[0] != DRO_SOF || p[28] != DRO_EOF) {
        s_pkt_err++;
        return;
    }
    // TYPE — не критично для работы, но логируем
    // if (p[2] != DRO_TYPE_POS) { ... }

    // CRC: bytes[1..25] → 25 байт
    uint16_t crc_calc = crc16_modbus(p + 1, 25);
    uint16_t crc_recv = (uint16_t)p[26] | ((uint16_t)p[27] << 8);
    if (crc_calc != crc_recv) {
        s_pkt_err++;
        return;
    }

    // X: bytes[6..9], int32 LE
    s_pos_x = (int32_t)(
        (uint32_t)p[6]        |
        ((uint32_t)p[7] << 8) |
        ((uint32_t)p[8] << 16)|
        ((uint32_t)p[9] << 24)
    );

    // Y: bytes[10..13], int32 LE
    s_pos_y = (int32_t)(
        (uint32_t)p[10]        |
        ((uint32_t)p[11] << 8) |
        ((uint32_t)p[12] << 16)|
        ((uint32_t)p[13] << 24)
    );

    s_btn   = p[18];
    s_btn_b = p[19];
    s_new   = 1;
    s_pkt_ok++;
}

// ============================================================
// API
// ============================================================
void DRV_DRO_Init(void) {
    DROSerial.begin(DRO_UART_BAUD);
    s_rxlen  = 0;
    s_synced = 0;
    s_new    = 0;
    s_pkt_ok = s_pkt_err = 0;
}

void DRV_DRO_Process(void) {
    s_new = 0;

    // Читаем все доступные байты за этот вызов loop()
    while (DROSerial.available()) {
        uint8_t b = (uint8_t)DROSerial.read();

        if (!s_synced) {
            // Ищем SOF
            if (b == DRO_SOF) {
                s_rxbuf[0] = b;
                s_rxlen    = 1;
                s_synced   = 1;
            }
            continue;
        }

        // Накапливаем байты
        if (s_rxlen < DRO_PACKET_LEN) {
            s_rxbuf[s_rxlen++] = b;
        }

        if (s_rxlen == DRO_PACKET_LEN) {
            // Проверяем EOF — если нет, ищем следующий SOF
            if (s_rxbuf[DRO_PACKET_LEN - 1] != DRO_EOF) {
                // Попытка ресинхронизации: ищем SOF внутри буфера
                s_synced = 0;
                s_rxlen  = 0;
                for (uint8_t i = 1; i < DRO_PACKET_LEN; i++) {
                    if (s_rxbuf[i] == DRO_SOF) {
                        // Копируем остаток в начало буфера
                        s_rxlen = DRO_PACKET_LEN - i;
                        for (uint8_t j = 0; j < s_rxlen; j++) {
                            s_rxbuf[j] = s_rxbuf[i + j];
                        }
                        s_synced = 1;
                        break;
                    }
                }
                s_pkt_err++;
            } else {
                parse_packet(s_rxbuf);
                // Готовы к следующему пакету
                s_synced = 0;
                s_rxlen  = 0;
            }
        }
    }
}

int32_t  DRV_DRO_GetX(void)            { return s_pos_x; }
int32_t  DRV_DRO_GetY(void)            { return s_pos_y; }
uint8_t  DRV_DRO_GetBtn(void)          { return s_btn; }
uint8_t  DRV_DRO_GetBtnB(void)         { return s_btn_b; }
uint8_t  DRV_DRO_IsNewPacket(void)     { return s_new; }
uint32_t DRV_DRO_GetPacketCount(void)  { return s_pkt_ok; }
uint32_t DRV_DRO_GetErrorCount(void)   { return s_pkt_err; }

#else
// USE_DRO_HS800 = 0 — всё пустые стабы
void     DRV_DRO_Init(void)            {}
void     DRV_DRO_Process(void)         {}
int32_t  DRV_DRO_GetX(void)            { return 0; }
int32_t  DRV_DRO_GetY(void)            { return 0; }
uint8_t  DRV_DRO_GetBtn(void)          { return 0; }
uint8_t  DRV_DRO_GetBtnB(void)         { return 0; }
uint8_t  DRV_DRO_IsNewPacket(void)     { return 0; }
uint32_t DRV_DRO_GetPacketCount(void)  { return 0; }
uint32_t DRV_DRO_GetErrorCount(void)   { return 0; }
#endif
