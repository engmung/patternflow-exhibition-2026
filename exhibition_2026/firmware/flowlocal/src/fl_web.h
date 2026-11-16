#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — web console (ESPAsyncWebServer)
//
// Routes
//   GET  /              → dashboard HTML (status + links)
//   GET  /api/status    → JSON  (ap, clients, broker, uptime…)
//   POST /api/ap        → change AP SSID / pass / enable
//   GET  /api/reboot    → restart the device
//
// USB flash serving is mounted by FlUsb (left USB / OTG port).
// ═══════════════════════════════════════════════════════════

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../flowlocal_config.h"
#include "fl_wifi.h"
#include "fl_broker.h"
#include "fl_store.h"
#include "fl_library.h"
#include "fl_ntp.h"
#include "fl_ota.h"
#include "fl_display_prefs.h"
#include "fl_weather.h"
#include "fl_display.h"

namespace FlWeb {

static AsyncWebServer server(FL_WEB_PORT);
static bool builtinDirector = false;

// ── HTML dashboard (inline, no SPIFFS required for MVP) ───────
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlowLocal</title>
<style>
  :root{--bg:#111;--fg:#eee;--acc:#7ef;--red:#f55;--card:#1e1e1e;--inp:#2a2a2a;}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;padding:1rem}
  h1{color:var(--acc);font-size:1.4rem;margin-bottom:1rem}
  h2{font-size:1rem;color:#aaa;margin:.8rem 0 .4rem}
  .card{background:var(--card);border-radius:8px;padding:1rem;margin-bottom:1rem}
  .row{display:flex;justify-content:space-between;padding:.2rem 0;border-bottom:1px solid #2a2a2a}
  .row:last-child{border:none}
  .label{color:#888}
  .val{font-family:monospace}
  input,select{width:100%;padding:.45rem .6rem;background:var(--inp);border:1px solid #444;
    border-radius:5px;color:var(--fg);margin-top:.3rem}
  button{padding:.5rem 1.2rem;border:none;border-radius:5px;cursor:pointer;font-size:.9rem;margin-top:.5rem}
  .btn-save{background:var(--acc);color:#111;font-weight:700}
  .btn-reboot{background:var(--red);color:#fff}
  .badge{display:inline-block;padding:.1rem .5rem;border-radius:4px;font-size:.75rem}
  .on{background:#1a4}  .off{background:#611}
  #msg{margin-top:.6rem;font-size:.85rem;color:var(--acc)}
</style>
</head>
<body>
<h1>⚡ FlowLocal</h1>

<div class="card" id="status-card">
  <h2>Status</h2>
  <div class="row"><span class="label">AP SSID</span><span class="val" id="s-ssid">…</span></div>
  <div class="row"><span class="label">AP IP</span><span class="val" id="s-ip">…</span></div>
  <div class="row"><span class="label">AP clients</span><span class="val" id="s-cli">…</span></div>
  <div class="row"><span class="label">MQTT clients</span><span class="val" id="s-mqtt">…</span></div>
  <div class="row"><span class="label">Upstream (STA)</span><span class="val" id="s-sta">—</span></div>
  <div class="row"><span class="label">NTP / time</span><span class="val" id="s-ntp">—</span></div>
  <div class="row"><span class="label">Media</span><span class="val" id="s-usb">…</span></div>
  <div class="row"><span class="label">Card root</span><span class="val" id="s-root">…</span></div>
  <div class="row"><span class="label">Uptime</span><span class="val" id="s-up">…</span></div>
  <div class="row"><span class="label">Free heap</span><span class="val" id="s-heap">…</span></div>
  <div class="row"><span class="label">Version</span><span class="val" id="s-ver">…</span></div>
</div>

<div class="card">
  <h2>Director / Launchpad</h2>
  <p id="dir-note" style="color:#888;font-size:.9rem">Director is stored on the ESP. USB/SD is only for shows, decks, and patterns — MQTT Connect works without a stick.</p>
  <a id="dir-link" href="/director/index.html" class="btn-save" style="display:inline-block;text-decoration:none">Open Director</a>
</div>

<div class="card">
  <h2>Access Point</h2>
  <p style="color:#888;font-size:.85rem;margin-bottom:.5rem">
    Multi-island: change SSID and AP IP here, then add that SSID on each panel’s Wi‑Fi list
    and set the same IP under MQTT → FlowLocal.
  </p>
  <label>SSID<input id="ap-ssid" maxlength="32"></label>
  <label>Password (min 8 chars, blank = open)<input id="ap-pass" type="password" maxlength="63"></label>
  <label>AP IP<input id="ap-ip" maxlength="15" placeholder="192.168.66.1"></label>
  <label>
    AP enabled
    <select id="ap-en">
      <option value="1">Yes</option>
      <option value="0">No (disable)</option>
    </select>
  </label>
  <br>
  <button class="btn-save" onclick="saveAp()">Save &amp; Apply</button>
  <div id="msg"></div>
</div>

<div class="card">
  <h2>Upstream Wi-Fi (STA)</h2>
  <p style="color:#888;font-size:.85rem;margin-bottom:.5rem">
    Optional. FlowLocal joins this network for NTP and weather.
    Panels stay on the AP and do not need internet.
  </p>
  <label>SSID<input id="sta-ssid" maxlength="32"></label>
  <label>Password<input id="sta-pass" type="password" maxlength="63"></label>
  <label>Timezone offset (minutes east of UTC, e.g. 240 for UTC+4)
    <input id="tz-min" type="number" min="-840" max="840" step="15" value="0">
  </label>
  <label>
    STA enabled
    <select id="sta-en">
      <option value="1">Yes</option>
      <option value="0">No</option>
    </select>
  </label>
  <br>
  <button class="btn-save" onclick="saveSta()">Save STA &amp; timezone</button>
  <div id="msg-sta"></div>
</div>

<div class="card">
  <h2>System</h2>
  <a href="/display" class="btn-save" style="display:inline-block;text-decoration:none;margin-right:.5rem">Display fields</a>
  <a href="/weather" class="btn-save" style="display:inline-block;text-decoration:none;margin-right:.5rem">Weather hub</a>
  <a href="/update" class="btn-save" style="display:inline-block;text-decoration:none;margin-right:.5rem">Firmware update</a>
  <button class="btn-reboot" onclick="if(confirm('Reboot?'))fetch('/api/reboot')">Reboot</button>
</div>

<script>
async function poll(){
  try{
    const d=await(await fetch('/api/status')).json();
    document.getElementById('s-ssid').textContent=d.ap_ssid+(d.ap_enabled?'':' (disabled)');
    document.getElementById('s-ip').textContent=d.ap_ip;
    document.getElementById('s-cli').textContent=d.ap_clients;
    document.getElementById('s-mqtt').textContent=d.mqtt_clients;
    document.getElementById('s-sta').textContent=d.sta_ip?(d.sta_ip+' · '+(d.sta_ssid||'')):(d.sta_enabled?'connecting…':'—');
    document.getElementById('s-ntp').textContent=d.ntp_synced
      ?((d.time_iso||'ok')+' (UTC'+(d.tz_offset_min>=0?'+':'')+d.tz_offset_min+')')
      :(d.ntp_started?'syncing…':(d.sta_ip?'waiting':'need STA'));
    document.getElementById('s-usb').textContent=d.media_mounted?'mounted':('not mounted · '+(d.media_error||''));
    document.getElementById('s-root').textContent=(d.media_dir||'')+' · '+(d.media_list||'—');
    document.getElementById('dir-note').textContent=d.director_builtin
      ?'Built-in FlowLocal Director. Connect uses this box as the MQTT broker, then Launch.'
      :(d.media_mounted
        ?'Media mounted. Open Director, Connect, then Launch.'
        :(d.media==='sd'
          ?'No SD card. FAT32. CS=4 SCK=5 MOSI=6 MISO=7.'
          :'No USB stick. Close USB-OTG jumper, plug FAT32 USB-C into USB jack, power from COM.'));
    document.getElementById('s-up').textContent=fmt(d.uptime_s);
    document.getElementById('s-heap').textContent=(d.free_heap/1024).toFixed(1)+' kB';
    document.getElementById('s-ver').textContent=d.version;
    if(!document.getElementById('ap-ssid').dataset.filled){
      document.getElementById('ap-ssid').value=d.ap_ssid||'';
      document.getElementById('ap-ip').value=d.ap_ip_cfg||d.ap_ip||'';
      document.getElementById('ap-en').value=d.ap_enabled?'1':'0';
      document.getElementById('ap-ssid').dataset.filled='1';
    }
    if(!document.getElementById('sta-ssid').dataset.filled){
      document.getElementById('sta-ssid').value=d.sta_ssid||'';
      document.getElementById('sta-en').value=d.sta_enabled?'1':'0';
      document.getElementById('tz-min').value=d.tz_offset_min||0;
      document.getElementById('sta-ssid').dataset.filled='1';
    }
  }catch(e){}
}
function fmt(s){
  const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;
  return `${h}h ${m}m ${ss}s`;
}
async function saveAp(){
  const ssid=document.getElementById('ap-ssid').value.trim();
  const pass=document.getElementById('ap-pass').value;
  const ip  =document.getElementById('ap-ip').value.trim();
  const en  =document.getElementById('ap-en').value==='1';
  const msg =document.getElementById('msg');
  if(en&&ssid.length===0){msg.textContent='SSID cannot be empty when AP is enabled.';return;}
  const r=await fetch('/api/ap',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid,pass,enabled:en,ip,gw:ip,sn:'255.255.255.0'})});
  const j=await r.json();
  msg.textContent=j.ok?'Saved! Reconnect to the AP at the new IP if it changed.':('Error: '+j.error);
}
async function saveSta(){
  const ssid=document.getElementById('sta-ssid').value.trim();
  const pass=document.getElementById('sta-pass').value;
  const en  =document.getElementById('sta-en').value==='1';
  const tz  =parseInt(document.getElementById('tz-min').value,10)||0;
  const msg =document.getElementById('msg-sta');
  const r=await fetch('/api/sta',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid,pass,enabled:en,tz_offset_min:tz})});
  const j=await r.json();
  msg.textContent=j.ok?'Saved. STA '+(en?'connecting…':'disabled.')+(' TZ '+tz):('Error: '+(j.error||''));
  document.getElementById('sta-ssid').dataset.filled='';
}
poll();setInterval(poll,3000);
</script>
</body>
</html>
)rawhtml";

static const char* mimeFor(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".json") || path.endsWith(".webmanifest")) return "application/json";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".zip")) return "application/zip";
    if (path.endsWith(".txt") || path.endsWith(".md")) return "text/plain";
    return "application/octet-stream";
}

