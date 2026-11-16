#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — browser OTA (POST multipart .bin to /update)
//
// Arm first (POST /api/ota/arm) so casual probes cannot flash.
// Uses ESP-IDF OTA (esp_ota_ops) — no Arduino Update library.
// ═══════════════════════════════════════════════════════════

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_system.h>
#include "../flowlocal_config.h"

namespace FlOta {

inline bool armed = false;
inline uint32_t armedUntilMs = 0;
inline bool uploading = false;
inline bool failed = false;
inline char lastError[96] = {};
inline size_t bytesWritten = 0;
inline esp_ota_handle_t otaHandle = 0;
inline const esp_partition_t* otaPart = nullptr;

inline void disarm() {
    armed = false;
    armedUntilMs = 0;
}

inline void arm(uint32_t windowMs = 120000) {
    armed = true;
    armedUntilMs = millis() + windowMs;
    failed = false;
    lastError[0] = '\0';
    bytesWritten = 0;
    Serial.println("[ota] armed — /update accepts firmware");
}

inline bool isArmed() {
    if (!armed) return false;
    if ((int32_t)(millis() - armedUntilMs) > 0) {
        disarm();
        return false;
    }
    return true;
}

inline void setError(const char* msg) {
    failed = true;
    snprintf(lastError, sizeof(lastError), "%s", msg ? msg : "error");
    Serial.printf("[ota] %s\n", lastError);
}

inline void abortOta() {
    if (otaHandle) {
        esp_ota_abort(otaHandle);
        otaHandle = 0;
    }
    otaPart = nullptr;
    uploading = false;
}

static const char UPDATE_HTML[] PROGMEM = R"rawhtml(
<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FlowLocal Update</title>
<style>
:root{--bg:#111;--fg:#eee;--acc:#7ef;--red:#f55;--card:#1e1e1e}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;padding:1rem;max-width:520px;margin:0 auto}
h1{color:var(--acc);font-size:1.3rem}
.card{background:var(--card);border-radius:8px;padding:1rem;margin:1rem 0}
button,.file{padding:.55rem 1rem;border:none;border-radius:5px;cursor:pointer;font-size:.95rem}
.arm{background:var(--acc);color:#111;font-weight:700}
.go{background:#2a6;color:#fff;font-weight:700}
a{color:var(--acc)}
#st{margin-top:.8rem;font-family:monospace;font-size:.85rem;white-space:pre-wrap}
</style></head><body>
<h1>FlowLocal firmware update</h1>
<div class="card">
<p>1. Arm update (2 minutes).<br>2. Choose a <code>firmware.bin</code> built for this board.<br>3. Upload — device reboots on success.</p>
<button class="arm" id="arm">Arm update</button>
<form id="f" style="margin-top:1rem">
<input class="file" type="file" id="bin" accept=".bin" required>
<br><button class="go" type="submit" id="go" disabled>Upload &amp; flash</button>
</form>
<div id="st"></div>
<p style="margin-top:1rem"><a href="/">← Dashboard</a></p>
</div>
<script>
const st=document.getElementById('st');
const go=document.getElementById('go');
document.getElementById('arm').onclick=async()=>{
  const r=await fetch('/api/ota/arm',{method:'POST'});
  const j=await r.json();
  st.textContent=j.ok?'Armed for 2 minutes. Choose a .bin and upload.':('Arm failed: '+(j.error||r.status));
  go.disabled=!j.ok;
};
document.getElementById('f').onsubmit=async(ev)=>{
  ev.preventDefault();
  const file=document.getElementById('bin').files[0];
  if(!file){st.textContent='Pick a .bin first';return;}
  go.disabled=true;
  st.textContent='Uploading '+file.name+' ('+file.size+' bytes)…';
  const fd=new FormData();
  fd.append('firmware',file,file.name);
  try{
    const r=await fetch('/update',{method:'POST',body:fd});
    const t=await r.text();
    st.textContent=r.ok?(t+'\nRebooting…'):('Failed: '+t);
  }catch(e){
    st.textContent='Upload finished or connection dropped (device may be rebooting).\n'+e;
  }
};
</script>
</body></html>
)rawhtml";

inline void begin(AsyncWebServer& server) {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", UPDATE_HTML);
    });

    server.on("/api/ota/arm", HTTP_POST, [](AsyncWebServerRequest* req) {
        arm();
        req->send(200, "application/json", "{\"ok\":true,\"window_s\":120}");
    });

    server.on(
        "/update", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            uploading = false;
            if (failed || bytesWritten == 0 || !otaHandle) {
                if (!lastError[0]) setError("empty upload or not armed");
                String body = String("{\"ok\":false,\"error\":\"") + lastError + "\"}";
                req->send(400, "application/json", body);
                abortOta();
                disarm();
                return;
            }
            esp_err_t err = esp_ota_end(otaHandle);
            otaHandle = 0;
            if (err != ESP_OK) {
                setError(esp_err_to_name(err));
                String body = String("{\"ok\":false,\"error\":\"") + lastError + "\"}";
                req->send(500, "application/json", body);
                otaPart = nullptr;
                disarm();
                return;
            }
            err = esp_ota_set_boot_partition(otaPart);
            otaPart = nullptr;
            if (err != ESP_OK) {
                setError(esp_err_to_name(err));
                String body = String("{\"ok\":false,\"error\":\"") + lastError + "\"}";
                req->send(500, "application/json", body);
                disarm();
                return;
            }
            Serial.printf("[ota] success (%u bytes) — reboot\n", (unsigned)bytesWritten);
            req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
            disarm();
            delay(400);
            ESP.restart();
        },
        [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data,
           size_t len, bool final) {
            if (index == 0) {
                failed = false;
                lastError[0] = '\0';
                bytesWritten = 0;
                abortOta();
                uploading = true;
                if (!isArmed()) {
                    setError("not armed — POST /api/ota/arm first");
                    return;
                }
                otaPart = esp_ota_get_next_update_partition(nullptr);
                if (!otaPart) {
                    setError("no OTA partition");
                    return;
                }
                Serial.printf("[ota] start %s → %s @ 0x%08lx (%lu bytes)\n", filename.c_str(),
                              otaPart->label, (unsigned long)otaPart->address,
                              (unsigned long)otaPart->size);
                esp_err_t err = esp_ota_begin(otaPart, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
                if (err != ESP_OK) {
                    setError(esp_err_to_name(err));
                    otaHandle = 0;
                    return;
                }
            }
            if (failed || !otaHandle) return;
            if (len) {
                esp_err_t err = esp_ota_write(otaHandle, data, len);
                if (err != ESP_OK) {
                    setError(esp_err_to_name(err));
                    abortOta();
                    return;
                }
                bytesWritten += len;
            }
            if (final) {
                Serial.printf("[ota] body complete (%u bytes)\n", (unsigned)bytesWritten);
            }
        });

    Serial.println("[ota] Ready — http://flowlocal.local/update");
}

}  // namespace FlOta
