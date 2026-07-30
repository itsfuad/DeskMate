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
  if ((uint16_t)lx >= (uint16_t)tileW_ ||
      (uint16_t)ly >= (uint16_t)tileH_) return;
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

void gfxRenderTiled(TileRenderCallback render, void* context,
                    uint16_t clearColor) {
  Arduino_GFX* out = gfxDev();
  if (!out || !render) return;

  // One static 3.2 KiB RGB565 backbuffer. No heap allocation or fragmentation.
  static TileCanvas canvas;

  for (int16_t y = 0; y < TFT_HEIGHT; y += TileCanvas::MAX_TILE) {
    const int16_t h = (TFT_HEIGHT - y < TileCanvas::MAX_TILE) ? TFT_HEIGHT - y : TileCanvas::MAX_TILE;
    for (int16_t x = 0; x < TFT_WIDTH; x += TileCanvas::MAX_TILE) {
      const int16_t w = (TFT_WIDTH - x < TileCanvas::MAX_TILE) ? TFT_WIDTH - x : TileCanvas::MAX_TILE;
      canvas.beginTile(x, y, w, h, clearColor);
      render(canvas, context);

      // The complete final tile replaces the old LCD pixels in one bitmap push.
      out->draw16bitRGBBitmap(x, y, canvas.pixels(), w, h);
    }
    // Feed Wi-Fi/watchdog only between complete tile rows, never mid-tile.
    yield();
  }
}
