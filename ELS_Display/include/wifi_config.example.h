#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// Шаблон. Скопируйте в wifi_config.h (он в .gitignore) и впишите свои данные:
//   cp ELS_Display/include/wifi_config.example.h ELS_Display/include/wifi_config.h

// === РЕЖИМ 1: Домашний роутер (STA) — основной ===============================
#define WIFI_STA_SSID  "your-ssid"
#define WIFI_STA_PASS  "your-password"

// === РЕЖИМ 2: AP fallback (если роутер недоступен) ===========================
#define WIFI_AP_SSID   "ELS-WiFi"
#define WIFI_AP_PASS   "change-me-8chars"

// Таймаут подключения к STA (мс), потом переход в AP.
#define WIFI_STA_TIMEOUT_MS  30000

#endif // WIFI_CONFIG_H
