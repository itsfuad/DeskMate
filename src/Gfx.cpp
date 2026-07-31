#include "Gfx.h"
#include "Platform.h"
#include "TileRenderer.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>

// The DeskMate's ST7789 has its CS line tied to GND and only latches SPI in
// **mode 3**. Arduino_GFX's stock Arduino_ST7789 forces SPI_MODE2 on the ESP8266
// (wrong clock edge for this panel), so the controller never initializes and the
// screen stays black even with the backlight on. Subclass begin() to force mode 3
// — matching the known-good GeekMagic community firmwares. (On ESP32 the base
// class already selects mode 3, so the override is harmless there.)
class Arduino_ST7789_DeskMate : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;   // inherit constructors
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE3;
    return Arduino_TFT::begin(speed);
  }

#if TFT_BGR
  // This board's panel is wired B-G-R. Arduino_ST7789 hardcodes the MADCTL RGB
  // order, so re-issue MADCTL with the BGR bit (0x08) set on every rotation
  // change. Only rotations 0-3 are used by the DeskMate (setRotation(r & 3)).
  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);           // updates _rotation + width/height
    uint8_t madctl;
    switch (_rotation) {
      case 1:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV; break;
      case 2:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY; break;
      case 3:  madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV; break;
      default: madctl = 0; break;          // case 0
    }
    madctl |= 0x08;                         // BGR
    _bus->beginWrite();
    _bus->writeC8D8(ST7789_MADCTL, madctl);
    _bus->endWrite();
  }
#endif
};

static Arduino_DataBus* bus = nullptr;
static Arduino_GFX*     gfx = nullptr;
static uint8_t          bootBrightness = DEFAULT_BRIGHTNESS;
static bool             bootBacklightInverted = false;
static bool             backlightRevealed = false;

Arduino_GFX* gfxDev() { return gfx; }

// ---------------------------------------------------------------------------
void gfxBegin(const Settings& s) {
#ifdef TFT_PWR_PIN
  // Boards with a switched panel power rail (NM-TV-154): energize the display
  // before anything else or the panel never comes up.
  pinMode(TFT_PWR_PIN, OUTPUT);
  digitalWrite(TFT_PWR_PIN, TFT_PWR_ON);
#endif
  // Keep the backlight dark while the controller is initialized and the first
  // complete tile-rendered boot frame is composed. Revealing the panel only after
  // that frame is on-screen removes the power-on clear/draw flash.
  bootBrightness = s.brightness;
  bootBacklightInverted = s.backlightInverted;
  backlightRevealed = false;
  pinMode(TFT_BL, OUTPUT);
  platformAnalogWriteInit(TFT_BL);
  gfxSetBrightness(0, s.backlightInverted);

#if defined(DESKMATE_ESP32C2) || defined(DESKMATE_ESP32)
  // Hardware SPI via the Arduino SPI library (IDF spi_master driver) on explicit
  // GPIOs. The register-level Arduino_ESP32SPI hangs in begin() on the C2, and
  // Arduino_SWSPI's fast-IO path doesn't cover the C2 — Arduino_HWSPI uses the
  // stock driver (what the working ESPHome config used) and honors SPI mode 3
  // (see the subclass). Pins come from the board header; a TFT_CS of -1 means
  // the panel's CS is tied to GND and is never toggled.
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, &SPI);
#else
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);   // ESP8266 HW-SPI (fixed SCLK/MOSI)
#endif
  // IPS=true so the panel colors are not inverted; full 240x240, no offsets.
  // Use the DeskMate variant so the SPI bus comes up in mode 3 (see class above).
  gfx = new Arduino_ST7789_DeskMate(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/,
                                   TFT_WIDTH, TFT_HEIGHT, 0, 0, 0, 0);
  gfx->begin();
  gfx->setRotation(s.rotation & 3);
  // Nothing in this UI ever wants wrapped text: overflowing labels used to
  // wrap around to x=0 on the next line (stray characters at the left edge).
  gfx->setTextWrap(false);
  gfx->fillScreen(C_BLACK);
}

void gfxSetBrightness(uint8_t pct, bool inverted) {
  if (pct > 100) pct = 100;
  int duty = (int)pct * 255 / 100;
  if (inverted) duty = 255 - duty;
  analogWrite(TFT_BL, duty);
}

void gfxSetRotation(uint8_t r) {
  if (gfx) gfx->setRotation(r & 3);
}

