// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - small helpers shared by every part of the core
//
// The core (pfe_*.h) is written to compile three ways from the same source:
//   - into the hub and panel firmware (ESP32-S3, Arduino),
//   - natively on a PC for the tests (g++ / clang / zig),
//   - freestanding to WebAssembly for the console's demo mode.
// So it uses no heap, no libc and no C++ standard library: only the
// compiler's own <stdint.h> / <stddef.h> and a handful of builtins.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pfe {

inline void copyBytes(void* dst, const void* src, size_t n) { __builtin_memcpy(dst, src, n); }
inline void zeroBytes(void* dst, size_t n) { __builtin_memset(dst, 0, n); }
inline bool sameBytes(const void* a, const void* b, size_t n) { return __builtin_memcmp(a, b, n) == 0; }
inline size_t textLen(const char* s) { return s ? __builtin_strlen(s) : 0; }

template <typename T>
constexpr T clampTo(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float absF(float v) { return v < 0.0f ? -v : v; }
inline float sqrtF(float v) { return __builtin_sqrtf(v); }
inline long roundToLong(float v) { return (long)(v >= 0.0f ? v + 0.5f : v - 0.5f); }

// Copy a C string into a fixed field, always terminated, zero-padded.
inline void setText(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t i = 0;
  if (src) for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
  for (; i < cap; i++) dst[i] = 0;
}

// The same for a name a person typed: UTF-8, and never cut inside a
// character (half a Korean syllable is not a shorter name, it is garbage).
inline void setTextUtf8(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t n = 0;
  if (src) for (; n + 1 < cap && src[n]; n++) dst[n] = src[n];
  size_t i = n;
  while (i > 0 && ((unsigned char)dst[i - 1] & 0xC0) == 0x80) i--;   // back over continuation bytes
  if (i > 0 && ((unsigned char)dst[i - 1] & 0x80)) {
    const unsigned char lead = (unsigned char)dst[i - 1];
    const size_t need = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1));
    if (n - (i - 1) < need) n = i - 1;
  }
  for (; n < cap; n++) dst[n] = 0;
}

// Milliseconds on a wrapping 32-bit clock: how long ago `then` was.
constexpr uint32_t since(uint32_t now, uint32_t then) { return now - then; }

// ── Little-endian fields ────────────────────────────────────────────────
inline void putU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void putU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline uint16_t getU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t getU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ── A text sink over a caller's buffer ──────────────────────────────────
// Everything the hub says to the console is built with this. It never
// writes past the buffer; `overflow` says a line was cut, and the caller
// drops such a line instead of sending half a JSON object.
struct Out {
  char* buf;
  size_t cap;
  size_t len = 0;
  bool overflow = false;

  Out(char* b, size_t c) : buf(b), cap(c) { if (cap) buf[0] = 0; }

  void reset() { len = 0; overflow = false; if (cap) buf[0] = 0; }

  Out& ch(char c) {
    if (len + 1 < cap) { buf[len++] = c; buf[len] = 0; } else overflow = true;
    return *this;
  }
  Out& str(const char* s) {
    if (s) while (*s) ch(*s++);
    return *this;
  }
  Out& u32(uint32_t v) {
    char tmp[10];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) ch(tmp[--n]);
    return *this;
  }
  Out& i32(int32_t v) {
    if (v < 0) { ch('-'); return u32((uint32_t)(-(int64_t)v)); }
    return u32((uint32_t)v);
  }
  // A float with three decimals, trailing zeros trimmed: 1.5, 0.25, 12.
  Out& num(float v) {
    if (v != v) return str("0");
    if (v < 0) { ch('-'); v = -v; }
    if (v > 4.0e6f) v = 4.0e6f;
    uint32_t milli = (uint32_t)(v * 1000.0f + 0.5f);
    u32(milli / 1000);
    uint32_t frac = milli % 1000;
    if (frac) {
      ch('.');
      ch((char)('0' + frac / 100));
      if (frac % 100) {
        ch((char)('0' + (frac / 10) % 10));
        if (frac % 10) ch((char)('0' + frac % 10));
      }
    }
    return *this;
  }
  Out& boolean(bool b) { return str(b ? "true" : "false"); }
  Out& hex8(uint8_t v) {
    static const char d[] = "0123456789abcdef";
    return ch(d[v >> 4]).ch(d[v & 15]);
  }
  // A JSON string, quoted. Quotes and backslashes are escaped and control
  // characters replaced: names come off the air and must not be able to
  // break a line the console parses. Bytes above ASCII pass through, so a
  // UTF-8 name (a Korean one, say) survives.
  Out& json(const char* s, size_t maxLen = 64) {
    ch('"');
    for (size_t i = 0; s && s[i] && i < maxLen; i++) {
      const unsigned char c = (unsigned char)s[i];
      if (c == '"' || c == '\\') { ch('\\'); ch((char)c); }
      else if (c < 0x20 || c == 0x7f) ch('?');
      else ch((char)c);
    }
    return ch('"');
  }
};

// ── Text into numbers ───────────────────────────────────────────────────
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads a decimal number (sign, digits, fraction, no exponent) from s[0..n).
inline bool parseNumber(const char* s, size_t n, float* out) {
  size_t i = 0;
  bool neg = false;
  if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
  bool any = false;
  double v = 0;
  for (; i < n && isDigit(s[i]); i++) { v = v * 10 + (s[i] - '0'); any = true; }
  if (i < n && s[i] == '.') {
    i++;
    double scale = 0.1;
    for (; i < n && isDigit(s[i]); i++) { v += (s[i] - '0') * scale; scale *= 0.1; any = true; }
  }
  if (!any || i != n) return false;
  *out = (float)(neg ? -v : v);
  return true;
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "a1b2c3d4e5f6" (12 hex digits, colons allowed) into six bytes.
inline bool parseUid(const char* s, size_t n, uint8_t uid[6]) {
  int got = 0;
  int hi = -1;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == ':' || s[i] == '-') continue;
    const int v = hexNibble(s[i]);
    if (v < 0) return false;
    if (hi < 0) { hi = v; continue; }
    if (got >= 6) return false;
    uid[got++] = (uint8_t)((hi << 4) | v);
    hi = -1;
  }
  return got == 6 && hi < 0;
}

inline bool textEquals(const char* s, size_t n, const char* lit) {
  const size_t m = textLen(lit);
  return n == m && sameBytes(s, lit, n);
}

}  // namespace pfe