static const char SW_PASSTHROUGH[] PROGMEM = R"JS(
self.addEventListener("install",function(e){self.skipWaiting()});
self.addEventListener("activate",function(e){
  e.waitUntil(caches.keys().then(function(keys){
    return Promise.all(keys.map(function(k){return caches.delete(k)}));
  }).then(function(){return self.clients.claim()}));
});
self.addEventListener("fetch",function(e){e.respondWith(fetch(e.request))});
)JS";

static void sendOpenFile(AsyncWebServerRequest* req, File& f, const String& path) {
    AsyncWebServerResponse* res = req->beginResponse(f, path, mimeFor(path));
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
}

static String joinFs(const String& root, const String& rel) {
    if (root.length() == 0 || root == "/") return rel;
    if (rel.startsWith("/")) return root + rel;
    return root + "/" + rel;
}

static void sendFsPath(AsyncWebServerRequest* req, const String& full) {
    fs::FS& fs = FlStore::storage();
    File f = fs.open(full);
    if (f && f.isDirectory()) {
        f.close();
        String idx = full.endsWith("/") ? full + "index.html" : full + "/index.html";
        f = fs.open(idx);
        if (f && !f.isDirectory()) {
            sendOpenFile(req, f, idx);
            return;
        }
        if (f) f.close();
        req->send(404, "text/plain", String("Not found: ") + full);
        return;
    }
    if (!f) {
        req->send(404, "text/plain", String("Not found: ") + full);
        return;
    }
    sendOpenFile(req, f, full);
}

