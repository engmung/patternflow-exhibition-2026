#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — NTP when upstream STA is connected
//
// Uses the IDF SNTP client (configTime). Panels on the AP can
// read the synced clock via GET /api/time.
// ═══════════════════════════════════════════════════════════

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "../flowlocal_config.h"

namespace FlNtp {

inline bool started = false;
inline bool synced = false;
inline int32_t tzOffsetMin = 0;  // minutes east of UTC
inline char lastIso[32] = {};

inline void loadTz() {
    Preferences p;
    p.begin(FL_NVS_NS, true);
    tzOffsetMin = p.getInt("tz_min", FL_TZ_OFFSET_MIN);
    p.end();
}

inline void saveTz(int32_t minutes) {
    if (minutes < -14 * 60) minutes = -14 * 60;
    if (minutes > 14 * 60) minutes = 14 * 60;
    tzOffsetMin = minutes;
    Preferences p;
    p.begin(FL_NVS_NS, false);
    p.putInt("tz_min", tzOffsetMin);
    p.end();
}

inline void begin() {
    loadTz();
}

// Call when STA just came up (or periodically while STA is up).
inline void startSync() {
    // GMT offset applied by configTime so localtime() is venue-local.
    long gmt = (long)tzOffsetMin * 60L;
    configTime(gmt, 0, FL_NTP_SERVER, "time.nist.gov");
    started = true;
    synced = false;
    Serial.printf("[ntp] syncing via %s (UTC%+d min)\n", FL_NTP_SERVER,
                  (int)tzOffsetMin);
}

inline void stop() {
    started = false;
    synced = false;
    lastIso[0] = '\0';
}

inline void tick() {
    if (!started || synced) return;
    time_t now = time(nullptr);
    if (now < 1700000000) return;  // not yet 2023+
    synced = true;
    struct tm ti;
    localtime_r(&now, &ti);
    strftime(lastIso, sizeof(lastIso), "%Y-%m-%dT%H:%M:%S", &ti);
    Serial.printf("[ntp] synced %s (tz %+d)\n", lastIso, (int)tzOffsetMin);
}

inline bool isSynced() { return synced; }
inline bool isStarted() { return started; }

inline time_t unixNow() {
    time_t now = time(nullptr);
    return (now > 1700000000) ? now : 0;
}

inline bool fillLocalTm(struct tm* out) {
    time_t now = unixNow();
    if (!now || !out) return false;
    localtime_r(&now, out);
    return true;
}

inline void appendStatus(JsonDocument& doc) {
    doc["ntp_started"] = started;
    doc["ntp_synced"] = synced;
    doc["ntp_server"] = FL_NTP_SERVER;
    doc["tz_offset_min"] = tzOffsetMin;
    doc["time_unix"] = (uint32_t)unixNow();
    if (synced && lastIso[0]) doc["time_iso"] = lastIso;
    else if (unixNow()) {
        struct tm ti;
        if (fillLocalTm(&ti)) {
            strftime(lastIso, sizeof(lastIso), "%Y-%m-%dT%H:%M:%S", &ti);
            doc["time_iso"] = lastIso;
        }
    }
}

}  // namespace FlNtp
