#pragma once
#include <Arduino.h>

enum GcExecSt {
    GCE_IDLE = 0,
    GCE_RUNNING,      // читаем следующую строку
    GCE_WAITING_ACK,  // отправлена строка в STM32, ждём <OK>/<ERR>
    GCE_DWELL,        // G4 пауза
    GCE_PAUSED,       // M0 или ручная пауза
    GCE_DONE,         // M2/M30 — конец программы
    GCE_ERROR
};

// Callback: отправить строку в STM32 (реализует вызывающая сторона)
typedef void (*GcSendFn)(const char* gcode_line);

void        GCodeExec_Init(GcSendFn fn);
bool        GCodeExec_Run(const char* filepath);
void        GCodeExec_Stop();
void        GCodeExec_TogglePause();
void        GCodeExec_OnAck(bool ok, const char* err);  // вызвать при получении OK/ERR от STM32
void        GCodeExec_Process();                         // вызывать из loop()

GcExecSt    GCodeExec_GetState();
int         GCodeExec_GetProgress();   // 0-100 %
int         GCodeExec_GetLine();       // номер текущей строки (1-based)
const char* GCodeExec_GetFilename();
const char* GCodeExec_GetError();
