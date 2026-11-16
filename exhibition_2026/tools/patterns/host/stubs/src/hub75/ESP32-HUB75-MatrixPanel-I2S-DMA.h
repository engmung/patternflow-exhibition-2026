// Host stand-in for the vendored HUB75 driver. PFCanvas::present() (the real
// src/core_canvas.h) ends in blitRGB888() with the finished canvas while the
// pattern's logical frame (setFrame) is still set - the runner hooks in here
// to read the frame back, exactly what the panel would have been sent.
#pragma once
#include <Arduino.h>
inline void (*g_hostBlitHook)(const uint8_t* rgb) = nullptr;
class MatrixPanel_I2S_DMA {
 public:
  void blitRGB888(const uint8_t* rgb, const uint8_t*, const uint8_t*, const uint8_t*, int) {
    if (g_hostBlitHook) g_hostBlitHook(rgb);
  }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
  void drawPixelRGB888(int16_t, int16_t, uint8_t, uint8_t, uint8_t) {}
};
