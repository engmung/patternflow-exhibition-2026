// ═══════════════════════════════════════════════════════════
// FlowLocal — standalone Patternflow show appliance
//
// Hardware: ESP32-S3-DevKitC-1 N16R8 (16 MB flash / 8 MB OPI PSRAM)
// Role    : Wi-Fi AP  +  embedded MQTT broker  +  web console
//           optional ST7735 status TFT (USB build); weather hub on STA
//
// Exhibit build (env:exhibit, FL_EXHIBIT=1): Wi-Fi AP + RTP-MIDI → USB-MIDI
// bridge + a status console, nothing else. See EXHIBIT.md.
//
// License : MIT
// ═══════════════════════════════════════════════════════════

#include "flowlocal_config.h"

#if FL_EXHIBIT

#include "src/fl_midi_bridge.h"
#include "src/fl_wifi.h"
#include "src/fl_web_exhibit.h"

void setup() {
    Serial.begin(115200);   // UART0 = the COM jack
    // Why this boot happened (rsn= / bc= / why=), before anything reports it.
    FlMidiBridge::bootBegin();
    // USB first, so the laptop enumerates the MIDI port while Wi-Fi comes up;
    // the bridge task starts with it and answers the PF monitor meanwhile.
    FlMidiBridge::beginUsb();
    delay(300);
    Serial.printf("\n\n[FlowLocal] v%s exhibit  (ESP32-S3, %u kB heap)\n",
                  FL_VERSION, (unsigned)(ESP.getFreeHeap() / 1024));
    FlMidiBridge::logBoot();

    // How this boot's channel is chosen (FL_EXHIBIT_AP_CHANNEL, else the PF
    // monitor's command 13 choice in NVS, else the scan), so the status reply
    // says chm= right through the boot silence.
    FlMidiBridge::publishChannelMode(FlWifi::planChannel());
    FlWifi::begin();              // boot silence, channel scan, then the AP
    FlMidiBridge::publishChannelMode(FlWifi::channelMode);   // auto if a manual channel failed
    FlMidiBridge::publishChannelScan(FlWifi::channelScanned, FlWifi::channelScores);   // cs=
    FlMidiBridge::pollHotspot();  // ap= / ch= right away, not at the first loop()
    FlMidiBridge::begin();
    FlWeb::begin();

    Serial.println("[FlowLocal] exhibit ready.");
}

void loop() {
    // Everything time-critical runs in the fl-midi task and the AsyncTCP
    // task (no STA, no broker, no media here). This checks that the bridge
    // task still passes (it restarts FlowLocal when it has not for 3 s; loop()
    // starts after setup(), so the boot silence never counts), and refreshes
    // the hotspot facts for the PF monitor's status reply (AP up, channel,
    // stations, the panel's RSSI), which the bridge task does not fetch
    // itself (Wi-Fi driver and DHCP server calls).
    FlMidiBridge::watchBridge();
    FlMidiBridge::pollHotspot();
    delay(500);
}

#else  // regular FlowLocal

#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "src/fl_wifi.h"
#include "src/fl_store.h"
#include "src/fl_broker.h"
#include "src/fl_weather.h"
#include "src/fl_display.h"
#include "src/fl_web.h"

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n\n[FlowLocal] v%s  (ESP32-S3, %u kB heap)\n",
                  FL_VERSION, ESP.getFreeHeap() / 1024);

    FlWifi::begin();
    delay(200);
    FlStore::begin();
    // Give USB OTG a few seconds to enumerate before Wi‑Fi STA / TFT SPI
    // steal cycles (regression after display was added).
    for (int i = 0; i < 40; ++i) {
        FlStore::tick();
        delay(50);
    }
    FlBroker::begin();
    FlWeather::load();
    FlDisplay::begin();
    FlWeb::begin();

    Serial.println("[FlowLocal] ready.");
}

void loop() {
    FlWifi::tick();
    FlStore::tick();
    FlBroker::tick();
    FlWeather::tick();
    FlDisplay::tick();
    delay(10);
}

#endif  // FL_EXHIBIT
