#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — content library on USB/SD (patterns / shows / decks)
//
//   GET    /api/library
//   GET    /api/library/{patterns|shows|launchpads}
//   GET    /api/library/{kind}/file?name=
//   PUT    /api/library/{kind}/file?name=   (raw body)
//   DELETE /api/library/{kind}/file?name=
//
// Media layout (canonical):
//   patterns/<slug>.pfm|.json
//   patterns/128x64/<slug>.pfm|.json   (absolute-ready Director library)
//   patterns/64x64/<slug>.pfm|.json
//   shows/<slug>.pfs
//   launchpads/<slug>.json
// ═══════════════════════════════════════════════════════════

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include "fl_store.h"

namespace FlLibrary {

enum class Kind : uint8_t { Patterns, Shows, Launchpads, Invalid };

static Kind parseKind(const String& s) {
    if (s == "patterns") return Kind::Patterns;
    if (s == "shows") return Kind::Shows;
    if (s == "launchpads") return Kind::Launchpads;
    return Kind::Invalid;
}

static const char* kindDir(Kind k) {
    switch (k) {
        case Kind::Patterns: return "/patterns";
        case Kind::Shows: return "/shows";
        case Kind::Launchpads: return "/launchpads";
        default: return "";
    }
}

static bool allowedExt(Kind k, const String& name) {
    String lower = name;
    lower.toLowerCase();
    if (k == Kind::Patterns)
        return lower.endsWith(".pfm") || lower.endsWith(".json");
    if (k == Kind::Shows) return lower.endsWith(".pfs");
    if (k == Kind::Launchpads) return lower.endsWith(".json");
    return false;
}

// Basename or one-level relative path: "foo.pfm" or "128x64/foo.pfm"
static bool safeName(const String& name) {
    if (name.length() == 0 || name.length() > 96) return false;
    if (name.indexOf("..") >= 0 || name.indexOf('\\') >= 0) return false;
    if (name.startsWith("/") || name.endsWith("/")) return false;
    int slash = name.indexOf('/');
    if (slash >= 0) {
        if (name.indexOf('/', slash + 1) >= 0) return false;  // only one folder
    }
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
                  c == '/';
        if (!ok) return false;
    }
    return true;
}

static String baseNameOf(const String& name) {
    int slash = name.lastIndexOf('/');
    return slash >= 0 ? name.substring(slash + 1) : name;
}

static String joinPath(const String& a, const String& b) {
    if (a.endsWith("/")) return a + b;
    return a + "/" + b;
}

static bool ensureKindDir(Kind k) {
    if (!FlStore::mounted()) return false;
    fs::FS& fs = FlStore::storage();
    const char* dir = kindDir(k);
    if (fs.exists(dir)) return true;
    return fs.mkdir(dir);
}

static bool ensureParentDirs(const String& fullPath) {
    fs::FS& fs = FlStore::storage();
    int slash = fullPath.lastIndexOf('/');
    if (slash <= 0) return true;
    String dir = fullPath.substring(0, slash);
    if (fs.exists(dir)) return true;
    // one nested folder under kind root is enough
    return fs.mkdir(dir);
}

static const char* mimeForName(const String& name) {
    String lower = name;
    lower.toLowerCase();
    if (lower.endsWith(".json")) return "application/json";
    if (lower.endsWith(".pfs")) return "application/octet-stream";
    if (lower.endsWith(".pfm")) return "application/octet-stream";
    return "application/octet-stream";
}

static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    AsyncWebServerResponse* res = req->beginResponse(code, "application/json", out);
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("Access-Control-Allow-Origin", "*");
    req->send(res);
}

static void sendErr(AsyncWebServerRequest* req, int code, const char* msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    sendJson(req, doc, code);
}

static void appendFileEntry(JsonArray& files, const String& relName, File& entry) {
    JsonObject o = files.add<JsonObject>();
    o["name"] = relName;
    o["size"] = (uint32_t)entry.size();
    o["mtime"] = (uint32_t)entry.getLastWrite();
    String base = baseNameOf(relName);
    int dot = base.lastIndexOf('.');
    if (dot > 0) o["slug"] = base.substring(0, dot);
    else o["slug"] = base;
    int slash = relName.indexOf('/');
    if (slash > 0) o["folder"] = relName.substring(0, slash);
}

static void handleRoot(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["mounted"] = FlStore::mounted();
    doc["writable"] = FlStore::mounted();
    doc["media"] = FL_USE_SD ? "sd" : "usb";
    doc["media_error"] = FlStore::errorText();
    JsonArray kinds = doc["kinds"].to<JsonArray>();
    kinds.add("patterns");
    kinds.add("shows");
    kinds.add("launchpads");
    sendJson(req, doc);
}

