#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — exhibition web console (env:exhibit only)
//
// The regular console (fl_web.h) is built around the MQTT broker, the USB/SD
// library, Director, weather and the TFT, none of which exist in this build.
// This one shows what venue staff and the operator need, and nothing to change:
//
//   GET  /               → status page: panel session, panel address with links
//                          to the panel's own pages (or, with no session, the
//                          hotspot's other devices), laptop USB, counters
//   GET  /api/status     → JSON (ap_* with the channel's mode, choice and scan
//                          scores, ap_stations, viewer_ip, panel_ip, uptime,
//                          heap, "restart": {...} and "midi": {...})
//   POST /api/ap         → 403: the hotspot is fixed in firmware
//   POST /api/sta        → 403: no upstream Wi-Fi in this build
//   POST /api/resend     → send the panel's current Program Change to Live again,
//                          with the program mirror and the knob values (K1-K3)
//   POST /api/reboot     → restart
//   GET  /update         → browser OTA (arm first; see fl_ota.h)
// ═══════════════════════════════════════════════════════════

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "../flowlocal_config.h"
#include "fl_wifi.h"
#include "fl_ota.h"
#include "fl_midi_bridge.h"

#if !FL_EXHIBIT
#  error "fl_web_exhibit.h belongs to the exhibit build (FL_EXHIBIT=1)"
#endif

