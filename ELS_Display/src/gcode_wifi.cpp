/**
 * gcode_wifi.cpp
 * WiFi (AP or STA) + LittleFS + ESP-IDF httpd для GCode файлового менеджера.
 * Порт 80. Загрузка файлов через PUT /api/file?name=xxx (тело = сырой файл).
 */

#include "gcode_wifi.h"
#include "wifi_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_http_server.h>

// ─── Состояние ────────────────────────────────────────────────────────────────

static const char* GCODE_DIR = "/gcode";

enum GcWifiSt { GCW_INIT, GCW_CONNECTING, GCW_CONNECTED, GCW_AP };
static GcWifiSt          s_state    = GCW_INIT;
static uint32_t          s_conn_t   = 0;
static httpd_handle_t    s_httpd    = nullptr;
static GCWifiConnectedCb s_conn_cb  = nullptr;
static char              s_ip[20]   = {};

// ─── HTML (встроен в flash) ───────────────────────────────────────────────────

static const char HTML_PAGE[] = R"rawhtml(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ELS GCode</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#1a1a2e;color:#e0e0f0;font-family:monospace;padding:10px;font-size:13px}
h1{color:#4fc3f7;margin-bottom:10px;font-size:17px}
.bar{background:#0d2137;padding:7px 10px;border-radius:4px;margin-bottom:10px;font-size:12px;color:#aac}
.sec{background:#16213e;border-radius:6px;padding:10px;margin-bottom:10px}
h2{color:#7986cb;font-size:13px;margin-bottom:8px;border-bottom:1px solid #2a3a5a;padding-bottom:4px}
button{background:#1976d2;color:#fff;border:none;padding:5px 10px;border-radius:4px;cursor:pointer;font-size:12px}
button:hover{background:#1565c0}
.del{background:#c62828}.del:hover{background:#b71c1c}
input[type=file]{background:#0d2137;color:#e0e0f0;border:1px solid #3a5068;padding:5px;border-radius:4px;font-family:monospace;font-size:12px}
ul{list-style:none}
li{display:flex;align-items:center;gap:6px;padding:5px 0;border-bottom:1px solid #1e2e40}
li:last-child{border-bottom:none}
li span{flex:1}
#viewer{background:#0a1628;padding:8px;border-radius:4px;white-space:pre;font-size:11px;max-height:280px;overflow:auto}
#prog{font-size:12px;color:#aaa;margin-top:6px;min-height:16px}
.tag{font-size:10px;color:#888;margin-left:4px}
</style>
</head>
<body>
<h1>ELS GCode Manager</h1>
<div class="bar" id="st">Соединение...</div>

<div class="sec">
<h2>Файлы GCode</h2>
<ul id="fl"><li><span>Загрузка...</span></li></ul>
<div style="display:flex;gap:6px;margin-top:8px;align-items:center;flex-wrap:wrap">
<input type="file" id="fi" accept=".gcode,.nc,.tap,.txt">
<button onclick="up()">Загрузить</button>
</div>
<div id="prog"></div>
</div>

<div class="sec" id="viewsec" style="display:none">
<h2>Просмотр: <span id="vn" style="color:#e0e0f0"></span>
<button onclick="document.getElementById('viewsec').style.display='none'" style="font-size:10px;padding:2px 6px;float:right">&#x2715;</button></h2>
<div id="viewer"></div>
</div>

<div class="sec">
<h2>Подключение</h2>
<div id="wi" style="font-size:12px;color:#aac;line-height:1.8"></div>
<div style="font-size:11px;color:#557;margin-top:6px">Для смены сети — измените wifi_config.h и перепрошейте.</div>
</div>

<script>
async function loadSt(){
  try{
    const d=await(await fetch('/api/status')).json();
    document.getElementById('st').innerHTML=
      'WiFi: <b>'+d.mode+'</b> | IP: <b>'+d.ip+'</b> | '+
      'Файлов: '+d.fc+' | Свободно: '+(d.fb/1024).toFixed(0)+' КБ';
    document.getElementById('wi').innerHTML=
      'Режим: <b>'+d.mode+'</b><br>SSID: <b>'+d.ssid+'</b><br>'+
      'IP: <b>'+d.ip+'</b><br>Адрес: <b>http://'+d.ip+'</b>';
  }catch(e){document.getElementById('st').textContent='Ошибка связи';}
}
async function loadFiles(){
  try{
    const files=await(await fetch('/api/files')).json();
    const ul=document.getElementById('fl');
    ul.innerHTML='';
    if(!files.length){ul.innerHTML='<li><span style="color:#667">Нет файлов</span></li>';return;}
    files.forEach(f=>{
      const li=document.createElement('li');
      li.innerHTML='<span>'+f.name+'<span class="tag">('+
        (f.size>=1024?(f.size/1024).toFixed(1)+' КБ':f.size+' Б')+')</span></span>'+
        '<button onclick="view(\''+f.name+'\')">Просмотр</button>'+
        '<button class="del" onclick="del(\''+f.name+'\')">Удалить</button>';
      ul.appendChild(li);
    });
  }catch(e){document.getElementById('fl').innerHTML='<li><span>Ошибка загрузки</span></li>';}
}
async function view(n){
  try{
    const t=await(await fetch('/api/file?name='+encodeURIComponent(n))).text();
    document.getElementById('vn').textContent=n;
    document.getElementById('viewer').textContent=t;
    document.getElementById('viewsec').style.display='block';
    document.getElementById('viewsec').scrollIntoView({behavior:'smooth'});
  }catch(e){alert('Ошибка загрузки файла');}
}
async function del(n){
  if(!confirm('Удалить "'+n+'"?'))return;
  try{
    const r=await fetch('/api/file?name='+encodeURIComponent(n),{method:'DELETE'});
    if(r.ok)loadFiles(); else alert('Ошибка удаления');
  }catch(e){alert('Ошибка сети');}
}
async function up(){
  const fi=document.getElementById('fi');
  if(!fi.files.length){alert('Выберите .gcode файл');return;}
  const file=fi.files[0];
  const pr=document.getElementById('prog');
  pr.textContent='Загрузка...';
  try{
    const r=await fetch('/api/file?name='+encodeURIComponent(file.name),{method:'PUT',body:file});
    const d=await r.json();
    pr.textContent=d.ok?'Загружено: '+d.name:'Ошибка: '+d.error;
    if(d.ok){loadFiles();fi.value='';}
  }catch(e){pr.textContent='Ошибка сети при загрузке';}
}
loadSt();loadFiles();setInterval(loadSt,8000);
</script>
</body>
</html>)rawhtml";

// ─── Утилиты ──────────────────────────────────────────────────────────────────

static bool get_query_param(httpd_req_t* req, const char* key, char* out, size_t out_len) {
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

// Базовое URL-декодирование %xx → символ (in-place)
static void url_decode(char* s) {
    char* r = s;
    char* w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            char h[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(h, nullptr, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' '; r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static const char* basename_of(const char* path) {
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int count_gcode_files() {
    File dir = LittleFS.open(GCODE_DIR);
    if (!dir || !dir.isDirectory()) return 0;
    int n = 0;
    File f = dir.openNextFile();
    while (f) { if (!f.isDirectory()) n++; f = dir.openNextFile(); }
    return n;
}

static void notify_connected() {
    if (!s_conn_cb) return;
    s_conn_cb(s_ip, s_state == GCW_AP);
}

// ─── HTTP handlers ────────────────────────────────────────────────────────────

static esp_err_t h_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, HTML_PAGE, -1);
    return ESP_OK;
}

static esp_err_t h_status(httpd_req_t* req) {
    bool ap = (s_state == GCW_AP);
    const char* ssid = ap ? WIFI_AP_SSID : WIFI_STA_SSID;
    uint32_t total = LittleFS.totalBytes();
    uint32_t used  = LittleFS.usedBytes();
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"fc\":%d,\"tb\":%lu,\"fb\":%lu}",
        ap ? "AP" : "STA", ssid, s_ip,
        count_gcode_files(),
        (unsigned long)total, (unsigned long)(total - used));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

static esp_err_t h_files(httpd_req_t* req) {
    String json = "[";
    bool first = true;
    File dir = LittleFS.open(GCODE_DIR);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                if (!first) json += ",";
                first = false;
                char entry[128];
                snprintf(entry, sizeof(entry),
                    "{\"name\":\"%s\",\"size\":%lu}",
                    basename_of(f.name()), (unsigned long)f.size());
                json += entry;
            }
            f = dir.openNextFile();
        }
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

static esp_err_t h_file_get(httpd_req_t* req) {
    char name[64];
    if (!get_query_param(req, "name", name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
        return ESP_FAIL;
    }
    url_decode(name);
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", GCODE_DIR, name);
    File f = LittleFS.open(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    char buf[512];
    int n;
    while ((n = f.read((uint8_t*)buf, sizeof(buf))) > 0)
        httpd_resp_send_chunk(req, buf, n);
    httpd_resp_send_chunk(req, nullptr, 0);
    f.close();
    return ESP_OK;
}

static esp_err_t h_file_delete(httpd_req_t* req) {
    char name[64];
    if (!get_query_param(req, "name", name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
        return ESP_FAIL;
    }
    url_decode(name);
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", GCODE_DIR, name);
    httpd_resp_set_type(req, "application/json");
    if (LittleFS.remove(path)) {
        httpd_resp_send(req, "{\"ok\":true}", -1);
        Serial.printf("[gcode] Deleted: %s\n", path);
    } else {
        httpd_resp_send(req, "{\"error\":\"not found\"}", -1);
    }
    return ESP_OK;
}

static esp_err_t h_file_put(httpd_req_t* req) {
    char name[64];
    if (!get_query_param(req, "name", name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
        return ESP_FAIL;
    }
    url_decode(name);
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", GCODE_DIR, name);

    File f = LittleFS.open(path, "w");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
        return ESP_FAIL;
    }

    char buf[512];
    int remaining = req->content_len;
    Serial.printf("[gcode] Upload: %s  (%d B)\n", path, remaining);
    while (remaining > 0) {
        int to_recv = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
        int got = httpd_req_recv(req, buf, to_recv);
        if (got <= 0) {
            f.close();
            LittleFS.remove(path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv error");
            return ESP_FAIL;
        }
        f.write((uint8_t*)buf, got);
        remaining -= got;
    }
    f.close();
    Serial.printf("[gcode] Upload done: %s\n", path);

    char resp[80];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"name\":\"%s\"}", name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, -1);
    return ESP_OK;
}

// ─── Запуск сервера ───────────────────────────────────────────────────────────

static void start_server() {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port        = 80;
    cfg.stack_size         = 8192;
    cfg.send_wait_timeout  = 30;
    cfg.max_uri_handlers   = 10;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        Serial.println("[gcode] httpd_start FAILED");
        return;
    }
    const httpd_uri_t routes[] = {
        { "/",          HTTP_GET,    h_root,        nullptr },
        { "/api/status",HTTP_GET,    h_status,      nullptr },
        { "/api/files", HTTP_GET,    h_files,       nullptr },
        { "/api/file",  HTTP_GET,    h_file_get,    nullptr },
        { "/api/file",  HTTP_DELETE, h_file_delete, nullptr },
        { "/api/file",  HTTP_PUT,    h_file_put,    nullptr },
    };
    for (auto& r : routes) httpd_register_uri_handler(s_httpd, &r);
    Serial.printf("[gcode] Web server: http://%s\n", s_ip);
    notify_connected();
}

static void start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    s_state = GCW_AP;
    WiFi.softAPIP().toString().toCharArray(s_ip, sizeof(s_ip));
    Serial.printf("[gcode] AP: %s  IP: %s\n", WIFI_AP_SSID, s_ip);
    start_server();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void GCodeWiFi_SetConnectedCallback(GCWifiConnectedCb cb) { s_conn_cb = cb; }

void GCodeWiFi_Init() {
    if (!LittleFS.begin(false)) {
        // Первый запуск — форматируем один раз
        Serial.println("[gcode] LittleFS mount failed, formatting...");
        if (LittleFS.begin(true))
            Serial.println("[gcode] LittleFS formatted OK");
        else
            Serial.println("[gcode] LittleFS FAILED");
    } else {
        Serial.printf("[gcode] LittleFS: %lu/%lu B\n",
            (unsigned long)LittleFS.usedBytes(),
            (unsigned long)LittleFS.totalBytes());
    }
    if (!LittleFS.exists(GCODE_DIR)) LittleFS.mkdir(GCODE_DIR);

    const char* ssid = WIFI_STA_SSID;
    if (ssid && ssid[0] != '\0') {
        Serial.printf("[gcode] WiFi STA → %s\n", ssid);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(ssid, WIFI_STA_PASS);
        s_state   = GCW_CONNECTING;
        s_conn_t  = millis();
    } else {
        start_ap();
    }
}

void GCodeWiFi_Process() {
    if (s_state != GCW_CONNECTING) return;
    if (WiFi.status() == WL_CONNECTED) {
        s_state = GCW_CONNECTED;
        WiFi.setSleep(false);
        WiFi.localIP().toString().toCharArray(s_ip, sizeof(s_ip));
        Serial.printf("[gcode] STA connected: %s\n", s_ip);
        start_server();
    } else if (millis() - s_conn_t > WIFI_STA_TIMEOUT_MS) {
        Serial.println("[gcode] STA timeout → AP");
        WiFi.disconnect(true);
        delay(100);
        start_ap();
    }
}

const char* GCodeWiFi_GetIP()      { return s_ip; }
bool GCodeWiFi_IsConnected()       { return s_state == GCW_CONNECTED || s_state == GCW_AP; }
bool GCodeWiFi_IsAP()              { return s_state == GCW_AP; }
httpd_handle_t GCodeWiFi_GetHttpd(){ return s_httpd; }
