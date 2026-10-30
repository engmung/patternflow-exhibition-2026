#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — Wi-Fi manager
//
// AP for panels/Director + optional upstream STA for NTP/weather.
// SSID/pass/IP are NVS-backed so multi-island venues can change them.
// ═══════════════════════════════════════════════════════════

#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "../flowlocal_config.h"
#include "fl_ntp.h"

#if FL_EXHIBIT
#include "esp_wifi.h"
#include "fl_ap_channel.h"
#include "fl_channel_store.h"

// The exhibition hotspot must never come up with the touring kit's public
// password, nor with an unusable one. Checked by the compiler, so a wrong
// secrets file fails the build instead of shipping.
namespace FlWifiCheck {
constexpr bool same(const char* a, const char* b) {
    return *a == *b && (*a == '\0' || same(a + 1, b + 1));
}
constexpr unsigned len(const char* s) { return *s ? 1 + len(s + 1) : 0; }
}  // namespace FlWifiCheck
static_assert(!FlWifiCheck::same(FL_AP_PASS, "patternflow"),
              "exhibit build: FL_AP_PASS is still the public default");
static_assert(FlWifiCheck::len(FL_AP_PASS) >= 8 && FlWifiCheck::len(FL_AP_PASS) <= 63,
              "exhibit build: WPA2 password must be 8..63 characters");
static_assert(FL_EXHIBIT_AP_CHANNEL >= 0 && FL_EXHIBIT_AP_CHANNEL <= 13,
              "exhibit build: FL_EXHIBIT_AP_CHANNEL must be 0 (auto) or a channel 1..13");
static_assert(FL_AP_CHANNEL == 1 || FL_AP_CHANNEL == 6 || FL_AP_CHANNEL == 11,
              "exhibit build: FL_AP_CHANNEL (the auto channel's fallback) must be 1, 6 or 11");
#endif

