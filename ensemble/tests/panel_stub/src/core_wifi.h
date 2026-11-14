// Host stand-in for src/core_wifi.h: the saved networks and the join that
// features/ensemble uses to go back to the hub's network.
#pragma once
#include "Arduino.h"
#include <string>
#include <vector>

namespace stub {
struct Join { std::string ssid, pass; };
inline std::vector<Join> joins;   // every applyCredentials() call
}  // namespace stub

namespace PatternflowWifi {
constexpr int MAX_NETWORKS = 5;
inline String savedSsids[MAX_NETWORKS];
inline String savedPasses[MAX_NETWORKS];
inline int savedCountValue = 0;
inline int bootIdx = 0;
inline String activeSsid;

inline int savedCount() { return savedCountValue; }
inline const String& savedSsid(int i) { return savedSsids[i]; }
inline int getBootIndex() { return bootIdx; }
inline const String& currentSsid() { return activeSsid; }
inline void applyCredentials(const String& ssid, const String& pass) {
  stub::joins.push_back({ssid.c_str(), pass.c_str()});
  activeSsid = ssid;
}
}  // namespace PatternflowWifi
