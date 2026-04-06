#pragma once
#include <stdint.h>

// Протокол HS800-2:
//   UART 57600/8N1, пакет 29 байт
//   byte[0]     = 0xFE  (SOF)
//   byte[2]     = 0x33  (тип)
//   byte[6..9]  = X позиция, int32 LE, 0.001 мм
//   byte[10..13]= Y позиция, int32 LE, 0.001 мм
//   byte[18]    = кнопки (маска)
//   byte[19]    = кнопки B (маска)
//   byte[26..27]= CRC16/MODBUS от bytes[1..25]
//   byte[28]    = 0xEF  (EOF)

#define DRO_PACKET_LEN      29
#define DRO_SOF             0xFE
#define DRO_EOF             0xEF
#define DRO_TYPE_POS        0x33

void    DRV_DRO_Init(void);
void    DRV_DRO_Process(void);     // Вызывать из loop()
int32_t DRV_DRO_GetX(void);        // Позиция X, 0.001 мм
int32_t DRV_DRO_GetY(void);        // Позиция Y, 0.001 мм
uint8_t DRV_DRO_GetBtn(void);      // Кнопки byte[18]
uint8_t DRV_DRO_GetBtnB(void);     // Кнопки byte[19]
uint8_t DRV_DRO_IsNewPacket(void); // 1 если с прошлого вызова пришёл пакет
uint32_t DRV_DRO_GetPacketCount(void); // Общее число принятых пакетов
uint32_t DRV_DRO_GetErrorCount(void);  // Число пакетов с ошибкой CRC
