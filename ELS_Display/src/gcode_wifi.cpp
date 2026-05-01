/**
 * @file gcode_wifi.cpp
 * @brief WiFi + LittleFS + AsyncWebServer для хранения и управления GCode файлами.
 *
 * Логика подключения:
 *   1. Читаем SSID/пароль из Preferences ("gcode_wifi" / "ssid" + "pass")
 *   2. Если есть — стартуем STA, ждём 12 сек (неблокирующе в GCodeWiFi_Process)
 *   3. Таймаут или нет настроек → AP "ELS-Setup" / "12345678"
 *   4. Как только сеть готова — стартуем AsyncWebServer на порту 80
 *
 * Конфигурация WiFi через веб: POST /api/wifi  (form: ssid=...&pass=...)
 * После сохранения ESP32 перезагружается.
 */

#include "gcode_wifi.h"
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

// ─── Константы ───────────────────────────────────────────────────────────────
static const char* AP_SSID       = "ELS-Setup";
static const char* AP_PASS       = "12345678";
static const char* PREFS_NS      = "gcode_wifi";
static const char* GCODE_DIR     = "/gcode";
static const uint32_t STA_TIMEOUT_MS = 12000;

// ─── Состояние модуля ─────────────────────────────────────────────────────────
enum GcWifiSt { GCW_INIT, GCW_CONNECTING, GCW_CONNECTED, GCW_AP };
static GcWifiSt       s_state         = GCW_INIT;
static uint32_t       s_connect_t     = 0;
static AsyncWebServer s_srv(80);
static bool           s_srv_started   = false;
static bool           s_restart_req   = false;
static uint32_t       s_restart_t     = 0;

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
input[type=text],input[type=password],input[type=file]{background:#0d2137;color:#e0e0f0;border:1px solid #3a5068;padding:5px;border-radius:4px;font-family:monospace;font-size:12px}
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
<h2>WiFi настройки</h2>
<div id="wi" style="font-size:12px;margin-bottom:8px;color:#aac"></div>
<div style="display:flex;gap:6px;flex-wrap:wrap;align-items:center">
<input type="text" id="ss" placeholder="SSID" style="width:140px">
<input type="password" id="pw" placeholder="Пароль" style="width:120px">
<button onclick="svWifi()">Сохранить</button>
</div>
<div style="font-size:11px;color:#667;margin-top:6px">После сохранения ESP32 перезагрузится и подключится к новой сети.</div>
</div>

<script>
async function loadSt(){
  try{
    const d=await(await fetch('/api/status')).json();
    document.getElementById('st').innerHTML=
      'WiFi: <b>'+d.mode+'</b> | IP: <b>'+d.ip+'</b> | '+
      'Файлов: '+d.fc+' | Свободно: '+(d.fb/1024).toFixed(0)+' КБ';
    document.getElementById('wi').textContent=
      'Режим: '+d.mode+', SSID: '+d.ssid+', IP: '+d.ip;
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
  }catch(e){document.getElementById('fl').innerHTML='<li><span>Ошибка загрузки списка</span></li>';}
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
async function svWifi(){
  const ss=document.getElementById('ss').value.trim();
  const pw=document.getElementById('pw').value;
  if(!ss){alert('Введите SSID');return;}
  if(!confirm('Сохранить WiFi SSID="'+ss+'" и перезагрузить ESP32?'))return;
  try{
    const r=await fetch('/api/wifi',{
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'ssid='+encodeURIComponent(ss)+'&pass='+encodeURIComponent(pw)
    });
    const d=await r.json();
    alert(d.ok?'Сохранено! ESP32 перезагружается...':'Ошибка: '+d.error);
  }catch(e){alert('Нет ответа — вероятно ESP32 уже перезагружается');}
}
loadSt();
loadFiles();
setInterval(loadSt,8000);
</script>
</body>
</html>)rawhtml";

// ─── Вспомогательные функции ──────────────────────────────────────────────────

// Считает файлы в GCODE_DIR
static int _count_gcode_files() {
    File dir = LittleFS.open(GCODE_DIR);
    if (!dir || !dir.isDirectory()) return 0;
    int n = 0;
    File f = dir.openNextFile();
    while (f) { if (!f.isDirectory()) n++; f = dir.openNextFile(); }
    return n;
}

// Извлекает имя файла из полного пути (всё после последнего '/')
static const char* _basename(const char* path) {
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// ─── Запуск AsyncWebServer ────────────────────────────────────────────────────

static void _start_server() {
    if (s_srv_started) return;
    s_srv_started = true;

    // GET / — HTML веб-интерфейс
    s_srv.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", HTML_PAGE);
    });

    // GET /api/status
    s_srv.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        bool ap = (s_state == GCW_AP);
        String ip = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        String ssid_str;
        {
            Preferences p;
            p.begin(PREFS_NS, true);
            ssid_str = p.getString("ssid", ap ? String(AP_SSID) : "");
            p.end();
        }
        uint32_t total = LittleFS.totalBytes();
        uint32_t used  = LittleFS.usedBytes();
        int fc = _count_gcode_files();
        char buf[300];
        snprintf(buf, sizeof(buf),
            "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
            "\"fc\":%d,\"tb\":%lu,\"fb\":%lu}",
            ap ? "AP" : "STA",
            ssid_str.c_str(), ip.c_str(),
            fc, (unsigned long)total, (unsigned long)(total - used));
        req->send(200, "application/json", buf);
    });

    // GET /api/files — список файлов в /gcode
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

    // GET /api/file?name=... — содержимое файла
    s_srv.on("/api/file", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("name")) {
            req->send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }
        String name = req->getParam("name")->value();
        String path = String(GCODE_DIR) + "/" + name;
        if (!LittleFS.exists(path)) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }
        req->send(LittleFS, path, "text/plain");
    });

    // DELETE /api/file?name=... — удалить файл
    s_srv.on("/api/file", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("name")) {
            req->send(400, "application/json", "{\"error\":\"missing name\"}");
            return;
        }
        String name = req->getParam("name")->value();
        String path = String(GCODE_DIR) + "/" + name;
        if (LittleFS.remove(path)) {
            req->send(200, "application/json", "{\"ok\":true}");
        } else {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        }
    });

    // POST /api/upload — multipart upload GCode файла
    s_srv.on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            // Финальный ответ после завершения upload handler-а
            if (req->_tempObject) {
                // Имя файла сохранено в _tempObject
                char* fname = (char*)req->_tempObject;
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"name\":\"%s\"}", fname);
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
                Serial.printf("[gcode] Upload start: %s\n", path.c_str());
                upload_file = LittleFS.open(path, "w");
                // Сохраняем имя файла для финального ответа
                if (!req->_tempObject) {
                    req->_tempObject = malloc(64);
                    if (req->_tempObject)
                        strncpy((char*)req->_tempObject, filename.c_str(), 63);
                }
            }
            if (upload_file) upload_file.write(data, len);
            if (final) {
                if (upload_file) {
                    upload_file.close();
                    Serial.printf("[gcode] Upload done: %s (%u bytes)\n",
                        filename.c_str(), (unsigned)(index + len));
                }
            }
        }
    );

    // POST /api/wifi — сохранить SSID/пароль, перезагрузить
    // Body: application/x-www-form-urlencoded: ssid=...&pass=...
    s_srv.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid", true)) {
            req->send(400, "application/json", "{\"error\":\"missing ssid\"}");
            return;
        }
        String ssid_val = req->getParam("ssid", true)->value();
        String pass_val = req->hasParam("pass", true)
            ? req->getParam("pass", true)->value() : "";
        {
            Preferences p;
            p.begin(PREFS_NS, false);
            p.putString("ssid", ssid_val);
            p.putString("pass", pass_val);
            p.end();
        }
        req->send(200, "application/json", "{\"ok\":true}");
        Serial.printf("[gcode] WiFi saved: ssid=%s, restarting...\n", ssid_val.c_str());
        s_restart_req = true;
        s_restart_t   = millis();
    });

    s_srv.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    s_srv.begin();
    Serial.println("[gcode] Web server started on port 80");
}

