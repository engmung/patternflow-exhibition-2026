#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — MQTT broker (PicoMQTT) + activity / deck cache
// ═══════════════════════════════════════════════════════════

#include <WiFi.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <ArduinoJson.h>
#include <string.h>
#include "../flowlocal_config.h"

namespace FlBroker {

constexpr size_t ACT_LINES = 8;
constexpr size_t ACT_WIDTH = 22;
constexpr size_t DECK_BYTES = 40;

inline char deckName[DECK_BYTES] = {};
inline char actLines[ACT_LINES][ACT_WIDTH] = {};
inline uint8_t actHead = 0;
inline uint8_t actCount = 0;
inline uint32_t actSeq = 0;

inline void pushActivity(const char* topic, const char* payload) {
    char leaf[16] = {};
    if (topic) {
        const char* slash = strrchr(topic, '/');
        const char* src = slash ? slash + 1 : topic;
        // skip channel prefix noise: patternflow/pattern → pattern
        snprintf(leaf, sizeof(leaf), "%s", src);
    }
    char line[ACT_WIDTH];
    snprintf(line, sizeof(line), "%s %s", leaf,
             payload ? payload : "");
    snprintf(actLines[actHead], ACT_WIDTH, "%s", line);
    actHead = (actHead + 1) % ACT_LINES;
    if (actCount < ACT_LINES) actCount++;
    actSeq++;
}

inline void noteTopic(const char* topic, const char* payload) {
    if (!topic) return;
    Serial.printf("[broker] %s  %s\n", topic, payload ? payload : "");

    // Match …/snapshot, …/pattern, …/message (any prefix)
    const char* snap = strstr(topic, "/snapshot");
    const char* pat = strstr(topic, "/pattern");
    const char* msg = strstr(topic, "/message");
    bool isSnap = snap && snap[9] == '\0';
    bool isPat = pat && pat[8] == '\0';
    bool isMsg = msg && msg[8] == '\0';

    if (isSnap && payload && payload[0] == '{') {
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            const char* title = doc["title"] | "";
            const char* id = doc["id"] | "";
            if (title[0]) snprintf(deckName, sizeof(deckName), "%s", title);
            else if (id[0]) snprintf(deckName, sizeof(deckName), "%s", id);
        }
    }

    if (isSnap || isPat || isMsg || strstr(topic, "/param/")) {
        pushActivity(topic, payload);
    }
}

class FlBrokerImpl : public PicoMQTT::Server {
public:
    FlBrokerImpl(::WiFiServer& tcp, PicoWebsocket::Server<::WiFiServer>& ws)
        : PicoMQTT::Server(tcp, ws) {}

    int clientCount() const {
        return (int)clients.size();
    }

protected:
    void on_connected(const char* client_id) override {
        Serial.printf("[broker] +client %s\n", client_id ? client_id : "?");
    }

    void on_disconnected(const char* client_id) override {
        Serial.printf("[broker] -client %s\n", client_id ? client_id : "?");
    }
};

static ::WiFiServer tcpServer(FL_MQTT_PORT);
static ::WiFiServer wsUnderlying(FL_MQTT_WS_PORT);
static PicoWebsocket::Server<::WiFiServer> wsServer(wsUnderlying);
static FlBrokerImpl broker(tcpServer, wsServer);

inline void begin() {
    broker.subscribe("#", [](const char* topic, const char* payload) {
        noteTopic(topic, payload);
    });
    broker.begin();
    Serial.printf("[broker] PicoMQTT  TCP :%d  WS :%d\n", FL_MQTT_PORT, FL_MQTT_WS_PORT);
}

inline void tick() {
    broker.loop();
}

inline int clientCount() {
    return broker.clientCount();
}

inline const char* deck() { return deckName[0] ? deckName : "—"; }

inline void activityLine(uint8_t newestIndex, char* out, size_t outLen) {
    // newestIndex 0 = most recent
    if (newestIndex >= actCount || outLen == 0) {
        if (outLen) out[0] = '\0';
        return;
    }
    int idx = (int)actHead - 1 - (int)newestIndex;
    while (idx < 0) idx += ACT_LINES;
    snprintf(out, outLen, "%s", actLines[idx % ACT_LINES]);
}

inline uint32_t activitySeq() { return actSeq; }

// Optional lightweight publish helper (weather updated ping)
inline void publish(const char* topic, const char* payload) {
    if (!topic) return;
    broker.publish(topic, payload ? payload : "");
}

}  // namespace FlBroker
