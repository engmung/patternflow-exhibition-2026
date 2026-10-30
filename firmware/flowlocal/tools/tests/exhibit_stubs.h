// Stand-ins for the exhibit bridge's host tests (tools/check_exhibit_bridge.py):
// Arduino, FreeRTOS, a model USB host behind TinyUSB / USBMIDI, Wi-Fi (the soft
// AP's station list, a model of the panel's web server, UDP), the ESP-IDF
// pieces the bridge calls (timer, heap, reset reason, task watchdog, RTC
// memory, spinlocks) and NVS (Preferences). Shared by exhibit_bridge_test.cpp
// (with a stand-in for the AppleMIDI session) and exhibit_session_test.cpp
// (with the real AppleMIDI 3.5.0 library). Everything is a global the tests
// set up and inspect; the stub headers the script writes are empty.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

// ── Arduino ────────────────────────────────────────────────────────────────
static uint32_t g_now = 5000;
inline uint32_t millis() { return g_now; }
inline void delay(uint32_t ms) { g_now += ms; }
inline long random(long lo, long) { return lo; }
inline void randomSeed(unsigned long) {}
using std::min;   // Arduino-ESP32's Arduino.h does the same (AppleMIDI calls min())
using std::max;

class String {
public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
private:
    std::string s_;
};

class IPAddress {
public:
    IPAddress() = default;
    IPAddress(uint32_t a) : a_(a) {}
    operator uint32_t() const { return a_; }
    String toString() const {
        char b[16];
        std::snprintf(b, sizeof b, "%u.%u.%u.%u", a_ & 255u, (a_ >> 8) & 255u, (a_ >> 16) & 255u, a_ >> 24);
        return String(b);
    }
private:
    uint32_t a_ = 0;
};
static const IPAddress INADDR_NONE(0u);

