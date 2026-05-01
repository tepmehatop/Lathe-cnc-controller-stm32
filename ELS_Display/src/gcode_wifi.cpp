/**
 * @file gcode_wifi.cpp
 * @brief WiFi + LittleFS + AsyncWebServer для GCode файлового менеджера.
 *
 * Логика подключения (настройки — в wifi_config.h):
 *   STA: если WIFI_STA_SSID непустой → подключаемся к роутеру (неблокирующе)
 *        таймаут WIFI_STA_TIMEOUT_MS → fallback в AP режим
 *   AP:  ESP32 создаёт свою сеть WIFI_AP_SSID, IP = 192.168.4.1
 */

#include "gcode_wifi.h"
#include "wifi_config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

// ─── Состояние модуля ─────────────────────────────────────────────────────────
static const char* GCODE_DIR = "/gcode";

enum GcWifiSt { GCW_INIT, GCW_CONNECTING, GCW_CONNECTED, GCW_AP };
static GcWifiSt         s_state       = GCW_INIT;
static uint32_t         s_connect_t   = 0;
static AsyncWebServer   s_srv(80);
static bool             s_srv_started = false;
static GCWifiConnectedCb s_conn_cb    = nullptr;

// ─── HTML веб-интерфейс (PROGMEM) ─────────────────────────────────────────────
static const char HTML_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html>
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
<button onclick="document.getElementById('viewsec').style.display='none'" style="font-size:10px;padding:2px 6px;float:right">✕</button></h2>
<div id="viewer"></div>
</div>

<div class="sec">
<h2>Информация о подключении</h2>
<div id="wi" style="font-size:12px;color:#aac;line-height:1.8"></div>
<div style="font-size:11px;color:#557;margin-top:6px">
Для смены WiFi сети — измените wifi_config.h и перепрошейте устройство.</div>
</div>

<script>
async function loadSt(){
  try{
    const d=await(await fetch('/api/status')).json();
    document.getElementById('st').innerHTML=
      'WiFi: <b>'+d.mode+'</b> | IP: <b>'+d.ip+'</b> | '+
      'Файлов: '+d.fc+' | Свободно: '+(d.fb/1024).toFixed(0)+' КБ';
    document.getElementById('wi').innerHTML=
      'Режим: <b>'+d.mode+'</b><br>'+
      'SSID: <b>'+d.ssid+'</b><br>'+
      'IP: <b>'+d.ip+'</b><br>'+
      'Адрес веб-интерфейса: <b>http://'+d.ip+'</b>';
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
  if(!confirm('Удалить файл "'+n+'"?'))return;
  try{
    await fetch('/api/file?name='+encodeURIComponent(n),{method:'DELETE'});
    loadFiles();
  }catch(e){alert('Ошибка удаления');}
}
async function up(){
  const fi=document.getElementById('fi');
  if(!fi.files.length){alert('Выберите .gcode файл');return;}
  const fd=new FormData();
  fd.append('file',fi.files[0]);
  const pr=document.getElementById('prog');
  pr.textContent='Загрузка...';
  try{
    const d=await(await fetch('/api/upload',{method:'POST',body:fd})).json();
    pr.textContent=d.ok?'Загружено: '+d.name:'Ошибка: '+d.error;
    if(d.ok){loadFiles();fi.value='';}
  }catch(e){pr.textContent='Ошибка сети при загрузке';}
}
loadSt();
loadFiles();
setInterval(loadSt,8000);
</script>
</body>
</html>)rawhtml";

// ─── Вспомогательные функции ──────────────────────────────────────────────────

static int _count_gcode_files() {
    File dir = LittleFS.open(GCODE_DIR);
    if (!dir || !dir.isDirectory()) return 0;
    int n = 0;
    File f = dir.openNextFile();
    while (f) { if (!f.isDirectory()) n++; f = dir.openNextFile(); }
    return n;
}

