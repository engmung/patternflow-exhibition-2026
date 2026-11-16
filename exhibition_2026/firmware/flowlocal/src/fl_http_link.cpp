// Force-link Arduino HTTPClient / TLS (pioarduino LDF often skips framework libs
// when only included from headers).
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
