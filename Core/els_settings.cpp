/**
 * @file  els_settings.cpp
 * @brief Сохранение/загрузка настроек в Flash — Этап 22
 *
 * Sector 11 (0x080E0000, 128KB) — последний сектор STM32F407.
 * Firmware ~53KB (<< 64KB = начало sector 4), запись безопасна.
 *
 * Формат: ELS_FlashSettings_t с magic 0xEL5C0FFE.
 * Дебаунс: запись через 1000мс после последнего MarkDirty().
 */

#include "els_settings.h"
#include "els_state.h"
#include "els_config.h"
#include <Arduino.h>
#include <stm32f4xx_hal.h>

// ── Flash sector 11 ──────────────────────────────────────────────────────────
#define SETTINGS_FLASH_SECTOR   FLASH_SECTOR_11
#define SETTINGS_FLASH_ADDR     0x080E0000UL
#define SETTINGS_MAGIC          0xE15C0FFEUL
#define SETTINGS_SAVE_DEBOUNCE  1000   // мс

// ── Структура настроек ───────────────────────────────────────────────────────
typedef struct {
    uint32_t magic;

    // Подача
    uint16_t Feed_mm;
    uint16_t aFeed_mm;

    // Резьба
    uint8_t  Thread_Step;
    int16_t  Ph;
    int16_t  Pass_Fin;

    // Конус
    uint8_t  Cone_Step;

    // Цикл
    int32_t  Pass_Total;
    int16_t  Ap;

    // Сфера
    int32_t  Sph_R_mm;
    int32_t  Bar_R_mm;
    int32_t  Pass_Total_Sphr;
    uint8_t  Cutter_Step;
    uint8_t  Cutting_Step;

    // Делитель
    uint8_t  Total_Tooth;

    // Подрежимы
    uint8_t  sub_feed;
    uint8_t  sub_afeed;
    uint8_t  sub_thread;
    uint8_t  sub_cone;
    uint8_t  sub_sphere;

    // Padding to make size word-aligned (4 bytes)
    uint8_t  _pad[2];

} ELS_FlashSettings_t;

static_assert(sizeof(ELS_FlashSettings_t) % 4 == 0,
    "ELS_FlashSettings_t size must be multiple of 4");

// ── Модуль state ────────────────────────────────────────────────────────────
static bool     s_dirty   = false;
static uint32_t s_dirty_t = 0;

// ── Запись в Flash ───────────────────────────────────────────────────────────
static void _flash_write(const ELS_FlashSettings_t* cfg) {
    HAL_FLASH_Unlock();

    // Стираем sector 11
    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;   // VDD 2.7–3.6V
    erase.Sector       = SETTINGS_FLASH_SECTOR;
    erase.NbSectors    = 1;
    uint32_t sector_err = 0;
    HAL_FLASHEx_Erase(&erase, &sector_err);

    // Пишем слово за словом
    const uint32_t* src  = (const uint32_t*)cfg;
    uint32_t        addr = SETTINGS_FLASH_ADDR;
    uint32_t        nw   = sizeof(ELS_FlashSettings_t) / 4;
    for (uint32_t i = 0; i < nw; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]);
        addr += 4;
    }

    HAL_FLASH_Lock();
}

// ── Чтение из Flash → ELS_State ─────────────────────────────────────────────
static void _apply(const ELS_FlashSettings_t* cfg) {
    els.Feed_mm         = cfg->Feed_mm;
    // Validate aFeed_mm — corrupted flash can store out-of-range values (e.g. 33024)
    // which overflow int16_t in the display protocol and appear as -32512 on screen.
    if (cfg->aFeed_mm >= MIN_AFEED && cfg->aFeed_mm <= MAX_AFEED)
        els.aFeed_mm    = cfg->aFeed_mm;
    else
        els.aFeed_mm    = 100;   // default 100 mm/min
    els.feed            = cfg->Feed_mm;
    els.afeed           = els.aFeed_mm;
    els.Thread_Step     = cfg->Thread_Step;
    els.Ph              = cfg->Ph;
    els.thread_starts   = (uint8_t)cfg->Ph;
    els.Pass_Fin        = cfg->Pass_Fin;
    els.Cone_Step       = cfg->Cone_Step;
    els.Pass_Total      = cfg->Pass_Total;
    els.Ap              = cfg->Ap;
    els.Sph_R_mm        = cfg->Sph_R_mm;
    els.Bar_R_mm        = cfg->Bar_R_mm;
    els.Pass_Total_Sphr = cfg->Pass_Total_Sphr;
    els.Cutter_Step     = cfg->Cutter_Step;
    els.Cutting_Step    = cfg->Cutting_Step;
    els.Total_Tooth     = cfg->Total_Tooth;
    els.sub_feed        = cfg->sub_feed;
    els.sub_afeed       = cfg->sub_afeed;
    els.sub_thread      = cfg->sub_thread;
    els.sub_cone        = cfg->sub_cone;
    els.sub_sphere      = cfg->sub_sphere;
}

// ── ELS_State → структура для записи ────────────────────────────────────────
static void _pack(ELS_FlashSettings_t* cfg) {
    cfg->magic          = SETTINGS_MAGIC;
    cfg->Feed_mm        = els.Feed_mm;
    cfg->aFeed_mm       = els.aFeed_mm;
    cfg->Thread_Step    = els.Thread_Step;
    cfg->Ph             = els.Ph;
    cfg->Pass_Fin       = els.Pass_Fin;
    cfg->Cone_Step      = els.Cone_Step;
    cfg->Pass_Total     = els.Pass_Total;
    cfg->Ap             = els.Ap;
    cfg->Sph_R_mm       = els.Sph_R_mm;
    cfg->Bar_R_mm       = els.Bar_R_mm;
    cfg->Pass_Total_Sphr= els.Pass_Total_Sphr;
    cfg->Cutter_Step    = els.Cutter_Step;
    cfg->Cutting_Step   = els.Cutting_Step;
    cfg->Total_Tooth    = els.Total_Tooth;
    cfg->sub_feed       = els.sub_feed;
    cfg->sub_afeed      = els.sub_afeed;
    cfg->sub_thread     = els.sub_thread;
    cfg->sub_cone       = els.sub_cone;
    cfg->sub_sphere     = els.sub_sphere;
    cfg->_pad[0]        = 0;
    cfg->_pad[1]        = 0;
}

// ── Public API ───────────────────────────────────────────────────────────────

void ELS_Settings_MarkDirty(void) {
    s_dirty   = true;
    s_dirty_t = millis();
}

void ELS_Settings_Load(void) {
    const ELS_FlashSettings_t* flash =
        (const ELS_FlashSettings_t*)SETTINGS_FLASH_ADDR;

    if (flash->magic != SETTINGS_MAGIC) {
        // Flash пустая или невалидная — оставляем дефолты из ELS_State_Init
        return;
    }
    _apply(flash);
}

void ELS_Settings_Process(void) {
    if (!s_dirty) return;
    if ((millis() - s_dirty_t) < SETTINGS_SAVE_DEBOUNCE) return;

    s_dirty = false;

    ELS_FlashSettings_t cfg;
    _pack(&cfg);
    _flash_write(&cfg);
}
