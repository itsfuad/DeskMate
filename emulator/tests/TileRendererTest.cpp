#include <cassert>
#include <cstdio>
#include <chrono>

#include "TileRenderer.h"
#include "EmulatorDisplay.h"

namespace {
unsigned visits[TFT_WIDTH * TFT_HEIGHT];
unsigned callbacks;
int lastBlitY = -1;

// Retain the pre-optimization GFX paths as a pixel and timing reference.
class OriginalCanvas : public TileCanvas {
 public:
  using Adafruit_GFX::drawCircle;
  using Adafruit_GFX::fillCircle;
  using Adafruit_GFX::drawRoundRect;
  using Adafruit_GFX::fillRoundRect;
  using Adafruit_GFX::fillTriangle;
  size_t write(uint8_t c) override { return Adafruit_GFX::write(c); }
  void writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                   uint16_t color) override {
    Adafruit_GFX::writeLine(x0, y0, x1, y1, color);
  }
};

template <typename Canvas>
void scene(Canvas& g) {
  g.fillScreen(0x1234);
  for (int y = -10; y < 250; y += 23) {
    g.fillRoundRect(4, y, 231, 20, 6, 0x2345);
    g.drawRoundRect(-2, y, 246, 20, 6, 0x9876);
    for (int x = -8; x < 250; x += 29) {
      g.fillCircle(x, y, 11, 0x3456);
      g.drawCircle(x, y, 14, 0x4567);
      g.fillTriangle(x - 8, y - 5, x + 14, y + 3, x, y + 19, 0x5678);
      g.drawLine(x - 9, y - 12, x + 25, y + 17, 0xabcd);
    }
    g.setTextWrap(false);
    g.setTextSize(1 + (y + 10) % 3);
    g.setTextColor(0xffff, 0);
    g.setCursor(-9, y);
    g.print("Weather 29 C\r\nCloudy");
  }
  // Exercise the GFX fallback for wrapped text, and asymmetric glyph scaling.
  g.setTextWrap(true);
  g.setTextSize(2, 1);
  g.setCursor(227, 230);
  g.print("AB\nC");
}

template <typename Canvas>
long long benchmark(Canvas& g) {
  const auto start = std::chrono::steady_clock::now();
  for (int y = 0; y < TFT_HEIGHT; y += TileCanvas::MAX_HEIGHT) {
    for (int x = 0; x < TFT_WIDTH; x += TileCanvas::MAX_TILE) {
      g.beginTile(x, y, std::min(32, TFT_WIDTH - x), 8);
      scene(g);
    }
  }
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
}

void compareDrawing() {
  TileCanvas fast;
  OriginalCanvas original;
  for (int y = 0; y < TFT_HEIGHT; y += 8) {
    for (int x = 0; x < TFT_WIDTH; x += 32) {
      const int w = std::min(32, TFT_WIDTH - x);
      fast.beginTile(x, y, w, 8);
      original.beginTile(x, y, w, 8);
      scene(fast);
      scene(original);
      for (int i = 0; i < w * 8; ++i)
        assert(fast.pixels()[i] == original.pixels()[i]);
      assert(fast.getCursorX() == original.getCursorX());
      assert(fast.getCursorY() == original.getCursorY());
    }
  }
  const auto before = benchmark(original);
  const auto after = benchmark(fast);
  std::printf("Host synthetic frame: original %lld us, clipped %lld us (%.2fx)\n",
              before, after, double(before) / after);
}

void render(TileCanvas& canvas, void*) {
  assert(canvas.tileW() > 0 && canvas.tileW() <= 32);
  assert(canvas.tileH() > 0 && canvas.tileH() <= 8);
  ++callbacks;
  canvas.fillScreen(0x1234);
  canvas.fillRect(-10, -10, TFT_WIDTH + 20, TFT_HEIGHT + 20, 0x5678);
  canvas.drawFastHLine(-10, canvas.tileY(), TFT_WIDTH + 20, 0xabcd);
  canvas.drawFastVLine(canvas.tileX(), -10, TFT_HEIGHT + 20, 0xabcd);
  // Finish with a coordinate pattern to catch strip stride/offset mistakes.
  for (int y = 0; y < TFT_HEIGHT; ++y)
    for (int x = 0; x < TFT_WIDTH; ++x)
      canvas.drawPixel(x, y, static_cast<uint16_t>(y * TFT_WIDTH + x));
}

void reset() {
  for (auto& count : visits) count = 0;
  callbacks = 0;
  lastBlitY = -1;
}
}

namespace EmulatorDisplay {
void blit(int x, int y, const uint16_t* pixels, int width, int height,
          int stride) {
  assert(y >= lastBlitY);
  lastBlitY = y;
  assert(width > 0 && width <= 32 && height > 0 && height <= 8);
  assert(x >= 0 && y >= 0 && x + width <= TFT_WIDTH && y + height <= TFT_HEIGHT);
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const int index = (y + row) * TFT_WIDTH + x + col;
      assert(pixels[row * stride + col] == static_cast<uint16_t>(index));
      ++visits[index];
    }
  }
}
}

int main() {
  compareDrawing();
  TileCanvas canvas;
  canvas.beginTile(0, 0, 300, 300);
  assert(canvas.tileW() == 32 && canvas.tileH() == 8);
  canvas.fillScreen(0xffff);
  canvas.beginTile(-5, -5, -1, -1);
  assert(canvas.tileW() == 0 && canvas.tileH() == 0);
  canvas.drawPixel(-5, -5, 1);

  reset();
  gfxRenderTiled(render, nullptr);
  assert(callbacks == 240);
  for (unsigned count : visits) assert(count == 1);

  reset();
  gfxRenderTileMask(render, nullptr, 0, TileMask(1) << 63);
  assert(callbacks == 2);
  for (int y = 0; y < TFT_HEIGHT; ++y)
    for (int x = 0; x < TFT_WIDTH; ++x)
      assert(visits[y * TFT_WIDTH + x] == unsigned(x >= 224 && y >= 224));

  reset();
  gfxRenderRegion(render, nullptr, 0, -3, 229, 44, 20);
  assert(callbacks == 4);
  for (int y = 0; y < TFT_HEIGHT; ++y)
    for (int x = 0; x < TFT_WIDTH; ++x)
      assert(visits[y * TFT_WIDTH + x] == unsigned(x < 41 && y >= 229));

  puts("Tile renderer: strip bounds, clipping, stride and exact coverage passed");
}
