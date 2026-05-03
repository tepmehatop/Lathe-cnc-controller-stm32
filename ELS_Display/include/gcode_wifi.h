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
httpd_handle_t GCodeWiFi_GetHttpd();
