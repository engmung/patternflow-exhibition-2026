// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the console's requests to a panel's own web server
//
// A panel has a web server (its patterns, its status, its firmware update),
// and the console is usually not on the panels' network: it is on a USB
// cable to the hub. So the hub passes HTTP along. The console says what to
// ask which panel, sends the request's body as lines, and gets the answer
// back as lines - over the same link every other console line uses.
//
//   console -> hub
//     {"c":"http","id":7,"to":"<uid>","m":"PUT","path":"/api/patterns",
//      "h":{"X-PF-Name":"a.pfm"},"len":1234,"tmo":15000}
//     {"c":"hb","id":7,"d":"<base64, up to RELAY_CHUNK_IN bytes>"}   until len bytes went
//   hub -> console
//     {"t":"hk","id":7,"n":720}             body bytes passed on so far
//     {"t":"http","id":7,"s":200,"len":42}  the panel's status and Content-Length (-1: not said)
//     {"t":"hb","id":7,"d":"<base64>"}      the answer's body, in order
//     {"t":"he","id":7,"n":42}              done: body bytes in all
//     {"t":"he","id":7,"err":"timeout"}     or it failed, at any point
//
// A Relay carries one request at a time; the hub keeps a few, so a few
// panels can be worked on at once (the "id" says whose line is whose). No
// Arduino here: the hub hands it a socket and the PC tests a pretend one.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pfe_console.h"

namespace pfe {

// What the relay needs of a TCP connection.
struct RelaySocket {
  virtual ~RelaySocket() {}
  virtual bool connect(uint32_t ip, uint16_t port, uint32_t timeoutMs) = 0;
  virtual size_t write(const uint8_t* data, size_t len) = 0;   // how much went: all of it, or it failed
  virtual int available() = 0;
  virtual int read(uint8_t* out, size_t cap) = 0;
  virtual bool connected() = 0;        // not closed yet (what it sent may still be waiting to be read)
  virtual void stop() = 0;
};

struct RelayIo {
  void* ctx = nullptr;
  void (*line)(void* ctx, const char* text, size_t len) = nullptr;   // one line to the console
  size_t (*room)(void* ctx) = nullptr;   // bytes of lines the console link can take now; null = any amount
};

constexpr size_t RELAY_CHUNK = 360;               // body bytes a line to the console carries: 480 characters of base64
constexpr size_t RELAY_CHUNK_IN = 1080;           // and a line from it: 1440 characters
constexpr uint32_t RELAY_TIMEOUT_MS = 15000;      // with nothing moving, unless the request says otherwise
constexpr uint32_t RELAY_CONNECT_MS = 800;        // the hub does nothing else meanwhile
constexpr long RELAY_MAX_BODY = 4L * 1024 * 1024; // of a request
constexpr size_t RELAY_MAX_REPLY = 512 * 1024;    // of an answer

inline size_t base64Encode(const uint8_t* in, size_t n, char* out) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = ((uint32_t)in[i] << 16) | ((i + 1 < n ? in[i + 1] : 0u) << 8) | (i + 2 < n ? in[i + 2] : 0u);
    out[o++] = A[(v >> 18) & 63];
    out[o++] = A[(v >> 12) & 63];
    out[o++] = i + 1 < n ? A[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? A[v & 63] : '=';
  }
  return o;
}

// Returns the bytes written, or -1 for text that is not base64 or does not fit.
inline long base64Decode(const char* in, size_t n, uint8_t* out, size_t cap) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  if (n % 4) return -1;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    const int a = value(in[i]), b = value(in[i + 1]);
    const bool pad2 = in[i + 2] == '=', pad3 = in[i + 3] == '=';
    const int c = pad2 ? 0 : value(in[i + 2]), d = pad3 ? 0 : value(in[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) || ((pad2 || pad3) && i + 4 != n)) return -1;
    const uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    const size_t bytes = pad2 ? 1 : pad3 ? 2 : 3;
    if (o + bytes > cap) return -1;
    out[o++] = (uint8_t)(v >> 16);
    if (bytes > 1) out[o++] = (uint8_t)(v >> 8);
    if (bytes > 2) out[o++] = (uint8_t)v;
  }
  return (long)o;
}

class Relay {
 public:
  void begin(RelaySocket* socket, const RelayIo& io) {
    socket_ = socket;
    io_ = io;
    state_ = IDLE;
  }

  bool busy() const { return state_ != IDLE; }
  int id() const { return id_; }

