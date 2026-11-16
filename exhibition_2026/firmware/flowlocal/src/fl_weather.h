#pragma once
// FlowLocal weather hub — STA pull every 30 min, serve to panels on AP.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "../flowlocal_config.h"
#include "fl_wifi.h"
#include "fl_broker.h"

namespace FlWeather {

constexpr size_t KEY_BYTES = 48;
constexpr size_t QUERY_BYTES = 64;
constexpr size_t COND_BYTES = 32;
constexpr size_t ERR_BYTES = 64;

inline bool enabled = false;
inline bool unitsMetric = true;
inline char apiKey[KEY_BYTES] = {};
inline char query[QUERY_BYTES] = {};
inline float lat = NAN;
inline float lon = NAN;

inline bool haveData = false;
inline int weatherId = 0;
inline char condition[COND_BYTES] = {};
inline char description[COND_BYTES] = {};
inline char iconCode[8] = {};
inline float tempC = 0;
inline float feelsC = 0;
inline float humidity = 0;
inline uint32_t fetchedAtMs = 0;
inline uint32_t nextFetchDueMs = 0;
inline char lastError[ERR_BYTES] = {};
inline bool fetchInFlight = false;

inline bool configured() {
    return apiKey[0] && (query[0] || (isfinite(lat) && isfinite(lon)));
}

inline void setError(const char* e) {
    snprintf(lastError, sizeof(lastError), "%s", e ? e : "");
}

inline uint32_t ageMin() {
    if (!haveData || !fetchedAtMs) return 9999;
    return (millis() - fetchedAtMs) / 60000UL;
}

inline const char* statusShort() {
    if (!enabled) return "off";
    if (!FlWifi::isStaUp()) return "need STA";
    if (!configured()) return "cfg";
    if (haveData) return "ok";
    if (lastError[0]) return lastError;
    return "…";
}

inline bool httpGet(const char* url, String& bodyOut, int* codeOut) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        if (codeOut) *codeOut = -1;
        return false;
    }
    int code = http.GET();
    if (codeOut) *codeOut = code;
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    bodyOut = http.getString();
    http.end();
    return true;
}

inline bool geocode(float* outLat, float* outLon) {
    char qenc[QUERY_BYTES * 3];
    size_t o = 0;
    for (size_t i = 0; query[i] && o + 3 < sizeof(qenc); i++) {
        char c = query[i];
        if (c == ' ') {
            qenc[o++] = '%';
            qenc[o++] = '2';
            qenc[o++] = '0';
        } else if (c == ',') {
            qenc[o++] = '%';
            qenc[o++] = '2';
            qenc[o++] = 'C';
        } else {
            qenc[o++] = c;
        }
    }
    qenc[o] = '\0';
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/geo/1.0/direct?q=%s&limit=1&appid=%s",
             qenc, apiKey);
    String body;
    int code = 0;
    if (!httpGet(url, body, &code)) {
        snprintf(lastError, sizeof(lastError), "geo %d", code);
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc.is<JsonArray>() || doc.size() == 0) {
        setError("geo miss");
        return false;
    }
    *outLat = doc[0]["lat"] | NAN;
    *outLon = doc[0]["lon"] | NAN;
    return isfinite(*outLat) && isfinite(*outLon);
}

inline void applyCurrent(JsonDocument& doc) {
    weatherId = doc["weather"][0]["id"] | 0;
    snprintf(condition, sizeof(condition), "%s",
             (const char*)(doc["weather"][0]["main"] | ""));
    snprintf(description, sizeof(description), "%s",
             (const char*)(doc["weather"][0]["description"] | ""));
    snprintf(iconCode, sizeof(iconCode), "%s",
             (const char*)(doc["weather"][0]["icon"] | ""));
    tempC = doc["main"]["temp"] | 0.0f;
    feelsC = doc["main"]["feels_like"] | tempC;
    humidity = doc["main"]["humidity"] | 0.0f;
    haveData = true;
    fetchedAtMs = millis();
    lastError[0] = '\0';
}