namespace FlWeb {

static AsyncWebServer server(FL_WEB_PORT);

static const char EXHIBIT_HTML[] = R"rawhtml(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlowLocal Exhibit</title>
<style>
  :root{--bg:#111;--fg:#eee;--acc:#7ef;--ok:#3c6;--bad:#e55;--warn:#eb4;--card:#1e1e1e}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;padding:1rem;max-width:560px;margin:0 auto}
  h1{color:var(--acc);font-size:1.3rem;margin-bottom:.8rem}
  h2{font-size:.95rem;color:#aaa;margin-bottom:.4rem}
  .card{background:var(--card);border-radius:8px;padding:1rem;margin-bottom:1rem}
  .row{display:flex;justify-content:space-between;gap:1rem;padding:.2rem 0;border-bottom:1px solid #2a2a2a}
  .row:last-child{border:none}
  .label{color:#888}
  .val{font-family:monospace;text-align:right}
  #state{font-size:1.1rem;font-weight:700}
  #state.ok{color:var(--ok)} #state.bad{color:var(--bad)} #state.warn{color:var(--warn)}
  #hints{margin-top:.4rem;color:#bbb;font-size:.9rem}
  #hints li{margin-left:1.2rem}
  a.btn,button{display:inline-block;padding:.5rem 1.1rem;border:none;border-radius:5px;cursor:pointer;font-size:.9rem;text-decoration:none;margin:.3rem .4rem 0 0}
  a.btn{background:var(--acc);color:#111;font-weight:700}
  button{background:var(--bad);color:#fff}
  button.send{background:var(--acc);color:#111;font-weight:700}
  .note{color:#888;font-size:.85rem}
  #s-panel{font-family:monospace;font-size:1.05rem}
  #panel-links a{color:var(--acc);margin-right:.9rem;white-space:nowrap;line-height:2}
  #panel-guess ul{list-style:none;margin:.2rem 0}
  #panel-guess li{font-family:monospace;line-height:2}
  #panel-guess a{color:var(--acc);margin-left:.9rem;font-family:system-ui,sans-serif;white-space:nowrap}
</style>
</head>
<body>
<h1>FlowLocal &middot; Exhibit</h1>

<div class="card">
  <div id="state">&hellip;</div>
  <ul id="hints"></ul>
</div>

<div class="card">
  <h2>Signal path</h2>
  <div class="row"><span class="label">Panel session (Wi-Fi)</span><span class="val" id="s-peers">&hellip;</span></div>
  <div class="row"><span class="label">Panel last heard</span><span class="val" id="s-heard">&hellip;</span></div>
  <div class="row"><span class="label">Laptop (USB jack)</span><span class="val" id="s-usb">&hellip;</span></div>
  <div class="row"><span class="label">Last message</span><span class="val" id="s-last">&hellip;</span></div>
  <div class="row"><span class="label">Current pattern (program)</span><span class="val" id="s-prog">&hellip;</span></div>
  <div class="row"><span class="label">In / out / dropped</span><span class="val" id="s-count">&hellip;</span></div>
  <div class="row"><span class="label">Program mirror to Live</span><span class="val" id="s-mirror">&hellip;</span></div>
  <div class="row"><span class="label">Knobs K1&ndash;K3 (re-sent with the pattern)</span><span class="val" id="s-knobs">&hellip;</span></div>
  <div class="row"><span class="label">Lost Wi-Fi packets</span><span class="val" id="s-gaps">&hellip;</span></div>
  <div class="row"><span class="label">Wi-Fi timing (jitter / worst, last minute)</span><span class="val" id="s-jit">&hellip;</span></div>
  <button class="send" onclick="resend()">Send current pattern to Live</button>
  <p class="note" id="s-resend">Sends the pattern and the knob positions K1&ndash;K3. Live's device asks for them by itself when it loads and from its own button. Use this if Live still plays the wrong scene or sounds different from the panel.</p>
</div>

<div class="card">
  <h2>Panel</h2>
  <div id="s-panel">Panel: &hellip;</div>
  <div id="panel-links" style="display:none">
    <a id="l-console" target="_blank" rel="noopener">Console</a>
    <a id="l-midi" target="_blank" rel="noopener">MIDI</a>
    <a id="l-knobs" target="_blank" rel="noopener">Knobs</a>
    <a id="l-cap0" target="_blank" rel="noopener">Captions off</a>
    <a id="l-cap1" target="_blank" rel="noopener">Captions on</a>
  </div>
  <div id="panel-guess" style="display:none">
    <p class="note">No MIDI session, so FlowLocal cannot tell which device is the panel. Other devices on the PatternflowLocal Wi-Fi (the panel should be one of them):</p>
    <ul id="guess-list"></ul>
  </div>
  <p class="note" id="panel-note">&hellip;</p>
</div>

<div class="card">
  <h2>Hotspot</h2>
  <div class="row"><span class="label">SSID</span><span class="val" id="s-ssid">&hellip;</span></div>
  <div class="row"><span class="label">IP</span><span class="val" id="s-ip">&hellip;</span></div>
  <div class="row"><span class="label">Wi-Fi devices</span><span class="val" id="s-cli">&hellip;</span></div>
  <div class="row"><span class="label">Channel</span><span class="val" id="s-ch">&hellip;</span></div>
  <p class="note" style="margin-top:.4rem">Name and password are fixed in this firmware and cannot be changed here.</p>
</div>

<div class="card">
  <h2>System</h2>
  <div class="row"><span class="label">Uptime</span><span class="val" id="s-up">&hellip;</span></div>
  <div class="row"><span class="label">Last restart</span><span class="val" id="s-rst">&hellip;</span></div>
  <div class="row"><span class="label">Self-repairs (USB reconnects / restarts)</span><span class="val" id="s-fix">&hellip;</span></div>
  <div class="row"><span class="label">Free heap</span><span class="val" id="s-heap">&hellip;</span></div>
  <div class="row"><span class="label">Firmware</span><span class="val" id="s-ver">&hellip;</span></div>
  <a href="/update" class="btn">Firmware update</a>
  <button onclick="reboot()">Reboot</button>
</div>

<script>
const $=id=>document.getElementById(id);
function fmt(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h+'h '+m+'m '+(s%60)+'s';}
function ago(ms){if(ms<0)return 'never';if(ms<1000)return 'just now';const s=Math.round(ms/1000);return s<120?s+' s ago':fmt(s)+' ago';}
async function reboot(){if(!confirm('Reboot FlowLocal? MIDI to Live stops until the panel has reconnected by itself, usually within a minute (not yet measured on hardware).'))return;try{await fetch('/api/reboot',{method:'POST'});}catch(e){}}
async function resend(){
  let t;
  try{const r=await fetch('/api/resend',{method:'POST'});const j=await r.json();t=j.ok?('Sending pattern '+j.program+(j.knobs>0?(' and '+j.knobs+' knob position'+(j.knobs>1?'s':'')):'')+' to Live.'):j.error;}
  catch(e){t='No answer from FlowLocal.';}
  $('s-resend').textContent=t;
}
const LINK_NOTE="Opens the panel's own pages; this phone must be on the PatternflowLocal Wi-Fi. On MIDI, every button, switch and slider saves to the panel at once: change nothing there unless you are repairing the setup.";
let panelKey;
function newLink(href,text){const a=document.createElement('a');a.href=href;a.textContent=text;a.target='_blank';a.rel='noopener';return a;}
// ip: the panel's session address, or '' with no session. With no session,
// the hotspot's other devices (not the phone looking at this page) are listed.
function setPanel(ip,stations,viewer){
  const others=ip?[]:(Array.isArray(stations)?stations:[]).filter(s=>s&&s!==viewer);
  const key=ip+'|'+others.join(',');
  if(key===panelKey)return;
  panelKey=key;
  $('s-panel').textContent='Panel: '+(ip||'unknown');
  $('panel-links').style.display=ip?'':'none';
  $('panel-guess').style.display=others.length?'':'none';
  const list=$('guess-list');
  list.textContent='';
  for(const s of others){
    const li=document.createElement('li');
    li.append(s,newLink('http://'+s+'/','Console'),newLink('http://'+s+'/midi','MIDI'));
    list.append(li);
  }
  $('panel-note').textContent=(ip||others.length)?LINK_NOTE:'No MIDI session, and no other device with an address is on the PatternflowLocal Wi-Fi: the panel is off or has not joined the hotspot.';
  if(!ip)return;
  const b='http://'+ip;
  $('l-console').href=b+'/';
  $('l-midi').href=b+'/midi';
  $('l-knobs').href=b+'/knobs';
  $('l-cap0').href=b+'/api/exhibit?caption=0';
  $('l-cap1').href=b+'/api/exhibit?caption=1';
}
async function poll(){
  let d;
  try{d=await(await fetch('/api/status',{cache:'no-store'})).json();}
  catch(e){$('state').className='bad';$('state').textContent='No answer from FlowLocal';return;}
  const m=d.midi||{};
  const hints=[];
  if(!m.task_running||m.bridge_stalled)hints.push('MIDI bridge is not running: reboot FlowLocal.');
  if(!m.usb_mounted)hints.push('Laptop not seen: USB cable must be in the jack labelled USB (not COM), laptop awake.');
  if(!(m.peers>0)){
    if(m.last_unknown_age_ms>=0&&m.last_unknown_age_ms<15000)hints.push('Panel is still on a session FlowLocal no longer has (FlowLocal restarted, or ended it after '+Math.round((m.rtp_session_timeout_ms||25000)/1000)+' s without hearing from it). FlowLocal told it to reconnect; it does so by itself, usually within a minute (not yet measured on hardware).');
    else if(d.ap_clients>=d.ap_max_clients)hints.push('Hotspot full ('+d.ap_clients+' of '+d.ap_max_clients+' devices): turn off Wi-Fi on a phone so the panel can join.');
    else hints.push('Panel not connected: check the panel is powered and on the PatternflowLocal Wi-Fi. After the panel or FlowLocal restarts, it reconnects by itself, usually within a minute (not yet measured on hardware).');
  }
  const ok=hints.length===0;
  $('state').className=ok?'ok':(m.usb_mounted||m.peers>0?'warn':'bad');
  $('state').textContent=ok?'Everything connected':'Needs attention';
  $('hints').innerHTML=hints.map(h=>'<li>'+h+'</li>').join('');
  $('s-peers').textContent=m.peers>0?('connected'+(m.peers>1?' ('+m.peers+')':'')):'not connected';
  $('s-usb').textContent=m.usb_mounted?('connected as "'+m.usb_product+'"'):(m.usb_started?'not connected':'USB failed to start');
  $('s-heard').textContent=ago(m.last_heard_age_ms);
  $('s-last').textContent=ago(m.last_rx_age_ms);
  $('s-prog').textContent=m.last_program>=0?(m.last_program+' (ch '+m.last_program_channel+')'):'none yet';
  $('s-count').textContent=m.rx+' / '+m.tx+' / '+m.dropped;
  $('s-mirror').textContent=(m.program_mirror||'off')==='off'?'off':(m.program_mirror.replace(' value=program','')+': '+m.program_mirrors+' sent');
  $('s-knobs').textContent=(m.knob_resend||'off')==='off'?'off':(m.knob_resend.replace(' value=last','')+': '+(m.knob_values||[]).map(v=>v==null?'\u2013':v).join(' / ')+', '+m.knob_resends+' re-sent');
  $('s-gaps').textContent=m.rtp_gaps;
  $('s-jit').textContent=m.rtp_jitter_ms>=0?(m.rtp_jitter_ms+' ms / '+(m.rtp_spread_ms>=0?m.rtp_spread_ms+' ms':'–')):'no data yet';
  setPanel(d.panel_ip||'',d.ap_stations,d.viewer_ip||'');
  $('s-ssid').textContent=d.ap_ssid;
  $('s-ip').textContent=d.ap_ip;
  $('s-cli').textContent=d.ap_clients+' of max '+d.ap_max_clients;
  const sc=d.ap_channel_scan,cm=d.ap_channel_mode;
  const scores=sc&&sc.ok&&sc.scores?', busy at boot 1: '+sc.scores['1']+' / 6: '+sc.scores['6']+' / 11: '+sc.scores['11']:'';
  $('s-ch').textContent=d.ap_channel+' ('+(cm==='auto'?(sc&&sc.ok?'auto, least busy of 1/6/11 at boot':'auto, scan failed: fallback'):cm==='manual'?'set from the laptop monitor':'fixed')+')'+scores;
  $('s-up').textContent=fmt(d.uptime_s);
  const rs=d.restart||{};
  $('s-rst').textContent=(rs.reset_reason||'?')+(rs.restart_note?' ('+rs.restart_note+')':'')+', boot '+rs.boots_since_power_on+' since power-on';
  $('s-fix').textContent=(m.usb_reconnects||0)+' / '+rs.recovery_restarts+' of '+rs.recovery_restarts_max;
  $('s-heap').textContent=(d.free_heap/1024).toFixed(1)+' kB';
  $('s-ver').textContent=d.version+' '+d.build;
}
poll();setInterval(poll,2000);
</script>
</body>
</html>
)rawhtml";

static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse* res = req->beginResponse(code, "application/json", out);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
}

static void refuse(AsyncWebServerRequest* req, const char* why) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = why;
    sendJson(req, doc, 403);
}

inline void begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(200, "text/html", EXHIBIT_HTML);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["version"]        = FL_VERSION;
        doc["build"]          = "exhibit";
        doc["exhibit"]        = true;
        doc["uptime_s"]       = (uint32_t)(millis() / 1000UL);
        doc["free_heap"]      = ESP.getFreeHeap();
        doc["ap_ssid"]        = FlWifi::apSsid;
        doc["ap_ip"]          = FlWifi::apIp();
        doc["ap_enabled"]     = true;
        doc["ap_clients"]     = FlWifi::apClients();
        doc["ap_max_clients"] = FL_AP_MAX_CONN;
        // The channel the AP started on and how it was picked (fl_wifi.h):
        // "auto" (the boot scan), "fixed" (FL_EXHIBIT_AP_CHANNEL) or "manual"
        // (the PF monitor's command 13, kept in NVS: ap_channel_choice, 0 =
        // automatic). The scan's scores whenever it ran (auto and manual).
        doc["ap_channel"]     = FlWifi::apChannel;
        doc["ap_channel_mode"] = FlApChannel::modeName(FlWifi::channelMode);
        doc["ap_channel_choice"] = FlWifi::channelStored;
        if (FlWifi::channelMode != FlApChannel::Mode::Fixed) {
            JsonObject scan = doc["ap_channel_scan"].to<JsonObject>();
            scan["ok"]       = FlWifi::channelScanned;
            scan["ms"]       = FlWifi::channelScanMs;
            scan["networks"] = FlWifi::channelScores.networks;
            JsonObject score = scan["scores"].to<JsonObject>();
            score["1"]  = FlWifi::channelScores.score[0];
            score["6"]  = FlWifi::channelScores.score[1];
            score["11"] = FlWifi::channelScores.score[2];
        }
        doc["sta_enabled"]    = false;
        const String panelIp  = FlMidiBridge::panelIpString();   // the newest RTP session's address
        if (panelIp.length()) doc["panel_ip"] = panelIp;
        else doc["panel_ip"] = nullptr;
        // Every station on the hotspot that has a DHCP lease (at most
        // FL_AP_MAX_CONN), and the address this request came from. With no
        // session they are the page's only lead to the panel's address: a panel
        // with MIDI off or another host saved never opens a session. The
        // lease lookup runs in the lwIP thread (esp_netif IPC), so it is safe
        // from this task.
        JsonArray stations = doc["ap_stations"].to<JsonArray>();
        wifi_sta_list_t staList{};
        wifi_sta_mac_ip_list_t ipList{};
        if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK &&
            esp_wifi_ap_get_sta_list_with_ip(&staList, &ipList) == ESP_OK) {
            for (int i = 0; i < ipList.num && i < ESP_WIFI_MAX_CONN_NUM; ++i)
                if (ipList.sta[i].ip.addr != 0)
                    stations.add(IPAddress(ipList.sta[i].ip.addr).toString());
        }
        const IPAddress viewer = req->client() ? req->client()->remoteIP() : IPAddress();
        if ((uint32_t)viewer != 0) doc["viewer_ip"] = viewer.toString();
        else doc["viewer_ip"] = nullptr;
        FlMidiBridge::appendStatus(doc);
        sendJson(req, doc);
    });

    // The regular dashboard's forms, refused with a reason instead of a 404,
    // so an old bookmarked page or script says why nothing changed.
    server.on("/api/ap", HTTP_POST, [](AsyncWebServerRequest* req) {
        refuse(req, "exhibit build: the hotspot (SSID, password, IP, on/off) is fixed in "
                    "firmware and cannot be changed from the dashboard");
    });
    server.on("/api/sta", HTTP_POST, [](AsyncWebServerRequest* req) {
        refuse(req, "exhibit build: upstream Wi-Fi (STA) is disabled");
    });

    // The bridge task does the write; this only raises its flag.
    server.on("/api/resend", HTTP_POST, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        const bool ok = FlMidiBridge::requestResend();
        doc["ok"] = ok;
        if (ok) {
            doc["program"] = FlMidiBridge::lastProgram.load();
            doc["knobs"]   = FlMidiBridge::knobResendEnabled() ? FlMidiBridge::knownKnobs() : 0;
        }
        else doc["error"] = "No pattern yet: the panel has not sent a Program Change since FlowLocal started.";
        sendJson(req, doc, ok ? 200 : 409);
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Rebooting");
        delay(300);
        FlMidiBridge::restartFor(FlRecovery::Note::Web);   // the next boot reports why=web
    });

    FlOta::begin(server);

    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.printf("[FlWeb] exhibit console at http://%s (port %d)\n",
                  FlWifi::apIp().c_str(), FL_WEB_PORT);
}

}  // namespace FlWeb
