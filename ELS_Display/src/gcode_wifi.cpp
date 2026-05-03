/**
 * gcode_wifi.cpp
 * WiFi (STA→AP fallback) + LittleFS + простой HTTP сервер на WiFiServer.
 * Обрабатывает одного клиента за раз прямо в loop() — без отдельных задач.
 */

#include "gcode_wifi.h"
#include "wifi_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <LittleFS.h>

// ─── Состояние ────────────────────────────────────────────────────────────────

static const char* GCODE_DIR    = "/gcode";
static const uint16_t HTTP_PORT = 8080;

enum GcWifiSt { GCW_INIT, GCW_CONNECTING, GCW_CONNECTED, GCW_AP };
static GcWifiSt          s_state   = GCW_INIT;
static uint32_t          s_conn_t  = 0;
static WiFiServer*       s_srv     = nullptr;
static GCWifiConnectedCb s_conn_cb = nullptr;
static char              s_ip[20]  = {};

// ─── HTML ─────────────────────────────────────────────────────────────────────

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
<div class="bar" id="st">...</div>
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
<script>
const PORT=8080;
const BASE='http://'+location.hostname+':'+PORT;
async function loadSt(){
  try{
    const d=await(await fetch(BASE+'/api/status')).json();
    document.getElementById('st').innerHTML=
      'WiFi: <b>'+d.mode+'</b> | IP: <b>'+d.ip+'</b> | Файлов: '+d.fc+' | Свободно: '+(d.fb/1024).toFixed(0)+' КБ';
  }catch(e){document.getElementById('st').textContent='Ошибка связи';}
}
async function loadFiles(){
  try{
    const files=await(await fetch(BASE+'/api/files')).json();
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
  }catch(e){document.getElementById('fl').innerHTML='<li><span>Ошибка</span></li>';}
}
async function view(n){
  try{
    const t=await(await fetch(BASE+'/api/file?name='+encodeURIComponent(n))).text();
    document.getElementById('vn').textContent=n;
    document.getElementById('viewer').textContent=t;
    document.getElementById('viewsec').style.display='block';
    document.getElementById('viewsec').scrollIntoView({behavior:'smooth'});
  }catch(e){alert('Ошибка');}
}
async function del(n){
  if(!confirm('Удалить "'+n+'"?'))return;
  await fetch(BASE+'/api/file?name='+encodeURIComponent(n),{method:'DELETE'});
  loadFiles();
}
async function up(){
  const fi=document.getElementById('fi');
  if(!fi.files.length){alert('Выберите файл');return;}
  const file=fi.files[0];
  const pr=document.getElementById('prog');
  pr.textContent='Загрузка...';
  try{
    const r=await fetch(BASE+'/api/file?name='+encodeURIComponent(file.name),{method:'PUT',body:file});
    const d=await r.json();
    pr.textContent=d.ok?'Загружено: '+d.name:'Ошибка: '+d.error;
    if(d.ok){loadFiles();fi.value='';}
  }catch(e){pr.textContent='Ошибка сети';}
}
loadSt();loadFiles();setInterval(loadSt,8000);
</script>
</body>
</html>)rawhtml";

// ─── HTTP утилиты ─────────────────────────────────────────────────────────────

static void send_ok_json(WiFiClient& cl, const String& body) {
    cl.printf("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\nContent-Length: %u\r\n"
              "Connection: close\r\n\r\n", body.length());
    cl.print(body);
}

static void send_404(WiFiClient& cl) {
    cl.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

// URL decode %xx in-place
static void url_decode(String& s) {
    String out;
    out.reserve(s.length());
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] == '%' && i+2 < (int)s.length()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    s = out;
}

static String get_param(const String& query, const char* key) {
    String k = String(key) + "=";
    int i = query.indexOf(k);
    if (i < 0) return "";
    i += k.length();
    int j = query.indexOf('&', i);
    String val = (j < 0) ? query.substring(i) : query.substring(i, j);
    url_decode(val);
    return val;
}

static const char* basename_of(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s+1 : p;
}

// ─── HTTP handlers ────────────────────────────────────────────────────────────

