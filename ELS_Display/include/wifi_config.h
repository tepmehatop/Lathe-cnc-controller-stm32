#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// === РЕЖИМ 1: Домашний роутер (STA) — основной ===============================
#define WIFI_STA_SSID  "VIRUS932"
#define WIFI_STA_PASS  "111111111"

// === РЕЖИМ 2: AP fallback (если роутер недоступен) ===========================
#define WIFI_AP_SSID   "ELS-WiFi"
#define WIFI_AP_PASS   "12345678"

// Таймаут подключения к STA (мс), потом переход в AP.
#define WIFI_STA_TIMEOUT_MS  30000

#endif // WIFI_CONFIG_H