static bool sendFromFs(AsyncWebServerRequest* req, fs::FS& fs, const String& full) {
    File f = fs.open(full);
    if (f && f.isDirectory()) {
        f.close();
        String idx = full.endsWith("/") ? full + "index.html" : full + "/index.html";
        f = fs.open(idx);
        if (f && !f.isDirectory()) {
            Serial.printf("[web] 200 %s (%u bytes)\n", idx.c_str(), (unsigned)f.size());
            sendOpenFile(req, f, idx);
            return true;
        }
        if (f) f.close();
        return false;
    }
    if (!f || f.isDirectory()) {
        if (f) f.close();
        return false;
    }
    Serial.printf("[web] 200 %s (%u bytes)\n", full.c_str(), (unsigned)f.size());
    sendOpenFile(req, f, full);
    return true;
}

static void sendDirectorFile(AsyncWebServerRequest* req) {
    const String url = req->url();
    if (url.indexOf("..") >= 0) {
        req->send(400, "text/plain", "bad path");
        return;
    }

    if (url.endsWith("/sw.js") || url.endsWith("sw.js")) {
        AsyncWebServerResponse* res = req->beginResponse_P(200, "application/javascript", SW_PASSTHROUGH);
        res->addHeader("Cache-Control", "no-store");
        res->addHeader("Service-Worker-Allowed", "/director/");
        req->send(res);
        return;
    }

    String rel;
    if (url == "/director" || url == "/director/" || url == "/director/index.html") {
        rel = "/index.html";
    } else if (url.startsWith("/director/")) {
        rel = url.substring(strlen("/director"));
        if (rel.length() == 0 || rel == "/") rel = "/index.html";
        else if (rel.endsWith("/")) rel += "index.html";
    } else {
        req->send(404, "text/plain", "Not found");
        return;
    }

    if (builtinDirector && sendFromFs(req, LittleFS, joinFs("/director", rel))) return;

    if (FlStore::mounted()) {
        String full = joinFs(FlStore::contentRoot(), rel);
        if (sendFromFs(req, FlStore::storage(), full)) return;
        Serial.printf("[web] 404 %s -> %s\n", url.c_str(), full.c_str());
        req->send(404, "text/plain", String("Not found: ") + full);
        return;
    }

    req->send(503, "text/plain", "Director not installed. Flash LittleFS (uploadfs).");
}

