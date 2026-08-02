#pragma once

#include <Arduino.h>
#include "TileRenderer.h"

// A tiny time-based double heartbeat LED status dot.
namespace StatusDot {
constexpr uint16_t PERIOD_MS = 1200;

inline bool onAt(uint32_t now, uint32_t epoch) {
  const uint16_t phase = static_cast<uint16_t>((now - epoch) % PERIOD_MS);
  return phase < 105 || (phase >= 205 && phase < 310);
}

inline uint16_t scaleRgb565(uint16_t color, uint8_t scale) {
  const uint16_t r = static_cast<uint16_t>((color >> 11) & 0x1F);
  const uint16_t g = static_cast<uint16_t>((color >> 5) & 0x3F);
  const uint16_t b = static_cast<uint16_t>(color & 0x1F);
  return static_cast<uint16_t>(
      ((r * scale / 255U) << 11) |
      ((g * scale / 255U) << 5) |
      (b * scale / 255U));
}

inline void draw(TileCanvas& g, int16_t x, int16_t y, uint16_t color,
                 bool on, bool solidBusy = false) {
  const uint16_t bezel = scaleRgb565(color, 42);
  const uint16_t off = scaleRgb565(color, 72);

  // Dark bezel + one solid fill. No pulse ring and no changing radius.
  g.fillCircle(x, y, 4, bezel);
  g.fillCircle(x, y, 3, (solidBusy || on) ? color : off);
  if (solidBusy || on) g.drawPixel(x - 1, y - 1, scaleRgb565(color, 245));
}
}  // namespace StatusDot