static void listKind(AsyncWebServerRequest* req, Kind k) {
    if (!FlStore::mounted()) {
        sendErr(req, 503, "media not mounted");
        return;
    }
    ensureKindDir(k);
    fs::FS& fs = FlStore::storage();
    String dir = kindDir(k);
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) {
        sendErr(req, 500, "cannot open library folder");
        return;
    }

    // Large pattern libraries need headroom (ArduinoJson on PSRAM-capable builds).
    JsonDocument doc;
    doc["ok"] = true;
    doc["kind"] = (k == Kind::Patterns) ? "patterns"
                 : (k == Kind::Shows)   ? "shows"
                                        : "launchpads";
    JsonArray files = doc["files"].to<JsonArray>();

    for (;;) {
        File entry = root.openNextFile();
        if (!entry) break;
        String leaf = entry.name();
        int slash = leaf.lastIndexOf('/');
        if (slash >= 0) leaf = leaf.substring(slash + 1);

        if (entry.isDirectory()) {
            // One nested folder (e.g. patterns/128x64/)
            String subPath = joinPath(dir, leaf);
            File sub = fs.open(subPath);
            if (sub && sub.isDirectory()) {
                for (;;) {
                    File child = sub.openNextFile();
                    if (!child) break;
                    if (!child.isDirectory()) {
                        String childLeaf = child.name();
                        int cs = childLeaf.lastIndexOf('/');
                        if (cs >= 0) childLeaf = childLeaf.substring(cs + 1);
                        String rel = leaf + "/" + childLeaf;
                        if (allowedExt(k, childLeaf) && safeName(rel)) {
                            appendFileEntry(files, rel, child);
                        }
                    }
                    child.close();
                }
            }
            if (sub) sub.close();
            entry.close();
            continue;
        }

        if (allowedExt(k, leaf) && safeName(leaf)) {
            appendFileEntry(files, leaf, entry);
        }
        entry.close();
    }
    root.close();
    sendJson(req, doc);
}

static void handleGetFile(AsyncWebServerRequest* req, Kind k) {
    if (!FlStore::mounted()) {
        sendErr(req, 503, "media not mounted");
        return;
    }
    if (!req->hasParam("name")) {
        sendErr(req, 400, "missing name");
        return;
    }
    String name = req->getParam("name")->value();
    // URL decoders may leave %2F as / already
    if (!safeName(name) || !allowedExt(k, name)) {
        sendErr(req, 400, "invalid name");
        return;
    }
    String path = joinPath(kindDir(k), name);
    fs::FS& fs = FlStore::storage();
    File f = fs.open(path, "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        sendErr(req, 404, "not found");
        return;
    }
    String downloadName = baseNameOf(name);
    AsyncWebServerResponse* res = req->beginResponse(f, path, mimeForName(name));
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Content-Disposition",
                   String("attachment; filename=\"") + downloadName + "\"");
    req->send(res);
}

static void handleDeleteFile(AsyncWebServerRequest* req, Kind k) {
    if (!FlStore::mounted()) {
        sendErr(req, 503, "media not mounted");
        return;
    }
    if (!req->hasParam("name")) {
        sendErr(req, 400, "missing name");
        return;
    }
    String name = req->getParam("name")->value();
    if (!safeName(name) || !allowedExt(k, name)) {
        sendErr(req, 400, "invalid name");
        return;
    }
    String path = joinPath(kindDir(k), name);
    fs::FS& fs = FlStore::storage();
    if (!fs.exists(path)) {
        sendErr(req, 404, "not found");
        return;
    }
    if (!fs.remove(path)) {
        sendErr(req, 500, "delete failed");
        return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["name"] = name;
    sendJson(req, doc);
}

// Upload state for one in-flight PUT.
struct PutState {
    Kind kind = Kind::Invalid;
    String name;
    String tempPath;
    String finalPath;
    File file;
    bool failed = false;
    String error;
};

static PutState put;

static void resetPut() {
    if (put.file) put.file.close();
    put = PutState{};
}

static void handlePutBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                          size_t index, size_t total) {
    if (index == 0) {
        resetPut();
        String url = req->url();
        // /api/library/<kind>/file
        Kind k = Kind::Invalid;
        if (url.indexOf("/patterns/") >= 0) k = Kind::Patterns;
        else if (url.indexOf("/shows/") >= 0) k = Kind::Shows;
        else if (url.indexOf("/launchpads/") >= 0) k = Kind::Launchpads;
        if (k == Kind::Invalid) {
            put.failed = true;
            put.error = "bad kind";
            return;
        }
        if (!FlStore::mounted()) {
            put.failed = true;
            put.error = "media not mounted";
            return;
        }
        if (!req->hasParam("name")) {
            put.failed = true;
            put.error = "missing name";
            return;
        }
        String name = req->getParam("name")->value();
        if (!safeName(name) || !allowedExt(k, name)) {
            put.failed = true;
            put.error = "invalid name";
            return;
        }
        if (!ensureKindDir(k)) {
            put.failed = true;
            put.error = "cannot create folder";
            return;
        }
        put.kind = k;
        put.name = name;
        put.finalPath = joinPath(kindDir(k), name);
        put.tempPath = put.finalPath + ".tmp";
        if (!ensureParentDirs(put.finalPath)) {
            put.failed = true;
            put.error = "cannot create nested folder";
            return;
        }
        fs::FS& fs = FlStore::storage();
        if (fs.exists(put.tempPath)) fs.remove(put.tempPath);
        put.file = fs.open(put.tempPath, "w");
        if (!put.file) {
            put.failed = true;
            put.error = "cannot open temp file";
            return;
        }
        Serial.printf("[library] PUT %s (%u bytes)\n", put.finalPath.c_str(),
                      (unsigned)total);
    }

    if (put.failed) return;
    if (!put.file || !data || len == 0) return;
    size_t wrote = put.file.write(data, len);
    if (wrote != len) {
        put.failed = true;
        put.error = "write failed";
        put.file.close();
        FlStore::storage().remove(put.tempPath);
    }
}

