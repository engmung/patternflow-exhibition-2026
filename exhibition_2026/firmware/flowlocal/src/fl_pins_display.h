#pragma once
// FlowLocal — TFT pin defaults (USB build). Shared with former SD SPI pins.
// When FL_USE_SD=1 the TFT is not compiled in.

#ifndef FL_TFT_ENABLE
#  if defined(FL_USE_SD) && FL_USE_SD
#    define FL_TFT_ENABLE 0
#  else
#    define FL_TFT_ENABLE 1
#  endif
#endif

#ifndef FL_TFT_CS
#  define FL_TFT_CS   4
#endif
#ifndef FL_TFT_SCK
#  define FL_TFT_SCK  5
#endif
#ifndef FL_TFT_MOSI
#  define FL_TFT_MOSI 6
#endif
#ifndef FL_TFT_DC
#  define FL_TFT_DC   7
#endif
#ifndef FL_TFT_RST
#  define FL_TFT_RST  8
#endif
#ifndef FL_TFT_BL
#  define FL_TFT_BL   3
#endif
// Override after including Adafruit_ST7735.h if needed, e.g. INITR_GREENTAB
#ifndef FL_TFT_USE_GREENTAB
#  define FL_TFT_USE_GREENTAB 1
#endif

#ifndef FL_WEATHER_POLL_MS
#  define FL_WEATHER_POLL_MS 1800000UL  // 30 minutes
#endif

#ifndef FL_MQTT_PRESET_USER
#  define FL_MQTT_PRESET_USER ""
#endif
#ifndef FL_MQTT_PRESET_PASS
#  define FL_MQTT_PRESET_PASS ""
#endif