  // {"c":"http",...} for the panel at `ip` (0: the console named no panel the hub knows).
  void request(const JObject& o, uint32_t ip, uint32_t nowMs) {
    int id = 0;
    o.integer("id", &id);
    if (state_ != IDLE) { fail(id, "busy with another request"); return; }
    int len = 0, tmo = 0;
    o.integer("len", &len);
    o.integer("tmo", &tmo);
    const JVal* method = o.get("m");
    const JVal* path = o.get("path");
    if (!ip) { fail(id, "no such panel, or it is not connected"); return; }
    if (!method || method->type != 's' || !plain(*method, false) || method->n > 7) { fail(id, "m: GET, POST, PUT or DELETE"); return; }
    if (!path || path->type != 's' || path->n < 1 || path->n > 200 || path->p[0] != '/' || !plain(*path, false)) {
      fail(id, "path: /...");
      return;
    }
    if (len < 0 || (long)len > RELAY_MAX_BODY) { fail(id, "len: too long"); return; }

    // The head of the request, whole, before anything is opened.
    Out head(buf_, sizeof(buf_));
    head.raw(method->p, method->n).ch(' ').raw(path->p, path->n).str(" HTTP/1.1\r\nHost: panel\r\nConnection: close\r\n");
    head.str("Content-Length: ").u32((uint32_t)len).str("\r\n");
    const JVal* headers = o.get("h");
    if (headers && headers->type == 'o') {
      size_t i = 0;
      for (;;) {
        JVal key, value;
        json::skipSpace(headers->p, headers->n, &i);
        if (i >= headers->n || !json::readValue(headers->p, headers->n, &i, &key) || key.type != 's') break;
        json::skipSpace(headers->p, headers->n, &i);
        if (i >= headers->n || headers->p[i] != ':') break;
        i++;
        json::skipSpace(headers->p, headers->n, &i);
        if (!json::readValue(headers->p, headers->n, &i, &value)) break;
        // A name or a value that would end the line early is left out.
        if (value.type == 's' && key.n && key.n <= 40 && value.n <= 120 && plain(key, false) && plain(value, true))
          head.raw(key.p, key.n).str(": ").raw(value.p, value.n).str("\r\n");
        json::skipSpace(headers->p, headers->n, &i);
        if (i < headers->n && headers->p[i] == ',') i++;
      }
    }
    head.str("\r\n");
    if (head.overflow) { fail(id, "too many headers"); return; }

    if (!socket_->connect(ip, 80, RELAY_CONNECT_MS)) { fail(id, "the panel does not answer"); return; }
    if (socket_->write((const uint8_t*)buf_, head.len) != head.len) {
      socket_->stop();
      fail(id, "the panel closed the connection");
      return;
    }
    id_ = id;
    bodyLen_ = (uint32_t)len;
    bodySent_ = 0;
    timeoutMs_ = tmo >= 1000 && tmo <= 120000 ? (uint32_t)tmo : RELAY_TIMEOUT_MS;
    movedMs_ = nowMs;
    headLen_ = 0;
    headDone_ = false;
    carried_ = 0;
    replyLen_ = -1;
    replyGot_ = 0;
    state_ = bodyLen_ ? SENDING : WAITING;
  }

  // {"c":"hb",...}: more of the request's body.
  void body(const JObject& o, uint32_t nowMs) {
    int id = 0;
    o.integer("id", &id);
    if (state_ != SENDING || id != id_) return;      // nothing is waiting for it: a request that already failed
    const JVal* d = o.get("d");
    uint8_t bytes[RELAY_CHUNK_IN + 24];
    const long n = d && d->type == 's' ? base64Decode(d->p, d->n, bytes, sizeof(bytes)) : -1;
    if (n < 0 || (uint32_t)n > bodyLen_ - bodySent_) { end("d: not the bytes that were promised"); return; }
    if (n && socket_->write(bytes, (size_t)n) != (size_t)n) { end("the panel closed the connection"); return; }
    bodySent_ += (uint32_t)n;
    movedMs_ = nowMs;
    Out out(buf_, sizeof(buf_));
    out.str("{\"t\":\"hk\",\"id\":").i32(id_).str(",\"n\":").u32(bodySent_).ch('}');
    emit(out);
    if (bodySent_ == bodyLen_) state_ = WAITING;
  }

