// Host stand-in for the Arduino core: only what features/ensemble touches.
// The tests script the clock and read back what the feature printed.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace stub {
inline uint32_t nowMs = 1000;
inline std::string serial;
inline int restarts = 0;
}  // namespace stub

inline uint32_t millis() { return stub::nowMs; }

struct SerialStub {
  void printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    stub::serial += buf;
  }
  void println(const char* s) { stub::serial += s; stub::serial += "\n"; }
};
inline SerialStub Serial;

struct EspStub {
  void restart() { stub::restarts++; }
};
inline EspStub ESP;

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String& operator+=(const char* s) { s_ += s; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(int v) { s_ += std::to_string(v); return *this; }
  String& operator+=(unsigned int v) { s_ += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  const char* c_str() const { return s_.c_str(); }
 private:
  std::string s_;
};

class IPAddress {
 public:
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { v_[0] = a; v_[1] = b; v_[2] = c; v_[3] = d; }
  IPAddress(uint32_t raw) { memcpy(v_, &raw, 4); }
  bool fromString(const char* s) {
    unsigned a, b, c, d;
    char tail;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
    *this = IPAddress((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
    return true;
  }
  String toString() const {
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", v_[0], v_[1], v_[2], v_[3]);
    return String(b);
  }
  operator uint32_t() const { uint32_t raw; memcpy(&raw, v_, 4); return raw; }
  bool operator==(const IPAddress& o) const { return memcmp(v_, o.v_, 4) == 0; }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }
  uint8_t operator[](int i) const { return v_[i]; }
 private:
  uint8_t v_[4] = {0, 0, 0, 0};
};
