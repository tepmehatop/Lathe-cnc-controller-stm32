/**
 * gcode_wifi.cpp
 * WiFi (STA→AP fallback) + LittleFS + простой HTTP сервер на WiFiServer.
 *
 * WiFi и HTTP работают в отдельной задаче FreeRTOS: вызовы WiFi (подключение, softAP,
 * таймауты связи с WiFi-чипом) и загрузка файлов длятся секундами и не должны
 * останавливать отрисовку UI и приём UART в loop(). Всё, что трогает LVGL или
 * исполнитель G-кода, передаётся в loop() через запросы и выполняется в
 * GCodeWiFi_Process().
 */

#include "gcode_wifi.h"
#include "gcode_exec.h"
#ifdef ELS_RELEASE
#include "wifi_config.example.h"   // релиз (tools/make_release.sh): только шаблон, без чужих сетей и паролей
#else
#include "wifi_config.h"
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#if CONFIG_ESP_HOSTED_ENABLED
#include "esp_hosted.h"
#include "esp_hosted_host_fw_ver.h"
#endif

// ─── Состояние ────────────────────────────────────────────────────────────────

static const char* GCODE_DIR    = "/gcode";
static const uint16_t HTTP_PORT = 80;
static bool s_mdns_started = false;

// GCW_STACK_WAIT: STA ассоциирован, ждём ~2с пока поднимется TCP/IP (без блокировки loop)
enum GcWifiSt { GCW_INIT, GCW_CONNECTING, GCW_STACK_WAIT, GCW_CONNECTED, GCW_AP };
static GcWifiSt          s_state      = GCW_INIT;
static uint32_t          s_conn_t     = 0;
static uint32_t          s_ap_retry_t = 0;   // повторная попытка STA из AP-режима
static uint32_t          s_srv_start_t  = 0; // когда запустили сервер
static uint32_t          s_last_client_t = 0; // последний входящий клиент
static WiFiServer*       s_srv     = nullptr;
static GCWifiConnectedCb s_conn_cb = nullptr;
static char              s_ip[20]  = {};
static char              s_notified_ip[20] = {};  // последний адрес, о котором показан алерт

// Запросы из WiFi-задачи в loop() (под s_req_mux)
enum GcwReq { GCW_REQ_NONE, GCW_REQ_RUN, GCW_REQ_STOP, GCW_REQ_PAUSE };
static portMUX_TYPE      s_req_mux = portMUX_INITIALIZER_UNLOCKED;
static GcwReq            s_req = GCW_REQ_NONE;
static char              s_req_path[96] = {};
static bool              s_notify_pending = false;
static char              s_notify_ip[20] = {};
static bool              s_notify_ap = false;

// Сеть роутера — из NVS (задаётся на экране: НАСТРОЙКИ → WI-FI); нет в NVS — из wifi_config.h.
// Запросы экрана (подключиться/забыть/поиск) выполняет WiFi-задача — WiFi-вызовы длятся секундами.
static char              s_sta_ssid[33] = {};
static char              s_sta_pass[65] = {};
static portMUX_TYPE      s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static bool              s_cfg_req = false;      // новая сеть или «забыть» (ssid пустой)
static char              s_new_ssid[33] = {}, s_new_pass[65] = {};
static volatile bool     s_scan_req = false, s_scan_busy = false;
static char              s_scan_list[GCW_SCAN_MAX][33];
static volatile int      s_scan_n = -1;          // -1 — поиска ещё не было

static void sta_load() {
    Preferences p;
    if (p.begin("wifi", true)) {
        String ss = p.getString("ssid", ""), pw = p.getString("pass", "");
        bool have = p.isKey("ssid");
        p.end();
        if (have) {                      // сохранено с экрана (пустой — «забыть»: только точка доступа)
            strlcpy(s_sta_ssid, ss.c_str(), sizeof(s_sta_ssid));
            strlcpy(s_sta_pass, pw.c_str(), sizeof(s_sta_pass));
            return;
        }
    }
    strlcpy(s_sta_ssid, WIFI_STA_SSID, sizeof(s_sta_ssid));
    strlcpy(s_sta_pass, WIFI_STA_PASS, sizeof(s_sta_pass));
}