namespace FlWifi {

inline String apSsid;
inline String apPass;
inline String apIpStr;
inline String apGwStr;
inline String apSnStr;
inline bool   apEnabled = true;
inline String staSsid;
inline String staPass;
inline bool   staEnabled = false;
inline bool   staConnected = false;
inline bool   staEverUp = false;
// The channel the AP was started on and how it was picked. Written in
// planChannel() / begin() before the web server and loop() run, read-only
// afterwards.
inline uint8_t apChannel = FL_AP_CHANNEL;
#if FL_EXHIBIT
inline FlApChannel::Mode channelMode =
    FL_EXHIBIT_AP_CHANNEL != 0 ? FlApChannel::Mode::Fixed : FlApChannel::Mode::Auto;
inline uint8_t  channelStored = 0;        // the PF monitor's command 13 choice in NVS: 0 automatic, 1..13
inline bool     channelPlanned = false;
inline bool     channelScanned = false;   // the boot scan finished (auto: its result was used)
inline FlApChannel::Scores channelScores;
inline uint32_t channelScanMs = 0;
#endif

static void loadSettings() {
#if FL_EXHIBIT
    // Exhibition: the compiled network is the only network. Whatever an
    // earlier firmware or the regular dashboard saved in NVS (another
    // password, AP off, another IP, an upstream router) is ignored, and this
    // build never writes those keys back (see setAp / setSta).
    apSsid     = FL_AP_SSID;
    apPass     = FL_AP_PASS;
    apEnabled  = true;
    apIpStr    = FL_AP_IP;
    apGwStr    = FL_AP_GATEWAY;
    apSnStr    = FL_AP_SUBNET;
    staSsid    = "";
    staPass    = "";
    staEnabled = false;
#else
    Preferences p;
    p.begin(FL_NVS_NS, /*readOnly=*/true);
    apSsid    = p.getString("ap_ssid", FL_AP_SSID);
    apPass    = p.getString("ap_pass",  FL_AP_PASS);
    apEnabled = p.getBool  ("ap_on",    true);
    apIpStr   = p.getString("ap_ip",    FL_AP_IP);
    apGwStr   = p.getString("ap_gw",    FL_AP_GATEWAY);
    apSnStr   = p.getString("ap_sn",    FL_AP_SUBNET);
    staSsid   = p.getString("sta_ssid", "");
    staPass   = p.getString("sta_pass", "");
    staEnabled = p.getBool("sta_on", false);
    p.end();

    if (apIpStr.length() == 0) apIpStr = FL_AP_IP;
    if (apGwStr.length() == 0) apGwStr = FL_AP_GATEWAY;
    if (apSnStr.length() == 0) apSnStr = FL_AP_SUBNET;

#ifdef FL_STA_SSID
    if (staSsid.length() == 0) {
        staSsid = FL_STA_SSID;
#ifdef FL_STA_PASS
        staPass = FL_STA_PASS;
#endif
        staEnabled = true;
    }
#endif
#endif  // FL_EXHIBIT
}

static void saveSettings() {
    Preferences p;
    p.begin(FL_NVS_NS, /*readOnly=*/false);
    p.putString("ap_ssid", apSsid);
    p.putString("ap_pass",  apPass);
    p.putBool  ("ap_on",    apEnabled);
    p.putString("ap_ip",    apIpStr);
    p.putString("ap_gw",    apGwStr);
    p.putString("ap_sn",    apSnStr);
    p.putString("sta_ssid", staSsid);
    p.putString("sta_pass", staPass);
    p.putBool  ("sta_on",   staEnabled);
    p.end();
}

static bool applyApConfig() {
    IPAddress ip, gw, sn;
    if (!ip.fromString(apIpStr) || !gw.fromString(apGwStr) || !sn.fromString(apSnStr)) {
        return false;
    }
    WiFi.softAPConfig(ip, gw, sn);
    return true;
}

static void beginSta() {
    if (!staEnabled || staSsid.length() == 0) {
        WiFi.disconnect(false, false);
        return;
    }
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    Serial.printf("[FlWifi] STA connecting to \"%s\"…\n", staSsid.c_str());
}

#if FL_EXHIBIT
#  ifndef FL_EXHIBIT_AP_START_DELAY_MS
#    define FL_EXHIBIT_AP_START_DELAY_MS 12000
#  endif
#  ifndef FL_EXHIBIT_CHANNEL_SCAN_MS
#    define FL_EXHIBIT_CHANNEL_SCAN_MS 3000        // the boot scan's deadline
#  endif
#  ifndef FL_EXHIBIT_CHANNEL_SCAN_DWELL_MS
#    define FL_EXHIBIT_CHANNEL_SCAN_DWELL_MS 150   // active scan, longest per channel
#  endif

// This boot's channel plan (fl_ap_channel.h, Mode): FL_EXHIBIT_AP_CHANNEL
// when the build sets one; else the channel the laptop's PF monitor stored
// with command 13 (fl_channel_store.h); else the scan. Read once per boot,
// from setup() before begin() so the bridge's status reply says it through
// the boot silence (begin() calls it too).
inline FlApChannel::Mode planChannel() {
    if (channelPlanned) return channelMode;
    channelPlanned = true;
    channelStored = FlChannelStore::load();
    channelMode = FlApChannel::modeFor(FL_EXHIBIT_AP_CHANNEL, channelStored);
    return channelMode;
}

// The boot scan (auto and manual; a fixed channel skips it): one active scan
// of all channels, hidden networks included, in STA mode while the AP is
// still silent (a scan needs the station interface; it sends probe requests
// from the station's MAC and no beacon from the AP's). The networks it hears
// are scored for 1 / 6 / 11 by fl_ap_channel.h. A scan that fails or has not
// finished by FL_EXHIBIT_CHANNEL_SCAN_MS is stopped, so the AP never starts
// later than that deadline allows. Returns the channel the scores pick, or
// FL_AP_CHANNEL without scores; channelScanned says which.
inline uint8_t pickChannel() {
    const uint32_t start = millis();
    int16_t n = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true, /*passive=*/false,
                                  FL_EXHIBIT_CHANNEL_SCAN_DWELL_MS);
    while (n == WIFI_SCAN_RUNNING && millis() - start < FL_EXHIBIT_CHANNEL_SCAN_MS) {
        delay(20);
        n = WiFi.scanComplete();
    }
    channelScanMs = millis() - start;
    if (n < 0) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        if (channelMode == FlApChannel::Mode::Manual)
            Serial.printf("[FlWifi] channel scan %s after %u ms: ch %u (manual, from the PF monitor)\n",
                          n == WIFI_SCAN_RUNNING ? "unfinished" : "failed", (unsigned)channelScanMs,
                          (unsigned)channelStored);
        else
            Serial.printf("[FlWifi] channel scan %s after %u ms: ch %d (fallback)\n",
                          n == WIFI_SCAN_RUNNING ? "unfinished" : "failed", (unsigned)channelScanMs,
                          FL_AP_CHANNEL);
        return FL_AP_CHANNEL;
    }

    FlApChannel::Scores scores;
    uint8_t heard[15] = {};     // networks per primary channel
    int8_t  loudest[15] = {};   // strongest RSSI per primary channel
    for (int i = 0; i < n; ++i) {
        const wifi_ap_record_t* r = static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
        if (r == nullptr) continue;
        FlApChannel::Network net;
        net.primary = r->primary;
        net.second = (uint8_t)r->second;
        net.rssi = r->rssi;
        FlApChannel::add(scores, net);
        if (r->primary >= 1 && r->primary <= 14) {
            if (heard[r->primary] == 0 || r->rssi > loudest[r->primary]) loudest[r->primary] = r->rssi;
            if (heard[r->primary] < 255) ++heard[r->primary];
        }
    }
    WiFi.scanDelete();
    channelScores = scores;
    channelScanned = true;
    const uint8_t ch = FlApChannel::choose(scores, FL_AP_CHANNEL);

    char seen[15 * 16] = "";   // "ch9 x3 -45dBm, " per channel heard
    size_t at = 0;
    for (int c = 1; c <= 14 && at < sizeof seen; ++c) {
        if (heard[c] == 0) continue;
        const int w = snprintf(seen + at, sizeof seen - at, "%sch%d x%u %ddBm", at ? ", " : "", c,
                               (unsigned)heard[c], (int)loudest[c]);
        if (w < 0) break;
        at += (size_t)w;
    }
    Serial.printf("[FlWifi] channel scan: %u networks in %u ms (%s)\n", (unsigned)scores.networks,
                  (unsigned)channelScanMs, at ? seen : "none");
    if (channelMode == FlApChannel::Mode::Manual)
        Serial.printf("[FlWifi] channel scores ch1 %lu, ch6 %lu, ch11 %lu -> ch %u (manual, from the PF monitor; "
                      "the scan would pick %u)\n",
                      (unsigned long)scores.score[0], (unsigned long)scores.score[1],
                      (unsigned long)scores.score[2], (unsigned)channelStored, (unsigned)ch);
    else
        Serial.printf("[FlWifi] channel scores ch1 %lu, ch6 %lu, ch11 %lu -> ch %u (auto)\n",
                      (unsigned long)scores.score[0], (unsigned long)scores.score[1],
                      (unsigned long)scores.score[2], (unsigned)ch);
    return ch;
}
#endif  // FL_EXHIBIT