inline bool fetchOnce() {
    if (fetchInFlight) return false;
    if (!FlWifi::isStaUp()) {
        setError("wifi down");
        return false;
    }
    if (!configured()) {
        setError("not configured");
        return false;
    }
    fetchInFlight = true;
    float la = lat, lo = lon;
    if ((!isfinite(la) || !isfinite(lo)) && query[0]) {
        if (!geocode(&la, &lo)) {
            fetchInFlight = false;
            nextFetchDueMs = millis() + 60000;
            return false;
        }
        lat = la;
        lon = lo;
    }
    const char* units = unitsMetric ? "metric" : "imperial";
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?lat=%.4f&lon=%.4f"
             "&units=%s&appid=%s",
             la, lo, units, apiKey);
    String body;
    int code = 0;
    if (!httpGet(url, body, &code)) {
        snprintf(lastError, sizeof(lastError), "http %d", code);
        fetchInFlight = false;
        nextFetchDueMs = millis() + 60000;
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        setError("json");
        fetchInFlight = false;
        return false;
    }
    applyCurrent(doc);
    fetchInFlight = false;
    nextFetchDueMs = millis() + FL_WEATHER_POLL_MS;
    FlBroker::publish("patternflow/weather/updated", "1");
    Serial.printf("[wx] %s %.1fC id=%d\n", condition, tempC, weatherId);
    return true;
}

inline void requestFetch() { nextFetchDueMs = 0; }

inline void tick() {
    if (!enabled || !configured()) return;
    if (!FlWifi::isStaUp()) return;
    if (nextFetchDueMs != 0 && (int32_t)(millis() - nextFetchDueMs) < 0) return;
    fetchOnce();
}

inline void load() {
    Preferences p;
    if (!p.begin(FL_NVS_NS, true)) return;
    enabled = p.getBool("wx_en", false);
    unitsMetric = p.getBool("wx_metric", true);
    if (p.isKey("wx_key")) p.getString("wx_key", apiKey, sizeof(apiKey));
    if (p.isKey("wx_q")) p.getString("wx_q", query, sizeof(query));
    if (p.isKey("wx_lat")) lat = p.getFloat("wx_lat", NAN);
    if (p.isKey("wx_lon")) lon = p.getFloat("wx_lon", NAN);
    p.end();
    if (enabled && configured()) requestFetch();
}

inline void save(bool en, const char* key, const char* q, float la, float lo,
                 bool metric, bool newKey) {
    enabled = en;
    unitsMetric = metric;
    if (q) snprintf(query, sizeof(query), "%s", q);
    lat = la;
    lon = lo;
    if (newKey && key) snprintf(apiKey, sizeof(apiKey), "%s", key);
    Preferences p;
    if (p.begin(FL_NVS_NS, false)) {
        p.putBool("wx_en", enabled);
        p.putBool("wx_metric", unitsMetric);
        p.putString("wx_q", query);
        p.putFloat("wx_lat", lat);
        p.putFloat("wx_lon", lon);
        if (newKey && key) p.putString("wx_key", apiKey);
        p.end();
    }
    if (enabled) requestFetch();
}

inline void appendStatus(JsonDocument& doc) {
    doc["wx_enabled"] = enabled;
    doc["wx_have"] = haveData;
    doc["wx_status"] = statusShort();
    doc["wx_age_min"] = haveData ? ageMin() : -1;
    doc["wx_temp"] = tempC;
    doc["wx_condition"] = condition;
    doc["weatherId"] = weatherId;
    doc["tempC"] = tempC;
    doc["feelsC"] = feelsC;
    doc["humidity"] = humidity;
    doc["condition"] = condition;
    doc["description"] = description;
    doc["icon"] = iconCode;
    doc["units_metric"] = unitsMetric;
    doc["query"] = query;
    doc["lat"] = isfinite(lat) ? lat : (double)NAN;
    doc["lon"] = isfinite(lon) ? lon : (double)NAN;
    doc["has_key"] = apiKey[0] != '\0';
    doc["error"] = lastError;
    doc["ok"] = haveData;
}

}  // namespace FlWeather
