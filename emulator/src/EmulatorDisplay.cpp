#include "EmulatorDisplay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace EmulatorDisplay {
namespace {
std::array<uint16_t, TFT_WIDTH * TFT_HEIGHT> framebuffer{};

uint32_t rgb565ToArgb(uint16_t color) {
  const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 0x1F);
  const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 0x3F);
  const uint8_t b5 = static_cast<uint8_t>(color & 0x1F);
  const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
  const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
  const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
  return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

void writeLe16(FILE* file, uint16_t value) {
  std::fputc(value & 0xFF, file);
  std::fputc((value >> 8) & 0xFF, file);
}

void writeLe32(FILE* file, uint32_t value) {
  std::fputc(value & 0xFF, file);
  std::fputc((value >> 8) & 0xFF, file);
  std::fputc((value >> 16) & 0xFF, file);
  std::fputc((value >> 24) & 0xFF, file);
}
}  // namespace

void clear(uint16_t color) {
  framebuffer.fill(color);
}

void blit(int x, int y, const uint16_t* pixels, int width, int height,
          int sourceStride) {
  if (!pixels || width <= 0 || height <= 0) return;
  if (sourceStride <= 0) sourceStride = width;

  for (int row = 0; row < height; ++row) {
    const int destinationY = y + row;
    if (destinationY < 0 || destinationY >= TFT_HEIGHT) continue;
    for (int column = 0; column < width; ++column) {
      const int destinationX = x + column;
      if (destinationX < 0 || destinationX >= TFT_WIDTH) continue;
      framebuffer[destinationY * TFT_WIDTH + destinationX] =
          pixels[row * sourceStride + column];
    }
  }
}

uint16_t* data() { return framebuffer.data(); }
const uint16_t* dataConst() { return framebuffer.data(); }
int width() { return TFT_WIDTH; }
int height() { return TFT_HEIGHT; }

std::vector<uint32_t> toArgb8888(int scale) {
  scale = std::max(1, scale);
  const int outputWidth = TFT_WIDTH * scale;
  const int outputHeight = TFT_HEIGHT * scale;
  std::vector<uint32_t> output(
      static_cast<size_t>(outputWidth) * outputHeight);

  for (int y = 0; y < TFT_HEIGHT; ++y) {
    for (int x = 0; x < TFT_WIDTH; ++x) {
      const uint32_t color = rgb565ToArgb(framebuffer[y * TFT_WIDTH + x]);
      for (int sy = 0; sy < scale; ++sy) {
        uint32_t* row = output.data() +
            static_cast<size_t>(y * scale + sy) * outputWidth + x * scale;
        std::fill(row, row + scale, color);
      }
    }
  }
  return output;
}

bool saveBmp(const std::string& path, int scale) {
  scale = std::max(1, scale);
  const int outputWidth = TFT_WIDTH * scale;
  const int outputHeight = TFT_HEIGHT * scale;
  const uint32_t rowBytes = static_cast<uint32_t>(outputWidth * 4);
  const uint32_t pixelBytes = rowBytes * outputHeight;
  const uint32_t fileSize = 14 + 40 + pixelBytes;

  const std::filesystem::path destination(path);
  if (destination.has_parent_path()) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
  }

  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) return false;

  std::fputc('B', file);
  std::fputc('M', file);
  writeLe32(file, fileSize);
  writeLe16(file, 0);
  writeLe16(file, 0);
  writeLe32(file, 54);

  writeLe32(file, 40);
  writeLe32(file, static_cast<uint32_t>(outputWidth));
  writeLe32(file, static_cast<uint32_t>(outputHeight));
  writeLe16(file, 1);
  writeLe16(file, 32);
  writeLe32(file, 0);
  writeLe32(file, pixelBytes);
  writeLe32(file, 2835);
  writeLe32(file, 2835);
  writeLe32(file, 0);
  writeLe32(file, 0);

  for (int outputY = outputHeight - 1; outputY >= 0; --outputY) {
    const int sourceY = outputY / scale;
    for (int outputX = 0; outputX < outputWidth; ++outputX) {
      const int sourceX = outputX / scale;
      const uint16_t color = framebuffer[sourceY * TFT_WIDTH + sourceX];
      const uint8_t r5 = static_cast<uint8_t>((color >> 11) & 0x1F);
      const uint8_t g6 = static_cast<uint8_t>((color >> 5) & 0x3F);
      const uint8_t b5 = static_cast<uint8_t>(color & 0x1F);
      const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
      const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
      const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
      std::fputc(b, file);
      std::fputc(g, file);
      std::fputc(r, file);
      std::fputc(0xFF, file);
    }
  }

  const bool ok = std::fclose(file) == 0;
  return ok;
}

}  // namespace EmulatorDisplay
