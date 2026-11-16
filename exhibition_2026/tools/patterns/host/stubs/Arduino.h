// Host stand-in for the Arduino-ESP32 core: only what the pattern headers and
// the portable core headers (core_math/color/noise/tables/canvas/mem) touch.
// Deterministic on purpose - millis()/micros() are the runner's frame clock
// and random() is a fixed LCG, so two runs of one script draw the same frames.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <math.h>
#include <algorithm>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef HALF_PI
#define HALF_PI 1.5707963267948966192313216916398
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define sq(x) ((x) * (x))
#define IRAM_ATTR
#define DRAM_ATTR
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_float(a) (*(const float*)(a))

// Arduino-ESP32's Arduino.h pulls these into the global namespace.
using std::abs;
using std::isinf;
using std::isnan;
using std::max;
using std::min;

typedef uint8_t byte;
typedef bool boolean;

// The runner's frame clock (set once per frame).
inline uint32_t g_hostMillis = 0;
inline uint32_t millis() { return g_hostMillis; }
inline uint32_t micros() { return g_hostMillis * 1000u; }
inline void delay(uint32_t) {}
inline void yield() {}

// Deterministic stand-ins for esp_random()-backed random().
inline uint32_t g_hostRand = 0x12345678u;
inline uint32_t esp_random() { g_hostRand = g_hostRand * 1664525u + 1013904223u; return g_hostRand; }
inline void randomSeed(uint32_t s) { g_hostRand = s ? s : 1u; }
inline long random(long hi) { return hi > 0 ? (long)(esp_random() % (uint32_t)hi) : 0; }
inline long random(long lo, long hi) { return hi > lo ? lo + random(hi - lo) : lo; }
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
inline bool psramFound() { return false; }

struct HostSerial {
  void begin(unsigned long) {}
  int printf(const char* f, ...) { va_list a; va_start(a, f); int n = vfprintf(stderr, f, a); va_end(a); return n; }
  void println(const char* s = "") { fprintf(stderr, "%s\n", s); }
  void print(const char* s) { fprintf(stderr, "%s", s); }
};
inline HostSerial Serial;
