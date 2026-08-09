#include "Gfx.h"
#include "FirmwareUi.h"
#include "SystemUi.h"
#include "TileRenderer.h"
#include "EmulatorDisplay.h"
#include "EmulatorPlatform.h"
#include <Arduino_GFX_Library.h>

#include <cstring>

namespace {
uint8_t brightness = 100;
uint8_t rotation = 0;

template <typename Context>
void render(void (*callback)(TileCanvas&, void*), Context& context) {
  gfxRenderTiled(callback, &context, C_UI_BG);
}
}

Arduino_GFX* gfxDev() { return nullptr; }

void gfxBegin(const Settings& settings) {
  EmulatorDisplay::clear(C_BLACK);
  brightness = settings.brightness;
  rotation = settings.rotation & 3;
}
void gfxSetBrightness(uint8_t value, bool) { brightness = constrain(value, 0, 100); }
void gfxSetRotation(uint8_t value) { rotation = value & 3; }

int gfxTextW(const char* text, uint8_t size) {
  return text ? static_cast<int>(std::strlen(text)) * 6 * size : 0;
}
void gfxDrawCentered(const char*, int, uint8_t, uint16_t) {}
uint8_t gfxFitSize(const char* text, int maxWidth, uint8_t maximumSize) {
  if (!text || !text[0]) return 1;
  for (uint8_t size = maximumSize; size > 1; --size)
    if (gfxTextW(text, size) <= maxWidth) return size;
  return 1;
}

void gfxBoot(const char* line1, const char* line2) {
  SystemUi::BootContext context{};
  strlcpy(context.line1, line1 && line1[0] ? line1 : "DeskMate", sizeof(context.line1));
  strlcpy(context.line2, line2 ? line2 : "", sizeof(context.line2));
  context.accent = SystemUi::bootAccent(context.line1, context.line2);
  render(SystemUi::renderBoot, context);
}
void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  SystemUi::ApContext context{};
  strlcpy(context.ssid, ssid && ssid[0] ? ssid : "DeskMate-Setup", sizeof(context.ssid));
  strlcpy(context.passwordState, pass && pass[0] ? "Password configured" : "Open network",
          sizeof(context.passwordState));
  std::snprintf(context.url, sizeof(context.url), "http://%s:%u",
                ip && ip[0] ? ip : "127.0.0.1", emulatorWebPort());
  render(SystemUi::renderAp, context);
}
void gfxMessage(const char* title, const char* message, uint16_t titleColor) {
  SystemUi::MessageContext context{};
  strlcpy(context.title, title && title[0] ? title : "DeskMate", sizeof(context.title));
  strlcpy(context.message, message ? message : "", sizeof(context.message));
  context.accent = titleColor == C_RED ? C_UI_ROSE : titleColor;
  render(SystemUi::renderMessage, context);
}
void gfxCrash(const char* epc, const char* addr, const char* ip) {
  SystemUi::CrashContext context{};
  strlcpy(context.epc, epc && epc[0] ? epc : "-", sizeof(context.epc));
  strlcpy(context.addr, addr && addr[0] ? addr : "-", sizeof(context.addr));
  strlcpy(context.ip, ip && ip[0] ? ip : "-", sizeof(context.ip));
  render(SystemUi::renderCrash, context);
}
void gfxFirmwareUpdate(GfxFirmwareState state, const char* artifact,
                       uint32_t writtenBytes, uint32_t totalBytes,
                       const char* detail) {
  FirmwareUi::Context context{};
  context.state = state;
  context.written = writtenBytes;
  context.total = totalBytes;
  strlcpy(context.artifact, artifact && artifact[0] ? artifact : "DeskMate firmware",
          sizeof(context.artifact));
  strlcpy(context.detail, detail ? detail : "", sizeof(context.detail));
  render(FirmwareUi::render, context);
}
void gfxFirmwareUpdateReset() {}
