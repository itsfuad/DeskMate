#include "Gfx.h"
#include "Platform.h"
#include "TileRenderer.h"
#include "FirmwareUi.h"
#include "SystemUi.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>

// The DeskMate's ST7789 has its CS line tied to GND and only latches SPI in
// **mode 3**. Arduino_GFX's stock Arduino_ST7789 forces SPI_MODE2 on the ESP8266
// (wrong clock edge for this panel), so the controller never initializes and the
// screen stays black even with the backlight on. Subclass begin() to force mode 3
// — matching the known-good DeskMate-compatible firmwares. (On ESP32 the base
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
void revealBacklight() {
  if (backlightRevealed) return;
  backlightRevealed = true;
  gfxSetBrightness(bootBrightness, bootBacklightInverted);
}

SystemUi::BootContext lastBoot = {};
bool lastBootValid = false;

TileMask bootDirtyMask(const SystemUi::BootContext& next) {
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

using FirmwareContext = FirmwareUi::Context;
FirmwareContext lastFirmware = {};
bool lastFirmwareValid = false;

void renderFirmware(TileCanvas& g, void* raw) {
  FirmwareUi::render(g, raw);
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
  SystemUi::BootContext next = {};
  strlcpy(next.line1, line1 && line1[0] ? line1 : "DeskMate",
          sizeof(next.line1));
  strlcpy(next.line2, line2 ? line2 : "", sizeof(next.line2));
  next.accent = SystemUi::bootAccent(next.line1, next.line2);

  const TileMask mask = bootDirtyMask(next);
  if (mask) gfxRenderTileMask(SystemUi::renderBoot, &next, C_UI_BG, mask);
  lastBoot = next;
  lastBootValid = true;
  revealBacklight();
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  SystemUi::ApContext c = {};
  strlcpy(c.ssid, ssid && ssid[0] ? ssid : "DeskMate-Setup", sizeof(c.ssid));
  strlcpy(c.passwordState, pass && pass[0] ? "Password configured" : "Open network",
          sizeof(c.passwordState));
  snprintf(c.url, sizeof(c.url), "http://%s", ip && ip[0] ? ip : "192.168.4.1");
  gfxRenderTiled(SystemUi::renderAp, &c, C_UI_BG);
  revealBacklight();
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  SystemUi::MessageContext c = {};
  strlcpy(c.title, title && title[0] ? title : "DeskMate", sizeof(c.title));
  strlcpy(c.message, msg ? msg : "", sizeof(c.message));
  c.accent = titleColor == C_RED ? C_UI_ROSE : titleColor;
  gfxRenderTiled(SystemUi::renderMessage, &c, C_UI_BG);
  revealBacklight();
}

void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  SystemUi::CrashContext c = {};
  strlcpy(c.epc, epc && epc[0] ? epc : "-", sizeof(c.epc));
  strlcpy(c.addr, addr && addr[0] ? addr : "-", sizeof(c.addr));
  strlcpy(c.ip, ip && ip[0] ? ip : "-", sizeof(c.ip));
  gfxRenderTiled(SystemUi::renderCrash, &c, C_UI_BG);
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