// ── JSON helpers ──────────────────────────────────────────────
static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

inline void begin() {
    builtinDirector = LittleFS.begin(true);
    if (builtinDirector && !LittleFS.exists("/director/index.html")) {
        builtinDirector = false;
        Serial.println("[web] LittleFS mounted but no /director/index.html — run uploadfs");
    } else if (builtinDirector) {
        Serial.println("[web] built-in FlowLocal Director (LittleFS)");
    } else {
        Serial.println("[web] LittleFS unavailable; Director will use USB/SD if present");
    }

    // Dashboard
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // Status API
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["version"]      = FL_VERSION;
        doc["uptime_s"]     = (uint32_t)(millis() / 1000UL);
        doc["free_heap"]    = ESP.getFreeHeap();
        doc["ap_ssid"]      = FlWifi::apSsid;
        doc["ap_ip"]        = FlWifi::apIp();
        doc["ap_ip_cfg"]    = FlWifi::apIpCfg();
        doc["ap_enabled"]   = FlWifi::apEnabled;
        doc["ap_clients"]   = FlWifi::apClients();
        doc["mqtt_clients"] = FlBroker::clientCount();
        doc["sta_ip"]       = FlWifi::staIp();
        doc["sta_ssid"]     = FlWifi::staSsid;
        doc["sta_enabled"]  = FlWifi::staEnabled;
        FlNtp::appendStatus(doc);
        FlWeather::appendStatus(doc);
        doc["deck"] = FlBroker::deck();
        doc["media"]         = FL_USE_SD ? "sd" : "usb";
        doc["media_mounted"] = FlStore::mounted();
        doc["media_error"]   = FlStore::errorText();
        doc["media_devices"] = FlStore::devices();
        doc["media_dir"]     = FlStore::contentRoot();
        doc["media_list"]    = FlStore::rootNames();
        doc["director_builtin"] = builtinDirector;
        sendJson(req, doc);
    });

    // AP config API
    server.on("/api/ap", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t /*index*/, size_t /*total*/) {
            JsonDocument in;
            if (deserializeJson(in, data, len)) {
                JsonDocument err;
                err["ok"] = false; err["error"] = "invalid JSON";
                sendJson(req, err, 400);
                return;
            }
            String  ssid = in["ssid"].as<String>();
            String  pass = in["pass"].as<String>();
            bool    en   = in["enabled"] | true;
            String  ip   = in["ip"].isNull() ? "" : in["ip"].as<String>();
            String  gw   = in["gw"].isNull() ? "" : in["gw"].as<String>();
            String  sn   = in["sn"].isNull() ? "" : in["sn"].as<String>();

            JsonDocument res;
            if (FlWifi::setAp(ssid, pass, en, ip, gw, sn)) {
                res["ok"] = true;
                res["ap_ip"] = FlWifi::apIp();
            } else {
                res["ok"]    = false;
                res["error"] = "bad password (>=8) or invalid AP IP";
            }
            sendJson(req, res);
        }
    );

    // Upstream STA + timezone
    server.on("/api/sta", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t /*index*/, size_t /*total*/) {
            JsonDocument in;
            if (deserializeJson(in, data, len)) {
                JsonDocument err;
                err["ok"] = false; err["error"] = "invalid JSON";
                sendJson(req, err, 400);
                return;
            }
            String ssid = in["ssid"].as<String>();
            String pass = in["pass"].as<String>();
            bool en = in["enabled"] | false;
            if (in["tz_offset_min"].is<int>()) {
                FlNtp::saveTz(in["tz_offset_min"].as<int>());
                if (FlWifi::isStaUp()) FlNtp::startSync();
            }
            FlWifi::setSta(ssid, pass, en);
            JsonDocument res;
            res["ok"] = true;
            res["tz_offset_min"] = FlNtp::tzOffsetMin;
            sendJson(req, res);
        }
    );

    // Time for panels on the AP (no internet required on the panel)
    server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        FlNtp::appendStatus(doc);
        doc["ok"] = FlNtp::unixNow() > 0;
        sendJson(req, doc);
    });

    // Weather hub for panels (FlowLocal STA pulls; AP serves cache)
    server.on("/api/weather", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        FlWeather::appendStatus(doc);
        sendJson(req, doc);
    });
    server.on("/api/weather/config", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument in;
            if (deserializeJson(in, data, len)) {
                JsonDocument err;
                err["ok"] = false;
                err["error"] = "invalid JSON";
                sendJson(req, err, 400);
                return;
            }
            bool en = in["enabled"] | false;
            bool metric = in["units_metric"] | true;
            bool newKey = in["api_key"].is<const char*>() && strlen(in["api_key"]) > 0;
            const char* key = newKey ? in["api_key"].as<const char*>() : "";
            const char* q = in["query"] | FlWeather::query;
            float la = in["lat"].isNull() ? FlWeather::lat : in["lat"].as<float>();
            float lo = in["lon"].isNull() ? FlWeather::lon : in["lon"].as<float>();
            FlWeather::save(en, key, q, la, lo, metric, newKey);
            JsonDocument res;
            res["ok"] = true;
            FlWeather::appendStatus(res);
            sendJson(req, res);
        });
    server.on("/api/weather/fetch", HTTP_POST, [](AsyncWebServerRequest* req) {
        FlWeather::requestFetch();
        bool ok = FlWeather::fetchOnce();
        JsonDocument doc;
        doc["ok"] = ok;
        FlWeather::appendStatus(doc);
        sendJson(req, doc, ok ? 200 : 502);
    });

    static const char DISPLAY_HTML[] PROGMEM = R"rawhtml(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlowLocal Display</title>