// ---- text helpers (built-in 6x8 font, integer scaled) ---------------------
int gfxTextW(const char* s, uint8_t size) {
  return s ? static_cast<int>(strlen(s)) * 6 * size : 0;
}

void gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  if (!gfx || !s) return;
  int x = (TFT_WIDTH - gfxTextW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Largest size (<= maxSize) whose rendered width fits within maxW.
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize) {
  if (!s || !s[0]) return 1;
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (gfxTextW(s, sz) <= maxW) return sz;
  }
  return 1;
}

// ---- themed boot/status canvas --------------------------------------------
namespace {
constexpr int kFrameX = 8;
constexpr int kFrameY = 8;
constexpr int kFrameW = 224;
constexpr int kFrameH = 224;
constexpr int kContentLeft = 20;
constexpr int kFooterY = 216;

static_assert(kFrameX >= 0 && kFrameY >= 0 &&
              kFrameX + kFrameW <= TFT_WIDTH &&
              kFrameY + kFrameH <= TFT_HEIGHT,
              "System frame must fit the 240x240 panel");
static_assert(20 + 200 <= TFT_WIDTH && 88 + 101 <= kFooterY,
              "Network boot card must not overlap the footer");

void revealBacklight() {
  if (backlightRevealed) return;
  backlightRevealed = true;
  gfxSetBrightness(bootBrightness, bootBacklightInverted);
}

void copyEllipsized(const char* source, char* output, size_t outputSize,
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
  size_t copied = min<size_t>(keep, outputSize - 1);
  memcpy(output, source, copied);
  output[copied] = 0;
  if (outputSize - copied > 3) strlcat(output, "...", outputSize);
}

void drawCenteredBounded(Adafruit_GFX& g, const char* text, int y, int maxW,
                         uint8_t maxSize, uint16_t color) {
  if (!text) return;
  uint8_t size = gfxFitSize(text, maxW, maxSize);
  char fitted[48];
  copyEllipsized(text, fitted, sizeof(fitted), maxW, size);
  const int width = gfxTextW(fitted, size);
  g.setTextWrap(false);
  g.setTextSize(size);
  g.setTextColor(color);
  g.setCursor(max(0, (TFT_WIDTH - width) / 2), y);
  g.print(fitted);
}

void drawSystemFrame(Adafruit_GFX& g, uint16_t accent, const char* eyebrow) {
  g.fillScreen(C_UI_BG);
  g.fillRoundRect(kFrameX, kFrameY, kFrameW, kFrameH, 15, C_UI_PANEL);
  g.drawRoundRect(kFrameX, kFrameY, kFrameW, kFrameH, 15, C_UI_LINE);
  g.fillRoundRect(kFrameX, kFrameY, kFrameW, 5, 3, accent);

  g.fillCircle(24, 28, 4, accent);
  g.drawCircle(24, 28, 8, C_UI_LINE);
  g.setTextSize(1);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(39, 24);
  char label[24];
  copyEllipsized(eyebrow ? eyebrow : "DESKMATE SYSTEM", label, sizeof(label),
                 124, 1);
  g.print(label);

  g.fillRoundRect(178, 24, 10, 5, 2, C_UI_CYAN);
  g.fillRoundRect(191, 24, 10, 5, 2, C_UI_VIOLET);
  g.fillRoundRect(204, 24, 10, 5, 2, C_UI_AMBER);
  g.drawFastHLine(kContentLeft, 45, 200, C_UI_LINE);

  g.setTextColor(C_UI_MUTED);
  g.setCursor(kContentLeft, kFooterY);
  g.print("DESKMATE");
  g.setCursor(198, kFooterY);
  g.print("240");
}

void drawStatusBadge(Adafruit_GFX& g, const char* text, uint16_t accent, int y) {
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

uint16_t bootAccent(const char* line1, const char* line2) {
  String value = String(line1 ? line1 : "") + " " + String(line2 ? line2 : "");
  value.toLowerCase();
  if (value.indexOf("fail") >= 0 || value.indexOf("crash") >= 0) return C_UI_ROSE;
  if (value.indexOf("update") >= 0) return C_UI_AMBER;
  if (value.indexOf("network") >= 0 || value.indexOf("wifi") >= 0) return C_UI_BLUE;
  return C_UI_VIOLET;
}

struct BootContext {
  char line1[48];
  char line2[64];
  uint16_t accent;
};

BootContext lastBoot = {};
bool lastBootValid = false;

void renderBoot(TileCanvas& g, void* raw) {
  const BootContext& c = *static_cast<BootContext*>(raw);

  // Deliberately minimal: one stable screen, one title, one accent rule and
  // one status line. Startup progress only repaints the regions that change.
  g.fillScreen(C_UI_BG);
  drawCenteredBounded(g, c.line1[0] ? c.line1 : "DeskMate",
                      84, 216, 3, C_UI_TEXT);
  g.fillRoundRect(72, 121, 96, 2, 1, c.accent);
  if (c.line2[0])
    drawCenteredBounded(g, c.line2, 143, 208, 1, C_UI_MUTED);
}

TileMask bootDirtyMask(const BootContext& next) {
  if (!lastBootValid) return gfxAllTilesMask();

  TileMask mask = 0;
  if (strcmp(lastBoot.line1, next.line1) != 0)
    gfxMarkRectTiles(mask, 12, 80, 216, 30, 2);
  if (strcmp(lastBoot.line2, next.line2) != 0)
    gfxMarkRectTiles(mask, 12, 138, 216, 18, 2);
  if (lastBoot.accent != next.accent)
    gfxMarkRectTiles(mask, 70, 118, 100, 8, 1);
  return mask;
}

struct ApContext {
  char ssid[48];
  char passwordState[28];
  char url[48];
};

void renderAp(TileCanvas& g, void* raw) {
  const ApContext& c = *static_cast<ApContext*>(raw);
  drawSystemFrame(g, C_UI_AMBER, "NETWORK SETUP");
  drawCenteredBounded(g, "SETUP MODE", 55, 196, 3, C_UI_AMBER);

  g.fillRoundRect(20, 88, 200, 101, 11, C_UI_PANEL2);
  g.drawRoundRect(20, 88, 200, 101, 11, C_UI_LINE);
  g.setTextSize(1);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 99);
  g.print("JOIN WIFI");
  drawCenteredBounded(g, c.ssid, 115, 176, 2, C_UI_TEXT);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 143);
  g.print(c.passwordState);
  drawCenteredBounded(g, c.url, 164, 176, 2, C_UI_BLUE);
}

