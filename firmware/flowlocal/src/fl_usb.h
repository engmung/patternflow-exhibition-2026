#pragma once
// ═══════════════════════════════════════════════════════════
// FlowLocal — USB OTG host (MSC / FAT32)
//
// USB-C stick in the jack labelled USB (GPIO19/20). The 5V header is VIN,
// not a host supply. Close the board's USB-OTG jumper so COM's 5 V feeds
// that jack's VBUS; power the board from COM. Leave the jumper off if a
// PC is on the native USB jack (do not feed two VBUS sources together).
// ═══════════════════════════════════════════════════════════

#include <FS.h>
#include <EspUsbHost.h>
#include "../flowlocal_config.h"

namespace FlUsb {

static EspUsbHost usb;
static EspUsbHostMscFS massStorage;
static bool hostOk = false;
static volatile int deviceCount = 0;
static volatile bool unmountPending = false;
static uint32_t lastMountTryMs = 0;
static uint32_t lastWaitLogMs = 0;
static String lastError = "idle";
static String contentRootStr = "/director";
static String rootNamesStr;

inline bool mounted() { return massStorage.mounted(); }

inline int devices() { return deviceCount; }

inline String errorText() { return lastError; }

inline fs::FS& storage() { return massStorage; }
inline String contentRoot() { return contentRootStr; }
inline String rootNames() { return rootNamesStr; }

static String leafName(String n) {
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    n.toLowerCase();
    return n;
}

static bool isReadableFile(fs::FS& fs, const String& path) {
    File f = fs.open(path);
    const bool ok = f && !f.isDirectory() && f.size() > 0;
    if (f) f.close();
    return ok;
}

static void listRoot() {
    rootNamesStr = "";
    File root = massStorage.open("/");
    if (!root || !root.isDirectory()) return;
    Serial.println("[usb] root:");
    for (;;) {
        File entry = root.openNextFile();
        if (!entry) break;
        String n = entry.name();
        Serial.printf("  %s %s\n", entry.isDirectory() ? "DIR " : "FILE", n.c_str());
        if (rootNamesStr.length()) rootNamesStr += " ";
        rootNamesStr += leafName(n);
        entry.close();
    }
    root.close();
}

static void findDirector() {
    contentRootStr = "/director";
    fs::FS& fs = massStorage;
    static const char* idx[] = {
        "/director/index.html",
        "/index.html",
        "/flash_image/director/index.html",
        "/Director/index.html",
    };
    for (const char* p : idx) {
        if (isReadableFile(fs, p)) {
            String s(p);
            int slash = s.lastIndexOf('/');
            contentRootStr = (slash <= 0) ? String("/") : s.substring(0, slash);
            if (contentRootStr.length() == 0) contentRootStr = "/";
            Serial.printf("[usb] Director at %s\n", contentRootStr.c_str());
            return;
        }
    }
    File root = fs.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[usb] no director/index.html on stick");
        return;
    }
    for (;;) {
        File e = root.openNextFile();
        if (!e) break;
        const String leaf = leafName(e.name());
        const bool dir = e.isDirectory();
        e.close();
        if (!dir) continue;
        String try1 = String("/") + leaf + "/index.html";
        String try2 = String("/") + leaf + "/director/index.html";
        if (isReadableFile(fs, try1)) {
            contentRootStr = String("/") + leaf;
            Serial.printf("[usb] Director at %s\n", contentRootStr.c_str());
            root.close();
            return;
        }
        if (isReadableFile(fs, try2)) {
            contentRootStr = String("/") + leaf + "/director";
            Serial.printf("[usb] Director at %s\n", contentRootStr.c_str());
            root.close();
            return;
        }
    }
    root.close();
    Serial.println("[usb] no director/index.html on stick");
}

inline void begin() {
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo& device) {
        deviceCount++;
        Serial.print("[usb] connected: ");
        espUsbHostPrint(device);
    });
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo& device) {
        if (deviceCount > 0) deviceCount--;
        unmountPending = true;
        Serial.print("[usb] disconnected: ");
        espUsbHostPrint(device);
    });

    if (!usb.begin()) {
        lastError = usb.lastErrorName();
        Serial.printf("[usb] host begin failed: %s\n", lastError.c_str());
        hostOk = false;
        return;
    }
    hostOk = true;
    lastError = "waiting";
    Serial.println("[usb] host ready — USB-C stick on USB jack, USB-OTG jumper closed, power from COM");
}

inline void tick() {
    if (!hostOk) return;
    if (unmountPending) {
        unmountPending = false;
        if (massStorage.mounted()) massStorage.end();
        lastError = "removed";
    }
    if (massStorage.mounted()) return;

    const uint32_t now = millis();

    // No MSC attached: never call massStorage.begin() — it blocks up to
    // ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS (5s) and starves PicoMQTT /
    // Director WebSocket on :9001. Host task still raises deviceCount.
    if (deviceCount <= 0) {
        lastError = "no device — OTG jumper + USB jack + power from COM";
        if (now - lastWaitLogMs >= 10000) {
            lastWaitLogMs = now;
            Serial.printf("[usb] idle (no stick) — broker/Director OK without media\n");
        }
        return;
    }

    // Stick present but not mounted yet — retry with a short timeout.
    if (now - lastMountTryMs < 2000) return;
    lastMountTryMs = now;

    constexpr uint32_t kMountTimeoutMs = 800;
    if (massStorage.begin(usb, FL_USB_MOUNT, ESP_USB_HOST_ANY_ADDRESS, 0, 4,
                          kMountTimeoutMs, true)) {
        lastError = "mounted";
        Serial.println("[usb] FAT mounted");
        listRoot();
        findDirector();
        return;
    }

    lastError = usb.lastErrorName();
    if (now - lastWaitLogMs >= 5000) {
        lastWaitLogMs = now;
        Serial.printf("[usb] not mounted  devices=%d  err=%s\n",
                      deviceCount, lastError.c_str());
    }
}

}  // namespace FlUsb
