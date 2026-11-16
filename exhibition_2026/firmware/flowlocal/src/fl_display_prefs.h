#pragma once
// Display field toggles in NVS (shared by /display web UI and TFT).

#include <Preferences.h>
#include <ArduinoJson.h>
#include "../flowlocal_config.h"

namespace FlDisplayPrefs {

struct Flags {
    bool ssid = true;
    bool ip = true;
    bool sta = true;
    bool clients = true;
    bool usb = true;
    bool deck = true;
    bool mqtt = true;
    bool ntp = false;
    bool heap = false;
    bool weather = true;
};

inline Flags flags;
inline uint32_t rev = 0;

inline void load() {
    Preferences p;
    if (!p.begin(FL_NVS_NS, true)) return;
    flags.ssid = p.getBool("d_ssid", true);
    flags.ip = p.getBool("d_ip", true);
    flags.sta = p.getBool("d_sta", true);
    flags.clients = p.getBool("d_cli", true);
    flags.usb = p.getBool("d_usb", true);
    flags.deck = p.getBool("d_deck", true);
    flags.mqtt = p.getBool("d_mqtt", true);
    flags.ntp = p.getBool("d_ntp", false);
    flags.heap = p.getBool("d_heap", false);
    flags.weather = p.getBool("d_wx", true);
    p.end();
}

inline void save() {
    Preferences p;
    if (!p.begin(FL_NVS_NS, false)) return;
    p.putBool("d_ssid", flags.ssid);
    p.putBool("d_ip", flags.ip);
    p.putBool("d_sta", flags.sta);
    p.putBool("d_cli", flags.clients);
    p.putBool("d_usb", flags.usb);
    p.putBool("d_deck", flags.deck);
    p.putBool("d_mqtt", flags.mqtt);
    p.putBool("d_ntp", flags.ntp);
    p.putBool("d_heap", flags.heap);
    p.putBool("d_wx", flags.weather);
    p.end();
    rev++;
}

inline void toJson(JsonDocument& doc) {
    doc["show_ssid"] = flags.ssid;
    doc["show_ip"] = flags.ip;
    doc["show_sta"] = flags.sta;
    doc["show_clients"] = flags.clients;
    doc["show_usb"] = flags.usb;
    doc["show_deck"] = flags.deck;
    doc["show_mqtt"] = flags.mqtt;
    doc["show_ntp"] = flags.ntp;
    doc["show_heap"] = flags.heap;
    doc["show_weather"] = flags.weather;
}

inline void fromJson(JsonDocument& in) {
    if (in["show_ssid"].is<bool>()) flags.ssid = in["show_ssid"];
    if (in["show_ip"].is<bool>()) flags.ip = in["show_ip"];
    if (in["show_sta"].is<bool>()) flags.sta = in["show_sta"];
    if (in["show_clients"].is<bool>()) flags.clients = in["show_clients"];
    if (in["show_usb"].is<bool>()) flags.usb = in["show_usb"];
    if (in["show_deck"].is<bool>()) flags.deck = in["show_deck"];
    if (in["show_mqtt"].is<bool>()) flags.mqtt = in["show_mqtt"];
    if (in["show_ntp"].is<bool>()) flags.ntp = in["show_ntp"];
    if (in["show_heap"].is<bool>()) flags.heap = in["show_heap"];
    if (in["show_weather"].is<bool>()) flags.weather = in["show_weather"];
    save();
}

}  // namespace FlDisplayPrefs