struct MessageContext {
  char title[48];
  char message[64];
  uint16_t accent;
};

void renderMessage(TileCanvas& g, void* raw) {
  const MessageContext& c = *static_cast<MessageContext*>(raw);
  drawSystemFrame(g, c.accent, "SYSTEM MESSAGE");
  drawCenteredBounded(g, c.title, 82, 196, 3, c.accent);
  drawStatusBadge(g, c.message, c.accent, 127);
}

struct CrashContext {
  char epc[24];
  char addr[24];
  char ip[32];
};

void renderCrash(TileCanvas& g, void* raw) {
  const CrashContext& c = *static_cast<CrashContext*>(raw);
  drawSystemFrame(g, C_UI_ROSE, "SAFE MODE");
  drawCenteredBounded(g, "RECOVERY", 54, 196, 3, C_UI_ROSE);

  g.fillRoundRect(20, 87, 200, 103, 11, C_UI_PANEL2);
  g.drawRoundRect(20, 87, 200, 103, 11, C_UI_LINE);
  g.setTextSize(1);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 100); g.print("EPC");
  g.setTextColor(C_UI_TEXT);
  g.setCursor(75, 100); g.print(c.epc);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 124); g.print("ADDR");
  g.setTextColor(C_UI_TEXT);
  g.setCursor(75, 124); g.print(c.addr);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 150); g.print("OTA RECOVERY");
  drawCenteredBounded(g, c.ip, 166, 176, 2, C_UI_BLUE);
}

struct FirmwareContext {
  GfxFirmwareState state = GfxFirmwareState::Preparing;
  char artifact[42] = "";
  char detail[48] = "";
  uint32_t written = 0;
  uint32_t total = 0;
};

FirmwareContext lastFirmware = {};
bool lastFirmwareValid = false;

const char* firmwareStateLabel(GfxFirmwareState state) {
  switch (state) {
    case GfxFirmwareState::Preparing:   return "PREPARING";
    case GfxFirmwareState::Downloading: return "DOWNLOADING";
    case GfxFirmwareState::Writing:     return "WRITING FLASH";
    case GfxFirmwareState::Verifying:   return "VERIFYING IMAGE";
    case GfxFirmwareState::Complete:    return "UPDATE COMPLETE";
    case GfxFirmwareState::Current:     return "ALREADY CURRENT";
    case GfxFirmwareState::Failed:      return "UPDATE FAILED";
  }
  return "FIRMWARE UPDATE";
}

