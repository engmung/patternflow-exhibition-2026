// ═══════════════════════════════════════════════════════════
// Patternflow Ensemble - the console link, as lines of JSON
//
// One JSON object per line, both ways, over whatever carries text: the
// hub's USB serial port, its WebSocket, or a function call in the demo.
// The hub's lines are built in pfe_conductor.h; this file reads the
// console's. ensemble/PROTOCOL.md lists every line.
//
// The reader is small on purpose: flat objects, strings, numbers, true /
// false / null and arrays of those. It never allocates and it never trusts
// a length it did not check.
//
// License: MIT
// ═══════════════════════════════════════════════════════════
#pragma once

#include "pfe_conductor.h"

namespace pfe {

struct JVal {
  char type = 0;          // 's' string, 'n' number, 'b' bool, 'z' null, 'a' array, 'o' object
  const char* p = nullptr;   // strings and arrays: the inside, without quotes or brackets
  size_t n = 0;
};

namespace json {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline void skipSpace(const char* s, size_t n, size_t* i) {
  while (*i < n && isSpace(s[*i])) (*i)++;
}

// Reads one value starting at s[*i]; leaves *i after it.
inline bool readValue(const char* s, size_t n, size_t* i, JVal* v) {
  if (*i >= n) return false;
  const char c = s[*i];
  if (c == '"') {
    size_t j = *i + 1;
    while (j < n && s[j] != '"') j += s[j] == '\\' ? 2 : 1;
    if (j >= n) return false;
    v->type = 's';
    v->p = s + *i + 1;
    v->n = j - (*i + 1);
    *i = j + 1;
    return true;
  }
  if (c == '[' || c == '{') {
    const char close = c == '[' ? ']' : '}';
    int depth = 0;
    size_t j = *i;
    for (; j < n; j++) {
      if (s[j] == '"') {
        j++;
        while (j < n && s[j] != '"') j += s[j] == '\\' ? 2 : 1;
        if (j >= n) return false;
      } else if (s[j] == '[' || s[j] == '{') {
        depth++;
      } else if (s[j] == ']' || s[j] == '}') {
        if (--depth == 0) break;
      }
    }
    if (j >= n || s[j] != close) return false;
    v->type = c == '[' ? 'a' : 'o';
    v->p = s + *i + 1;
    v->n = j - (*i + 1);
    *i = j + 1;
    return true;
  }
  size_t j = *i;
  while (j < n && s[j] != ',' && s[j] != '}' && s[j] != ']' && !isSpace(s[j])) j++;
  if (j == *i) return false;
  v->p = s + *i;
  v->n = j - *i;
  if (textEquals(v->p, v->n, "true") || textEquals(v->p, v->n, "false")) v->type = 'b';
  else if (textEquals(v->p, v->n, "null")) v->type = 'z';
  else v->type = 'n';
  *i = j;
  return true;
}

}  // namespace json

class JObject {
 public:
  static constexpr int MAX_FIELDS = 16;

  bool parse(const char* s, size_t n) {
    count_ = 0;
    size_t i = 0;
    json::skipSpace(s, n, &i);
    if (i >= n || s[i] != '{') return false;
    i++;
    for (;;) {
      json::skipSpace(s, n, &i);
      if (i < n && s[i] == '}') return true;
      JVal key;
      if (!json::readValue(s, n, &i, &key) || key.type != 's') return false;
      json::skipSpace(s, n, &i);
      if (i >= n || s[i] != ':') return false;
      i++;
      json::skipSpace(s, n, &i);
      JVal val;
      if (!json::readValue(s, n, &i, &val)) return false;
      if (count_ < MAX_FIELDS) {
        key_[count_] = key;
        val_[count_] = val;
        count_++;
      }
      json::skipSpace(s, n, &i);
      if (i < n && s[i] == ',') { i++; continue; }
      if (i < n && s[i] == '}') return true;
      return false;
    }
  }

  const JVal* get(const char* name) const {
    for (int i = 0; i < count_; i++)
      if (textEquals(key_[i].p, key_[i].n, name)) return &val_[i];
    return nullptr;
  }

  bool number(const char* name, float* out) const {
    const JVal* v = get(name);
    return v && v->type == 'n' && parseNumber(v->p, v->n, out);
  }
  bool integer(const char* name, int* out) const {
    float f;
    if (!number(name, &f)) return false;
    *out = (int)roundToLong(f);
    return true;
  }
  bool boolean(const char* name, bool* out) const {
    const JVal* v = get(name);
    if (!v) return false;
    if (v->type == 'b') { *out = v->p[0] == 't'; return true; }
    float f;
    if (v->type == 'n' && parseNumber(v->p, v->n, &f)) { *out = f != 0.0f; return true; }
    return false;
  }
  bool is(const char* name, const char* text) const {
    const JVal* v = get(name);
    return v && v->type == 's' && textEquals(v->p, v->n, text);
  }