inline void begin() {
    loadSettings();
#if FL_EXHIBIT
    // Silence first. A panel still associated with the previous boot of this
    // AP does not notice a quick restart: the same SSID and BSSID beacon again
    // within a second, so its beacon-loss check (about 8 s) never fires, and
    // with protected management frames it ignores the new AP's deauth. It then
    // keeps a dead link and its MIDI stays down. Measured 2026-09-19 with the
    // exhibit panel: FlowLocal reset and back in 0.9 s left the panel's MIDI
    // dead for over two minutes; held off for 15 s, the panel dropped the link
    // at 8.5 s and had its session back 1 s after the AP returned. So on every
    // boot the radio stays off long enough for any old station to let go. It
    // has to happen before WiFi.mode(): that call already starts the AP.
    //
    // The channel's scan runs in the last FL_EXHIBIT_CHANNEL_SCAN_MS of that
    // silence (the freshest picture of the hall), for the automatic channel
    // and, for its scores only, for one the PF monitor chose; the AP starts
    // when the silence is over, or when the scan has given up if the silence
    // is shorter than the scan. A channel chosen from the laptop (command 13)
    // takes effect here, after this same silence: the restart that stores it
    // leaves the panel associated with the old channel, and it has to notice
    // the AP is gone before it looks for it again.
    const uint32_t silenceStart = millis();
    const uint32_t silenceMs = FL_EXHIBIT_AP_START_DELAY_MS;
    if (silenceMs > 0)
        Serial.printf("[FlWifi] AP starts in %u s so a panel from before this boot lets go\n",
                      (unsigned)(silenceMs / 1000));
    planChannel();
    if (channelMode == FlApChannel::Mode::Fixed) {
        apChannel = FL_EXHIBIT_AP_CHANNEL;
        if (channelStored != 0)
            Serial.printf("[FlWifi] channel %d (fixed by FL_EXHIBIT_AP_CHANNEL; the PF monitor's ch %u is not used)\n",
                          FL_EXHIBIT_AP_CHANNEL, (unsigned)channelStored);
        else
            Serial.printf("[FlWifi] channel %d (fixed by FL_EXHIBIT_AP_CHANNEL)\n", FL_EXHIBIT_AP_CHANNEL);
    } else {
        if (channelMode == FlApChannel::Mode::Manual)
            Serial.printf("[FlWifi] channel %u chosen from the laptop (PF monitor command 13, NVS)\n",
                          (unsigned)channelStored);
        const uint32_t scanAt = silenceMs > FL_EXHIBIT_CHANNEL_SCAN_MS ? silenceMs - FL_EXHIBIT_CHANNEL_SCAN_MS : 0;
        const uint32_t waited = millis() - silenceStart;
        if (waited < scanAt) delay(scanAt - waited);
        const uint8_t scanned = pickChannel();
        apChannel = channelMode == FlApChannel::Mode::Manual ? channelStored : scanned;
    }
    {
        const uint32_t waited = millis() - silenceStart;
        if (waited < silenceMs) delay(silenceMs - waited);
    }

    // AP only: no upstream router, no NTP.
    //
    // The Wi-Fi driver keeps its own copy of the AP config (nvs.net80211) and
    // starts the AP with it inside WiFi.mode(), before softAP() below can
    // apply ours. WiFi.persistent(false) would not stop that (the stored
    // config is loaded before storage switches to RAM); it would only stop
    // ours from being saved, so every boot would first beacon whatever was
    // there: the regular build's "patternflow" password after a browser
    // update from it, or the driver's open "ESP_xxxxxx" network on an erased
    // board. So the config is left persistent: the first boot saves the
    // exhibit network, and later boots start the AP with it directly
    // (softAP() finds nothing to change and does not restart the AP). When
    // the automatic channel differs from the stored one, softAP() moves the AP
    // there within milliseconds of its start, before any station can join.
    WiFi.mode(WIFI_AP);
#else
    FlNtp::begin();

    WiFi.mode(WIFI_AP_STA);
#endif
    applyApConfig();

    if (apEnabled) {
        bool up = WiFi.softAP(apSsid.c_str(), apPass.c_str(), apChannel,
                              /*ssid_hidden=*/0, FL_AP_MAX_CONN);
        Serial.printf("[FlWifi] AP \"%s\" on %s ch%d, max %d stations%s\n",
                      apSsid.c_str(), WiFi.softAPIP().toString().c_str(), apChannel,
                      FL_AP_MAX_CONN, up ? "" : "  (softAP FAILED)");
#if FL_EXHIBIT
        // A channel stored from the laptop that the driver refuses (outside
        // the country's channels, say) must not keep the hotspot down on
        // every boot: this boot runs on the automatic choice instead, and
        // says so (chm=auto). The laptop's command 13 can still store another.
        if (!up && channelMode == FlApChannel::Mode::Manual) {
            apChannel = channelScanned ? FlApChannel::choose(channelScores, FL_AP_CHANNEL) : (uint8_t)FL_AP_CHANNEL;
            channelMode = FlApChannel::Mode::Auto;
            up = WiFi.softAP(apSsid.c_str(), apPass.c_str(), apChannel, /*ssid_hidden=*/0, FL_AP_MAX_CONN);
            Serial.printf("[FlWifi] AP on the PF monitor's ch %u FAILED: ch%d (auto) instead%s\n",
                          (unsigned)channelStored, apChannel, up ? "" : "  (softAP FAILED)");
        }
#endif
#if FL_EXHIBIT
        // 20 MHz: 1, 6 and 11 overlap nothing but their neighbours only at
        // that width, and a MIDI link needs no more. The driver stores the
        // width, so later boots start with it.
        wifi_bandwidth_t bw = WIFI_BW_HT20;
        if (esp_wifi_get_bandwidth(WIFI_IF_AP, &bw) == ESP_OK && bw != WIFI_BW_HT20) {
            const esp_err_t err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
            Serial.printf("[FlWifi] AP width 40 -> 20 MHz%s\n", err == ESP_OK ? "" : "  (FAILED)");
        }
        // The driver forgets this on every start, so it is set on every boot.
        const esp_err_t idle = esp_wifi_set_inactive_time(WIFI_IF_AP, FL_EXHIBIT_AP_INACTIVE_S);
        Serial.printf("[FlWifi] AP drops a station after %d s of silence%s\n",
                      FL_EXHIBIT_AP_INACTIVE_S, idle == ESP_OK ? "" : "  (FAILED)");
#endif
    } else {
        WiFi.softAPdisconnect(true);
        Serial.println("[FlWifi] AP disabled by settings");
    }

#if !FL_EXHIBIT
    beginSta();
#endif

    if (MDNS.begin(FL_HOSTNAME)) {
        MDNS.addService("http", "tcp", FL_WEB_PORT);
        Serial.printf("[FlWifi] mDNS: http://%s.local\n", FL_HOSTNAME);
    }
}