uint16_t firmwareAccent(GfxFirmwareState state) {
  switch (state) {
    case GfxFirmwareState::Preparing:   return C_UI_BLUE;
    case GfxFirmwareState::Downloading: return C_UI_CYAN;
    case GfxFirmwareState::Writing:     return C_UI_AMBER;
    case GfxFirmwareState::Verifying:   return C_UI_VIOLET;
    case GfxFirmwareState::Complete:    return C_UI_GREEN;
    case GfxFirmwareState::Current:     return C_UI_GREEN;
    case GfxFirmwareState::Failed:      return C_UI_ROSE;
  }
  return C_UI_BLUE;
}

void formatFirmwareBytes(uint32_t written, uint32_t total,
                         char* output, size_t outputSize) {
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

void renderFirmware(TileCanvas& g, void* raw) {
  const FirmwareContext& c = *static_cast<FirmwareContext*>(raw);
  const uint16_t accent = firmwareAccent(c.state);
  const uint8_t percent = c.total
      ? static_cast<uint8_t>(min<uint32_t>(100UL,
          (static_cast<uint64_t>(c.written) * 100ULL) / c.total))
      : 0;

  g.fillScreen(C_UI_BG);
  g.setTextWrap(false);
  drawCenteredBounded(g, "FIRMWARE UPDATE", 24, 216, 2, C_UI_TEXT);
  drawCenteredBounded(g, firmwareStateLabel(c.state), 53, 208, 1, accent);
  drawCenteredBounded(g, c.artifact[0] ? c.artifact : "DeskMate firmware",
                      70, 208, 1, C_UI_MUTED);

  char progress[8];
  if (c.state == GfxFirmwareState::Current)
    strlcpy(progress, "OK", sizeof(progress));
  else if (c.state == GfxFirmwareState::Complete && !c.total)
    strlcpy(progress, "100%", sizeof(progress));
  else if (c.total)
    snprintf(progress, sizeof(progress), "%u%%", percent);
  else
    strlcpy(progress, "...", sizeof(progress));
  drawCenteredBounded(g, progress, 91, 210, 4, C_UI_TEXT);

  constexpr int barX = 20;
  constexpr int barY = 132;
  constexpr int barW = 200;
  constexpr int barH = 10;
  static_assert(barX + barW <= TFT_WIDTH && barY + barH <= TFT_HEIGHT,
                "Firmware progress bar must fit the panel");
  g.fillRoundRect(barX, barY, barW, barH, 5, C_UI_PANEL2);
  g.drawRoundRect(barX, barY, barW, barH, 5, C_UI_LINE);
  if (c.state == GfxFirmwareState::Current ||
      (c.state == GfxFirmwareState::Complete && !c.total)) {
    g.fillRoundRect(barX + 1, barY + 1, barW - 2, barH - 2, 4, accent);
  } else if (c.total) {
    // A known upload size is determinate from the first frame. At 0% leave the
    // track empty; never draw the indeterminate segment in the middle and then
    // jump it to the left when the first chunk arrives.
    const int fillW = static_cast<int>((barW - 2) * percent / 100);
    if (fillW >= 8) {
      g.fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 4, accent);
    } else if (fillW > 0) {
      g.fillRect(barX + 1, barY + 1, fillW, barH - 2, accent);
    }
  } else if (c.state != GfxFirmwareState::Failed) {
    // GitHub release downloads may not expose a content length. Only those use
    // the centered indeterminate marker.
    g.fillRoundRect(barX + 72, barY + 1, 56, barH - 2, 4, accent);
  }

  char bytes[36];
  formatFirmwareBytes(c.written, c.total, bytes, sizeof(bytes));
  drawCenteredBounded(g, c.detail[0] ? c.detail : bytes,
                      151, 210, 1, C_UI_MUTED);

  g.fillRoundRect(16, 177, 208, 43, 10, C_UI_PANEL2);
  g.drawRoundRect(16, 177, 208, 43, 10, accent);
  if (c.state == GfxFirmwareState::Complete) {
    drawCenteredBounded(g, "RESTARTING DESKMATE", 187, 190, 1, C_UI_GREEN);
    drawCenteredBounded(g, "KEEP POWER CONNECTED", 202, 190, 1, C_UI_MUTED);
  } else if (c.state == GfxFirmwareState::Current) {
    drawCenteredBounded(g, "NO UPDATE WAS NEEDED", 187, 190, 1, C_UI_GREEN);
    drawCenteredBounded(g, "RETURNING TO DASHBOARD", 202, 190, 1, C_UI_MUTED);
  } else if (c.state == GfxFirmwareState::Failed) {
    drawCenteredBounded(g, "IMAGE WAS NOT INSTALLED", 187, 190, 1, C_UI_ROSE);
    drawCenteredBounded(g, "CHECK THE WEB PORTAL", 202, 190, 1, C_UI_MUTED);
  } else {
    drawCenteredBounded(g, "DO NOT UNPLUG POWER", 187, 190, 1, C_UI_AMBER);
    drawCenteredBounded(g, "OR RESTART THE DEVICE", 202, 190, 1, C_UI_MUTED);
  }
}

