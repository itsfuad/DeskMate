#pragma once

#include "Gfx.h"
#include "TileRenderer.h"

#include <cstring>

namespace FirmwareUi {

struct Context {
  GfxFirmwareState state = GfxFirmwareState::Preparing;
  char artifact[42] = "";
  char detail[48] = "";
  uint32_t written = 0;
  uint32_t total = 0;
};

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

inline const char* stateLabel(GfxFirmwareState state) {
  switch (state) {
    case GfxFirmwareState::Preparing: return "PREPARING";
    case GfxFirmwareState::Downloading: return "DOWNLOADING";
    case GfxFirmwareState::Writing: return "WRITING FLASH";
    case GfxFirmwareState::Verifying: return "VERIFYING IMAGE";
    case GfxFirmwareState::Complete: return "UPDATE COMPLETE";
    case GfxFirmwareState::Current: return "ALREADY CURRENT";
    case GfxFirmwareState::Failed: return "UPDATE FAILED";
  }
  return "FIRMWARE UPDATE";
}

inline uint16_t stateAccent(GfxFirmwareState state) {
  switch (state) {
    case GfxFirmwareState::Preparing: return C_UI_BLUE;
    case GfxFirmwareState::Downloading: return C_UI_CYAN;
    case GfxFirmwareState::Writing: return C_UI_AMBER;
    case GfxFirmwareState::Verifying: return C_UI_VIOLET;
    case GfxFirmwareState::Complete: return C_UI_GREEN;
    case GfxFirmwareState::Current: return C_UI_GREEN;
    case GfxFirmwareState::Failed: return C_UI_ROSE;
  }
  return C_UI_BLUE;
}

inline void formatBytes(uint32_t written, uint32_t total, char* output,
                        size_t outputSize) {
  if (!outputSize) return;
  if (total) {
    snprintf(output, outputSize, "%lu / %lu KiB",
             static_cast<unsigned long>((written + 1023UL) / 1024UL),
             static_cast<unsigned long>((total + 1023UL) / 1024UL));
  } else if (written) {
    snprintf(output, outputSize, "%lu KiB received",
             static_cast<unsigned long>((written + 1023UL) / 1024UL));
  } else {
    strlcpy(output, "Waiting for update data", outputSize);
  }
}

inline void render(TileCanvas& g, void* raw) {
  const Context& context = *static_cast<Context*>(raw);
  const uint16_t accent = stateAccent(context.state);
  const uint8_t percent = context.total
      ? static_cast<uint8_t>(min<uint32_t>(
            100UL, (static_cast<uint64_t>(context.written) * 100ULL) /
                       context.total))
      : 0;

  g.fillScreen(C_UI_BG);
  g.setTextWrap(false);
  drawCenteredBounded(g, "FIRMWARE UPDATE", 24, 216, 2, C_UI_TEXT);
  drawCenteredBounded(g, stateLabel(context.state), 53, 208, 1, accent);
  drawCenteredBounded(g,
                      context.artifact[0] ? context.artifact
                                          : "DeskMate firmware",
                      70, 208, 1, C_UI_MUTED);

  char progress[8];
  if (context.state == GfxFirmwareState::Current) {
    strlcpy(progress, "OK", sizeof(progress));
  } else if (context.state == GfxFirmwareState::Complete && !context.total) {
    strlcpy(progress, "100%", sizeof(progress));
  } else if (context.total) {
    snprintf(progress, sizeof(progress), "%u%%", percent);
  } else {
    strlcpy(progress, "...", sizeof(progress));
  }
  drawCenteredBounded(g, progress, 91, 210, 4, C_UI_TEXT);

  constexpr int barX = 20;
  constexpr int barY = 132;
  constexpr int barW = 200;
  constexpr int barH = 10;
  static_assert(barX + barW <= TFT_WIDTH && barY + barH <= TFT_HEIGHT,
                "Firmware progress bar must fit the panel");
  g.fillRoundRect(barX, barY, barW, barH, 5, C_UI_PANEL2);
  g.drawRoundRect(barX, barY, barW, barH, 5, C_UI_LINE);

  if (context.state == GfxFirmwareState::Current ||
      (context.state == GfxFirmwareState::Complete && !context.total)) {
    g.fillRoundRect(barX + 1, barY + 1, barW - 2, barH - 2, 4, accent);
  } else if (context.total) {
    const int fillWidth = static_cast<int>((barW - 2) * percent / 100);
    if (fillWidth >= 8) {
      g.fillRoundRect(barX + 1, barY + 1, fillWidth, barH - 2, 4, accent);
    } else if (fillWidth > 0) {
      g.fillRect(barX + 1, barY + 1, fillWidth, barH - 2, accent);
    }
  } else if (context.state != GfxFirmwareState::Failed) {
    g.fillRoundRect(barX + 72, barY + 1, 56, barH - 2, 4, accent);
  }

  char bytes[36];
  formatBytes(context.written, context.total, bytes, sizeof(bytes));
  drawCenteredBounded(g, context.detail[0] ? context.detail : bytes,
                      151, 210, 1, C_UI_MUTED);

  g.fillRoundRect(16, 177, 208, 43, 10, C_UI_PANEL2);
  g.drawRoundRect(16, 177, 208, 43, 10, accent);
  if (context.state == GfxFirmwareState::Complete) {
    drawCenteredBounded(g, "RESTARTING DESKMATE", 187, 190, 1, C_UI_GREEN);
    drawCenteredBounded(g, "KEEP POWER CONNECTED", 202, 190, 1, C_UI_MUTED);
  } else if (context.state == GfxFirmwareState::Current) {
    drawCenteredBounded(g, "NO UPDATE WAS NEEDED", 187, 190, 1, C_UI_GREEN);
    drawCenteredBounded(g, "RETURNING TO DASHBOARD", 202, 190, 1,
                        C_UI_MUTED);
  } else if (context.state == GfxFirmwareState::Failed) {
    drawCenteredBounded(g, "IMAGE WAS NOT INSTALLED", 187, 190, 1,
                        C_UI_ROSE);
    drawCenteredBounded(g, "CHECK THE WEB PORTAL", 202, 190, 1,
                        C_UI_MUTED);
  } else {
    drawCenteredBounded(g, "DO NOT UNPLUG POWER", 187, 190, 1, C_UI_AMBER);
    drawCenteredBounded(g, "OR RESTART THE DEVICE", 202, 190, 1,
                        C_UI_MUTED);
  }
}

}  // namespace FirmwareUi