  // Every pass of the hub's loop: the panel's answer, as it comes.
  void tick(uint32_t nowMs) {
    if (state_ == IDLE) return;
    if (since(nowMs, movedMs_) > timeoutMs_) { end("timeout"); return; }
    if (state_ == SENDING) return;
    for (int budget = 4; budget > 0 && state_ != IDLE; budget--) {
      if (!headDone_) {
        const int have = socket_->available();
        if (have <= 0) {
          if (!socket_->connected()) end("the panel closed the connection without answering");
          return;
        }
        const int got = socket_->read((uint8_t*)head_ + headLen_, sizeof(head_) - headLen_);
        if (got <= 0) return;
        headLen_ += (size_t)got;
        movedMs_ = nowMs;
        const long endAt = headEnd();
        if (endAt < 0) {
          if (headLen_ >= sizeof(head_)) end("the panel's answer has too long a head");
          continue;
        }
        const int status = parseStatus();
        if (status < 100) { end("not an HTTP answer"); return; }
        replyLen_ = parseLength((size_t)endAt);
        carried_ = (size_t)endAt;
        headDone_ = true;
        Out out(buf_, sizeof(buf_));
        out.str("{\"t\":\"http\",\"id\":").i32(id_).str(",\"s\":").i32(status).str(",\"len\":").i32((int)replyLen_).ch('}');
        emit(out);
        continue;
      }
      if (replyLen_ >= 0 && replyGot_ >= (size_t)replyLen_) { finish(); return; }
      if (io_.room && io_.room(io_.ctx) < 900) return;       // the console is behind: later
      uint8_t bytes[RELAY_CHUNK];
      size_t n = 0;
      if (carried_ < headLen_) {
        // What came in with the head.
        n = headLen_ - carried_;
        if (n > sizeof(bytes)) n = sizeof(bytes);
        copyBytes(bytes, (const uint8_t*)head_ + carried_, n);
        carried_ += n;
      } else {
        if (socket_->available() <= 0) {
          if (!socket_->connected()) finish();
          return;
        }
        const int got = socket_->read(bytes, sizeof(bytes));
        if (got <= 0) return;
        n = (size_t)got;
      }
      if (replyLen_ >= 0 && replyGot_ + n > (size_t)replyLen_) n = (size_t)replyLen_ - replyGot_;
      replyGot_ += n;
      movedMs_ = nowMs;
      if (replyGot_ > RELAY_MAX_REPLY) { end("the answer is too long"); return; }
      Out out(buf_, sizeof(buf_));
      out.str("{\"t\":\"hb\",\"id\":").i32(id_).str(",\"d\":\"");
      if (out.len + ((n + 2) / 3) * 4 + 4 < out.cap) {
        out.len += base64Encode(bytes, n, out.buf + out.len);
        out.buf[out.len] = 0;
      } else {
        out.overflow = true;
      }
      out.str("\"}");
      emit(out);
    }
  }

 private:
  enum State : uint8_t { IDLE, SENDING, WAITING };

  static bool plain(const JVal& v, bool spaces) {
    for (size_t i = 0; i < v.n; i++) {
      const char c = v.p[i];
      if (c < 0x20 || c == 0x7f || c == '\\' || c == '"' || (c == ' ' && !spaces)) return false;
    }
    return true;
  }

  void emit(const Out& out) {
    if (!out.overflow && io_.line) io_.line(io_.ctx, out.buf, out.len);
  }
  void fail(int id, const char* why) {
    Out out(buf_, sizeof(buf_));
    out.str("{\"t\":\"he\",\"id\":").i32(id).str(",\"err\":").json(why, 100).ch('}');
    emit(out);
  }
  void end(const char* why) {
    socket_->stop();
    state_ = IDLE;
    fail(id_, why);
  }
  void finish() {
    socket_->stop();
    state_ = IDLE;
    Out out(buf_, sizeof(buf_));
    out.str("{\"t\":\"he\",\"id\":").i32(id_).str(",\"n\":").u32((uint32_t)replyGot_).ch('}');
    emit(out);
  }

  // Where the head ends (the first byte after the blank line), or -1.
  long headEnd() const {
    for (size_t i = 3; i < headLen_; i++)
      if (head_[i - 3] == '\r' && head_[i - 2] == '\n' && head_[i - 1] == '\r' && head_[i] == '\n') return (long)i + 1;
    return -1;
  }
  int parseStatus() const {
    size_t i = 0;
    while (i < headLen_ && head_[i] != ' ' && head_[i] != '\r') i++;
    if (i + 4 > headLen_ || head_[i] != ' ') return 0;
    int s = 0;
    for (size_t j = i + 1; j < i + 4; j++) {
      if (head_[j] < '0' || head_[j] > '9') return 0;
      s = s * 10 + (head_[j] - '0');
    }
    return s;
  }
  long parseLength(size_t headBytes) const {
    static const char name[] = "content-length:";
    const size_t nameLen = sizeof(name) - 1;
    for (size_t i = 0; i + nameLen < headBytes; i++) {
      if (i && head_[i - 1] != '\n') continue;
      size_t k = 0;
      while (k < nameLen && (char)(head_[i + k] | 0x20) == name[k]) k++;
      if (k < nameLen) continue;
      size_t j = i + nameLen;
      while (j < headBytes && head_[j] == ' ') j++;
      long v = 0;
      bool any = false;
      while (j < headBytes && head_[j] >= '0' && head_[j] <= '9' && v < 100000000L) { v = v * 10 + (head_[j++] - '0'); any = true; }
      return any ? v : -1;
    }
    return -1;
  }

  RelaySocket* socket_ = nullptr;
  RelayIo io_;
  State state_ = IDLE;
  int id_ = 0;
  uint32_t bodyLen_ = 0, bodySent_ = 0;
  uint32_t timeoutMs_ = RELAY_TIMEOUT_MS, movedMs_ = 0;
  char head_[1024];
  size_t headLen_ = 0, carried_ = 0;
  bool headDone_ = false;
  long replyLen_ = -1;
  size_t replyGot_ = 0;
  char buf_[720];
};

}  // namespace pfe
