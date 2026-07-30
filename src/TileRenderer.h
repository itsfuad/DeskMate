#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>

// A small RGB565 backbuffer that presents full-screen coordinates to ordinary
// Adafruit_GFX drawing code. Pixels outside the current tile are clipped.
// A complete tile is composed in RAM before it replaces the corresponding LCD
// pixels, so the user never sees an intermediate clear/draw state.
class TileCanvas : public Adafruit_GFX {
 public:
  static constexpr int16_t MAX_TILE = 40;
  static constexpr int16_t COLS = (240 + MAX_TILE - 1) / MAX_TILE;
  static constexpr int16_t ROWS = (240 + MAX_TILE - 1) / MAX_TILE;
  static constexpr int16_t COUNT = COLS * ROWS;

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

using TileMask = uint64_t;  // 6 x 6 = 36 tiles on the 240 x 240 panel.
using TileRenderCallback = void (*)(TileCanvas& canvas, void* context);

constexpr TileMask gfxAllTilesMask() {
  return (TileCanvas::COUNT == 64)
      ? ~static_cast<TileMask>(0)
      : ((static_cast<TileMask>(1) << TileCanvas::COUNT) - 1);
}

// Mark the tile(s) touched by a screen-space point/line. Padding expands the
// mark around the geometry so outlines crossing a tile edge cannot leave stale
// pixels behind.
void gfxMarkPointTiles(TileMask& mask, int16_t x, int16_t y,
                       int16_t padding = 0);
void gfxMarkLineTiles(TileMask& mask, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, int16_t padding = 0);

// Recompose either all tiles or a selected dirty-tile mask. Unselected tiles
// remain untouched in the LCD controller's own display RAM.
void gfxRenderTileMask(TileRenderCallback render, void* context,
                       uint16_t clearColor, TileMask mask);
void gfxRenderTiled(TileRenderCallback render, void* context,
                    uint16_t clearColor = 0x0000);
