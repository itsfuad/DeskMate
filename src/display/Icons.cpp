#include "Icons.h"
#include <Adafruit_GFX.h>
#include <math.h>

namespace {
// kIconMetaStride bytes per icon: offset high, offset low, width, height,
// rotation frames.
inline uint16_t metaOffset(uint8_t index) {
  return static_cast<uint16_t>(
             pgm_read_byte(&kIconMeta[index * kIconMetaStride])) << 8 |
         pgm_read_byte(&kIconMeta[index * kIconMetaStride + 1]);
}

inline uint8_t metaByte(uint8_t index, uint8_t field) {
  return pgm_read_byte(&kIconMeta[index * kIconMetaStride + field]);
}

inline bool valid(Icon id, uint8_t& index) {
  index = static_cast<uint8_t>(id);
  return index < kIconCount;
}
}  // namespace

uint8_t gfxIconW(Icon id) {
  uint8_t index;
  if (!valid(id, index)) return 0;
  return metaByte(index, 2);
}

uint8_t gfxIconH(Icon id) {
  uint8_t index;
  if (!valid(id, index)) return 0;
  return metaByte(index, 3);
}

uint8_t gfxIconFrames(Icon id) {
  uint8_t index;
  if (!valid(id, index)) return 0;
  const uint8_t frames = metaByte(index, 4);
  return frames ? frames : 1;
}

void gfxDrawIcon(Adafruit_GFX& g, Icon id, int16_t x, int16_t y,
                 uint16_t color) {
  uint8_t index;
  if (!valid(id, index)) return;
  const uint8_t w = metaByte(index, 2);
  const uint8_t h = metaByte(index, 3);
  // drawBitmap reads the mask with pgm_read_byte and skips clear bits, so the
  // flash-resident glyph needs no RAM copy and leaves the background intact.
  g.drawBitmap(x, y, kIconBits + metaOffset(index), w, h, color);
}

void gfxDrawIconCentered(Adafruit_GFX& g, Icon id, int16_t cx, int16_t cy,
                         uint16_t color) {
  uint8_t index;
  if (!valid(id, index)) return;
  const uint8_t w = metaByte(index, 2);
  const uint8_t h = metaByte(index, 3);
  gfxDrawIcon(g, id, cx - w / 2, cy - h / 2, color);
}

void gfxDrawIconRotated(Adafruit_GFX& g, Icon id, int16_t cx, int16_t cy,
                        float degrees, uint16_t color) {
  uint8_t index;
  if (!valid(id, index)) return;
  const uint8_t w = metaByte(index, 2);
  const uint8_t h = metaByte(index, 3);
  const uint8_t frames = gfxIconFrames(id);
  if (frames < 2) {
    gfxDrawIcon(g, id, cx - w / 2, cy - h / 2, color);
    return;
  }

  // Wrap into [0, 360) before quantizing so a negative or over-wound heading
  // still selects a sane frame.
  float wrapped = fmodf(degrees, 360.0f);
  if (wrapped < 0.0f) wrapped += 360.0f;
  uint8_t frame = static_cast<uint8_t>(
      lroundf(wrapped * frames / 360.0f)) % frames;

  const uint16_t stride = static_cast<uint16_t>((w + 7) / 8) * h;
  g.drawBitmap(cx - w / 2, cy - h / 2,
               kIconBits + metaOffset(index) + frame * stride, w, h, color);
}
