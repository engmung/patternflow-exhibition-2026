#pragma once
// Optional ST7735 status display. Soft-fail: missing glass does not block boot.

#include "../flowlocal_config.h"

#if FL_TFT_ENABLE

#include "fl_st7735.h"
#include "fl_wifi.h"
#include "fl_broker.h"
#include "fl_store.h"
#include "fl_ntp.h"
#include "fl_weather.h"
#include "fl_display_prefs.h"

namespace FlDisplay {

inline FlSt7735* tft = nullptr;
inline bool ready = false;
inline bool pending = false;
inline uint32_t allowInitAfterMs = 0;
inline uint32_t lastDrawMs = 0;
inline uint32_t lastActSeq = 0;
inline uint32_t lastPrefRev = 0;
inline char lastFingerprint[96] = {};

// Defer SPI until USB MSC has had time to enumerate. Early SPI.begin() on the
// shared-era pins correlated with devices=0 / stick never appearing.
inline void begin() {
    FlDisplayPrefs::load();
    pending = true;
    allowInitAfterMs = millis() + 8000;
    Serial.println("[tft] deferred until USB settles (or 8s)");
}

inline void tryInit() {
    if (ready || !pending) return;
    const bool usbOk = FlStore::mounted();
    if (!usbOk && (int32_t)(millis() - allowInitAfterMs) < 0) return;

    pending = false;
    pinMode(FL_TFT_BL, OUTPUT);
    digitalWrite(FL_TFT_BL, HIGH);
    tft = new FlSt7735();
    tft->begin();
    ready = true;
    Serial.printf("[tft] ST7735 ready (usb=%s)\n", usbOk ? "mounted" : "timeout");
}

inline void trunc(char* s, size_t max) {
    if (strlen(s) > max) {
        s[max - 1] = '\0';
        if (max > 1) s[max - 2] = '.';
    }
}

inline void line(int16_t y, const char* label, const char* val, uint16_t lc,
                 uint16_t vc) {
    tft->setTextSize(1);
    tft->setCursor(2, y);
    tft->setTextColor(lc, 0x0000);
    tft->print(label);
    tft->setTextColor(vc, 0x0000);
    tft->print(val);
}

inline void redraw() {
    if (!ready || !tft) return;
    const auto& f = FlDisplayPrefs::flags;
    tft->fillScreen(0x0000);
    int16_t y = 2;
    const uint16_t dim = FlSt7735::color565(80, 80, 90);
    const uint16_t acc = FlSt7735::color565(120, 220, 255);
    const uint16_t wh = 0xFFFF;

    tft->setTextSize(1);
    tft->setCursor(2, y);
    tft->setTextColor(acc, 0x0000);
    tft->printf("FlowLocal %s", FL_VERSION);
    y += 12;

    if (f.ssid) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%s", FlWifi::apSsid.c_str());
        trunc(buf, 18);
        line(y, "SSID ", buf, dim, wh);
        y += 10;
    }
    if (f.ip) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%s", FlWifi::apIp().c_str());
        line(y, "AP   ", buf, dim, wh);
        y += 10;
    }
    if (f.sta) {
        char buf[22];
        if (!FlWifi::staEnabled) {
            snprintf(buf, sizeof(buf), "off");
        } else if (!FlWifi::isStaUp()) {
            snprintf(buf, sizeof(buf), "...");
        } else {
            snprintf(buf, sizeof(buf), "%s", FlWifi::staIp().c_str());
            trunc(buf, 14);
            if (FlNtp::isSynced()) {
                size_t n = strlen(buf);
                snprintf(buf + n, sizeof(buf) - n, " N");
            }
            if (FlWeather::haveData) {
                size_t n = strlen(buf);
                snprintf(buf + n, sizeof(buf) - n, " W");
            }
        }
        line(y, "STA  ", buf, dim, wh);
        y += 10;
    }
    if (f.clients) {
        char buf[22];
        snprintf(buf, sizeof(buf), "WiFi %d  MQTT %d", FlWifi::apClients(),
                 FlBroker::clientCount());
        line(y, "", buf, dim, wh);
        y += 10;
    }
    if (f.usb) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%s", FlStore::mounted() ? "USB ok" : "USB -");
        line(y, "Med  ", buf, dim, wh);
        y += 10;
    }
    if (f.deck) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%s", FlBroker::deck());
        trunc(buf, 18);
        line(y, "Deck ", buf, dim, acc);
        y += 10;
    }
    if (f.weather && FlWeather::enabled) {
        char buf[22];
        if (FlWeather::haveData) {
            snprintf(buf, sizeof(buf), "%.0fC %s %um", FlWeather::tempC,
                     FlWeather::condition, (unsigned)FlWeather::ageMin());
        } else {
            snprintf(buf, sizeof(buf), "%s", FlWeather::statusShort());
        }
        trunc(buf, 18);
        line(y, "Wx   ", buf, dim, wh);
        y += 10;
    }
    if (f.ntp && FlNtp::unixNow()) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%s", FlNtp::lastIso[0] ? FlNtp::lastIso : "-");
        trunc(buf, 18);
        line(y, "Time ", buf, dim, wh);
        y += 10;
    }
    if (f.heap) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%u kB  %lus",
                 (unsigned)(ESP.getFreeHeap() / 1024),
                 (unsigned long)(millis() / 1000));
        line(y, "Heap ", buf, dim, wh);
        y += 10;
    }
    if (f.mqtt) {
        tft->drawFastHLine(0, y, 128, dim);
        y += 3;
        tft->setCursor(2, y);
        tft->setTextColor(acc, 0x0000);
        tft->print("MQTT");
        y += 10;
        for (uint8_t i = 0; i < 6 && i < FlBroker::actCount; i++) {
            char buf[22];
            FlBroker::activityLine(i, buf, sizeof(buf));
            trunc(buf, 21);
            tft->setCursor(2, y);
            tft->setTextColor(wh, 0x0000);
            tft->print(buf);
            y += 9;
            if (y > 150) break;
        }
    }
}

inline void tick() {
    tryInit();
    if (!ready) return;
    uint32_t now = millis();
    if (now - lastDrawMs < 500) return;

    char fp[96];
    snprintf(fp, sizeof(fp), "%s|%s|%d|%d|%s|%u|%s|%u|%u",
             FlWifi::apSsid.c_str(), FlWifi::apIp().c_str(),
             FlWifi::apClients(), FlBroker::clientCount(),
             FlWifi::staIp().c_str(), (unsigned)FlBroker::activitySeq(),
             FlBroker::deck(), FlDisplayPrefs::rev,
             FlWeather::haveData ? (unsigned)FlWeather::ageMin() : 0u);
    if (strcmp(fp, lastFingerprint) == 0 &&
        FlBroker::activitySeq() == lastActSeq &&
        FlDisplayPrefs::rev == lastPrefRev) {
        lastDrawMs = now;
        return;
    }
    snprintf(lastFingerprint, sizeof(lastFingerprint), "%s", fp);
    lastActSeq = FlBroker::activitySeq();
    lastPrefRev = FlDisplayPrefs::rev;
    lastDrawMs = now;
    redraw();
}

}  // namespace FlDisplay

#else

namespace FlDisplay {
inline void begin() {}
inline void tick() {}
}

#endif
