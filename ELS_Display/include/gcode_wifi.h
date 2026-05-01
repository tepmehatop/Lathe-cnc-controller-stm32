#pragma once
#include <Arduino.h>

// Callback: вызывается когда WiFi готов (STA подключился или AP поднялся)
// ip    — IP-адрес (строка, e.g. "192.168.1.100" или "192.168.4.1")
// is_ap — true если ESP32 в режиме точки доступа
typedef void (*GCWifiConnectedCb)(const char* ip, bool is_ap);

// Initialize WiFi (non-blocking) + LittleFS + AsyncWebServer
// Читает настройки из wifi_config.h (WIFI_STA_SSID / WIFI_STA_PASS)
void GCodeWiFi_Init();

// Call in loop() — drives WiFi state machine
void GCodeWiFi_Process();

// Callback вызывается один раз при установке соединения
void GCodeWiFi_SetConnectedCallback(GCWifiConnectedCb cb);

// Returns current IP string, or "" if not yet connected
const char* GCodeWiFi_GetIP();

// true when network available (STA connected or AP started)
bool GCodeWiFi_IsConnected();

// true when running in AP mode
bool GCodeWiFi_IsAP();