// The bridge's log, one line per printf / println (kept for the checks).
static std::vector<std::string> g_log;
struct SerialStub {
    template <class... A> void printf(const char* f, A... a) {
        if constexpr (sizeof...(A) == 0) {
            g_log.push_back(f);
        } else {
            char b[512];
            std::snprintf(b, sizeof b, f, a...);
            g_log.push_back(b);
        }
    }
    void println(const char* s) { g_log.push_back(std::string(s) + "\n"); }
    void flush() {}
} Serial;
static bool logged(const char* part) {
    for (const std::string& l : g_log)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

static int g_restarts = 0;
struct EspStub {
    uint64_t getEfuseMac() { return 0x0000A1B2C3D4E5F6ull; }
    void restart() { ++g_restarts; }
} ESP;

// ── FreeRTOS ───────────────────────────────────────────────────────────────
typedef void* TaskHandle_t;
typedef unsigned UBaseType_t;
#define pdPASS 1
#define pdFAIL 0
static int g_taskCreated = 0;
static std::vector<std::string> g_taskNames;   // every task started, in order
static bool g_taskCreateFails = false;
inline int xTaskCreatePinnedToCore(void (*)(void*), const char* name, uint32_t, void*, int,
                                   TaskHandle_t* handle, int) {
    if (g_taskCreateFails) return pdFAIL;
    ++g_taskCreated;
    g_taskNames.push_back(name);
    if (handle) *handle = &g_taskCreated;
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}

// ── USB host model ─────────────────────────────────────────────────────────
typedef struct {
    uint8_t header;
    uint8_t byte1;
    uint8_t byte2;
    uint8_t byte3;
} midiEventPacket_t;

struct HostModel {
    bool mounted = false;
    bool suspended = false;
    bool reading = true;                     // the laptop drains the FIFO every frame
    size_t capacity = 16;                    // TinyUSB MIDI TX FIFO, in packets
    std::deque<midiEventPacket_t> fifo;      // written by the bridge, not read yet
    std::vector<midiEventPacket_t> got;      // what the laptop read, in order
    std::deque<midiEventPacket_t> toDevice;  // what the laptop sent
    // Soft reconnect: tud_disconnect() and the host lets go of the device at
    // once; after tud_connect() it enumerates it again 50 ms later (a bus
    // reset: TinyUSB's FIFOs emptied, the suspend flag cleared), unless it
    // ignores the device. resetFixesReading: whether that bus reset also
    // gets a laptop that stopped reading (reading false) reading again.
    bool pullup = true;
    int disconnects = 0;
    int connects = 0;
    bool takesBack = true;
    bool resetFixesReading = true;
    bool reenumPending = false;
    uint32_t reenumAt = 0;
    void frame();
};
static HostModel host;

#define CONFIG_TINYUSB_MIDI_ENABLED 1
#define CFG_TUD_MIDI_TX_BUFSIZE 64               // tusb_config.h (CONFIG_TINYUSB_MIDI_TX_BUFSIZE)
#define CFG_TUD_MIDI_TX_EPSIZE 64                // midi_device.h, full speed
inline bool tud_midi_mounted() { return host.mounted; }
inline bool tud_mounted() { return host.mounted; }
inline bool tud_suspended() { return host.suspended; }
inline bool tud_disconnect() {
    host.pullup = false;
    ++host.disconnects;
    host.mounted = false;
    host.reenumPending = false;
    return true;
}
inline bool tud_connect() {
    host.pullup = true;
    ++host.connects;
    if (host.takesBack) {
        host.reenumPending = true;
        host.reenumAt = g_now + 50;
    }
    return true;
}
void HostModel::frame() {
    if (reenumPending && (int32_t)(g_now - reenumAt) >= 0) {
        reenumPending = false;
        mounted = true;
        suspended = false;
        fifo.clear();
        toDevice.clear();
        if (resetFixesReading) reading = true;
    }
    if (!mounted || suspended || !reading) return;
    while (!fifo.empty()) {
        got.push_back(fifo.front());
        fifo.pop_front();
    }
}

class USBMIDI {
public:
    explicit USBMIDI(const char*) {}
    void begin() {}
    bool writePacket(midiEventPacket_t* p) {
        if (host.fifo.size() >= host.capacity) return false;
        host.fifo.push_back(*p);
        return true;
    }
    bool readPacket(midiEventPacket_t* p) {
        if (host.toDevice.empty()) return false;
        *p = host.toDevice.front();
        host.toDevice.pop_front();
        return true;
    }
};

struct UsbStub {
    void productName(const char*) {}
    void manufacturerName(const char*) {}
    bool begin() { return true; }
} USB;

// A pass of the bridge task ends at its stack high-water mark read.
struct EndOfPass {};
static int g_passesLeft = 0;
inline void vTaskDelay(uint32_t ticks) {
    g_now += ticks;
    host.frame();
}
inline UBaseType_t uxTaskGetStackHighWaterMark(void*) {
    if (--g_passesLeft <= 0) throw EndOfPass{};
    return 4096;
}

// ── Wi-Fi ──────────────────────────────────────────────────────────────────
static bool g_apStarted = true;   // the core's AP-started bit (WIFI_EVENT_AP_START seen)
struct WiFiStub {
    IPAddress softAPIP() { return IPAddress(0x0142A8C0u); }
    struct {
        bool started() const { return g_apStarted; }
    } AP;
} WiFi;
// UDP as the RTP session sees it: the datagrams the panel sent, per local
// port (5004 control, 5005 data), and every datagram FlowLocal sent. The
// bridge test's session stand-in never touches it; the session test runs the
// real AppleMIDI library over it.
struct Datagram {
    uint16_t local = 0;     // FlowLocal's port it arrived on / left from
    uint32_t ip = 0;        // the other end (IPAddress raw)
    uint16_t port = 0;
    std::vector<uint8_t> bytes;
};
struct UdpNet {
    std::map<uint16_t, std::deque<Datagram>> inbound;   // by FlowLocal's port, oldest first
    std::vector<Datagram> sent;                          // everything FlowLocal sent, in order
};
static UdpNet udp;
class WiFiUDP {
public:
    uint8_t begin(uint16_t port) { local_ = port; return 1; }
    void stop() {}
    int parsePacket() {
        std::deque<Datagram>& q = udp.inbound[local_];
        have_ = !q.empty();
        if (!have_) return 0;
        cur_ = q.front();
        q.pop_front();
        at_ = 0;
        return (int)cur_.bytes.size();
    }
    int available() { return have_ ? (int)(cur_.bytes.size() - at_) : 0; }
    int read(uint8_t* b, size_t n) {
        size_t k = 0;
        while (have_ && k < n && at_ < cur_.bytes.size()) b[k++] = cur_.bytes[at_++];
        return (int)k;
    }
    int read() { return have_ && at_ < cur_.bytes.size() ? cur_.bytes[at_++] : -1; }
    IPAddress remoteIP() { return IPAddress(cur_.ip); }
    uint16_t remotePort() { return cur_.port; }
    int beginPacket(IPAddress ip, uint16_t port) {
        out_ = Datagram{};
        out_.local = local_;
        out_.ip = ip;
        out_.port = port;
        return port != 0 ? 1 : 0;
    }
    size_t write(const uint8_t* b, size_t n) { out_.bytes.insert(out_.bytes.end(), b, b + n); return n; }
    size_t write(uint8_t b) { out_.bytes.push_back(b); return 1; }
    int endPacket() { udp.sent.push_back(out_); return 1; }
    void flush() {}
private:
    uint16_t local_ = 0;
    Datagram cur_;
    size_t at_ = 0;
    bool have_ = false;
    Datagram out_;
};

// The panel's web server, as "reboot panel" sees it through WiFiClient.
struct HttpModel {
    bool accept = true;          // the connection opens
    std::string answer;          // what the panel sends back
    bool closeAfter = true;      // and then it closes the connection
    std::string got;             // what FlowLocal sent
    uint32_t ip = 0;
    uint16_t port = 0;
    int32_t timeout = 0;
    int connects = 0;
    size_t readAt = 0;
};
static HttpModel http;
class WiFiClient {
public:
    int connect(IPAddress ip, uint16_t port, int32_t timeoutMs) {
        ++http.connects;
        http.ip = ip;
        http.port = port;
        http.timeout = timeoutMs;
        if (!http.accept) {
            g_now += (uint32_t)timeoutMs;
            return 0;
        }
        open_ = true;
        http.readAt = 0;
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) {
        if (!open_) return 0;
        http.got.append(reinterpret_cast<const char*>(b), n);
        return n;
    }
    int available() { return open_ ? (int)(http.answer.size() - http.readAt) : 0; }
    int read() { return open_ && http.readAt < http.answer.size() ? (uint8_t)http.answer[http.readAt++] : -1; }
    uint8_t connected() { return open_ && (http.readAt < http.answer.size() || !http.closeAfter); }
    void stop() { open_ = false; }
private:
    bool open_ = false;
};

// ── ESP-IDF: timer, heap, the soft AP's station list (pollHotspot) ─────────
inline int64_t esp_timer_get_time() { return (int64_t)g_now * 1000; }
#define MALLOC_CAP_INTERNAL (1u << 11)
static size_t g_freeInternal = 181234;
inline size_t heap_caps_get_free_size(uint32_t) { return g_freeInternal; }

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_WIFI_NOT_INIT 0x3001

// ── ESP-IDF: reset reason, task watchdog, RTC memory, spinlocks ───────────
typedef enum {
    ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO, ESP_RST_USB,
    ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;
static esp_reset_reason_t g_resetReason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return g_resetReason; }
#define CONFIG_ESP_TASK_WDT_PANIC 1               // as in this core's sdkconfig
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
static int g_wdtAdds = 0;
static int g_wdtFeeds = 0;
static bool g_wdtAddFails = false;
inline esp_err_t esp_task_wdt_add(TaskHandle_t) {
    ++g_wdtAdds;
    return g_wdtAddFails ? 0x103 : ESP_OK;
}
inline esp_err_t esp_task_wdt_reset() {
    ++g_wdtFeeds;
    return ESP_OK;
}
#define RTC_NOINIT_ATTR                           // a plain static here: zero, i.e. garbage to the record
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define ESP_WIFI_MAX_CONN_NUM 15
typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
static uint8_t g_channel = 1;   // the AP's channel as the driver reports it
inline esp_err_t esp_wifi_get_channel(uint8_t* primary, wifi_second_chan_t* second) {
    if (!g_apStarted) return ESP_ERR_WIFI_NOT_INIT;
    *primary = g_channel;
    *second = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}
typedef struct { uint8_t mac[6]; int8_t rssi; } wifi_sta_info_t;
typedef struct { wifi_sta_info_t sta[ESP_WIFI_MAX_CONN_NUM]; int num; } wifi_sta_list_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint8_t mac[6]; esp_ip4_addr_t ip; } esp_netif_pair_mac_ip_t;
typedef struct { esp_netif_pair_mac_ip_t sta[ESP_WIFI_MAX_CONN_NUM]; int num; } wifi_sta_mac_ip_list_t;

