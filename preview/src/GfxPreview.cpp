#include "Gfx.h"

#include <cstring>

Arduino_GFX* gfxDev() { return nullptr; }

void gfxBegin(const Settings&) {}
void gfxSetBrightness(uint8_t, bool) {}
void gfxSetRotation(uint8_t) {}

int gfxTextW(const char* text, uint8_t size) {
  return text ? static_cast<int>(std::strlen(text)) * 6 * size : 0;
}

void gfxDrawCentered(const char*, int, uint8_t, uint16_t) {}

uint8_t gfxFitSize(const char* text, int maxWidth, uint8_t maximumSize) {
  if (!text || !text[0]) return 1;
  for (uint8_t size = maximumSize; size > 1; --size) {
    if (gfxTextW(text, size) <= maxWidth) return size;
  }
  return 1;
}

void gfxBoot(const char*, const char*) {}
void gfxApInfo(const char*, const char*, const char*) {}
void gfxMessage(const char*, const char*, uint16_t) {}
void gfxCrash(const char*, const char*, const char*) {}
void gfxFirmwareUpdate(GfxFirmwareState, const char*, uint32_t, uint32_t,
                       const char*) {}
void gfxFirmwareUpdateReset() {}