static void handle_root(WiFiClient& cl) {
    size_t len = strlen(HTML_PAGE);
    cl.printf("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
              "Content-Length: %u\r\nConnection: close\r\n\r\n", len);
    // Отправляем кусками по 512 байт
    const char* p = HTML_PAGE;
    size_t rem = len;
    while (rem > 0) {
        size_t chunk = rem > 512 ? 512 : rem;
        cl.write((const uint8_t*)p, chunk);
        p += chunk; rem -= chunk;
    }
}

static void handle_status(WiFiClient& cl) {
    bool ap = (s_state == GCW_AP);
    uint32_t total = LittleFS.totalBytes();
    uint32_t used  = LittleFS.usedBytes();
    // Считаем файлы
    int fc = 0;
    File dir = LittleFS.open(GCODE_DIR);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) { if (!f.isDirectory()) fc++; f = dir.openNextFile(); }
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"fc\":%d,\"tb\":%lu,\"fb\":%lu}",
        ap ? "AP" : "STA",
        ap ? WIFI_AP_SSID : WIFI_STA_SSID,
        s_ip, fc,
        (unsigned long)total, (unsigned long)(total - used));
    send_ok_json(cl, buf);
}

static void handle_files(WiFiClient& cl) {
    String json = "[";
    bool first = true;
    File dir = LittleFS.open(GCODE_DIR);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                if (!first) json += ",";
                first = false;
                char e[128];
                snprintf(e, sizeof(e), "{\"name\":\"%s\",\"size\":%lu}",
                    basename_of(f.name()), (unsigned long)f.size());
                json += e;
            }
            f = dir.openNextFile();
        }
    }
    json += "]";
    send_ok_json(cl, json);
}

static void handle_file_get(WiFiClient& cl, const String& query) {
    String name = get_param(query, "name");
    if (!name.length()) { send_404(cl); return; }
    String path = String(GCODE_DIR) + "/" + name;
    File f = LittleFS.open(path, "r");
    if (!f) { send_404(cl); return; }
    cl.printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
              "Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)f.size());
    uint8_t buf[512];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) cl.write(buf, n);
    f.close();
}

static void handle_file_delete(WiFiClient& cl, const String& query) {
    String name = get_param(query, "name");
    if (!name.length()) { send_404(cl); return; }
    String path = String(GCODE_DIR) + "/" + name;
    if (LittleFS.remove(path)) {
        Serial.printf("[gcode] Deleted: %s\n", path.c_str());
        send_ok_json(cl, "{\"ok\":true}");
    } else {
        send_ok_json(cl, "{\"error\":\"not found\"}");
    }
}

static void handle_file_put(WiFiClient& cl, const String& query, int content_len) {
    String name = get_param(query, "name");
    if (!name.length()) { send_404(cl); return; }
    String path = String(GCODE_DIR) + "/" + name;
    File f = LittleFS.open(path, "w");
    if (!f) {
        send_ok_json(cl, "{\"error\":\"open failed\"}");
        return;
    }
    Serial.printf("[gcode] Upload: %s (%d B)\n", path.c_str(), content_len);
    uint8_t buf[512];
    int remaining = content_len;
    uint32_t t0 = millis();
    while (remaining > 0 && millis() - t0 < 30000) {
        int avail = cl.available();
        if (avail <= 0) { delay(1); continue; }
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        if (to_read > avail) to_read = avail;
        int got = cl.read(buf, to_read);
        if (got <= 0) break;
        f.write(buf, got);
        remaining -= got;
    }
    f.close();
    Serial.printf("[gcode] Upload done: %s\n", name.c_str());
    char resp[80];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"name\":\"%s\"}", name.c_str());
    send_ok_json(cl, resp);
}

static void handle_options(WiFiClient& cl) {
    // CORS preflight
    cl.print("HTTP/1.1 204 No Content\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Methods: GET, PUT, DELETE, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\n"
             "Connection: close\r\n\r\n");
}

// ─── Обработка одного HTTP клиента ───────────────────────────────────────────