struct Station { uint8_t mac[6]; int8_t rssi; uint32_t ip; };   // ip 0 = no lease yet
struct WifiModel {
    std::vector<Station> stations;
};
static WifiModel wifi;
inline esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t* list) {
    if (!g_apStarted) return ESP_ERR_WIFI_NOT_INIT;
    list->num = 0;
    for (const Station& s : wifi.stations) {
        std::memcpy(list->sta[list->num].mac, s.mac, 6);
        list->sta[list->num].rssi = s.rssi;
        ++list->num;
    }
    return ESP_OK;
}
// As in ESP-IDF: the MACs of the list, each with its DHCP lease (0 = none).
// Listed in reverse here, so a lookup that assumed the same order would fail.
inline esp_err_t esp_wifi_ap_get_sta_list_with_ip(const wifi_sta_list_t* list, wifi_sta_mac_ip_list_t* out) {
    out->num = list->num;
    for (int i = 0; i < list->num; ++i) {
        const wifi_sta_info_t& in = list->sta[list->num - 1 - i];
        std::memcpy(out->sta[i].mac, in.mac, 6);
        out->sta[i].ip.addr = 0;
        for (const Station& s : wifi.stations)
            if (std::memcmp(s.mac, in.mac, 6) == 0) out->sta[i].ip.addr = s.ip;
    }
    return ESP_OK;
}