 private:
  JVal key_[MAX_FIELDS];
  JVal val_[MAX_FIELDS];
  int count_ = 0;
};

// A JSON string's inside into plain UTF-8 text.
inline void unescape(const JVal& v, char* out, size_t cap) {
  size_t w = 0;
  for (size_t i = 0; i < v.n && w + 4 < cap; i++) {
    char c = v.p[i];
    if (c != '\\' || i + 1 >= v.n) { out[w++] = c; continue; }
    c = v.p[++i];
    if (c == 'n' || c == 'r' || c == 't') { out[w++] = ' '; continue; }
    if (c != 'u') { out[w++] = c; continue; }
    if (i + 4 >= v.n) break;
    uint32_t cp = 0;
    bool ok = true;
    for (int d = 1; d <= 4; d++) {
      const int h = hexNibble(v.p[i + d]);
      if (h < 0) { ok = false; break; }
      cp = (cp << 4) | (uint32_t)h;
    }
    i += 4;
    if (!ok || (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20) { out[w++] = '?'; continue; }
    if (cp < 0x80) out[w++] = (char)cp;
    else if (cp < 0x800) { out[w++] = (char)(0xC0 | (cp >> 6)); out[w++] = (char)(0x80 | (cp & 0x3F)); }
    else { out[w++] = (char)(0xE0 | (cp >> 12)); out[w++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[w++] = (char)(0x80 | (cp & 0x3F)); }
  }
  out[w] = 0;
}

// The next element of an array's inside; false at the end.
inline bool nextElement(const JVal& arr, size_t* i, JVal* v) {
  json::skipSpace(arr.p, arr.n, i);
  if (*i < arr.n && arr.p[*i] == ',') { (*i)++; json::skipSpace(arr.p, arr.n, i); }
  if (*i >= arr.n) return false;
  return json::readValue(arr.p, arr.n, i, v);
}

inline bool parseLink(const JVal& v, uint8_t* out) {
  if (v.type != 's') return false;
  if (textEquals(v.p, v.n, "solo")) { *out = LINK_SOLO; return true; }
  if (textEquals(v.p, v.n, "sync")) { *out = LINK_SYNC; return true; }
  if (textEquals(v.p, v.n, "ripple")) { *out = LINK_RIPPLE; return true; }
  return false;
}

struct ConsoleResult {
  bool rebootHub = false;   // the console asked for the hub itself to restart
};

class Console {
 public:
  // `hubInfo` is what the hub says about itself in its hello: a JSON
  // fragment such as  "fw":"0.1.0","hub":"esp32-s3"  (no braces).
  Console(Conductor& c, const ConductorIo& io, const char* hubInfo)
      : c_(c), io_(io), hubInfo_(hubInfo) {}

  // One line from the console. Anything that is not a JSON object with a
  // "c" is ignored without a reply: the same port carries boot logs.
  ConsoleResult handle(const char* s, size_t n) {
    ConsoleResult res;
    JObject o;
    if (!o.parse(s, n)) return res;
    const JVal* cmd = o.get("c");
    if (!cmd || cmd->type != 's') return res;
    // "ping" only tells the hub that a console is still listening on this
    // port; the hub's own glue notes that. Nothing to do, nothing to say.
    if (textEquals(cmd->p, cmd->n, "ping")) return res;
    const char* err = run(o, *cmd, &res);
    Out out(line_, sizeof(line_));
    out.str(err ? "{\"t\":\"err\",\"c\":\"" : "{\"t\":\"ack\",\"c\":\"");
    for (size_t i = 0; i < cmd->n && i < 16; i++) {
      const char ch = cmd->p[i];
      out.ch((ch >= 'a' && ch <= 'z') || ch == '.' ? ch : '_');
    }
    out.ch('"');
    int r;
    if (o.integer("r", &r)) out.str(",\"r\":").i32(r);
    if (err) out.str(",\"m\":").json(err, 120);
    out.ch('}');
    if (!out.overflow && io_.line) io_.line(io_.ctx, out.buf, out.len);
    return res;
  }

 private:
  // Which panel a "uid" / "to" / "from" names: its uid, or  #<number>.
  int panelOf(const JObject& o, const char* field) const {
    const JVal* v = o.get(field);
    if (!v || v->type != 's') return -1;
    if (v->n >= 2 && v->p[0] == '#') {
      float f;
      if (!parseNumber(v->p + 1, v->n - 1, &f)) return -1;
      return c_.findPanelById((int)f);
    }
    uint8_t uid[6];
    if (!parseUid(v->p, v->n, uid)) return -1;
    return c_.findPanel(uid);
  }

  const char* run(const JObject& o, const JVal& cmd, ConsoleResult* res) {
    auto is = [&](const char* name) { return textEquals(cmd.p, cmd.n, name); };

    if (is("hello") || is("get")) {
      if (is("hello")) {
        Out out(line_, sizeof(line_));
        out.str("{\"t\":\"hello\",\"proto\":").u32(PROTO_VERSION).str(",\"port\":").u32(UDP_PORT);
        if (hubInfo_ && hubInfo_[0]) out.ch(',').str(hubInfo_);
        out.ch('}');
        if (!out.overflow && io_.line) io_.line(io_.ctx, out.buf, out.len);
      }
      c_.sendAll();
      return nullptr;
    }

    if (is("cfg")) {
      Config& cfg = c_.config();
      const JVal* link = o.get("link");
      if (link) {
        if (link->type != 'a') return "link must be an array of four";
        size_t i = 0;
        JVal e;
        for (int k = 0; k < KNOBS && nextElement(*link, &i, &e); k++) {
          if (e.type == 'z') continue;
          if (!parseLink(e, &cfg.link[k])) return "link: solo, sync or ripple";
        }
      }
      const JVal* plink = o.get("plink");
      if (plink && !parseLink(*plink, &cfg.link[CH_PATTERN])) return "plink: solo, sync or ripple";
      float f;
      int n;
      bool b;
      if (o.number("speed", &f)) cfg.speed = f;
      if (o.integer("glide", &n)) cfg.glideMs = (uint16_t)clampTo<int>(n, 0, 60000);
      if (o.boolean("k4", &b)) cfg.k4Pattern = b;
      if (o.boolean("lock", &b)) cfg.lockMenus = b;
      if (o.integer("pmin", &n)) cfg.patMin = (uint8_t)clampTo<int>(n, 0, 254);
      if (o.integer("pmax", &n)) cfg.patMax = (uint8_t)clampTo<int>(n, 0, 255);
      if (o.integer("k4s", &n)) cfg.k4Steps = (uint8_t)clampTo<int>(n, 1, 24);
      c_.configChanged();
      return nullptr;
    }

    if (is("panel")) {
      const int i = panelOf(o, "uid");
      if (i < 0) return "no such panel";
      float x = 0, y = 0;
      int id = 0;
      bool linked = true;
      const bool hx = o.number("x", &x), hy = o.number("y", &y);
      const bool hid = o.integer("id", &id);
      const bool hl = o.boolean("linked", &linked);
      const int linkedInt = linked ? 1 : 0;
      char name[NAME_LEN + 8];
      const JVal* nv = o.get("name");
      if (nv && nv->type == 's') unescape(*nv, name, sizeof(name));
      if (hid && (id < 1 || id > 250)) return "id: 1 to 250";
      c_.editPanel(i, hx ? &x : nullptr, hy ? &y : nullptr, hid ? &id : nullptr,
                   nv && nv->type == 's' ? name : nullptr, hl ? &linkedInt : nullptr);
      return nullptr;
    }

    if (is("forget")) {
      return c_.forgetPanel(panelOf(o, "uid")) ? nullptr : "no such panel";
    }

    if (is("set")) {
      int pattern = -1;
      int knob[KNOBS] = {-1, -1, -1, -1};
      int glide = -1;
      o.integer("pat", &pattern);
      o.integer("glide", &glide);
      const JVal* k = o.get("k");
      if (k) {
        if (k->type != 'a') return "k must be an array of four";
        size_t i = 0;
        JVal e;
        for (int j = 0; j < KNOBS && nextElement(*k, &i, &e); j++) {
          float f;
          if (e.type == 'n' && parseNumber(e.p, e.n, &f)) knob[j] = (int)roundToLong(f);
        }
      }
      int kn, v;
      if (o.integer("kn", &kn) && o.integer("v", &v)) {
        if (kn < 0 || kn >= KNOBS) return "kn: 0 to 3";
        knob[kn] = v;
      }
      if (o.is("to", "all")) {
        c_.setAll(pattern, knob, o.is("mode", "ripple"), panelOf(o, "from"), glide);
        return nullptr;
      }
      return c_.setPanel(panelOf(o, "to"), pattern, knob, glide) ? nullptr : "no such panel";
    }

    if (is("scene")) {
      int slot = -1, glide = -1;
      o.integer("slot", &slot);
      o.integer("glide", &glide);
      if (o.is("op", "save")) return c_.sceneSave(slot) ? nullptr : "nothing to save";
      if (o.is("op", "clear")) return c_.sceneClear(slot) ? nullptr : "no such scene";
      if (o.is("op", "recall"))
        return c_.sceneRecall(slot, o.is("mode", "ripple"), panelOf(o, "from"), glide) ? nullptr
                                                                                       : "empty scene";
      return "op: save, recall or clear";
    }

    if (is("identify")) {
      bool on = true;
      o.boolean("on", &on);
      if (o.is("uid", "all")) { c_.identifyAll(on); return nullptr; }
      return c_.identify(panelOf(o, "uid"), on) ? nullptr : "no such panel";
    }

    if (is("reboot")) {
      if (o.is("uid", "hub")) { res->rebootHub = true; return nullptr; }
      return c_.rebootPanel(panelOf(o, "uid")) ? nullptr : "that panel is not connected";
    }

    return "unknown command";
  }

  Conductor& c_;
  ConductorIo io_;
  const char* hubInfo_;
  char line_[256];
};

}  // namespace pfe
