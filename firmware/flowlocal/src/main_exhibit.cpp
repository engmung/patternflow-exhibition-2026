// FlowLocal — PlatformIO entry point for env:exhibit (Wi-Fi AP + RTP-MIDI →
// USB-MIDI bridge). env:exhibit builds this file instead of main.cpp, and the
// regular envs leave it out (build_src_filter in platformio.ini).
//
// The includes below are what the library dependency finder sees: it does not
// follow flowlocal.ino into src/, and it does not evaluate #if, so a shared
// main.cpp would drag SD / SPI / LittleFS / PicoMQTT into this build (and
// lib_ignore for them makes pioarduino rewrite the shared framework build
// script, which a failed build leaves rewritten for every other env).
// AppleMIDI is not included here: src/fl_midi_bridge.h must include it first,
// after its USE_EXT_CALLBACKS define (lib_deps names it for the finder).

#if !defined(FL_EXHIBIT) || !FL_EXHIBIT
#  error "main_exhibit.cpp is the env:exhibit entry point (build with -DFL_EXHIBIT=1)"
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <USB.h>
#include <USBMIDI.h>     // framework library "USB" (Arduino-ESP32 libraries/USB)
#include "../flowlocal.ino"
