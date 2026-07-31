#include "Gfx.h"
#include "Platform.h"
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

Arduino_GFX* gfxDev() { return gfx; }

// ---------------------------------------------------------------------------
void gfxBegin(const Settings& s) {
#ifdef TFT_PWR_PIN
  // Boards with a switched panel power rail (NM-TV-154): energize the display
  // before anything else or the panel never comes up.
  pinMode(TFT_PWR_PIN, OUTPUT);
  digitalWrite(TFT_PWR_PIN, TFT_PWR_ON);
#endif
  // Backlight FIRST: do it before the panel/SPI init so the screen lights up even
  // if panel init has trouble. A dark backlight then means the sketch didn't get
  // this far (early crash / bad flash) — a useful boot indicator.
  pinMode(TFT_BL, OUTPUT);
  platformAnalogWriteInit(TFT_BL);
  gfxSetBrightness(s.brightness, s.backlightInverted);

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

void drawCenteredBounded(const char* text, int y, int maxW, uint8_t maxSize,
                         uint16_t color) {
  if (!gfx || !text) return;
  uint8_t size = gfxFitSize(text, maxW, maxSize);
  char fitted[48];
  copyEllipsized(text, fitted, sizeof(fitted), maxW, size);
  const int width = gfxTextW(fitted, size);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(max(0, (TFT_WIDTH - width) / 2), y);
  gfx->print(fitted);
}

void drawSystemFrame(uint16_t accent, const char* eyebrow) {
  gfx->fillScreen(C_UI_BG);
  gfx->fillRoundRect(kFrameX, kFrameY, kFrameW, kFrameH, 15, C_UI_PANEL);
  gfx->drawRoundRect(kFrameX, kFrameY, kFrameW, kFrameH, 15, C_UI_LINE);
  gfx->fillRoundRect(kFrameX, kFrameY, kFrameW, 5, 3, accent);

  gfx->fillCircle(24, 28, 4, accent);
  gfx->drawCircle(24, 28, 8, C_UI_LINE);
  gfx->setTextSize(1);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(39, 24);
  char label[24];
  copyEllipsized(eyebrow ? eyebrow : "DESKMATE SYSTEM", label, sizeof(label), 124, 1);
  gfx->print(label);

  gfx->fillRoundRect(178, 24, 10, 5, 2, C_UI_CYAN);
  gfx->fillRoundRect(191, 24, 10, 5, 2, C_UI_VIOLET);
  gfx->fillRoundRect(204, 24, 10, 5, 2, C_UI_AMBER);
  gfx->drawFastHLine(kContentLeft, 45, 200, C_UI_LINE);

  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(kContentLeft, kFooterY);
  gfx->print("DESKMATE");
  gfx->setCursor(198, kFooterY);
  gfx->print("240");
}

void drawStatusBadge(const char* text, uint16_t accent, int y) {
  if (!text || !text[0]) return;
  char fitted[38];
  copyEllipsized(text, fitted, sizeof(fitted), 166, 1);
  const int width = min(190, gfxTextW(fitted, 1) + 30);
  const int x = (TFT_WIDTH - width) / 2;
  gfx->fillRoundRect(x, y, width, 28, 8, C_UI_PANEL2);
  gfx->drawRoundRect(x, y, width, 28, 8, C_UI_LINE);
  gfx->fillCircle(x + 13, y + 14, 3, accent);
  gfx->setTextSize(1);
  gfx->setTextColor(C_UI_TEXT);
  gfx->setCursor(x + 23, y + 10);
  gfx->print(fitted);
}

uint16_t bootAccent(const char* line1, const char* line2) {
  String value = String(line1 ? line1 : "") + " " + String(line2 ? line2 : "");
  value.toLowerCase();
  if (value.indexOf("fail") >= 0 || value.indexOf("crash") >= 0) return C_UI_ROSE;
  if (value.indexOf("update") >= 0) return C_UI_AMBER;
  if (value.indexOf("network") >= 0 || value.indexOf("wifi") >= 0) return C_UI_BLUE;
  return C_UI_VIOLET;
}
}