<style>
:root{--bg:#111;--fg:#eee;--acc:#7ef;--card:#1e1e1e}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;padding:1rem;max-width:480px;margin:0 auto}
h1{color:var(--acc);font-size:1.2rem}.card{background:var(--card);border-radius:8px;padding:1rem;margin:1rem 0}
label{display:block;padding:.35rem 0}button{padding:.5rem 1rem;border:none;border-radius:5px;background:var(--acc);color:#111;font-weight:700;margin-top:.5rem}
a{color:var(--acc)}#msg{margin-top:.6rem;font-size:.85rem}
</style></head><body>
<h1>TFT fields</h1>
<div class="card">
<p style="color:#888;font-size:.9rem">Optional ST7735. Unplugged display does not affect FlowLocal. Choose rows to show:</p>
<label><input type="checkbox" id="show_ssid"> SSID</label>
<label><input type="checkbox" id="show_ip"> AP IP</label>
<label><input type="checkbox" id="show_sta"> STA (upstream NTP / weather)</label>
<label><input type="checkbox" id="show_clients"> Wi-Fi + MQTT clients</label>
<label><input type="checkbox" id="show_usb"> USB media</label>
<label><input type="checkbox" id="show_deck"> Deck name</label>
<label><input type="checkbox" id="show_weather"> Weather summary</label>
<label><input type="checkbox" id="show_mqtt"> MQTT activity</label>
<label><input type="checkbox" id="show_ntp"> Time string</label>
<label><input type="checkbox" id="show_heap"> Heap / uptime</label>
<button onclick="save()">Save</button>
<div id="msg"></div>
<p><a href="/">← Dashboard</a></p>
</div>
<script>
const keys=['show_ssid','show_ip','show_sta','show_clients','show_usb','show_deck','show_weather','show_mqtt','show_ntp','show_heap'];
async function load(){
  const d=await(await fetch('/api/display')).json();
  keys.forEach(k=>{const el=document.getElementById(k);if(el)el.checked=!!d[k]});
}
async function save(){
  const body={};keys.forEach(k=>body[k]=document.getElementById(k).checked);
  const r=await fetch('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const j=await r.json();
  document.getElementById('msg').textContent=j.ok?'Saved — TFT will refresh.':'Error';
}
load();
</script></body></html>
)rawhtml";

    static const char WEATHER_HTML[] PROGMEM = R"rawhtml(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlowLocal Weather</title>
<style>
:root{--bg:#111;--fg:#eee;--acc:#7ef;--card:#1e1e1e;--inp:#2a2a2a}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;padding:1rem;max-width:480px;margin:0 auto}
h1{color:var(--acc);font-size:1.2rem}.card{background:var(--card);border-radius:8px;padding:1rem;margin:1rem 0}
label{display:block;margin:.4rem 0}input,select{width:100%;padding:.45rem;background:var(--inp);border:1px solid #444;border-radius:5px;color:var(--fg)}
button{padding:.5rem 1rem;border:none;border-radius:5px;background:var(--acc);color:#111;font-weight:700;margin:.4rem .4rem 0 0}
a{color:var(--acc)}#st{font-family:monospace;font-size:.85rem;margin-top:.6rem;white-space:pre-wrap}
</style></head><body>
<h1>Weather hub</h1>
<div class="card">
<p style="color:#888;font-size:.9rem">Pulls on STA every 30 min. Panels on the AP use <code>GET /api/weather</code> (no internet on the panel).</p>
<label>Enabled<select id="en"><option value="1">Yes</option><option value="0">No</option></select></label>
<label>OpenWeather API key<input id="key" type="password" placeholder="leave blank to keep"></label>
<label>City (e.g. Dubai,AE)<input id="q" maxlength="63"></label>
<label>Units<select id="u"><option value="1">Metric °C</option><option value="0">Imperial °F</option></select></label>
<button onclick="save()">Save</button>
<button onclick="fetchNow()">Fetch now</button>
<div id="st"></div>
<p><a href="/">← Dashboard</a></p>
</div>
<script>
async function load(){
  const d=await(await fetch('/api/weather')).json();
  document.getElementById('en').value=d.wx_enabled?'1':'0';
  document.getElementById('q').value=d.query||'';
  document.getElementById('u').value=d.units_metric?'1':'0';
  document.getElementById('st').textContent=JSON.stringify(d,null,2);
}
async function save(){
  const body={enabled:document.getElementById('en').value==='1',
    query:document.getElementById('q').value.trim(),
    units_metric:document.getElementById('u').value==='1'};
  const k=document.getElementById('key').value.trim();
  if(k)body.api_key=k;
  const j=await(await fetch('/api/weather/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})).json();
  document.getElementById('st').textContent=JSON.stringify(j,null,2);
}
async function fetchNow(){
  const j=await(await fetch('/api/weather/fetch',{method:'POST'})).json();
  document.getElementById('st').textContent=JSON.stringify(j,null,2);
}
load();setInterval(load,15000);
</script></body></html>
)rawhtml";

    server.on("/display", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", DISPLAY_HTML);
    });
    server.on("/api/display", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        FlDisplayPrefs::toJson(doc);
        doc["ok"] = true;
        sendJson(req, doc);
    });
    server.on("/api/display", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument in;
            if (deserializeJson(in, data, len)) {
                JsonDocument err;
                err["ok"] = false;
                sendJson(req, err, 400);
                return;
            }
            FlDisplayPrefs::fromJson(in);
            JsonDocument res;
            res["ok"] = true;
            FlDisplayPrefs::toJson(res);
            sendJson(req, res);
        });

    server.on("/weather", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", WEATHER_HTML);
    });

    server.on(AsyncURIMatcher::prefix("/director"), HTTP_GET, [](AsyncWebServerRequest* req) {
        sendDirectorFile(req);
    });

    FlLibrary::begin(server);
    FlOta::begin(server);

    // Reboot
    server.on("/api/reboot", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Rebooting…");
        delay(300);
        ESP.restart();
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
        const String url = req->url();
        if (url.startsWith("/files/")) {
            String rel = url.substring(strlen("/files"));
            if (rel.indexOf("..") >= 0) {
                req->send(400, "text/plain", "bad path");
                return;
            }
            if (rel.length() == 0 || rel == "/") rel = "/";
            sendFsPath(req, rel);
            return;
        }
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.printf("[FlWeb] console at http://%s.local (port %d)\n", FL_HOSTNAME, FL_WEB_PORT);
}

} // namespace FlWeb
