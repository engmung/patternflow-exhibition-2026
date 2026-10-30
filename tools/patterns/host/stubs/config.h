// Host stand-in for firmware/patternflow/config.h: the panel geometry and the
// canvas calibration defaults (identity - the canvas buffer keeps the
// pattern's raw values either way; present() is a no-op here).
#pragma once
#include <Arduino.h>
#define PANEL_RES_W 128
#define PANEL_RES_H 64
#define PANEL_CHAIN 1
#ifndef LED_GAMMA_R
#define LED_GAMMA_R 1.0f
#endif
#ifndef LED_GAMMA_G
#define LED_GAMMA_G 1.0f
#endif
#ifndef LED_GAMMA_B
#define LED_GAMMA_B 1.0f
#endif
#ifndef LED_WB_R
#define LED_WB_R 1.0f
#endif
#ifndef LED_WB_G
#define LED_WB_G 1.0f
#endif
#ifndef LED_WB_B
#define LED_WB_B 1.0f
#endif
#ifndef LED_SAT_BOOST
#define LED_SAT_BOOST 1.0f
#endif
