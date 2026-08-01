#pragma once

#include <Arduino.h>
#include "config.h"

// The ST7789 panel is 240 x 240, but the built-in Adafruit_GFX font is eight
// pixels high at text size 1. Keeping all authored UI inside this inset avoids
// glyphs, rounded cards, and status dots being clipped by the physical edge or
// by panels whose visible area is a pixel or two smaller than advertised.
namespace DisplayLayout {
constexpr int16_t Left = 8;
constexpr int16_t Top = 8;
constexpr int16_t Right = TFT_WIDTH - 8;      // exclusive
constexpr int16_t Bottom = TFT_HEIGHT - 8;    // exclusive
constexpr int16_t Width = Right - Left;
constexpr int16_t Height = Bottom - Top;

constexpr int16_t textHeight(uint8_t size) {
  return static_cast<int16_t>(8 * size);
}

constexpr bool fits(int16_t x, int16_t y, int16_t w, int16_t h) {
  return x >= 0 && y >= 0 && w >= 0 && h >= 0 &&
         x + w <= TFT_WIDTH && y + h <= TFT_HEIGHT;
}

constexpr bool fitsSafe(int16_t x, int16_t y, int16_t w, int16_t h) {
  return x >= Left && y >= Top && w >= 0 && h >= 0 &&
         x + w <= Right && y + h <= Bottom;
}
}  // namespace DisplayLayout
