#include "Gfx.h"
#include "SystemUi.h"
#include "FirmwareUi.h"

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

void gfxBoot(const char* line1, const char* line2) {
  SystemUi::BootContext context = {};
  strlcpy(context.line1, line1 && line1[0] ? line1 : "DeskMate", sizeof(context.line1));
  strlcpy(context.line2, line2 ? line2 : "", sizeof(context.line2));
  context.accent = SystemUi::bootAccent(context.line1, context.line2);
  gfxRenderTiled(SystemUi::renderBoot, &context, C_UI_BG);
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  SystemUi::ApContext context = {};
  strlcpy(context.ssid, ssid && ssid[0] ? ssid : "DeskMate-Setup", sizeof(context.ssid));
  strlcpy(context.passwordState, pass && pass[0] ? "Password configured" : "Open network",
          sizeof(context.passwordState));
  std::snprintf(context.url, sizeof(context.url), "http://%s", ip && ip[0] ? ip : "192.168.4.1");
  gfxRenderTiled(SystemUi::renderAp, &context, C_UI_BG);
}

void gfxMessage(const char* title, const char* message, uint16_t titleColor) {
  SystemUi::MessageContext context = {};
  strlcpy(context.title, title && title[0] ? title : "DeskMate", sizeof(context.title));
  strlcpy(context.message, message ? message : "", sizeof(context.message));
  context.accent = titleColor == C_RED ? C_UI_ROSE : titleColor;
  gfxRenderTiled(SystemUi::renderMessage, &context, C_UI_BG);
}

void gfxCrash(const char* epc, const char* addr, const char* ip) {
  SystemUi::CrashContext context = {};
  strlcpy(context.epc, epc && epc[0] ? epc : "-", sizeof(context.epc));
  strlcpy(context.addr, addr && addr[0] ? addr : "-", sizeof(context.addr));
  strlcpy(context.ip, ip && ip[0] ? ip : "-", sizeof(context.ip));
  gfxRenderTiled(SystemUi::renderCrash, &context, C_UI_BG);
}

void previewRenderBoot(const char* line1, const char* line2) {
  gfxBoot(line1, line2);
}

void previewRenderAp(const char* ssid, const char* password, const char* ip) {
  gfxApInfo(ssid, password, ip);
}

void previewRenderMessage(const char* title, const char* message,
                          uint16_t color) {
  gfxMessage(title, message, color);
}

void previewRenderCrash(const char* epc, const char* addr, const char* ip) {
  gfxCrash(epc, addr, ip);
}
void gfxFirmwareUpdate(GfxFirmwareState, const char*, uint32_t, uint32_t,
                       const char*) {}
void gfxFirmwareUpdateReset() {}