static void post_exec_request(GcwReq req, const char* path = nullptr) {
    portENTER_CRITICAL(&s_req_mux);
    s_req = req;
    if (path) {
        strncpy(s_req_path, path, sizeof(s_req_path) - 1);
        s_req_path[sizeof(s_req_path) - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_req_mux);
}

// ─── HTML ─────────────────────────────────────────────────────────────────────

static const char HTML_PAGE[] = R"rawhtml(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Токарный станок — G-код</title>
<style>
:root{--bg:#0f1420;--card:#171e2d;--line:#263247;--text:#e6ebf3;--dim:#8a97ab;--acc:#3fb6f0;--ok:#43c463;--warn:#f0a330;--err:#ef5350;--done:#ff8a50}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font:15px/1.45 -apple-system,"Segoe UI",Roboto,Arial,sans-serif;padding:14px;max-width:1100px;margin:0 auto}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px 16px;margin-bottom:12px}
h1{font-size:20px;font-weight:600}
#net{color:var(--dim);font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:12px}
h2{font-size:14px;font-weight:600;color:var(--dim);text-transform:uppercase;letter-spacing:.04em;margin-bottom:10px}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
select,input[type=file]{background:#0c111b;color:var(--text);border:1px solid var(--line);border-radius:7px;padding:8px 10px;font-size:15px}
select{flex:1;min-width:180px}
button{border:0;border-radius:7px;padding:9px 16px;font-size:15px;font-weight:600;color:#fff;background:#2b6cb0;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
.go{background:#2f9e4f}.pause{background:#c27c12}.stop{background:#c83a37}.ghost{background:#24304a}
#state{font-size:18px;font-weight:600;margin:12px 0 6px}
.bar{height:8px;background:#0c111b;border-radius:4px;overflow:hidden}
.bar div{height:100%;width:0;background:var(--acc);transition:width .4s}
#err{display:none;margin-top:10px;padding:10px 12px;border-radius:7px;background:#3a1618;color:#ffb3b0;font-weight:600}
#cv{width:100%;height:340px;display:block;background:#0b1019;border-radius:8px}
.legend{display:flex;flex-wrap:wrap;gap:6px 16px;margin-top:8px;color:var(--dim);font-size:13px}
.legend i{display:inline-block;width:22px;height:0;vertical-align:middle;margin-right:6px;border-top:3px solid}
#info{color:var(--dim);font-size:13px;margin-top:6px}
#code{max-height:300px;overflow:auto;background:#0b1019;border-radius:8px;font:13px/1.55 ui-monospace,Menlo,Consolas,monospace;padding:6px 0}
#code div{padding:0 10px;white-space:pre}
#code div.cur{background:#3a2a14;color:#ffd08a}
#code div.done{color:#6f7d92}
#code div.cm{color:#6b8f71;font-style:italic}
#code span{display:inline-block;width:3.2em;color:#4c5b74;text-align:right;margin-right:12px}
#files div{display:flex;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid var(--line)}
#files div:last-child{border-bottom:0}
#files b{flex:1;font-weight:500;word-break:break-all}
#files small{color:var(--dim)}
#files button{padding:6px 12px;font-size:14px}
#upmsg{color:var(--dim);font-size:13px;margin-top:8px}
dialog{margin:auto;background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:12px;padding:16px;max-width:640px;width:calc(100% - 24px)}
dialog::backdrop{background:#000a}
dialog h3{font-size:18px;margin-bottom:10px}
dialog svg{width:100%;height:auto;border-radius:8px;margin-bottom:10px}
dialog ol{padding-left:22px}dialog li{margin:6px 0}
dialog b{color:#ffd54a}
.hint{color:var(--dim);font-size:13px;margin-top:8px}
</style>
</head>
<body>
<header><h1>Токарный станок — G-код</h1><span id="net">…</span></header>

<div class="card">
<h2>Программа</h2>
<div class="row">
<select id="sel" onchange="pick(this.value)"><option value="">— выберите файл —</option></select>
<button class="go" id="bgo" onclick="run()">Старт</button>
<button class="pause" id="bpa" onclick="post('/api/pause')">Пауза</button>
<button class="stop" onclick="post('/api/stop')">Стоп</button>
</div>
<div id="state">Нет программы</div>
<div class="bar"><div id="pf"></div></div>
<div id="err"></div>
</div>

<dialog id="dlg">
<h3>Перед стартом</h3>
<svg viewBox="0 0 560 250" font-family="-apple-system,Segoe UI,Arial,sans-serif" font-size="13">
<rect width="560" height="250" fill="#0b1019" rx="8"/>
<!-- патрон -->
<rect x="14" y="38" width="46" height="154" rx="4" fill="#2a3550"/>
<rect x="44" y="62" width="22" height="20" fill="#3b4a6b"/><rect x="44" y="148" width="22" height="20" fill="#3b4a6b"/>
<text x="16" y="210" fill="#8a97ab">патрон</text>
<!-- заготовка -->
<rect x="60" y="80" width="300" height="70" fill="#5d6b80"/>
<rect x="60" y="80" width="300" height="8" fill="#7b8aa1"/>
<text x="150" y="122" fill="#e6ebf3">заготовка</text>
<!-- ось шпинделя -->
<line x1="8" y1="115" x2="545" y2="115" stroke="#3a4a66" stroke-dasharray="10 4 2 4"/>
<text x="474" y="128" fill="#5b6b82">X = 0</text>
<!-- ноль -->
<circle cx="360" cy="115" r="5" fill="#43c463"/>
<text x="368" y="150" fill="#43c463">ноль: торец на оси</text>
<!-- диаметр -->
<line x1="90" y1="80" x2="90" y2="150" stroke="#e070ff" stroke-width="1.5"/>
<path d="M90 80 l-4 8 h8z M90 150 l-4 -8 h8z" fill="#e070ff"/>
<text x="96" y="146" fill="#e070ff">⌀</text>
<!-- резец 1: торец -->
<polygon points="362,96 400,84 400,108" fill="#ffd54a"/>
<rect x="400" y="88" width="60" height="16" fill="#9a8a50"/>
<text x="370" y="72" fill="#ffd54a">① коснуться торца → Z = 0</text>
<!-- резец 2: диаметр -->
<polygon points="250,78 238,40 262,40" fill="#3fb6f0"/>
<rect x="242" y="14" width="16" height="26" fill="#3a6f8f"/>
<text x="268" y="34" fill="#3fb6f0">② коснуться диаметра →</text>
<text x="268" y="50" fill="#3fb6f0">ввести ⌀ заготовки</text>
<!-- оси -->
<line x1="330" y1="222" x2="420" y2="222" stroke="#4da6ff" stroke-width="2"/>
<path d="M428 222 l-10 -5 v10z" fill="#4da6ff"/>
<text x="434" y="226" fill="#4da6ff">Z+ продольная</text>
<line x1="330" y1="222" x2="330" y2="172" stroke="#e070ff" stroke-width="2"/>
<path d="M330 164 l-5 10 h10z" fill="#e070ff"/>
<text x="338" y="180" fill="#e070ff">X+ поперечная</text>
</svg>
<ol id="steps"></ol>
<div class="hint">Первый раз — прогон «в воздухе»: поставьте ноль Z на 50 мм правее торца, резец пройдёт всю программу, не касаясь детали.</div>
<div class="row" style="margin-top:12px;justify-content:flex-end">
<button class="ghost" onclick="$('dlg').close()">Отмена</button>
<button class="go" onclick="$('dlg').close();go()">Всё сделано — Старт</button>
</div>
</dialog>

<div class="card">
<h2>Траектория резца</h2>
<canvas id="cv"></canvas>
<div class="legend">
<span><i style="border-color:#5b6b82;border-top-style:dashed"></i>ускоренный ход G0</span>
<span><i style="border-color:#3fb6f0"></i>резание G1/G2/G3</span>
<span><i style="border-color:#ff8a50"></i>уже выполнено</span>
<span><i style="border-color:#ffd54a;border-top-width:8px;width:8px;border-radius:4px"></i>резец сейчас</span>
</div>
<div id="info"></div>
</div>

<div class="card">
<h2>Код программы</h2>
<div id="code"><div>Выберите файл</div></div>
</div>

<div class="card">
<h2>Файлы</h2>
<div id="files"></div>
<div class="row" style="margin-top:10px">
<input type="file" id="fi" accept=".nc,.gcode,.ngc,.tap,.txt">
<button onclick="up()">Загрузить</button>
</div>
<div id="upmsg">Обычный текстовый G-код: .nc .gcode .ngc .tap .txt</div>
</div>

<script>
const $=id=>document.getElementById(id);
let prog=null, curFile='', lastLine=-1, planErr='', curStep=0, lastStep=-1;
// время: 75 → «1 мин 15 с», 4000 → «1 ч 07 мин»
function dur(s){s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),c=s%60;
  return h?h+' ч '+String(m).padStart(2,'0')+' мин':(m?m+' мин '+String(c).padStart(2,'0')+' с':c+' с');}
let estTxt='';
const STATE={IDLE:'Нет программы',RUNNING:'Выполняется',WAITING_ACK:'Выполняется',DWELL:'Пауза G4',PAUSED:'Пауза',DONE:'Готово',ERROR:'Ошибка'};
const COLOR={RUNNING:'#43c463',WAITING_ACK:'#43c463',DWELL:'#3fb6f0',PAUSED:'#f0a330',DONE:'#3fb6f0',ERROR:'#ef5350'};

async function j(u,o){const r=await fetch(u,o);return r.json();}
async function post(u){try{await fetch(u,{method:'POST'});}catch(e){}}

async function net(){
  try{const d=await j('/api/status');
    $('net').textContent=(d.mode=='STA'?'Wi-Fi: '+d.ssid:'Точка доступа')+' · '+d.ip+' · свободно '+(d.fb/1024|0)+' КБ';
  }catch(e){$('net').textContent='нет связи со станком';}
}

async function files(){
  try{
    const fl=await j('/api/files'), box=$('files'), sel=$('sel'), keep=sel.value;
    box.innerHTML=''; while(sel.options.length>1) sel.remove(1);
    if(!fl.length) box.innerHTML='<div><b style="color:#8a97ab">Файлов нет — загрузите программу</b></div>';
    fl.forEach(f=>{
      const d=document.createElement('div');
      d.innerHTML='<b>'+f.name+'</b><small>'+(f.size>=1024?(f.size/1024).toFixed(1)+' КБ':f.size+' Б')+'</small>'+
        '<button class="ghost">Открыть</button><button class="stop">Удалить</button>';
      d.children[2].onclick=()=>{sel.value=f.name;pick(f.name);};
      d.children[3].onclick=()=>del(f.name);
      box.appendChild(d);
      const o=document.createElement('option'); o.value=o.textContent=f.name; sel.appendChild(o);
    });
    sel.value=curFile||keep;
  }catch(e){}
}
async function del(n){
  if(!confirm('Удалить «'+n+'»?')) return;
  await fetch('/api/file?name='+encodeURIComponent(n),{method:'DELETE'}); files();
}
async function up(){
  const f=$('fi').files[0]; if(!f){$('upmsg').textContent='Выберите файл на компьютере';return;}
  $('upmsg').textContent='Загрузка…';
  try{const d=await j('/api/file?name='+encodeURIComponent(f.name),{method:'PUT',body:f});
    $('upmsg').textContent=d.ok?'Загружено: '+d.name:'Ошибка: '+d.error;
    if(d.ok){$('fi').value='';await files();$('sel').value=d.name;pick(d.name);}
  }catch(e){$('upmsg').textContent='Ошибка сети';}
}
// «Старт» — сначала окно: как поставить ноль и что подготовить (по числам выбранной программы)
async function run(){
  const n=$('sel').value; if(!n){alert('Выберите файл');return;}
  if(curFile!==n) await pick(n);
  const f=v=>+v.toFixed(2);
  let st=null,zmin=0;
  for(const s of prog?prog.segs:[]){if(!st&&s.r)st=s.b;if(!s.r)for(const p of [s.a,s.b])zmin=Math.min(zmin,p[0]);}
  const tools=[];
  for(const l of prog?prog.src:[]){const m=l.t.match(/\bT(\d{2,4})\b/i);if(m){const c=(l.t.match(/\(([^)]*)\)/)||[])[1];tools.push('T'+m[1]+(c?' — '+c:''));}}
  const li=[];
  li.push('<b>Заготовка</b>: круглая, ⌀ меньше '+(st?f(2*st[1]):'?')+' мм — с этого ⌀ резец подходит к ней ускоренно; торчит из патрона больше чем на '+f(Math.abs(zmin)+5)+' мм (программа режет до Z '+f(zmin)+').');
  li.push('<b>Ноль Z — торец</b> ①: подведите резец к торцу до лёгкого касания (или подрежьте торец), на дисплее — двойной тап по «ПОЗИЦИЯ Z»: станет 0.');
  li.push('<b>Ноль X — ось</b> ②: коснитесь резцом наружного диаметра (или проточите пояском и измерьте штангенциркулем), на дисплее в режиме G-КОД — долгое нажатие на «⌀ ДИАМЕТР X» и введите этот диаметр.');
  li.push('<b>Отведите резец</b> правее торца и выше заготовки. Первым ходом станок сам поедет в точку старта'+(st?' Z '+f(st[0])+', ⌀ '+f(2*st[1]):'')+'.');
  li.push('<b>Резец</b>: '+(tools.length?[...new Set(tools)].join('; '):'в программе не указан — проходной')+'. Смены резца станок не делает.');
  li.push('<b>Шпиндель</b>: включите вручную'+(/G\s*9[59]/i.test(prog?prog.src.map(l=>l.t).join('\n'):'')?' — подача на оборот, ≈600 об/мин':'')+'. На станке — режим M8 (G-КОД).');
  $('steps').innerHTML=li.map(x=>'<li>'+x+'</li>').join('');
  $('dlg').showModal();
}
async function go(){ await post('/api/run?name='+encodeURIComponent($('sel').value)); }

// ── Разбор программы — как исполнитель станка: строки без комментариев, пустые не считаются ──
// plan — развёрнутая станком программа (циклы G71/G70): [[номер строки, кадр], …]; рисуется она
function parse(text,plan){
  const src=[], segs=[], cmds=[]; let n=0;
  let z=0,x=0, abs=true, diam=true, unit=1, mot=0;
  for(const raw of text.split(/\r?\n/)){
    const ln=raw.replace(/\([^)]*\)/g,'').replace(/;.*/,'').trim().toUpperCase();
    if(!ln){src.push({t:raw,n:0});continue;}
    n++; src.push({t:raw,n}); cmds.push([n,ln]);
  }
  let k=0;                                   // k — номер кадра развёрнутой программы (step исполнителя)
  for(const [n,ln] of (plan||cmds)){
    k++;
    if(/^[O%]/.test(ln)) continue;
    const w=c=>{const m=ln.match(new RegExp('(?:^|[^A-Z])'+c+'\\s*(-?[\\d.]+)'));return m?parseFloat(m[1]):null;};
    for(const g of ln.matchAll(/G\s*(\d+)/g)){
      const v=+g[1];
      if(v<=3) mot=v; else if(v==7) diam=true; else if(v==8) diam=false;
      else if(v==20) unit=25.4; else if(v==21) unit=1; else if(v==90) abs=true; else if(v==91) abs=false;
      else if(v==28){if(/[UW]/.test(ln))continue;segs.push({n,k,a:[z,x],b:[z,0],r:1});segs.push({n,k,a:[z,0],b:[0,0],r:1});z=0;x=0;}
    }
    let Z=w('Z'), X=w('X');
    if(Z===null&&X===null) continue;
    let tz=Z===null?z:(abs?Z*unit:z+Z*unit);
    let tx=X===null?x:(abs?X*unit/(diam?2:1):x+X*unit/(diam?2:1));
    if(mot>=2){
      let I=w('I'),K=w('K'),R=w('R'),cz,cx; const cw=mot==2;
      if(I!==null||K!==null){cz=z+(K||0)*unit;cx=x+(I||0)*unit;}
      else if(R!==null){
        R*=unit; const mz=(z+tz)/2,mx=(x+tx)/2,dz=tz-z,dx=tx-x,d=Math.hypot(dz,dx);
        const h=Math.sqrt(Math.max(R*R-d*d/4,0)), s=(cw?-1:1)*(R>0?1:-1);
        cz=mz-s*h*dx/d; cx=mx+s*h*dz/d;
      }
      if(cz!==undefined){
        let a0=Math.atan2(x-cx,z-cz),a1=Math.atan2(tx-cx,tz-cz),da=a1-a0,r=Math.hypot(z-cz,x-cx);
        if(cw){while(da>=0)da-=2*Math.PI;}else{while(da<=0)da+=2*Math.PI;}
        const k=Math.max(4,Math.ceil(Math.abs(da)*r/0.2)); let pz=z,px=x;
        for(let i=1;i<=k;i++){const a=a0+da*i/k,qz=i==k?tz:cz+r*Math.cos(a),qx=i==k?tx:cx+r*Math.sin(a);
          segs.push({n,k,a:[pz,px],b:[qz,qx],r:0});pz=qz;px=qx;}
        z=tz;x=tx;continue;
      }
    }
    segs.push({n,k,a:[z,x],b:[tz,tx],r:mot==0?1:0}); z=tz; x=tx;
  }
  return {src,segs,lines:n,plan:!!plan};
}

async function pick(n){
  if(!n){prog=null;curFile='';draw(-1);return;}
  try{
    const t=await(await fetch('/api/file?name='+encodeURIComponent(n))).text();
    let plan=null; planErr='';
    if(/G0*7[01]\b/i.test(t.replace(/\([^)]*\)/g,''))){          // циклы — рисуем развёрнутые станком проходы
      const p=await(await fetch('/api/plan?name='+encodeURIComponent(n))).text();
      if(p.startsWith('ERR\t')) planErr=p.slice(4);
      else plan=p.split('\n').filter(l=>l).map(l=>{const i=l.indexOf('\t');return [+l.slice(0,i),l.slice(i+1)];});
    }
    prog=parse(t,plan); curFile=n; lastLine=-1; $('sel').value=n;
    estTxt='';
    try{const e=await j('/api/info?name='+encodeURIComponent(n));
      if(e.sec>=0) estTxt='Время ≈ '+dur(e.sec)+(/G\s*9[59]/i.test(t)?' (подача на оборот — при '+e.rpm+' об/мин)':'');}catch(e){}
    const c=$('code'); c.innerHTML='';
    prog.src.forEach(l=>{const d=document.createElement('div');d.dataset.n=l.n;if(!l.n)d.className='cm';
      d.innerHTML='<span>'+(l.n||'')+'</span>'+l.t.replace(/&/g,'&amp;').replace(/</g,'&lt;');c.appendChild(d);});
    draw(-1);
  }catch(e){}
}

// ── Рисунок: Z по горизонтали (патрон слева), X — радиус вверх от оси шпинделя, деталь зеркально внизу ──
function nice(v){const p=Math.pow(10,Math.floor(Math.log10(v))),m=v/p;return (m<1.5?1:m<3?2:m<7?5:10)*p;}
function draw(line){
  const cv=$('cv'),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
  cv.width=W*dpr;cv.height=H*dpr;const g=cv.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
  g.fillStyle='#0b1019';g.fillRect(0,0,W,H);
  g.font='13px -apple-system,Segoe UI,Arial';
  if(!prog||!prog.segs.length){g.fillStyle='#5b6b82';g.fillText(prog?'В программе нет перемещений':'Выберите файл — здесь будет траектория',16,H/2);$('info').textContent='';return;}
  const P=prog.plan&&line>0;                  // циклы: ход — по кадрам развёрнутой программы
  // масштаб — по ходам резания (дальние отводы G0 обрезаются краем), ноль всегда в кадре
  const cut=prog.segs.filter(s=>!s.r), base=cut.length?cut:prog.segs;
  let zmin=0,zmax=0,xmax=0;
  for(const s of base)for(const p of [s.a,s.b]){zmin=Math.min(zmin,p[0]);zmax=Math.max(zmax,p[0]);xmax=Math.max(xmax,Math.abs(p[1]));}
  const pz=((zmax-zmin)||1)*0.08; zmin-=pz; zmax+=pz; xmax*=1.12;
  const L=46,Rp=16,T=16,B=26, zr=(zmax-zmin)||1, xr=xmax||1;
  const sc=Math.min((W-L-Rp)/zr,(H-T-B)/(2*xr)*1.0);
  const cy=T+(H-T-B)/2, ox=L+((W-L-Rp)-zr*sc)/2;
  const tx=z=>ox+(z-zmin)*sc, ty=x=>cy-x*sc;
  // сетка и размеры
  const st=nice(Math.max(zr,2*xr)/8);
  g.strokeStyle='#18233a';g.lineWidth=1;g.fillStyle='#5b6b82';
  for(let z=Math.ceil(zmin/st)*st;z<=zmax+1e-9;z+=st){g.beginPath();g.moveTo(tx(z),T);g.lineTo(tx(z),H-B);g.stroke();g.fillText(+z.toFixed(2)+'',tx(z)-8,H-8);}
  for(let x=0;x<=xr+1e-9;x+=st){for(const s of [1,-1]){g.beginPath();g.moveTo(L,ty(s*x));g.lineTo(W-Rp,ty(s*x));g.stroke();}
    if(x>0)g.fillText('⌀'+(+(2*x).toFixed(2)),4,ty(x)+4);}
  g.fillText('Z, мм',W-Rp-40,H-8);
  g.save();g.beginPath();g.rect(L,T-4,W-L-Rp,H-T-B+8);g.clip();
  // ось шпинделя
  g.strokeStyle='#3a4a66';g.setLineDash([10,4,2,4]);g.beginPath();g.moveTo(L,cy);g.lineTo(W-Rp,cy);g.stroke();g.setLineDash([]);
  // ноль
  g.fillStyle='#43c463';g.beginPath();g.arc(tx(0),cy,4,0,7);g.fill();g.fillText('0',tx(0)+6,cy-6);
  // траектория (верх) и её зеркало (низ, бледно — силуэт детали)
  for(const mir of [-1,1]){
    for(const s of prog.segs){
      const done=P?s.k<curStep:(line>0&&s.n<line), cur=P?s.k==curStep:s.n==line;
      g.lineWidth=mir<0?1:(s.r?1.2:2.2);
      g.setLineDash(s.r?[6,5]:[]);
      g.strokeStyle=mir<0?(s.r?'#1d2638':'#1f4a66'):(s.r?'#5b6b82':(done?'#ff8a50':(cur?'#ffd54a':'#3fb6f0')));
      g.beginPath();g.moveTo(tx(s.a[0]),ty(mir*s.a[1]));g.lineTo(tx(s.b[0]),ty(mir*s.b[1]));g.stroke();
    }
  }
  g.setLineDash([]);g.restore();
  // резец — конец последнего отрезка текущей строки
  if(line>0){let p=null;for(const s of prog.segs)if(P?s.k<=curStep:s.n<=line)p=s.b;
    if(p){g.fillStyle='#ffd54a';g.beginPath();g.moveTo(tx(p[0]),ty(p[1]));g.lineTo(tx(p[0])+9,ty(p[1])-14);g.lineTo(tx(p[0])-9,ty(p[1])-14);g.fill();}}
  let az=0,bz=0,mx=0;for(const s of base)for(const p of [s.a,s.b]){az=Math.min(az,p[0]);bz=Math.max(bz,p[0]);mx=Math.max(mx,Math.abs(p[1]));}
  $('info').textContent=(planErr?'Ошибка цикла: '+planErr+' · ':'')+(estTxt?estTxt+' · ':'')+'Резание: Z от '+az.toFixed(2)+' до '+bz.toFixed(2)+' мм · наибольший ⌀ '+(2*mx).toFixed(2)+' мм · строк '+prog.lines+' · X в программе — диаметр (G7), если не указано G8';
}

async function poll(){
  try{
    const d=await j('/api/exec'), st=d.state||'IDLE';
    let t=STATE[st]||st;
    if(d.file&&st!='IDLE') t+=' · '+d.file;
    if(d.line&&st!='IDLE'&&st!='DONE') t+=' · строка '+d.line+(prog&&curFile==d.file?' из '+prog.lines:'');
    if(st=='RUNNING'||st=='WAITING_ACK'||st=='PAUSED'){t+=' · '+d.pct+'%';if(d.eta>=0)t+=' · осталось ≈ '+dur(d.eta);}
    if(st=='DONE'&&d.total>0) t+=' · расчёт был ≈ '+dur(d.total);
    $('state').textContent=t;$('state').style.color=COLOR[st]||'#e6ebf3';
    $('pf').style.width=(st=='DONE'?100:(d.pct||0))+'%';
    $('err').style.display=d.error?'block':'none';$('err').textContent=d.error||'';
    $('bpa').textContent=st=='PAUSED'?'Продолжить':'Пауза';
    if(d.file&&d.file!==curFile&&st!='IDLE'){$('sel').value=d.file;await pick(d.file);}
    const ln=(st=='DONE')?1e9:d.line;
    curStep=st=='DONE'?1e9:(d.step||0);
    if(prog&&prog.plan&&curStep!==lastStep&&st!='IDLE'){lastStep=curStep;if(ln===lastLine)draw(ln);}
    if(prog&&ln!==lastLine){lastLine=ln;draw(st=='IDLE'?-1:ln);
      for(const el of $('code').children){const n=+el.dataset.n;
        el.className=!n?'cm':(n==ln?'cur':(n<ln?'done':''));
        if(n&&n==ln){const c=$('code');c.scrollTop=el.offsetTop-c.offsetTop-c.clientHeight/2;}}}
  }catch(e){}
}
window.onresize=()=>draw(lastLine);
net();files();poll();setInterval(net,10000);setInterval(poll,700);
// ссылка …/#start=файл — сразу окно «Перед стартом» для этого файла
if(location.hash.startsWith('#start='))pick(decodeURIComponent(location.hash.slice(7))).then(run);
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
        ap ? WIFI_AP_SSID : s_sta_ssid,
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

// Развёрнутая программа (G71/G70 → G0/G1/G2/G3) — для рисунка: «номер<TAB>кадр»; ошибка — «ERR<TAB>текст»
static void handle_plan(WiFiClient& cl, const String& query) {
    String name = get_param(query, "name");
    if (!name.length()) { send_404(cl); return; }
    String path = String(GCODE_DIR) + "/" + name, out, err;
    if (!GCodeExec_Plan(path.c_str(), out, err)) out = "ERR\t" + err;
    cl.printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
              "Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)out.length());
    cl.print(out);
}

// Расчётное время программы до запуска: {"sec":…,"rpm":…} (подача на оборот — при этих оборотах)
static void handle_info(WiFiClient& cl, const String& query) {
    String name = get_param(query, "name");
    if (!name.length()) { send_404(cl); return; }
    String path = String(GCODE_DIR) + "/" + name, err;
    float sec = 0; int rpm = 0;
    char buf[64];
    if (GCodeExec_Estimate(path.c_str(), sec, rpm, err)) snprintf(buf, sizeof(buf), "{\"sec\":%d,\"rpm\":%d}", (int)(sec + 0.5f), rpm);
    else snprintf(buf, sizeof(buf), "{\"sec\":-1}");
    send_ok_json(cl, buf);
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
    cl.print("HTTP/1.1 204 No Content\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Methods: GET, PUT, DELETE, POST, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\n"
             "Connection: close\r\n\r\n");
}

static const char* exec_state_str(GcExecSt st) {
    switch (st) {
        case GCE_IDLE:        return "IDLE";
        case GCE_RUNNING:     return "RUNNING";
        case GCE_WAITING_ACK: return "WAITING_ACK";
        case GCE_DWELL:       return "DWELL";
        case GCE_PAUSED:      return "PAUSED";
        case GCE_DONE:        return "DONE";
        case GCE_ERROR:       return "ERROR";
        default:              return "IDLE";
    }
}

static void handle_exec_status(WiFiClient& cl) {
    GcExecSt st = GCodeExec_GetState();
    const char* fn = GCodeExec_GetFilename();
    const char* bn = basename_of(fn && fn[0] ? fn : "");
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"file\":\"%s\",\"line\":%d,\"step\":%d,\"pct\":%d,\"eta\":%d,\"total\":%d,\"error\":\"%s\"}",
        exec_state_str(st), bn, GCodeExec_GetLine(), GCodeExec_GetStep(),
        GCodeExec_GetProgress(), GCodeExec_GetEta(), GCodeExec_GetTotal(), GCodeExec_GetError());
    send_ok_json(cl, buf);
}

static void handle_run(WiFiClient& cl, const String& query) {
    String name = get_param(query, "name");
    if (!name.length()) {
        send_ok_json(cl, "{\"error\":\"name required\"}");
        return;
    }
    String path = String(GCODE_DIR) + "/" + name;
    bool ok = LittleFS.exists(path);   // запуск выполнит loop() (GCodeWiFi_Process)
    if (ok) post_exec_request(GCW_REQ_RUN, path.c_str());
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":%s,\"file\":\"%s\"}",
             ok ? "true" : "false", name.c_str());
    send_ok_json(cl, buf);
}

static void handle_stop(WiFiClient& cl) {
    post_exec_request(GCW_REQ_STOP);
    send_ok_json(cl, "{\"ok\":true}");
}

static void handle_pause(WiFiClient& cl) {
    post_exec_request(GCW_REQ_PAUSE);
    send_ok_json(cl, "{\"ok\":true}");
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
    } else if (method == "GET" && path == "/api/info") {
        handle_info(cl, query);
    } else if (method == "GET" && path == "/api/plan") {
        handle_plan(cl, query);
    } else if (method == "DELETE" && path == "/api/file") {
        handle_file_delete(cl, query);
    } else if (method == "PUT" && path == "/api/file") {
        handle_file_put(cl, query, content_len);
    } else if (method == "GET" && path == "/api/exec") {
        handle_exec_status(cl);
    } else if (method == "POST" && path == "/api/run") {
        handle_run(cl, query);
    } else if (method == "POST" && path == "/api/stop") {
        handle_stop(cl);
    } else if (method == "POST" && path == "/api/pause") {
        handle_pause(cl);
    } else {
        send_404(cl);
    }

    delay(2);
    cl.stop();
}

// ─── WiFi helpers ─────────────────────────────────────────────────────────────

// Алерт только при смене адреса: повторные циклы STA→AP→STA с тем же IP не всплывают
static void notify_connected() {
    if (strcmp(s_notified_ip, s_ip) == 0) return;
    strncpy(s_notified_ip, s_ip, sizeof(s_notified_ip) - 1);
    portENTER_CRITICAL(&s_req_mux);
    strncpy(s_notify_ip, s_ip, sizeof(s_notify_ip) - 1);
    s_notify_ap = (s_state == GCW_AP);
    s_notify_pending = true;          // callback (LVGL-алерт) вызовет loop()
    portEXIT_CRITICAL(&s_req_mux);
}

static void start_server(bool notify = true) {
    if (s_srv) { s_srv->close(); delete s_srv; s_srv = nullptr; }
    s_srv = new WiFiServer(HTTP_PORT);
    if (!s_srv) {
        Serial.println("[gcode] ERROR: WiFiServer alloc failed!");
        return;
    }
    s_srv->begin();
    s_srv_start_t   = millis();
    s_last_client_t = millis();
    // mDNS: инициализируем один раз
    if (!s_mdns_started) {
        if (MDNS.begin("els")) {
            MDNS.addService("http", "tcp", HTTP_PORT);
            s_mdns_started = true;
            Serial.println("[gcode] mDNS: http://els.local/");
        } else {
            Serial.println("[gcode] mDNS: failed");
        }
    }
    if (HTTP_PORT == 80) {
        Serial.printf("[gcode] WiFiServer started: http://%s/  (http://els.local/)\n", s_ip);
    } else {
        Serial.printf("[gcode] WiFiServer started: http://%s:%u/  (http://els.local:%u/)\n",
                      s_ip, HTTP_PORT, HTTP_PORT);
    }
    if (notify) notify_connected();
}

static void start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    s_state      = GCW_AP;
    s_ap_retry_t = millis();
    WiFi.softAPIP().toString().toCharArray(s_ip, sizeof(s_ip));
    Serial.printf("[gcode] AP: %s  IP: %s\n", WIFI_AP_SSID, s_ip);
    start_server();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void GCodeWiFi_SetConnectedCallback(GCWifiConnectedCb cb) { s_conn_cb = cb; }

// WiFi через сопроцессор (ESP32-P4 + ESP32-C6, esp_hosted): версии хоста и слейва в лог
static void log_hosted_versions() {
#if CONFIG_ESP_HOSTED_ENABLED
    esp_hosted_coprocessor_fwver_t v = {};
    int r = esp_hosted_get_coprocessor_fwversion(&v);
    Serial.printf("[wifi] esp_hosted host=%d.%d.%d, C6 slave: %s %lu.%lu.%lu\n",
                  ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1, ESP_HOSTED_VERSION_PATCH_1,
                  r == 0 ? "OK" : "нет ответа",
                  (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
#endif
}

// WiFi через сопроцессор: команды до поднятия SDIO-связи с C6 теряются — ждём её (до 10 с)
static void wait_coprocessor() {
#if CONFIG_ESP_HOSTED_ENABLED
    WiFi.mode(WIFI_STA);   // запускает esp_hosted
    esp_hosted_coprocessor_fwver_t v = {};
    uint32_t t0 = millis();
    while (esp_hosted_get_coprocessor_fwversion(&v) != 0 && millis() - t0 < 10000) delay(200);
    Serial.printf("[wifi] C6 готов через %lu мс\n", (unsigned long)(millis() - t0));
    log_hosted_versions();
#endif
}

static void sta_connect();
static uint32_t s_retry_t = 0;   // последняя попытка begin() — повтор каждые 10 с в окне подключения

static void wifi_init_impl() {
    // Причина отказа подключения к роутеру — в лог (коды wifi_err_reason_t:
    // 15/202/204 — пароль/рукопожатие, 201 — сеть не найдена)
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        Serial.printf("[wifi] STA disconnected, reason=%d\n",
                      (int)info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (!LittleFS.begin(false)) {
        Serial.println("[gcode] LittleFS formatting...");
        LittleFS.begin(true);
    } else {
        Serial.printf("[gcode] LittleFS: %lu/%lu B\n",
            (unsigned long)LittleFS.usedBytes(),
            (unsigned long)LittleFS.totalBytes());
    }
    if (!LittleFS.exists(GCODE_DIR)) LittleFS.mkdir(GCODE_DIR);

    sta_load();
    const char* ssid = s_sta_ssid;
    if (ssid && ssid[0] != '\0') {
        Serial.printf("[gcode] WiFi STA → %s (пароль: %u симв.)\n", ssid, (unsigned)strlen(s_sta_pass));
        wait_coprocessor();
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        sta_connect();
    } else {
        start_ap();
    }
}

// Подключение к роутеру — одна последовательность для старта, повтора из точки доступа и ввода с экрана
// (раньше при старте и повторе была другая: после перезагрузки дисплей не подключался, а с экрана — да)
static void sta_connect() {
    if (s_srv) { s_srv->close(); delete s_srv; s_srv = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    // 10.1" (ESP32-P4 + C6 через esp_hosted, слейв 2.1.10): без поиска сетей перед begin() подключение не
    // начинается вовсе (после перезагрузки — «disconnected» и тишина); с экрана работало, потому что экран
    // Wi-Fi сначала ищет сети
    int n = WiFi.scanNetworks();
    Serial.printf("[wifi] перед подключением: сетей %d\n", n);
    WiFi.scanDelete();
    delay(1000);
    WiFi.begin(s_sta_ssid, s_sta_pass);
    s_state  = GCW_CONNECTING;
    s_conn_t = millis();
    s_retry_t = millis();
}

// Запросы экрана: новая сеть роутера / «забыть», поиск сетей
static void wifi_screen_requests() {
    bool cfg = false;
    portENTER_CRITICAL(&s_cfg_mux);
    if (s_cfg_req) {
        cfg = true; s_cfg_req = false;
        memcpy(s_sta_ssid, s_new_ssid, sizeof(s_sta_ssid));
        memcpy(s_sta_pass, s_new_pass, sizeof(s_sta_pass));
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    if (cfg) {
        Preferences p;
        if (p.begin("wifi", false)) { p.putString("ssid", s_sta_ssid); p.putString("pass", s_sta_pass); p.end(); }
        Serial.printf("[wifi] сеть роутера с экрана: %s\n", s_sta_ssid[0] ? s_sta_ssid : "(нет — только точка доступа)");
        if (s_sta_ssid[0]) sta_connect();
        else {
            if (s_srv) { s_srv->close(); delete s_srv; s_srv = nullptr; }
            WiFi.disconnect(true);
            delay(200);
            start_ap();
        }
    }
    if (s_scan_req) {
        s_scan_req = false; s_scan_busy = true;
        if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);   // в режиме точки доступа поиск не работает
        int n = WiFi.scanNetworks();
        Serial.printf("[wifi] поиск сетей: %d\n", n);
        int k = 0;
        for (int i = 0; i < n && k < GCW_SCAN_MAX; i++) {
            String ss = WiFi.SSID(i);
            if (!ss.length()) continue;
            bool dup = false;
            for (int j = 0; j < k; j++) if (ss == s_scan_list[j]) dup = true;
            if (!dup) strlcpy(s_scan_list[k++], ss.c_str(), sizeof(s_scan_list[0]));
        }
        WiFi.scanDelete();
        s_scan_n = k; s_scan_busy = false;
    }
}

static void wifi_process_impl() {
    wifi_screen_requests();
    // Обработка клиентов
    if (s_srv && s_srv->hasClient()) {
        WiFiClient cl = s_srv->accept();
        if (cl) {
            s_last_client_t = millis();
            Serial.printf("[gcode] client: %s\n", cl.remoteIP().toString().c_str());
            handle_client(cl);
        }
    }

    // Состояния WiFi
    if (s_state == GCW_CONNECTING) {
        if (WiFi.status() != WL_CONNECTED && millis() - s_retry_t > 10000 &&
            millis() - s_conn_t < WIFI_STA_TIMEOUT_MS - 5000) {
            // C6 (esp_hosted) иногда «забывает» первую попытку — повторить, не дожидаясь перехода в AP
            s_retry_t = millis();
            Serial.println("[wifi] повтор подключения");
            WiFi.disconnect(false);
            delay(300);
            WiFi.begin(s_sta_ssid, s_sta_pass);
        }
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            if (ip[0] == 0) return;
            s_state = GCW_STACK_WAIT;
            s_conn_t = millis();
            WiFi.setSleep(false);
            ip.toString().toCharArray(s_ip, sizeof(s_ip));
            Serial.printf("[gcode] STA connected: %s\n", s_ip);
        } else if (millis() - s_conn_t > WIFI_STA_TIMEOUT_MS) {
            Serial.printf("[gcode] STA timeout → AP (status=%d)\n", (int)WiFi.status());
            {   // диагностика: видна ли сеть роутера (чужие SSID не печатаем)
                int n = WiFi.scanNetworks();
                int rssi = 0;
                for (int i = 0; i < n; i++)
                    if (WiFi.SSID(i) == s_sta_ssid) { rssi = WiFi.RSSI(i); break; }
                Serial.printf("[wifi] scan: сетей=%d, сеть роутера %s%s\n", n,
                              rssi ? "видна, RSSI=" : "не найдена", rssi ? String(rssi).c_str() : "");
                WiFi.scanDelete();
            }
            WiFi.disconnect(true);
            delay(100);
            start_ap();
        }
    } else if (s_state == GCW_STACK_WAIT) {
        // TCP/IP стек на ESP32-S3 требует ~1-2с после ассоциации
        if (millis() - s_conn_t > 2000) {
            s_state = GCW_CONNECTED;
            start_server();
        }
    } else if (s_state == GCW_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[gcode] STA lost → reconnecting...");
            s_state  = GCW_CONNECTING;
            s_conn_t = millis();
            WiFi.disconnect(true);
            delay(100);
            WiFi.begin(s_sta_ssid, s_sta_pass);
        } else if (s_srv && millis() - s_srv_start_t > 30000
                         && millis() - s_last_client_t > 30000) {
            // Watchdog: сервер запущен >30с, но ни одного клиента → перезапускаем
            Serial.println("[gcode] watchdog: restarting server...");
            s_last_client_t = millis();
            start_server(false);  // watchdog — без повторного алерта
        }
    } else if (s_state == GCW_AP) {
        // Повторная попытка STA каждые 60 секунд (если SSID задан)
        const char* ssid = s_sta_ssid;
        if (ssid && ssid[0] != '\0' && millis() - s_ap_retry_t > 60000) {
            Serial.println("[gcode] AP → retrying STA...");
            log_hosted_versions();
            sta_connect();
        }
    }
}

static void wifi_task(void*) {
    wifi_init_impl();
    for (;;) {
        wifi_process_impl();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void GCodeWiFi_Init() {
    xTaskCreatePinnedToCore(wifi_task, "gcode_wifi", 8192, nullptr, 1, nullptr, 0);
}

// Вызывается из loop(): выполнить запросы WiFi-задачи в контексте UI/исполнителя
void GCodeWiFi_Process() {
    GcwReq req;
    char path[sizeof(s_req_path)];
    bool notify;
    char ip[sizeof(s_notify_ip)];
    bool is_ap;
    portENTER_CRITICAL(&s_req_mux);
    req = s_req;  s_req = GCW_REQ_NONE;
    memcpy(path, s_req_path, sizeof(path));
    notify = s_notify_pending;  s_notify_pending = false;
    memcpy(ip, s_notify_ip, sizeof(ip));
    is_ap = s_notify_ap;
    portEXIT_CRITICAL(&s_req_mux);

    if (req == GCW_REQ_RUN)   GCodeExec_Run(path);
    if (req == GCW_REQ_STOP)  GCodeExec_Stop();
    if (req == GCW_REQ_PAUSE) GCodeExec_TogglePause();
    if (notify && s_conn_cb)  s_conn_cb(ip, is_ap);
}

const char* GCodeWiFi_GetIP()      { return s_ip; }
const char* GCodeWiFi_GetStaSsid() { return s_sta_ssid; }
const char* GCodeWiFi_GetApSsid()  { return WIFI_AP_SSID; }
const char* GCodeWiFi_GetApPass()  { return WIFI_AP_PASS; }
void GCodeWiFi_SetSta(const char* ssid, const char* pass) {
    portENTER_CRITICAL(&s_cfg_mux);
    strlcpy(s_new_ssid, ssid ? ssid : "", sizeof(s_new_ssid));
    strlcpy(s_new_pass, pass ? pass : "", sizeof(s_new_pass));
    s_cfg_req = true;
    portEXIT_CRITICAL(&s_cfg_mux);
}
void GCodeWiFi_Scan()              { s_scan_n = -1; s_scan_req = true; }
int  GCodeWiFi_ScanCount()         { return (s_scan_req || s_scan_busy) ? -1 : s_scan_n; }
const char* GCodeWiFi_ScanSsid(int i) { return (i >= 0 && i < s_scan_n) ? s_scan_list[i] : ""; }
bool GCodeWiFi_IsConnected()       { return s_state == GCW_CONNECTED || s_state == GCW_AP; }
bool GCodeWiFi_IsAP()              { return s_state == GCW_AP; }
httpd_handle_t GCodeWiFi_GetHttpd(){ return nullptr; }
