# Claude Rules for This Project

## Git: Always Commit and Push After Changes

After completing any code changes, **always create a git commit and push to remote**. This provides a rollback point and preserves history.

- Stage the changed files specifically (not `git add -A`)
- Write a meaningful commit message (what changed and why)
- Push to `origin main` after committing
- Do this even for small fixes

## Project Context

STM32F407 CNC lathe controller (Electronic Lead Screw), ported from Arduino Mega.
PlatformIO / STM32duino (Arduino API). See MIGRATION_PLAN.md for full context.

### Key Files
- `Core/els_config.h` — all compile-time constants and feature flags
- `Core/els_state.h` / `els_state.cpp` — global machine state `ELS_State_t els`
- `Core/els_menu.cpp` — mode/submode/parameter handling, ESP32 touch RX
- `Core/els_control.cpp` — motor control loops per mode
- `Core/els_main.cpp` — main loop, DRO update, periodic sends
- `Drivers/drv_display.cpp` — ESP32 UART protocol (TX/RX)
- `Drivers/drv_stepper.cpp` — TIM1 step generation

### Axis Mapping (IMPORTANT)
| Physical    | STM32 var  | ESP32 cmd |
|-------------|------------|-----------|
| Carriage (Z)| `pos_y`    | `POS_Z`   |
| Cross (X)   | `pos_x`    | `POS_X`   |

### ESP32 Protocol
- STM32 → ESP32: `<CMD:value>\n`
- ESP32 → STM32: `<TOUCH:CMD>` or `<TOUCH:CMD:value>`
- Modes are 0-based on STM32, 1-based on ESP32 (STM32 sends `mode+1`)
- Submodes are 0-based on STM32, 1-based on ESP32 (STM32 sends `submode+1`)

### Units
- `Feed_mm`: мм/об × 100 (25 = 0.25 мм/об)
- `aFeed_mm`: мм/мин (330 = 330 мм/мин)
- `thread_pitch`: 0.001 мм (200 = 0.200 мм)
- `THREAD` command to ESP32: шаг × 100 (400 = 4.00 мм)
- Positions: 0.001 мм (microns)
