#include "TileRenderer.h"

#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "config.h"

TileCanvas::TileCanvas() : Adafruit_GFX(TFT_WIDTH, TFT_HEIGHT) {}

void TileCanvas::beginTile(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t clearColor) {
  tileX_ = x;
  tileY_ = y;
  tileW_ = constrain(w, 0, MAX_TILE);
  tileH_ = constrain(h, 0, MAX_TILE);
  fillScreen(clearColor);
  setCursor(0, 0);
  setTextWrap(false);
}

void TileCanvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  const int16_t lx = x - tileX_;
  const int16_t ly = y - tileY_;
  if (static_cast<uint16_t>(lx) >= static_cast<uint16_t>(tileW_) ||
      static_cast<uint16_t>(ly) >= static_cast<uint16_t>(tileH_)) return;
  pixels_[ly * tileW_ + lx] = color;
}

void TileCanvas::writePixel(int16_t x, int16_t y, uint16_t color) {
  drawPixel(x, y, color);
}

void TileCanvas::drawFastHLine(int16_t x, int16_t y, int16_t w,
                               uint16_t color) {
  if (w <= 0 || y < tileY_ || y >= tileY_ + tileH_) return;
  int16_t x0 = (x > tileX_) ? x : tileX_;
  int16_t x1 = (x + w < tileX_ + tileW_) ? x + w : tileX_ + tileW_;
  if (x1 <= x0) return;
  uint16_t* p = pixels_ + (y - tileY_) * tileW_ + (x0 - tileX_);
  while (x0++ < x1) *p++ = color;
}

void TileCanvas::drawFastVLine(int16_t x, int16_t y, int16_t h,
                               uint16_t color) {
  if (h <= 0 || x < tileX_ || x >= tileX_ + tileW_) return;
  int16_t y0 = (y > tileY_) ? y : tileY_;
  int16_t y1 = (y + h < tileY_ + tileH_) ? y + h : tileY_ + tileH_;
  if (y1 <= y0) return;
  uint16_t* p = pixels_ + (y0 - tileY_) * tileW_ + (x - tileX_);
  while (y0++ < y1) {
    *p = color;
    p += tileW_;
  }
}

void TileCanvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
  if (w <= 0 || h <= 0) return;
  int16_t x0 = (x > tileX_) ? x : tileX_;
  int16_t y0 = (y > tileY_) ? y : tileY_;
  int16_t x1 = (x + w < tileX_ + tileW_) ? x + w : tileX_ + tileW_;
  int16_t y1 = (y + h < tileY_ + tileH_) ? y + h : tileY_ + tileH_;
  if (x1 <= x0 || y1 <= y0) return;
  for (int16_t yy = y0; yy < y1; ++yy) {
    uint16_t* p = pixels_ + (yy - tileY_) * tileW_ + (x0 - tileX_);
    for (int16_t xx = x0; xx < x1; ++xx) *p++ = color;
  }
}

void TileCanvas::fillScreen(uint16_t color) {
  const int count = tileW_ * tileH_;
  for (int i = 0; i < count; ++i) pixels_[i] = color;
}

void gfxMarkPointTiles(TileMask& mask, int16_t x, int16_t y,
                       int16_t padding) {
  int16_t x0 = constrain(x - padding, 0, TFT_WIDTH - 1);
  int16_t y0 = constrain(y - padding, 0, TFT_HEIGHT - 1);
  int16_t x1 = constrain(x + padding, 0, TFT_WIDTH - 1);
  int16_t y1 = constrain(y + padding, 0, TFT_HEIGHT - 1);
  const int16_t col0 = x0 / TileCanvas::MAX_TILE;
  const int16_t row0 = y0 / TileCanvas::MAX_TILE;
  const int16_t col1 = x1 / TileCanvas::MAX_TILE;
  const int16_t row1 = y1 / TileCanvas::MAX_TILE;
  for (int16_t row = row0; row <= row1; ++row) {
    for (int16_t col = col0; col <= col1; ++col) {
      const int16_t index = row * TileCanvas::COLS + col;
      mask |= static_cast<TileMask>(1) << index;
    }
  }
}

void gfxMarkRectTiles(TileMask& mask, int16_t x, int16_t y,
                      int16_t w, int16_t h, int16_t padding) {
  if (w <= 0 || h <= 0) return;
  int16_t x0 = constrain(x - padding, 0, TFT_WIDTH - 1);
  int16_t y0 = constrain(y - padding, 0, TFT_HEIGHT - 1);
  int16_t x1 = constrain(x + w - 1 + padding, 0, TFT_WIDTH - 1);
  int16_t y1 = constrain(y + h - 1 + padding, 0, TFT_HEIGHT - 1);
  const int16_t col0 = x0 / TileCanvas::MAX_TILE;
  const int16_t row0 = y0 / TileCanvas::MAX_TILE;
  const int16_t col1 = x1 / TileCanvas::MAX_TILE;
  const int16_t row1 = y1 / TileCanvas::MAX_TILE;
  for (int16_t row = row0; row <= row1; ++row) {
    for (int16_t col = col0; col <= col1; ++col) {
      const int16_t index = row * TileCanvas::COLS + col;
      mask |= static_cast<TileMask>(1) << index;
    }
  }
}

void gfxMarkLineTiles(TileMask& mask, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, int16_t padding) {
  // Bresenham walk: marking every pixel is cheap here (radar rays are <=112 px)
  // and guarantees that a line grazing a tile corner marks every affected tile.
  int16_t dx = abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  for (;;) {
    gfxMarkPointTiles(mask, x0, y0, padding);
    if (x0 == x1 && y0 == y1) break;
    const int16_t e2 = static_cast<int16_t>(2 * err);
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void gfxRenderTileMask(TileRenderCallback render, void* context,
                       uint16_t clearColor, TileMask mask) {
  Arduino_GFX* out = gfxDev();
  if (!out || !render || !mask) return;

  // One static 3.2 KiB RGB565 backbuffer. No runtime allocation/fragmentation.
  static TileCanvas canvas;

  for (int16_t row = 0; row < TileCanvas::ROWS; ++row) {
    bool wroteRow = false;
    const int16_t y = row * TileCanvas::MAX_TILE;
    const int16_t remainingH = TFT_HEIGHT - y;
    const int16_t h = remainingH < TileCanvas::MAX_TILE
        ? remainingH : TileCanvas::MAX_TILE;
    for (int16_t col = 0; col < TileCanvas::COLS; ++col) {
      const int16_t index = row * TileCanvas::COLS + col;
      if ((mask & (static_cast<TileMask>(1) << index)) == 0) continue;
      const int16_t x = col * TileCanvas::MAX_TILE;
      const int16_t remainingW = TFT_WIDTH - x;
      const int16_t w = remainingW < TileCanvas::MAX_TILE
          ? remainingW : TileCanvas::MAX_TILE;
      canvas.beginTile(x, y, w, h, clearColor);
      render(canvas, context);
      out->draw16bitRGBBitmap(x, y, canvas.pixels(), w, h);
      wroteRow = true;
    }
    // Feed Wi-Fi/watchdog only between complete rows, never during a tile push.
    if (wroteRow) yield();
  }
}

void gfxRenderTiled(TileRenderCallback render, void* context,
                    uint16_t clearColor) {
  gfxRenderTileMask(render, context, clearColor, gfxAllTilesMask());
}
