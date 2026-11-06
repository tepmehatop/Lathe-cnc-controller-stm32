#pragma once
#include <Arduino.h>
#include <esp_http_server.h>

typedef void (*GCWifiConnectedCb)(const char* ip, bool is_ap);

void           GCodeWiFi_SetConnectedCallback(GCWifiConnectedCb cb);
void           GCodeWiFi_Init();
void           GCodeWiFi_Process();
const char*    GCodeWiFi_GetIP();
bool           GCodeWiFi_IsConnected();
bool           GCodeWiFi_IsAP();
// Сеть роутера с экрана (NVS) и поиск сетей — выполняет WiFi-задача
#define GCW_SCAN_MAX 12
const char*    GCodeWiFi_GetStaSsid();             // текущая сеть роутера ("" — только точка доступа)
const char*    GCodeWiFi_GetApSsid();
const char*    GCodeWiFi_GetApPass();
void           GCodeWiFi_SetSta(const char* ssid, const char* pass);   // ssid "" — забыть
void           GCodeWiFi_Scan();
int            GCodeWiFi_ScanCount();              // -1 — поиск идёт / не было
const char*    GCodeWiFi_ScanSsid(int i);
httpd_handle_t GCodeWiFi_GetHttpd();