static void handlePutDone(AsyncWebServerRequest* req) {
    if (put.failed) {
        String err = put.error.length() ? put.error : "upload failed";
        resetPut();
        sendErr(req, 400, err.c_str());
        return;
    }
    if (!put.file) {
        resetPut();
        sendErr(req, 400, "no upload body");
        return;
    }
    put.file.close();
    fs::FS& fs = FlStore::storage();
    if (fs.exists(put.finalPath)) fs.remove(put.finalPath);
    if (!fs.rename(put.tempPath, put.finalPath)) {
        // Some FAT stacks lack rename — fall back to copy.
        File src = fs.open(put.tempPath, "r");
        File dst = fs.open(put.finalPath, "w");
        bool ok = src && dst;
        uint8_t buf[512];
        while (ok && src.available()) {
            size_t n = src.read(buf, sizeof(buf));
            if (n == 0) break;
            if (dst.write(buf, n) != n) ok = false;
        }
        if (src) src.close();
        if (dst) dst.close();
        fs.remove(put.tempPath);
        if (!ok) {
            resetPut();
            sendErr(req, 500, "finalize failed");
            return;
        }
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["name"] = put.name;
    doc["kind"] = (put.kind == Kind::Patterns) ? "patterns"
                 : (put.kind == Kind::Shows)   ? "shows"
                                               : "launchpads";
    resetPut();
    sendJson(req, doc);
}

static void handleOptions(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(204);
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Methods", "GET,PUT,DELETE,OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type,X-PF-Name");
    req->send(res);
}

static Kind kindFromUrl(const String& url) {
    if (url.indexOf("/patterns") >= 0) return Kind::Patterns;
    if (url.indexOf("/shows") >= 0) return Kind::Shows;
    if (url.indexOf("/launchpads") >= 0) return Kind::Launchpads;
    return Kind::Invalid;
}

inline void begin(AsyncWebServer& server) {
    server.on("/api/library", HTTP_GET, handleRoot);
    server.on("/api/library", HTTP_OPTIONS, handleOptions);

    auto listHandler = [](AsyncWebServerRequest* req) {
        Kind k = kindFromUrl(req->url());
        if (k == Kind::Invalid) {
            sendErr(req, 404, "unknown kind");
            return;
        }
        listKind(req, k);
    };
    auto getFile = [](AsyncWebServerRequest* req) {
        Kind k = kindFromUrl(req->url());
        if (k == Kind::Invalid) {
            sendErr(req, 404, "unknown kind");
            return;
        }
        handleGetFile(req, k);
    };
    auto delFile = [](AsyncWebServerRequest* req) {
        Kind k = kindFromUrl(req->url());
        if (k == Kind::Invalid) {
            sendErr(req, 404, "unknown kind");
            return;
        }
        handleDeleteFile(req, k);
    };

    const char* kinds[] = {"patterns", "shows", "launchpads"};
    for (const char* kind : kinds) {
        String base = String("/api/library/") + kind;
        String file = base + "/file";
        server.on(base.c_str(), HTTP_GET, listHandler);
        server.on(base.c_str(), HTTP_OPTIONS, handleOptions);
        server.on(file.c_str(), HTTP_GET, getFile);
        server.on(file.c_str(), HTTP_DELETE, delFile);
        server.on(file.c_str(), HTTP_OPTIONS, handleOptions);
        server.on(
            file.c_str(), HTTP_PUT,
            [](AsyncWebServerRequest* req) { handlePutDone(req); },
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
               size_t total) { handlePutBody(req, data, len, index, total); });
    }

    Serial.println("[library] /api/library ready (patterns, shows, launchpads)");
}

}  // namespace FlLibrary