inline void tick() {
#if FL_EXHIBIT
    return;  // no STA in the exhibit build
#endif
    bool now = (WiFi.status() == WL_CONNECTED) && staEnabled && staSsid.length() > 0;
    if (now && !staConnected) {
        staConnected = true;
        staEverUp = true;
        Serial.printf("[FlWifi] STA up, IP %s\n", WiFi.localIP().toString().c_str());
        FlNtp::startSync();
    } else if (!now && staConnected) {
        staConnected = false;
        FlNtp::stop();
        Serial.println("[FlWifi] STA lost");
        if (staEnabled && staSsid.length() > 0) {
            Serial.println("[FlWifi] STA reconnecting…");
            WiFi.begin(staSsid.c_str(), staPass.c_str());
        }
    }
    if (staConnected) FlNtp::tick();
}

// ip/gw/sn optional — empty keeps current NVS values.
inline bool setAp(const String& ssid, const String& pass, bool enable,
                  const String& ip = "", const String& gw = "", const String& sn = "") {
#if FL_EXHIBIT
    // Fixed in firmware: a visitor (or a stale browser tab) must not be able
    // to turn the hotspot off or change its password, now or after a reboot.
    return false;
#endif
    if (enable && pass.length() > 0 && pass.length() < 8) return false;
    if (ip.length()) apIpStr = ip;
    if (gw.length()) apGwStr = gw;
    else if (ip.length()) apGwStr = ip;  // gateway usually = AP IP
    if (sn.length()) apSnStr = sn;
    apSsid    = ssid;
    apPass    = pass;
    apEnabled = enable;
    if (!applyApConfig()) return false;
    saveSettings();

    if (enable && ssid.length() > 0) {
        WiFi.softAP(apSsid.c_str(), apPass.c_str(), FL_AP_CHANNEL, /*ssid_hidden=*/0,
                    FL_AP_MAX_CONN);
        Serial.printf("[FlWifi] AP updated → \"%s\" %s\n",
                      apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.softAPdisconnect(true);
        Serial.println("[FlWifi] AP disabled");
    }
    return true;
}

inline bool setSta(const String& ssid, const String& pass, bool enable) {
#if FL_EXHIBIT
    return false;  // no upstream Wi-Fi in the exhibit build
#endif
    staSsid = ssid;
    staPass = pass;
    staEnabled = enable && ssid.length() > 0;
    saveSettings();
    staConnected = false;
    FlNtp::stop();
    if (staEnabled) beginSta();
    else WiFi.disconnect(false, false);
    return true;
}

inline String apIp()     { return WiFi.softAPIP().toString(); }
inline String apIpCfg()  { return apIpStr; }
inline int    apClients(){ return WiFi.softAPgetStationNum(); }
inline String staIp()    { return staConnected ? WiFi.localIP().toString() : ""; }
inline bool   isStaUp()  { return staConnected; }

}  // namespace FlWifi