TileMask firmwareDirtyMask(const FirmwareContext& next) {
  if (!lastFirmwareValid) return gfxAllTilesMask();
  TileMask mask = 0;
  if (lastFirmware.state != next.state)
    gfxMarkRectTiles(mask, 12, 48, 216, 22, 2);
  if (strcmp(lastFirmware.artifact, next.artifact) != 0)
    gfxMarkRectTiles(mask, 12, 67, 216, 18, 2);
  if (lastFirmware.written != next.written || lastFirmware.total != next.total ||
      strcmp(lastFirmware.detail, next.detail) != 0)
    gfxMarkRectTiles(mask, 12, 88, 216, 72, 0);
  if (lastFirmware.state != next.state)
    gfxMarkRectTiles(mask, 12, 174, 216, 50, 2);
  return mask;
}
}

void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  BootContext next = {};
  strlcpy(next.line1, line1 && line1[0] ? line1 : "DeskMate",
          sizeof(next.line1));
  strlcpy(next.line2, line2 ? line2 : "", sizeof(next.line2));
  next.accent = bootAccent(next.line1, next.line2);

  const TileMask mask = bootDirtyMask(next);
  if (mask) gfxRenderTileMask(renderBoot, &next, C_UI_BG, mask);
  lastBoot = next;
  lastBootValid = true;
  revealBacklight();
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  ApContext c = {};
  strlcpy(c.ssid, ssid && ssid[0] ? ssid : "DeskMate-Setup", sizeof(c.ssid));
  strlcpy(c.passwordState, pass && pass[0] ? "Password configured" : "Open network",
          sizeof(c.passwordState));
  snprintf(c.url, sizeof(c.url), "http://%s", ip && ip[0] ? ip : "192.168.4.1");
  gfxRenderTiled(renderAp, &c, C_UI_BG);
  revealBacklight();
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  MessageContext c = {};
  strlcpy(c.title, title && title[0] ? title : "DeskMate", sizeof(c.title));
  strlcpy(c.message, msg ? msg : "", sizeof(c.message));
  c.accent = titleColor == C_RED ? C_UI_ROSE : titleColor;
  gfxRenderTiled(renderMessage, &c, C_UI_BG);
  revealBacklight();
}

void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  CrashContext c = {};
  strlcpy(c.epc, epc && epc[0] ? epc : "-", sizeof(c.epc));
  strlcpy(c.addr, addr && addr[0] ? addr : "-", sizeof(c.addr));
  strlcpy(c.ip, ip && ip[0] ? ip : "-", sizeof(c.ip));
  gfxRenderTiled(renderCrash, &c, C_UI_BG);
  revealBacklight();
}

void gfxFirmwareUpdate(GfxFirmwareState state, const char* artifact,
                       uint32_t writtenBytes, uint32_t totalBytes,
                       const char* detail) {
  if (!gfx) return;
  FirmwareContext next = {};
  next.state = state;
  next.written = writtenBytes;
  next.total = totalBytes;
  strlcpy(next.artifact, artifact && artifact[0] ? artifact : "DeskMate firmware",
          sizeof(next.artifact));
  strlcpy(next.detail, detail ? detail : "", sizeof(next.detail));

  const TileMask mask = firmwareDirtyMask(next);
  if (mask) gfxRenderTileMask(renderFirmware, &next, C_UI_BG, mask);
  lastFirmware = next;
  lastFirmwareValid = true;
  revealBacklight();
}

void gfxFirmwareUpdateReset() {
  lastFirmwareValid = false;
}
