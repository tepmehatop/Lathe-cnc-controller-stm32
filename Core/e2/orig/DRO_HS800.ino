// DRO HS800-2 — чтение позиций через Serial2 (pin 17 RX)
// Активен только при #define USE_DRO_HS800 в 7e2_Mod_LCD4.3_v1.3.ino
//
// ПОДКЛЮЧЕНИЕ: HS800-2 STM32 TX → Mega pin 17 (RX2), GND → GND
// ПРОТОКОЛ: 57600/8N1, пакет 0x33, 29 байт, CRC16/MODBUS
//   byte[6][7]   = X позиция (int16 LE, 0.001мм)
//   byte[10][11] = Y позиция (int16 LE, 0.001мм)
//   byte[18][19] = кнопка (0x00 0x00 = нет)

#ifdef USE_DRO_HS800

#define DRO_BAUD    57600
#define DRO_PKT_LEN 29
#define DRO_SOF     0xFE
#define DRO_EOF     0xEF
#define DRO_MAXPKT  32

// Знак осей: 1 = прямой, -1 = инвертированный
#define DRO_X_SIGN  1
#define DRO_Y_SIGN  1

// Публичные переменные (в 0.001мм)
int32_t dro_pos_x = 0;
int32_t dro_pos_y = 0;
bool    dro_btn_new  = false;
uint8_t dro_btn_b18  = 0;
uint8_t dro_btn_b19  = 0;

static uint8_t  s_buf[DRO_MAXPKT];
static uint8_t  s_len   = 0;
static bool     s_frame = false;
static uint32_t s_last_ms = 0;

static uint16_t _crc16(const uint8_t* d, uint8_t n) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

static void _handle(const uint8_t* p, uint8_t len) {
    if (len != 29) return;
    if (p[0] != DRO_SOF || p[28] != DRO_EOF) return;
    if (p[1] != 0x33) return;

    uint16_t crc_calc = _crc16(&p[1], 25);
    uint16_t crc_recv = p[26] | ((uint16_t)p[27] << 8);
    if (crc_calc != crc_recv) {
        Serial.println(F("DRO CRC ERR"));
        return;
    }

    // Позиции закодированы как int32 LE (4 байта): bytes[6..9]=X, bytes[10..13]=Y
    // int16 давал переполнение при значениях > 32.767мм
    dro_pos_x = (int32_t)((uint32_t)p[6]  | ((uint32_t)p[7]  << 8) | ((uint32_t)p[8]  << 16) | ((uint32_t)p[9]  << 24)) * DRO_X_SIGN;
    dro_pos_y = (int32_t)((uint32_t)p[10] | ((uint32_t)p[11] << 8) | ((uint32_t)p[12] << 16) | ((uint32_t)p[13] << 24)) * DRO_Y_SIGN;

    uint8_t b18 = p[18], b19 = p[19];
    if ((b18 || b19) && (b18 != dro_btn_b18 || b19 != dro_btn_b19)) {
        dro_btn_b18 = b18; dro_btn_b19 = b19; dro_btn_new = true;
        Serial.print(F("DRO BTN: 0x")); Serial.print(b18, HEX);
        Serial.print(' '); Serial.println(b19, HEX);
    } else if (!b18 && !b19) {
        dro_btn_b18 = dro_btn_b19 = 0;
    }

    static int32_t lx = 99999, ly = 99999;
    if (dro_pos_x != lx || dro_pos_y != ly) {
        lx = dro_pos_x; ly = dro_pos_y;
        Serial.print(F("DRO X=")); Serial.print(dro_pos_x);
        Serial.print(F(" Y="));   Serial.println(dro_pos_y);
    }

    s_last_ms = millis();
}

void DRO_Init() {
    Serial2.begin(DRO_BAUD);
    Serial.println(F("DRO HS800-2: Serial2 pin17 @ 57600"));
}

void DRO_Process() {
    dro_btn_new = false;
    while (Serial2.available()) {
        uint8_t b = Serial2.read();
        if (b == DRO_SOF && !s_frame) {
            s_buf[0] = b; s_len = 1; s_frame = true;
        } else if (s_frame) {
            if (s_len < DRO_MAXPKT) s_buf[s_len++] = b;
            if (s_len == DRO_PKT_LEN) {
                _handle(s_buf, s_len);
                s_len = 0; s_frame = false;
            } else if (s_len >= DRO_MAXPKT) {
                s_len = 0; s_frame = false;
            }
        }
    }
}

bool DRO_IsConnected() {
    return (s_last_ms > 0) && (millis() - s_last_ms < 500);
}

#endif // USE_DRO_HS800
