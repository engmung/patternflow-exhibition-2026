// Host stand-in for <WiFiUdp.h>: datagrams go into, and come out of, two
// queues the test owns.
#pragma once
#include <deque>
#include <vector>
#include "Arduino.h"

namespace stub {
struct Datagram {
  IPAddress ip;
  uint16_t port = 0;
  std::vector<uint8_t> data;
};
inline std::deque<Datagram> sent;      // what the panel put on the air
inline std::deque<Datagram> inbox;     // what is waiting for the panel
inline int boundPort = 0;
}  // namespace stub

class WiFiUDP {
 public:
  uint8_t begin(uint16_t port) { stub::boundPort = port; return 1; }
  void stop() { stub::boundPort = 0; }
  int parsePacket() {
    if (stub::inbox.empty()) return 0;
    cur_ = stub::inbox.front();
    stub::inbox.pop_front();
    return (int)cur_.data.size();
  }
  int read(uint8_t* buf, size_t len) {
    const size_t n = cur_.data.size() < len ? cur_.data.size() : len;
    memcpy(buf, cur_.data.data(), n);
    return (int)n;
  }
  IPAddress remoteIP() { return cur_.ip; }
  uint16_t remotePort() { return cur_.port; }
  int beginPacket(IPAddress ip, uint16_t port) { out_ = stub::Datagram(); out_.ip = ip; out_.port = port; return 1; }
  size_t write(const uint8_t* buf, size_t len) { out_.data.insert(out_.data.end(), buf, buf + len); return len; }
  int endPacket() { stub::sent.push_back(out_); return 1; }
 private:
  stub::Datagram cur_, out_;
};
