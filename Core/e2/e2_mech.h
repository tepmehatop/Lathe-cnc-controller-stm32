// [STM32] Механика станка — настройки во флеше, правятся с экрана (BACKLOG-028).
//
// Значения из els_config.h — исходные: до первого сохранения с экрана и после «Сброс к конфигу».
// Ниже имена из els_config.h переопределены на поля e2_mech, поэтому текст ядра 7e2 не меняется:
// везде, где автор пишет SCREW_Z или McSTEP_X, берётся текущая настройка.
// Подключается в e2_core.cpp после проверок конфига при сборке (static_assert) — они сверяют
// исходные значения; те же проверки для значений с экрана делает e2_mech_check().
#pragma once
#include <stdint.h>

struct E2Mech {
  int enc_lines;     // ENC_LINE_PER_REV — линий энкодера шпинделя
  int z_steps;       // MOTOR_Z_STEP_PER_REV — шагов мотора Z на оборот (без микрошага)
  int z_mc;          // McSTEP_Z — микрошаг Z (степень двойки: ядро округляет упоры по маске)
  int z_screw;       // SCREW_Z — шаг винта Z, 0.01 мм
  int x_steps;       // MOTOR_X_STEP_PER_REV
  int x_mc;          // McSTEP_X
  int x_screw;       // SCREW_X
  int bl_z_um;       // BACKLASH_Z_UM — люфт винта Z, мкм (0 — без компенсации)
  int bl_x_um;       // BACKLASH_X_UM
};

static constexpr E2Mech E2_MECH_CONFIG = {
  ENC_LINE_PER_REV, MOTOR_Z_STEP_PER_REV, McSTEP_Z, SCREW_Z,
  MOTOR_X_STEP_PER_REV, McSTEP_X, SCREW_X, BACKLASH_Z_UM, BACKLASH_X_UM
};

extern E2Mech e2_mech;

const char* e2_mech_check(const E2Mech& m);   // nullptr — годится, иначе причина (для экрана)
const char* e2_mech_apply(const E2Mech& m);   // проверить и применить: таблицы резьб/конусов, разгон, люфт
void        e2_mech_load(void);               // из флеша при старте; нет или испорчены — из els_config.h
const char* e2_mech_save(void);               // текущие → флеш; nullptr — сохранено

// Флеш STM32 (e2_store.cpp; в симуляции — заглушка)
bool e2_store_read(void* buf, uint32_t len);
bool e2_store_write(const void* buf, uint32_t len);

#undef ENC_LINE_PER_REV
#undef MOTOR_Z_STEP_PER_REV
#undef McSTEP_Z
#undef SCREW_Z
#undef MOTOR_X_STEP_PER_REV
#undef McSTEP_X
#undef SCREW_X
#undef BACKLASH_Z_UM
#undef BACKLASH_X_UM
#define ENC_LINE_PER_REV     (e2_mech.enc_lines)
#define MOTOR_Z_STEP_PER_REV (e2_mech.z_steps)
#define McSTEP_Z             (e2_mech.z_mc)
#define SCREW_Z              (e2_mech.z_screw)
#define MOTOR_X_STEP_PER_REV (e2_mech.x_steps)
#define McSTEP_X             (e2_mech.x_mc)
#define SCREW_X              (e2_mech.x_screw)
#define BACKLASH_Z_UM        (e2_mech.bl_z_um)
#define BACKLASH_X_UM        (e2_mech.bl_x_um)
