// Host stand-in for src/core_display.h: a display that remembers what was
// drawn on it.
#pragma once
#include <cstdint>
#include <string>

namespace stub {
inline std::string drawn;
inline int fills = 0;
inline uint16_t lastFill = 0xFFFF;   // the colour of the last filled shape
inline std::string lastShape;          // "rect", "circle" or "triangle"
}  // namespace stub

struct MatrixPanel_I2S_DMA {
  int width() { return 128; }
  int height() { return 64; }
  void fillRect(int, int, int, int, uint16_t c) { stub::fills++; stub::lastFill = c; stub::lastShape = "rect"; }
  void fillCircle(int, int, int, uint16_t c) { stub::fills++; stub::lastFill = c; stub::lastShape = "circle"; }
  void fillTriangle(int, int, int, int, int, int, uint16_t c) { stub::fills++; stub::lastFill = c; stub::lastShape = "triangle"; }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
};
inline MatrixPanel_I2S_DMA theDisplay;
inline MatrixPanel_I2S_DMA* dma_display = &theDisplay;