// ─── Запуск AP режима ─────────────────────────────────────────────────────────
static void _start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    s_state = GCW_AP;
    Serial.printf("[gcode] AP mode: SSID=%s, IP=%s\n",
        AP_SSID, WiFi.softAPIP().toString().c_str());
    Serial.printf("[gcode] Open browser: http://%s\n",
        WiFi.softAPIP().toString().c_str());
    _start_server();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void GCodeWiFi_Init() {
    // Монтируем LittleFS (formatOnFail=true для первого запуска)
    if (!LittleFS.begin(true)) {
        Serial.println("[gcode] LittleFS FAILED");
    } else {
        if (!LittleFS.exists(GCODE_DIR)) LittleFS.mkdir(GCODE_DIR);
        Serial.printf("[gcode] LittleFS OK: %lu/%lu bytes used\n",
            (unsigned long)LittleFS.usedBytes(),
            (unsigned long)LittleFS.totalBytes());
    }

    // Читаем сохранённые credentials
    Preferences p;
    p.begin(PREFS_NS, true);
    String ssid = p.getString("ssid", "");
    String pass = p.getString("pass", "");
    p.end();

    if (ssid.length() > 0) {
        Serial.printf("[gcode] WiFi STA → %s\n", ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(ssid.c_str(), pass.c_str());
        s_state     = GCW_CONNECTING;
        s_connect_t = millis();
    } else {
        _start_ap();
    }
}

void GCodeWiFi_Process() {
    // Отложенная перезагрузка (после ответа /api/wifi)
    if (s_restart_req && millis() - s_restart_t > 800) {
        ESP.restart();
    }

    if (s_state != GCW_CONNECTING) return;

    if (WiFi.status() == WL_CONNECTED) {
        s_state = GCW_CONNECTED;
        Serial.printf("[gcode] WiFi connected, IP: %s\n",
            WiFi.localIP().toString().c_str());
        Serial.printf("[gcode] Open browser: http://%s\n",
            WiFi.localIP().toString().c_str());
        _start_server();
    } else if (millis() - s_connect_t > STA_TIMEOUT_MS) {
        Serial.println("[gcode] WiFi STA timeout → AP mode");
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