// ── Preferences (NVS) ──────────────────────────────────────────────────────
// As Arduino-ESP32's: a read-only begin() fails while the namespace has never
// been written; putUChar() returns 1 once committed, 0 on failure.
struct NvsModel {
    std::map<std::string, std::map<std::string, uint8_t>> u8;   // namespace -> key -> value
    bool failWrites = false;
    int writes = 0;
};
static NvsModel nvs;
class Preferences {
public:
    bool begin(const char* name, bool readOnly = false, const char* = nullptr) {
        if (started_) return false;
        if (readOnly && nvs.u8.find(name) == nvs.u8.end()) return false;
        ns_ = name;
        readOnly_ = readOnly;
        started_ = true;
        return true;
    }
    void end() { started_ = false; }
    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) {
        if (!started_) return defaultValue;
        const auto n = nvs.u8.find(ns_);
        if (n == nvs.u8.end()) return defaultValue;
        const auto k = n->second.find(key);
        return k == n->second.end() ? defaultValue : k->second;
    }
    size_t putUChar(const char* key, uint8_t value) {
        if (!started_ || readOnly_ || nvs.failWrites) return 0;
        nvs.u8[ns_][key] = value;
        ++nvs.writes;
        return 1;
    }
    bool isKey(const char* key) {
        const auto n = nvs.u8.find(ns_);
        return started_ && n != nvs.u8.end() && n->second.count(key) != 0;
    }
private:
    std::string ns_;
    bool readOnly_ = false;
    bool started_ = false;
};