static const char* _basename(const char* path) {
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void _notify_connected() {
    if (!s_conn_cb) return;
    bool ap = (s_state == GCW_AP);
    static char ip_buf[20];
    if (ap)
        WiFi.softAPIP().toString().toCharArray(ip_buf, sizeof(ip_buf));
    else
        WiFi.localIP().toString().toCharArray(ip_buf, sizeof(ip_buf));
    s_conn_cb(ip_buf, ap);
}

// ─── AsyncWebServer endpoints ─────────────────────────────────────────────────

static void _start_server() {
    if (s_srv_started) return;
    s_srv_started = true;

    s_srv.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", HTML_PAGE);
    });

    s_srv.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        bool ap = (s_state == GCW_AP);
        String ip = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        const char* ssid_str = ap ? WIFI_AP_SSID : WIFI_STA_SSID;
        uint32_t total = LittleFS.totalBytes();
        uint32_t used  = LittleFS.usedBytes();
        int fc = _count_gcode_files();
        char buf[300];
        snprintf(buf, sizeof(buf),
            "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
            "\"fc\":%d,\"tb\":%lu,\"fb\":%lu}",
            ap ? "AP" : "STA",
            ssid_str, ip.c_str(),
            fc, (unsigned long)total, (unsigned long)(total - used));
        req->send(200, "application/json", buf);
    });

    s_srv.on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req) {
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
                        _basename(f.name()), (unsigned long)f.size());
                    json += entry;
                }
                f = dir.openNextFile();
            }
        }
        json += "]";
        req->send(200, "application/json", json);
    });

    s_srv.on("/api/file", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("name")) {
            req->send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }
        String path = String(GCODE_DIR) + "/" + req->getParam("name")->value();
        if (!LittleFS.exists(path)) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        req->send(LittleFS, path, "text/plain");
    });

    s_srv.on("/api/file", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("name")) {
            req->send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }
        String path = String(GCODE_DIR) + "/" + req->getParam("name")->value();
        req->send(LittleFS.remove(path) ? 200 : 404,
            "application/json",
            LittleFS.remove(path) ? "{\"ok\":true}" : "{\"error\":\"not found\"}");
    });

    s_srv.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (req->_tempObject) {
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"name\":\"%s\"}",
                    (char*)req->_tempObject);
                req->send(200, "application/json", buf);
                free(req->_tempObject);
                req->_tempObject = nullptr;
            } else {
                req->send(500, "application/json", "{\"error\":\"upload failed\"}");
            }
        },
        [](AsyncWebServerRequest* req, const String& filename,
           size_t index, uint8_t* data, size_t len, bool final) {
            static File upload_file;
            if (index == 0) {
                String path = String(GCODE_DIR) + "/" + filename;
                Serial.printf("[gcode] Upload: %s\n", path.c_str());
                upload_file = LittleFS.open(path, "w");
                if (!req->_tempObject) {
                    req->_tempObject = malloc(64);
                    if (req->_tempObject)
                        strncpy((char*)req->_tempObject, filename.c_str(), 63);
                }
            }
            if (upload_file) upload_file.write(data, len);
            if (final && upload_file) {
                upload_file.close();
                Serial.printf("[gcode] Upload done: %s (%u B)\n",
                    filename.c_str(), (unsigned)(index + len));
            }
        }
    );

    s_srv.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    s_srv.begin();
    Serial.println("[gcode] Web server started on port 80");
    _notify_connected();
}

static void _start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    s_state = GCW_AP;
    Serial.printf("[gcode] AP: SSID=%s  IP=%s\n",
        WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    Serial.printf("[gcode] Browser: http://%s\n",
        WiFi.softAPIP().toString().c_str());
    _start_server();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void GCodeWiFi_SetConnectedCallback(GCWifiConnectedCb cb) {
    s_conn_cb = cb;
}

void GCodeWiFi_Init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[gcode] LittleFS FAILED");
    } else {
        if (!LittleFS.exists(GCODE_DIR)) LittleFS.mkdir(GCODE_DIR);
        Serial.printf("[gcode] LittleFS OK: %lu/%lu B\n",
            (unsigned long)LittleFS.usedBytes(),
            (unsigned long)LittleFS.totalBytes());
    }

    const char* ssid = WIFI_STA_SSID;
    const char* pass = WIFI_STA_PASS;

    if (ssid && ssid[0] != '\0') {
        Serial.printf("[gcode] WiFi STA → %s\n", ssid);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(ssid, pass);
        s_state     = GCW_CONNECTING;
        s_connect_t = millis();
    } else {
        Serial.println("[gcode] WIFI_STA_SSID empty → AP mode");
        _start_ap();
    }
}

void GCodeWiFi_Process() {
    if (s_state != GCW_CONNECTING) return;

    if (WiFi.status() == WL_CONNECTED) {
        s_state = GCW_CONNECTED;
        Serial.printf("[gcode] STA connected  IP: %s\n",
            WiFi.localIP().toString().c_str());
        Serial.printf("[gcode] Browser: http://%s\n",
            WiFi.localIP().toString().c_str());
        _start_server();
    } else if (millis() - s_connect_t > WIFI_STA_TIMEOUT_MS) {
        Serial.println("[gcode] STA timeout → AP mode");
        WiFi.disconnect(true);
        delay(100);
        _start_ap();
    }
}

const char* GCodeWiFi_GetIP() {
    static char buf[20];
    if (s_state == GCW_CONNECTED)
        WiFi.localIP().toString().toCharArray(buf, sizeof(buf));
    else if (s_state == GCW_AP)
        WiFi.softAPIP().toString().toCharArray(buf, sizeof(buf));
    else
        buf[0] = '\0';
    return buf;
}

bool GCodeWiFi_IsConnected() {
    return s_state == GCW_CONNECTED || s_state == GCW_AP;
}

bool GCodeWiFi_IsAP() {
    return s_state == GCW_AP;
}
