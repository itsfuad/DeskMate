#include "Gfx.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>

// The GeekMagic SmallTV's ST7789 has its CS line tied to GND and only latches
// SPI in **mode 3**. Arduino_GFX's stock Arduino_ST7789 forces SPI_MODE2 on the
// ESP8266 (wrong clock edge for this panel), so the controller never initializes
// and the screen stays black even with the backlight on. Subclass begin() to
// force mode 3 — matching the known-good GeekMagic community firmwares.
class Arduino_ST7789_SmallTV : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;   // inherit constructors
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE3;
    return Arduino_TFT::begin(speed);
  }
};

static Arduino_DataBus* bus = nullptr;
static Arduino_GFX*     gfx = nullptr;

Arduino_GFX* gfxDev() { return gfx; }

// ---------------------------------------------------------------------------
void gfxBegin(const Settings& s) {
  // Backlight FIRST: do it before the panel/SPI init so the screen lights up even
  // if panel init has trouble. A dark backlight then means the sketch didn't get
  // this far (early crash / bad flash) — a useful boot indicator.
  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(255);
  gfxSetBrightness(s.brightness, s.backlightInverted);

  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
  // IPS=true so the panel colors are not inverted; full 240x240, no offsets.
  // Use the SmallTV variant so the SPI bus comes up in mode 3 (see class above).
  gfx = new Arduino_ST7789_SmallTV(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/,
                                   TFT_WIDTH, TFT_HEIGHT, 0, 0, 0, 0);
  gfx->begin();
  gfx->setRotation(s.rotation & 3);
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
int gfxTextW(const char* s, uint8_t size) { return (int)strlen(s) * 6 * size; }

void gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  if (!gfx) return;
  int x = (TFT_WIDTH - gfxTextW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Largest size (<= maxSize) whose rendered width fits within maxW.
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize) {
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (gfxTextW(s, sz) <= maxW) return sz;
  }
  return 1;
}

// ---------------------------------------------------------------------------
void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(line1, 95, 3, C_WHITE);
  if (line2 && line2[0]) gfxDrawCentered(line2, 130, 2, C_GRAY);
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("SETUP MODE", 18, 3, C_YELLOW);
  gfxDrawCentered("Join WiFi:", 64, 2, C_GRAY);
  gfxDrawCentered(ssid, 88, gfxFitSize(ssid, 232, 3), C_WHITE);
  if (pass && pass[0]) {
    gfxDrawCentered("Password:", 124, 2, C_GRAY);
    gfxDrawCentered(pass, 146, gfxFitSize(pass, 232, 2), C_WHITE);
  } else {
    gfxDrawCentered("(open network)", 124, 2, C_GRAY);
  }
  gfxDrawCentered("Then open:", 182, 2, C_GRAY);
  String url = String("http://") + ip;
  gfxDrawCentered(url.c_str(), 206, gfxFitSize(url.c_str(), 232, 2), C_GREEN);
}

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("CONNECTED", 18, 3, C_GREEN);
  gfxDrawCentered("Network:", 62, 2, C_GRAY);
  gfxDrawCentered(ssid && ssid[0] ? ssid : "-", 84, gfxFitSize(ssid, 232, 3), C_WHITE);
  gfxDrawCentered("Open in browser:", 126, 2, C_GRAY);
  // IP shown big (always fits at size 2); mDNS name below as a friendlier option.
  gfxDrawCentered(ip && ip[0] ? ip : "-", 150, gfxFitSize(ip, 232, 3), C_GREEN);
  if (host && host[0]) {
    String url = String("http://") + host + ".local";
    gfxDrawCentered(url.c_str(), 188, gfxFitSize(url.c_str(), 232, 2), C_GRAY);
  }
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(title, 90, 3, titleColor);
  if (msg && msg[0]) gfxDrawCentered(msg, 130, 2, C_GRAY);
}

// Persistent crash screen shown in safe mode (after an exception reset). Holds the
// crash PC + fault address still so they can be read, and the IP for OTA recovery.
void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("CRASH", 12, 4, C_RED);
  gfxDrawCentered("epc", 60, 2, C_GRAY);
  gfxDrawCentered(epc && epc[0] ? epc : "-", 80, 3, C_WHITE);
  gfxDrawCentered("addr", 124, 2, C_GRAY);
  gfxDrawCentered(addr && addr[0] ? addr : "-", 146, 2, C_WHITE);
  gfxDrawCentered("OTA flash to fix:", 182, 2, C_GRAY);
  gfxDrawCentered(ip && ip[0] ? ip : "-", 204, 2, C_GREEN);
}