static void handle_client(WiFiClient& cl) {
    // Ждём данные (макс 2 сек)
    uint32_t t0 = millis();
    while (!cl.available() && millis() - t0 < 2000) delay(1);
    if (!cl.available()) { cl.stop(); return; }

    // Читаем строку запроса
    String req = cl.readStringUntil('\n');
    req.trim();

    // Читаем заголовки
    int content_len = 0;
    while (cl.connected()) {
        String line = cl.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length:"))
            content_len = line.substring(15).toInt();
    }

    // Разбираем request line: "METHOD /path?query HTTP/1.x"
    int sp1 = req.indexOf(' ');
    int sp2 = req.lastIndexOf(' ');
    if (sp1 < 0 || sp2 <= sp1) { cl.stop(); return; }
    String method    = req.substring(0, sp1);
    String full_path = req.substring(sp1+1, sp2);

    int qi = full_path.indexOf('?');
    String path  = qi >= 0 ? full_path.substring(0, qi) : full_path;
    String query = qi >= 0 ? full_path.substring(qi+1)  : String();

    Serial.printf("[gcode] %s %s\n", method.c_str(), full_path.c_str());

    if (method == "OPTIONS") {
        handle_options(cl);
    } else if (method == "GET" && path == "/") {
        handle_root(cl);
    } else if (method == "GET" && path == "/api/status") {
        handle_status(cl);
    } else if (method == "GET" && path == "/api/files") {
        handle_files(cl);
    } else if (method == "GET" && path == "/api/file") {
        handle_file_get(cl, query);
    } else if (method == "DELETE" && path == "/api/file") {
        handle_file_delete(cl, query);
    } else if (method == "PUT" && path == "/api/file") {
        handle_file_put(cl, query, content_len);
    } else {
        send_404(cl);
    }

    delay(2);
    cl.stop();
}

// ─── WiFi helpers ─────────────────────────────────────────────────────────────

static void notify_connected() {
    if (s_conn_cb) s_conn_cb(s_ip, s_state == GCW_AP);
}

static void start_server() {
    if (s_srv) { s_srv->close(); delete s_srv; s_srv = nullptr; }
    s_srv = new WiFiServer(HTTP_PORT);
    s_srv->begin();
    Serial.printf("[gcode] WiFiServer started: http://%s:%u/\n", s_ip, HTTP_PORT);
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
        Serial.println("[gcode] LittleFS formatting...");
        LittleFS.begin(true);
    } else {
        Serial.printf("[gcode] LittleFS: %lu/%lu B\n",
            (unsigned long)LittleFS.usedBytes(),
            (unsigned long)LittleFS.totalBytes());
    }
    if (!LittleFS.exists(GCODE_DIR)) LittleFS.mkdir(GCODE_DIR);

    const char* ssid = WIFI_STA_SSID;
    if (ssid && ssid[0] != '\0') {
        Serial.printf("[gcode] WiFi STA → %s\n", ssid);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(true);
        delay(500);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(ssid, WIFI_STA_PASS);
        s_state  = GCW_CONNECTING;
        s_conn_t = millis();
    } else {
        start_ap();
    }
}

void GCodeWiFi_Process() {
    // Обработка клиентов
    if (s_srv && s_srv->hasClient()) {
        WiFiClient cl = s_srv->accept();
        if (cl) handle_client(cl);
    }

    // Состояния WiFi
    if (s_state == GCW_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            if (ip[0] == 0) return;
            s_state = GCW_CONNECTED;
            WiFi.setSleep(false);
            ip.toString().toCharArray(s_ip, sizeof(s_ip));
            Serial.printf("[gcode] STA connected: %s\n", s_ip);
            start_server();
        } else if (millis() - s_conn_t > WIFI_STA_TIMEOUT_MS) {
            Serial.println("[gcode] STA timeout → AP");
            WiFi.disconnect(true);
            delay(100);
            start_ap();
        }
    } else if (s_state == GCW_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[gcode] STA lost → reconnecting...");
            s_state  = GCW_CONNECTING;
            s_conn_t = millis();
            WiFi.disconnect(true);
            delay(100);
            WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
        }
    }
}

const char* GCodeWiFi_GetIP()      { return s_ip; }
bool GCodeWiFi_IsConnected()       { return s_state == GCW_CONNECTED || s_state == GCW_AP; }
bool GCodeWiFi_IsAP()              { return s_state == GCW_AP; }
httpd_handle_t GCodeWiFi_GetHttpd(){ return nullptr; }
