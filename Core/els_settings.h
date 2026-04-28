#pragma once
#include <stdint.h>

/**
 * @file  els_settings.h
 * @brief Сохранение/загрузка настроек в Flash — Этап 22
 *
 * Хранит в последнем секторе Flash (sector 11, 0x080E0000, 128KB).
 * Запись происходит не чаще 1 раза в секунду (debounce).
 */

// Пометить настройки "изменёнными" — сохранится через ~1 сек
void ELS_Settings_MarkDirty(void);

// Загрузить настройки из Flash в els (вызвать после ELS_State_Init)
void ELS_Settings_Load(void);

// Вызывать из ELS_Loop — выполняет отложенную запись если dirty
void ELS_Settings_Process(void);
