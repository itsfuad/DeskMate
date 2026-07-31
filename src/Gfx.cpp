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
static_assert(134 + 28 < 181,
              "Boot status badge must not overlap progress markers");

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
  drawSystemFrame(g, c.accent, "SYSTEM START");

  g.drawCircle(120, 74, 19, C_UI_LINE);
  g.drawCircle(120, 74, 12, c.accent);
  g.fillCircle(120, 74, 4, C_UI_CYAN);
  g.fillCircle(137, 74, 2, C_UI_AMBER);

  drawCenteredBounded(g, c.line1[0] ? c.line1 : "DeskMate",
                      101, 196, 3, C_UI_TEXT);
  drawStatusBadge(g, c.line2, c.accent, 134);

  g.fillRoundRect(48, 181, 31, 4, 2, C_UI_CYAN);
  g.fillRoundRect(85, 181, 31, 4, 2, C_UI_BLUE);
  g.fillRoundRect(122, 181, 31, 4, 2, C_UI_VIOLET);
  g.fillRoundRect(159, 181, 31, 4, 2, C_UI_AMBER);
}

TileMask bootDirtyMask(const BootContext& next) {
  if (!lastBootValid) return gfxAllTilesMask();

  TileMask mask = 0;
  if (strcmp(lastBoot.line1, next.line1) != 0)
    gfxMarkRectTiles(mask, 18, 97, 204, 31, 2);
  if (strcmp(lastBoot.line2, next.line2) != 0)
    gfxMarkRectTiles(mask, 18, 132, 204, 34, 2);
  if (lastBoot.accent != next.accent) {
    gfxMarkRectTiles(mask, 8, 8, 224, 7, 1);      // frame accent rail
    gfxMarkRectTiles(mask, 14, 18, 20, 20, 1);    // header status mark
    gfxMarkRectTiles(mask, 98, 52, 44, 44, 2);    // center glyph
    gfxMarkRectTiles(mask, 18, 132, 204, 34, 2);  // badge accent dot
  }
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

struct StaContext {
  char ssid[48];
  char ip[32];
  char url[64];
};

void renderSta(TileCanvas& g, void* raw) {
  const StaContext& c = *static_cast<StaContext*>(raw);
  drawSystemFrame(g, C_UI_GREEN, "NETWORK READY");
  drawCenteredBounded(g, "CONNECTED", 55, 196, 3, C_UI_GREEN);

  g.fillRoundRect(20, 88, 200, 101, 11, C_UI_PANEL2);
  g.drawRoundRect(20, 88, 200, 101, 11, C_UI_LINE);
  g.setTextSize(1);
  g.setTextColor(C_UI_MUTED);
  g.setCursor(32, 99);
  g.print("WIFI");
  drawCenteredBounded(g, c.ssid, 114, 176, 2, C_UI_TEXT);
  drawCenteredBounded(g, c.ip, 157, 176, 2, C_UI_BLUE);
  if (c.url[0]) drawCenteredBounded(g, c.url, 178, 176, 1, C_UI_VIOLET);
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

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  StaContext c = {};
  strlcpy(c.ssid, ssid && ssid[0] ? ssid : "-", sizeof(c.ssid));
  strlcpy(c.ip, ip && ip[0] ? ip : "-", sizeof(c.ip));
  if (host && host[0]) snprintf(c.url, sizeof(c.url), "%s.local", host);
  gfxRenderTiled(renderSta, &c, C_UI_BG);
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
