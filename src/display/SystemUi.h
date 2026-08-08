#pragma once

#include "Gfx.h"
#include "TileRenderer.h"

#include <cstring>

namespace SystemUi {

inline void copyEllipsized(const char* source, char* output, size_t outputSize,
                           int maxPixels, uint8_t size) {
  if (!outputSize) return;
  if (!source) source = "";
  const int maxChars = max(1, maxPixels / (6 * size));
  const size_t length = strlen(source);
  if (static_cast<int>(length) <= maxChars) {
    strlcpy(output, source, outputSize);
    return;
  }
  const int keep = max(1, maxChars - 3);
  const size_t copied = min<size_t>(keep, outputSize - 1);
  memcpy(output, source, copied);
  output[copied] = 0;
  if (outputSize - copied > 3) strlcat(output, "...", outputSize);
}

inline void drawCenteredBounded(Adafruit_GFX& g, const char* text, int y,
                                int maxWidth, uint8_t maximumSize,
                                uint16_t color) {
  if (!text) return;
  const uint8_t size = gfxFitSize(text, maxWidth, maximumSize);
  char fitted[48];
  copyEllipsized(text, fitted, sizeof(fitted), maxWidth, size);
  const int width = gfxTextW(fitted, size);
  g.setTextWrap(false);
  g.setTextSize(size);
  g.setTextColor(color);
  g.setCursor(max(0, (TFT_WIDTH - width) / 2), y);
  g.print(fitted);
}

inline void drawFrame(TileCanvas& g, uint16_t accent, const char* eyebrow) {
  g.fillScreen(C_UI_BG);
  g.fillRoundRect(8, 8, 224, 224, 15, C_UI_PANEL);
  g.drawRoundRect(8, 8, 224, 224, 15, C_UI_LINE);
  g.fillCircle(24, 20, 4, accent);
  g.drawCircle(24, 20, 8, C_UI_LINE);
  g.setTextSize(1);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(39, 16);
  char label[24];
  copyEllipsized(eyebrow ? eyebrow : "DESKMATE SYSTEM", label, sizeof(label),
                 124, 1);
  g.print(label);
  g.drawFastHLine(20, 37, 200, C_UI_LINE);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(20, 216); g.print("DESKMATE");
  g.setCursor(198, 216); g.print("240");
}

inline void drawBadge(TileCanvas& g, const char* text, uint16_t accent, int y) {
  if (!text || !text[0]) return;
  char fitted[38];
  copyEllipsized(text, fitted, sizeof(fitted), 166, 1);
  const int width = min(190, gfxTextW(fitted, 1) + 30);
  const int x = (TFT_WIDTH - width) / 2;
  g.fillRoundRect(x, y, width, 28, 8, C_UI_PANEL2);
  g.drawRoundRect(x, y, width, 28, 8, C_UI_LINE);
  g.fillCircle(x + 13, y + 14, 3, accent);
  g.setTextSize(1);
  g.setTextColor(C_UI_TEXT);
  g.setCursor(x + 23, y + 10);
  g.print(fitted);
}

inline uint16_t bootAccent(const char* line1, const char* line2) {
  auto contains = [](const char* text, const char* word) {
    if (!text || !word) return false;
    for (; *text; ++text) {
      const char* a = text;
      const char* b = word;
      while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a + 32) : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? static_cast<char>(*b + 32) : *b;
        if (ca != cb) break;
        ++a; ++b;
      }
      if (!*b) return true;
    }
    return false;
  };
  if (contains(line1, "fail") || contains(line2, "fail") ||
      contains(line1, "crash") || contains(line2, "crash")) return C_UI_ROSE;
  if (contains(line1, "update") || contains(line2, "update")) return C_UI_AMBER;
  if (contains(line1, "network") || contains(line2, "network") ||
      contains(line1, "wifi") || contains(line2, "wifi")) return C_UI_BLUE;
  return C_UI_VIOLET;
}

struct BootContext { char line1[48]; char line2[64]; uint16_t accent; };
inline void renderBoot(TileCanvas& g, void* raw) {
  const BootContext& c = *static_cast<BootContext*>(raw);
  g.fillScreen(C_UI_BG);
  drawCenteredBounded(g, c.line1[0] ? c.line1 : "DeskMate", 84, 216, 3, C_UI_TEXT);
  g.fillRoundRect(72, 121, 96, 2, 1, c.accent);
  if (c.line2[0]) drawCenteredBounded(g, c.line2, 143, 208, 1, C_UI_MUTED);
}

struct ApContext { char ssid[48]; char passwordState[28]; char url[48]; };
inline void renderAp(TileCanvas& g, void* raw) {
  const ApContext& c = *static_cast<ApContext*>(raw);
  drawFrame(g, C_UI_AMBER, "NETWORK SETUP");
  drawCenteredBounded(g, "SETUP MODE", 47, 196, 3, C_UI_AMBER);
  g.fillRoundRect(20, 76, 200, 113, 11, C_UI_PANEL2);
  g.drawRoundRect(20, 76, 200, 113, 11, C_UI_LINE);
  g.setTextSize(1); g.setTextColor(C_UI_MUTED); g.setCursor(32, 88); g.print("JOIN WIFI");
  drawCenteredBounded(g, c.ssid, 104, 176, 2, C_UI_TEXT);
  drawCenteredBounded(g, c.passwordState, 136, 176, 1, C_UI_MUTED);
  drawCenteredBounded(g, c.url, 157, 176, 2, C_UI_BLUE);
}

struct MessageContext { char title[48]; char message[64]; uint16_t accent; };
inline void renderMessage(TileCanvas& g, void* raw) {
  const MessageContext& c = *static_cast<MessageContext*>(raw);
  drawFrame(g, c.accent, "SYSTEM MESSAGE");
  drawCenteredBounded(g, c.title, 72, 196, 3, c.accent);
  drawBadge(g, c.message, c.accent, 115);
}

struct CrashContext { char epc[24]; char addr[24]; char ip[32]; };
inline void renderCrash(TileCanvas& g, void* raw) {
  const CrashContext& c = *static_cast<CrashContext*>(raw);
  drawFrame(g, C_UI_ROSE, "SAFE MODE");
  drawCenteredBounded(g, "RECOVERY", 47, 196, 3, C_UI_ROSE);
  g.fillRoundRect(20, 76, 200, 114, 11, C_UI_PANEL2);
  g.drawRoundRect(20, 76, 200, 114, 11, C_UI_LINE);
  g.setTextSize(1); g.setTextColor(C_UI_MUTED); g.setCursor(32, 89); g.print("EPC");
  g.setTextColor(C_UI_TEXT); g.setCursor(75, 89); g.print(c.epc);
  g.setTextColor(C_UI_MUTED); g.setCursor(32, 115); g.print("ADDR");
  g.setTextColor(C_UI_TEXT); g.setCursor(75, 115); g.print(c.addr);
  g.setTextColor(C_UI_MUTED); g.setCursor(32, 143); g.print("OTA RECOVERY");
  drawCenteredBounded(g, c.ip, 159, 176, 2, C_UI_BLUE);
}

}  // namespace SystemUi
