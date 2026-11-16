#pragma once
// SPI MicroSD — CS=4 SCK=5 MOSI=6 MISO=7 (3.3 V).

#include <SPI.h>
#include <SD.h>
#include "../flowlocal_config.h"

namespace FlSd {

static bool cardOk = false;
static uint32_t lastTryMs = 0;
static uint32_t lastLogMs = 0;
static String lastError = "idle";
static String contentRootStr = "/director";
static String rootNamesStr;
static SPIClass hspi(HSPI);

inline bool mounted() { return cardOk; }
inline int devices() { return cardOk ? 1 : 0; }
inline String errorText() { return lastError; }
inline fs::FS& storage() { return SD; }
inline String contentRoot() { return contentRootStr; }
inline String rootNames() { return rootNamesStr; }

static bool isReadableFile(const String& path) {
    File f = SD.open(path);
    const bool ok = f && !f.isDirectory();
    if (f) f.close();
    return ok;
}

static String leafName(String n) {
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    n.toLowerCase();
    return n;
}

static void listRoot() {
    rootNamesStr = "";
    File root = SD.open("/");
    if (!root || !root.isDirectory()) return;
    Serial.println("[sd] root:");
    for (;;) {
        File entry = root.openNextFile();
        if (!entry) break;
        String n = entry.name();
        Serial.printf("  %s %s\n", entry.isDirectory() ? "DIR " : "FILE", n.c_str());
        if (rootNamesStr.length()) rootNamesStr += " ";
        rootNamesStr += leafName(n);
        entry.close();
    }
    root.close();
}

static void findDirector() {
    contentRootStr = "/director";
    static const char* idx[] = {
        "/director/index.html",
        "/flash_image/director/index.html",
        "/Director/index.html",
        "/DIRECTOR/INDEX.HTM",
    };
    for (const char* p : idx) {
        if (isReadableFile(p)) {
            String s(p);
            contentRootStr = s.substring(0, s.lastIndexOf('/'));
            Serial.printf("[sd] Director at %s\n", contentRootStr.c_str());
            return;
        }
    }
    File root = SD.open("/");
    if (!root || !root.isDirectory()) return;
    for (;;) {
        File e = root.openNextFile();
        if (!e) break;
        const String leaf = leafName(e.name());
        const bool dir = e.isDirectory();
        e.close();
        if (!dir) continue;
        String try1 = String("/") + leaf + "/index.html";
        String try2 = String("/") + leaf + "/director/index.html";
        if (isReadableFile(try1)) {
            contentRootStr = String("/") + leaf;
            Serial.printf("[sd] Director at %s\n", contentRootStr.c_str());
            root.close();
            return;
        }
        if (isReadableFile(try2)) {
            contentRootStr = String("/") + leaf + "/director";
            Serial.printf("[sd] Director at %s\n", contentRootStr.c_str());
            root.close();
            return;
        }
    }
    root.close();
    Serial.println("[sd] no director/index.html on card");
}

static bool attemptBegin(SPIClass& bus, uint32_t hz, const char* tag) {
    Serial.printf("[sd] %s begin @ %u Hz  MISO=%d\n", tag, (unsigned)hz,
                  digitalRead(FL_SD_MISO));
    if (!SD.begin(FL_SD_CS, bus, hz, "/sd", 5, false)) {
        Serial.printf("[sd] %s begin failed\n", tag);
        SD.end();
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        Serial.println("[sd] CARD_NONE");
        SD.end();
        return false;
    }
    return true;
}

static bool tryMount() {
    pinMode(FL_SD_CS, OUTPUT);
    digitalWrite(FL_SD_CS, HIGH);
    pinMode(FL_SD_MISO, INPUT_PULLUP);
    delay(5);
    const int misoIdle = digitalRead(FL_SD_MISO);
    Serial.printf("[sd] MISO idle (CS high)=%d  (1=ok, 0=adapter holding line)\n", misoIdle);

    SPI.begin(FL_SD_SCK, FL_SD_MISO, FL_SD_MOSI, FL_SD_CS);
    delay(50);
    digitalWrite(FL_SD_CS, HIGH);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 16; i++) SPI.transfer(0xFF);
    SPI.endTransaction();
    delay(10);

    if (attemptBegin(SPI, 400000, "FSPI") || attemptBegin(SPI, 1000000, "FSPI")) {
        cardOk = true;
        lastError = "mounted";
        Serial.printf("[sd] mounted  %llu MB\n",
                      (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
        listRoot();
        findDirector();
        return true;
    }

    SPI.end();
    hspi.begin(FL_SD_SCK, FL_SD_MISO, FL_SD_MOSI, FL_SD_CS);
    delay(20);
    if (attemptBegin(hspi, 400000, "HSPI")) {
        cardOk = true;
        lastError = "mounted";
        Serial.printf("[sd] mounted  %llu MB via HSPI\n",
                      (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
        listRoot();
        findDirector();
        return true;
    }

    cardOk = false;
    if (misoIdle == 0) {
        lastError = "begin failed, MISO held LOW (blue adapter). Keep VCC on 5V, push card fully in.";
    } else {
        lastError = "begin failed. CS=4 SCK=5 MOSI=6 MISO=7.";
    }
    return false;
}

inline void begin() {
    delay(300);
    Serial.printf("[sd] CS=%d MOSI=%d SCK=%d MISO=%d\n",
                  FL_SD_CS, FL_SD_MOSI, FL_SD_SCK, FL_SD_MISO);
    if (!tryMount()) {
        Serial.printf("[sd] %s\n", lastError.c_str());
    }
}

inline void tick() {
    if (cardOk) return;
    const uint32_t now = millis();
    if (now - lastTryMs < 4000) return;
    lastTryMs = now;
    if (tryMount()) return;
    if (now - lastLogMs >= 8000) {
        lastLogMs = now;
        Serial.printf("[sd] not mounted (%s)\n", lastError.c_str());
    }
}

}  // namespace FlSd