void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  const uint16_t accent = bootAccent(line1, line2);
  drawSystemFrame(accent, "SYSTEM START");

  gfx->drawCircle(120, 74, 19, C_UI_LINE);
  gfx->drawCircle(120, 74, 12, accent);
  gfx->fillCircle(120, 74, 4, C_UI_CYAN);
  gfx->fillCircle(137, 74, 2, C_UI_AMBER);

  const char* title = line1 && line1[0] ? line1 : "DeskMate";
  drawCenteredBounded(title, 101, 196, 3, C_UI_TEXT);
  drawStatusBadge(line2, accent, 134);

  gfx->fillRoundRect(48, 181, 31, 4, 2, C_UI_CYAN);
  gfx->fillRoundRect(85, 181, 31, 4, 2, C_UI_BLUE);
  gfx->fillRoundRect(122, 181, 31, 4, 2, C_UI_VIOLET);
  gfx->fillRoundRect(159, 181, 31, 4, 2, C_UI_AMBER);
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  drawSystemFrame(C_UI_AMBER, "NETWORK SETUP");
  drawCenteredBounded("SETUP MODE", 55, 196, 3, C_UI_AMBER);

  gfx->fillRoundRect(20, 88, 200, 101, 11, C_UI_PANEL2);
  gfx->drawRoundRect(20, 88, 200, 101, 11, C_UI_LINE);
  gfx->setTextSize(1);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 99);
  gfx->print("JOIN WIFI");
  drawCenteredBounded(ssid && ssid[0] ? ssid : "DeskMate-Setup",
                      115, 176, 2, C_UI_TEXT);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 143);
  gfx->print(pass && pass[0] ? "Password configured" : "Open network");

  String url = String("http://") + (ip && ip[0] ? ip : "192.168.4.1");
  drawCenteredBounded(url.c_str(), 164, 176, 2, C_UI_BLUE);
}

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  drawSystemFrame(C_UI_GREEN, "NETWORK READY");
  drawCenteredBounded("CONNECTED", 55, 196, 3, C_UI_GREEN);

  gfx->fillRoundRect(20, 88, 200, 101, 11, C_UI_PANEL2);
  gfx->drawRoundRect(20, 88, 200, 101, 11, C_UI_LINE);
  gfx->setTextSize(1);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 99);
  gfx->print("WIFI");
  drawCenteredBounded(ssid && ssid[0] ? ssid : "-", 114, 176, 2, C_UI_TEXT);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 142);
  gfx->print("DASHBOARD");
  drawCenteredBounded(ip && ip[0] ? ip : "-", 157, 176, 2, C_UI_BLUE);
  if (host && host[0]) {
    String url = String(host) + ".local";
    drawCenteredBounded(url.c_str(), 178, 176, 1, C_UI_VIOLET);
  }
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  const uint16_t accent = titleColor == C_RED ? C_UI_ROSE : titleColor;
  drawSystemFrame(accent, "SYSTEM MESSAGE");
  drawCenteredBounded(title && title[0] ? title : "DeskMate",
                      82, 196, 3, accent);
  drawStatusBadge(msg, accent, 127);
}

void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  drawSystemFrame(C_UI_ROSE, "SAFE MODE");
  drawCenteredBounded("RECOVERY", 54, 196, 3, C_UI_ROSE);

  gfx->fillRoundRect(20, 87, 200, 103, 11, C_UI_PANEL2);
  gfx->drawRoundRect(20, 87, 200, 103, 11, C_UI_LINE);
  gfx->setTextSize(1);
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 100); gfx->print("EPC");
  gfx->setTextColor(C_UI_TEXT);
  gfx->setCursor(75, 100); gfx->print(epc && epc[0] ? epc : "-");
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 124); gfx->print("ADDR");
  gfx->setTextColor(C_UI_TEXT);
  gfx->setCursor(75, 124); gfx->print(addr && addr[0] ? addr : "-");
  gfx->setTextColor(C_UI_MUTED);
  gfx->setCursor(32, 150); gfx->print("OTA RECOVERY");
  drawCenteredBounded(ip && ip[0] ? ip : "-", 166, 176, 2, C_UI_BLUE);
}
