// [STM32] Настройки во флеше (BACKLOG-028): последний сектор STM32F407VG — сектор 11, 128 КБ с 0x080E0000.
// Прошивка (~110 КБ) его не занимает, а загрузка через ST-Link стирает только сектора под прошивку —
// настройки переживают перепрошивку. Стирание сектора — 1–2 с, всё это время процессор стоит (код во
// флеше): сохранять только на остановленном станке (проверяет вызывающий).
#include <Arduino.h>
#include <string.h>

#define E2_STORE_ADDR   0x080E0000UL

bool e2_store_read(void* buf, uint32_t len)
{
   memcpy(buf, (const void*)E2_STORE_ADDR, len);
   return true;
}

bool e2_store_write(const void* buf, uint32_t len)
{
   if (len % 4) return false;
   HAL_FLASH_Unlock();
   __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
   FLASH_EraseInitTypeDef er = {};
   er.TypeErase    = FLASH_TYPEERASE_SECTORS;
   er.Sector       = FLASH_SECTOR_11;
   er.NbSectors    = 1;
   er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
   uint32_t bad = 0;
   bool ok = HAL_FLASHEx_Erase(&er, &bad) == HAL_OK;
   const uint32_t* w = (const uint32_t*)buf;
   for (uint32_t i = 0; ok && i < len / 4; i++)
      ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, E2_STORE_ADDR + i * 4, w[i]) == HAL_OK;
   HAL_FLASH_Lock();
   return ok && memcmp(buf, (const void*)E2_STORE_ADDR, len) == 0;
}
