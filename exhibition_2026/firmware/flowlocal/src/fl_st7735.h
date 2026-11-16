#pragma once
// Minimal ST7735 (128x160) on SPI — Adafruit GFX only (no ST7735 package / SD dep).

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "../flowlocal_config.h"

class FlSt7735 : public Adafruit_GFX {
public:
    FlSt7735() : Adafruit_GFX(128, 160) {}

    void begin() {
#if FL_TFT_USE_GREENTAB
        _colstart = 2;
        _rowstart = 1;
#else
        _colstart = 0;
        _rowstart = 0;
#endif
        pinMode(FL_TFT_CS, OUTPUT);
        pinMode(FL_TFT_DC, OUTPUT);
        pinMode(FL_TFT_RST, OUTPUT);
        digitalWrite(FL_TFT_CS, HIGH);
        digitalWrite(FL_TFT_RST, HIGH);
        delay(10);
        digitalWrite(FL_TFT_RST, LOW);
        delay(10);
        digitalWrite(FL_TFT_RST, HIGH);
        delay(10);

        SPI.begin(FL_TFT_SCK, -1, FL_TFT_MOSI, FL_TFT_CS);
        // Black-tab init sequence (common 1.8" clones)
        writeCmd(0x01); delay(150);  // SWRESET
        writeCmd(0x11); delay(120);  // SLPOUT
        writeCmd(0xB1); writeData(0x01); writeData(0x2C); writeData(0x2D);
        writeCmd(0xB2); writeData(0x01); writeData(0x2C); writeData(0x2D);
        writeCmd(0xB3); writeData(0x01); writeData(0x2C); writeData(0x2D);
        writeData(0x01); writeData(0x2C); writeData(0x2D);
        writeCmd(0xB4); writeData(0x07);
        writeCmd(0xC0); writeData(0xA2); writeData(0x02); writeData(0x84);
        writeCmd(0xC1); writeData(0xC5);
        writeCmd(0xC2); writeData(0x0A); writeData(0x00);
        writeCmd(0xC3); writeData(0x8A); writeData(0x2A);
        writeCmd(0xC4); writeData(0x8A); writeData(0xEE);
        writeCmd(0xC5); writeData(0x0E);
        writeCmd(0x36); writeData(0xC8);  // MADCTL
        writeCmd(0x3A); writeData(0x05);  // 16-bit
        writeCmd(0x29);  // DISPON
        delay(20);
        fillScreen(0x0000);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= _width || y >= _height) return;
        setWindow(x, y, x, y);
        writeData16(color);
    }

    void fillScreen(uint16_t color) {
        fillRect(0, 0, _width, _height, color);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        if (w <= 0 || h <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > _width) w = _width - x;
        if (y + h > _height) h = _height - y;
        if (w <= 0 || h <= 0) return;
        setWindow(x, y, x + w - 1, y + h - 1);
        uint32_t n = (uint32_t)w * (uint32_t)h;
        digitalWrite(FL_TFT_DC, HIGH);
        digitalWrite(FL_TFT_CS, LOW);
        SPI.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
        while (n--) {
            SPI.write16(color);
        }
        SPI.endTransaction();
        digitalWrite(FL_TFT_CS, HIGH);
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        fillRect(x, y, w, 1, color);
    }

    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

private:
    void writeCmd(uint8_t c) {
        digitalWrite(FL_TFT_DC, LOW);
        digitalWrite(FL_TFT_CS, LOW);
        SPI.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
        SPI.write(c);
        SPI.endTransaction();
        digitalWrite(FL_TFT_CS, HIGH);
    }
    void writeData(uint8_t d) {
        digitalWrite(FL_TFT_DC, HIGH);
        digitalWrite(FL_TFT_CS, LOW);
        SPI.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
        SPI.write(d);
        SPI.endTransaction();
        digitalWrite(FL_TFT_CS, HIGH);
    }
    void writeData16(uint16_t d) {
        digitalWrite(FL_TFT_DC, HIGH);
        digitalWrite(FL_TFT_CS, LOW);
        SPI.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
        SPI.write16(d);
        SPI.endTransaction();
        digitalWrite(FL_TFT_CS, HIGH);
    }
    void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
        x0 += _colstart;
        x1 += _colstart;
        y0 += _rowstart;
        y1 += _rowstart;
        writeCmd(0x2A);
        writeData((uint8_t)(x0 >> 8));
        writeData((uint8_t)(x0 & 0xFF));
        writeData((uint8_t)(x1 >> 8));
        writeData((uint8_t)(x1 & 0xFF));
        writeCmd(0x2B);
        writeData((uint8_t)(y0 >> 8));
        writeData((uint8_t)(y0 & 0xFF));
        writeData((uint8_t)(y1 >> 8));
        writeData((uint8_t)(y1 & 0xFF));
        writeCmd(0x2C);
    }

    int16_t _colstart = 0;
    int16_t _rowstart = 0;
};
