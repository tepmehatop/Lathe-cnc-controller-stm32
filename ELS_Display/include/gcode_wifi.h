#pragma once
#include <Arduino.h>

// Initialize WiFi (non-blocking STA with AP fallback) + LittleFS + web server
void GCodeWiFi_Init();

// Call in loop() — drives WiFi state machine (connect/timeout/AP fallback)
void GCodeWiFi_Process();

// Returns current IP (STA or AP), or nullptr if not yet connected
const char* GCodeWiFi_GetIP();

// true when network is available (STA connected or AP started)
bool GCodeWiFi_IsConnected();

// true when running in AP mode (no saved credentials or STA timeout)
bool GCodeWiFi_IsAP();
