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

// Источник координат и линейки (BACKLOG-028): оба драйвера (блок DRO и линейки) в прошивке всегда,
// выбор — в настройках. Исходные значения — ELS_POS_SOURCE и SCALE_* из els_config.h.
// Смена источника или линеек применяется перезапуском STM32 (прерывания и таймеры линеек — при старте).
struct E2ScaleCfg { int type, nm_per_count, invert, sig_inv; };
struct E2Pos {
  int        source;      // POS_SOURCE_STEPS / _DRO_UART / _SCALES
  E2ScaleCfg ax[2];       // индекс ScaleAxis: 0 — Z, 1 — X
  int        fix_x;       // доводка X по линейке перед проходом (BACKLOG-025)
  int        loss_check;  // контроль пропуска шагов по линейкам
  int        loss_tol_um; // допуск расхождения шагов и линейки, мкм
};
static constexpr E2Pos E2_POS_CONFIG = {
  ELS_POS_SOURCE,
  { { SCALE_Z_TYPE, SCALE_Z_NM_PER_COUNT, SCALE_Z_INVERT, SCALE_Z_SIGNAL_INVERT },
    { SCALE_X_TYPE, SCALE_X_NM_PER_COUNT, SCALE_X_INVERT, SCALE_X_SIGNAL_INVERT } },
  SCALE_FIX_X, STEP_LOSS_CHECK, STEP_LOSS_TOL_UM
};
extern E2Pos e2_pos;
const char* e2_pos_check(const E2Pos& p);     // nullptr — годится

const char* e2_mech_check(const E2Mech& m);   // nullptr — годится, иначе причина (для экрана)
const char* e2_mech_apply(const E2Mech& m);   // проверить и применить: таблицы резьб/конусов, разгон, люфт
void        e2_mech_load(void);               // из флеша при старте; нет или испорчены — из els_config.h
const char* e2_mech_save(void);               // текущие (механика и e2_pos) → флеш; nullptr — сохранено

// Флеш STM32 (e2_store.cpp; в симуляции — заглушка)
bool e2_store_read(void* buf, uint32_t len);
bool e2_store_write(const void* buf, uint32_t len);
void e2_reboot(void);                          // перезапуск STM32 (новый источник координат / линейки)

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
