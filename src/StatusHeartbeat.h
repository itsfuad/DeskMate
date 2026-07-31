#pragma once

#include <Arduino.h>
#include "TileRenderer.h"

// A low-cost, time-based double heartbeat for tiny status indicators.
//
// The frame is derived from millis() rather than advanced once per loop. A
// blocking DNS/TLS/API operation can therefore skip an intermediate frame, but
// it cannot stretch a 1.2 second heartbeat into a slow multi-second pulse.
namespace StatusHeartbeat {
constexpr uint16_t PERIOD_MS = 1200;

// Four visible transitions followed by a deliberate rest:
//   strong -> release -> smaller beat -> release -> rest
inline uint8_t frameAt(uint32_t now, uint32_t epoch) {
  const uint16_t phase = static_cast<uint16_t>((now - epoch) % PERIOD_MS);
  if (phase < 110) return 0;
  if (phase < 220) return 1;
  if (phase < 330) return 2;
  if (phase < 460) return 3;
  return 4;
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
                 uint8_t frame, uint8_t maximumRadius) {
  const uint16_t dim = scaleRgb565(color, 105);
  const uint16_t soft = scaleRgb565(color, 175);

  // The center remains visible throughout the long rest so it continues to
  // communicate state even when no ring is being animated.
  g.fillCircle(x, y, 2, frame == 4 ? soft : color);

  switch (frame) {
    case 0:
      g.drawCircle(x, y, maximumRadius, color);
      break;
    case 1:
      g.drawCircle(x, y, maximumRadius > 5 ? maximumRadius - 2 : 3, dim);
      break;
    case 2:
      g.drawCircle(x, y, maximumRadius > 4 ? maximumRadius - 1 : 4, color);
      break;
    case 3:
      g.drawCircle(x, y, 3, dim);
      break;
    default:
      break;
  }
}
}  // namespace StatusHeartbeat
