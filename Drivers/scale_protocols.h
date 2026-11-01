#pragma once
/**
 * @file  scale_protocols.h
 * @brief Разбор кадров цифровых линеек в микроны (0.001 мм).
 *
 * Чистые функции без обращения к железу — тестируются на ПК:
 *   tools/run_unit_tests.sh  (tests/test_scale_protocols.cpp)
 *
 * Кадр собирается драйвером drv_scales.cpp: биты складываются младшим вперёд
 * (бит, принятый первым, — бит 0), уже с учётом инверсии схемы сопряжения.
 * Описание протоколов и источники — docs/scales/README.md.
 */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Квадратура: импульсы (×4) → мкм. nm_per_count — разрешение линейки в нм. */
int32_t ScaleProto_QuadToUm(int64_t count, int32_t nm_per_count);

/** Sylvac 2×24 бит (ёмкостные китайские 1.5 В).
 *  Биты 0..23 — абсолютная позиция от включения (знаковая, 20480 имп/дюйм),
 *  биты 24..47 — относительная (с учётом кнопки «ноль» на линейке) — не используется. */
#define SCALE_SYLVAC48_BITS     48
int32_t ScaleProto_Sylvac48ToUm(uint64_t frame);

/** BIN6, 24 бит: биты 0..19 — модуль значения, бит 20 — знак (1 = минус),
 *  бит 23 — единицы (1 = дюймы, шаг 0.0005"; 0 = мм, шаг 0.01 мм).
 *  Возвращает false, если кадр не похож на BIN6 (значение вне диапазона). */
#define SCALE_BIN6_BITS         24
bool ScaleProto_Bin6ToUm(uint32_t frame, int32_t* um);

/** iGaging 21 бит: знаковое число (дополнительный код), 2560 имп/дюйм. */
#define SCALE_IGAGING21_BITS    21
int32_t ScaleProto_IGaging21ToUm(uint32_t frame);

/** Mitutoyo Digimatic: 13 полубайтов (52 бит, младший бит полубайта первым).
 *  d1..d4 = 0xF, d5 = знак (0 / 8 = минус), d6..d11 = цифры BCD (d6 — старшая),
 *  d12 = позиция десятичной точки (цифр после точки), d13 = единицы (0 мм, 1 дюйм).
 *  nibbles[0] = d1. Возвращает false при неверном заголовке/цифрах. */
#define SCALE_DIGIMATIC_BITS    52
bool ScaleProto_DigimaticToUm(const uint8_t nibbles[13], int32_t* um);

/** Разложить 52-битный кадр (бит 0 — первый принятый) на 13 полубайтов. */
void ScaleProto_SplitNibbles(uint64_t frame, uint8_t nibbles[13]);

#ifdef __cplusplus
}
#endif
