#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>

// A small RGB565 backbuffer that presents full-screen coordinates to ordinary
// Adafruit_GFX drawing code. Pixels outside the current tile are clipped.
// This lets a complete final tile be composed in RAM before it replaces the
// corresponding pixels already retained by the LCD controller.
class TileCanvas : public Adafruit_GFX {
 public:
  static constexpr int16_t MAX_TILE = 40;

  TileCanvas();

  void beginTile(int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t clearColor = 0x0000);

  int16_t tileX() const { return tileX_; }
  int16_t tileY() const { return tileY_; }
  int16_t tileW() const { return tileW_; }
  int16_t tileH() const { return tileH_; }
  uint16_t* pixels() { return pixels_; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void writePixel(int16_t x, int16_t y, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override;
  void fillScreen(uint16_t color) override;

 private:
  int16_t tileX_ = 0;
  int16_t tileY_ = 0;
  int16_t tileW_ = 0;
  int16_t tileH_ = 0;
  uint16_t pixels_[MAX_TILE * MAX_TILE];
};

using TileRenderCallback = void (*)(TileCanvas& canvas, void* context);

// Recompose the full 240x240 scene tile-by-tile. The visible LCD contents are
// never erased first; each tile is replaced only after its final pixels exist.
void gfxRenderTiled(TileRenderCallback render, void* context,
                    uint16_t clearColor = 0x0000);
