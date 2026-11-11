// Host stand-in for <WiFi.h>.
#pragma once
#include "Arduino.h"

enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

namespace stub {
inline int wifiStatus = WL_CONNECTED;
inline uint8_t mac[6] = {0x24, 0x6f, 0x28, 0x01, 0x02, 0x03};
inline int rssi = -51;
}  // namespace stub

struct WiFiStub {
  int status() { return stub::wifiStatus; }
  uint8_t* macAddress(uint8_t* out) { memcpy(out, stub::mac, 6); return out; }
  int RSSI() { return stub::rssi; }
};
inline WiFiStub WiFi;
