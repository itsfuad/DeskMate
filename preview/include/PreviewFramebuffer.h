#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

namespace PreviewFramebuffer {

void clear(uint16_t color = 0);
void blit(int x, int y, const uint16_t* pixels, int width, int height,
          int sourceStride = 0);
uint16_t* data();
const uint16_t* dataConst();
int width();
int height();

bool saveBmp(const std::string& path, int scale = 1);
std::vector<uint32_t> toArgb8888(int scale = 1);

}  // namespace PreviewFramebuffer
