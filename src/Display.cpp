#include "Display.h"
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

// ---- Colors (RGB565) ------------------------------------------------------
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GRAY   0x8410
#define C_DGRAY  0x4208
#define C_YELLOW 0xFFE0
#define C_BLUE   0x041F

static Arduino_DataBus* bus = nullptr;
static Arduino_GFX*     gfx = nullptr;

// ---------------------------------------------------------------------------
void displayBegin(const Settings& s) {
  // Backlight FIRST: do it before the panel/SPI init so the screen lights up even
  // if panel init has trouble. A dark backlight then means the sketch didn't get
  // this far (early crash / bad flash) — a useful boot indicator.
  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(255);
  displaySetBrightness(s.brightness, s.backlightInverted);

  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
  // IPS=true so the panel colors are not inverted; full 240x240, no offsets.
  // Use the SmallTV variant so the SPI bus comes up in mode 3 (see class above).
  gfx = new Arduino_ST7789_SmallTV(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/,
                                   TFT_WIDTH, TFT_HEIGHT, 0, 0, 0, 0);
  gfx->begin();
  gfx->setRotation(s.rotation & 3);
  gfx->fillScreen(C_BLACK);
}

void displaySetBrightness(uint8_t pct, bool inverted) {
  if (pct > 100) pct = 100;
  int duty = (int)pct * 255 / 100;
  if (inverted) duty = 255 - duty;
  analogWrite(TFT_BL, duty);
}

void displaySetRotation(uint8_t r) {
  if (gfx) gfx->setRotation(r & 3);
}

// ---- text helpers (built-in 6x8 font, integer scaled) ---------------------
static int textW(const char* s, uint8_t size) { return (int)strlen(s) * 6 * size; }

static void drawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  int x = (TFT_WIDTH - textW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Largest size (<= maxSize) whose rendered width fits within maxW.
static uint8_t fitSize(const char* s, int maxW, uint8_t maxSize) {
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (textW(s, sz) <= maxW) return sz;
  }
  return 1;
}

// ---------------------------------------------------------------------------
void displayBootMessage(const char* line1, const char* line2) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  drawCentered(line1, 95, 3, C_WHITE);
  if (line2 && line2[0]) drawCentered(line2, 130, 2, C_GRAY);
}

void displayApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  drawCentered("SETUP MODE", 18, 3, C_YELLOW);
  drawCentered("Join WiFi:", 64, 2, C_GRAY);
  drawCentered(ssid, 88, fitSize(ssid, 232, 3), C_WHITE);
  if (pass && pass[0]) {
    drawCentered("Password:", 124, 2, C_GRAY);
    drawCentered(pass, 146, fitSize(pass, 232, 2), C_WHITE);
  } else {
    drawCentered("(open network)", 124, 2, C_GRAY);
  }
  drawCentered("Then open:", 182, 2, C_GRAY);
  String url = String("http://") + ip;
  drawCentered(url.c_str(), 206, fitSize(url.c_str(), 232, 2), C_GREEN);
}

void displayStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  drawCentered("CONNECTED", 18, 3, C_GREEN);
  drawCentered("Network:", 62, 2, C_GRAY);
  drawCentered(ssid && ssid[0] ? ssid : "-", 84, fitSize(ssid, 232, 3), C_WHITE);
  drawCentered("Open in browser:", 126, 2, C_GRAY);
  // IP shown big (always fits at size 2); mDNS name below as a friendlier option.
  drawCentered(ip && ip[0] ? ip : "-", 150, fitSize(ip, 232, 3), C_GREEN);
  if (host && host[0]) {
    String url = String("http://") + host + ".local";
    drawCentered(url.c_str(), 188, fitSize(url.c_str(), 232, 2), C_GRAY);
  }
}

void displayMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  drawCentered(title, 90, 3, titleColor);
  if (msg && msg[0]) drawCentered(msg, 130, 2, C_GRAY);
}

// ---- sparkline ------------------------------------------------------------
static void drawSparkline(const StockData& d, int top, int bottom, uint16_t color) {
  if (d.sparkCount < 2) return;
  float mn = d.spark[0], mx = d.spark[0];
  for (uint8_t i = 1; i < d.sparkCount; i++) {
    if (d.spark[i] < mn) mn = d.spark[i];
    if (d.spark[i] > mx) mx = d.spark[i];
  }
  float span = mx - mn;
  if (span <= 0) span = 1;

  const int padL = 6, padR = 6;
  int plotW = TFT_WIDTH - padL - padR;
  int plotH = bottom - top;

  int prevX = 0, prevY = 0;
  for (uint8_t i = 0; i < d.sparkCount; i++) {
    int x = padL + (int)((long)plotW * i / (d.sparkCount - 1));
    int y = bottom - (int)((d.spark[i] - mn) / span * plotH);
    if (i > 0) {
      gfx->drawLine(prevX, prevY, x, y, color);
      gfx->drawLine(prevX, prevY + 1, x, y + 1, color); // 2px thick
    }
    prevX = x;
    prevY = y;
  }
}

// ---- number formatting ----------------------------------------------------
static void fmtPrice(float v, char* out, size_t n) {
  float a = fabsf(v);
  if (a >= 1000)      snprintf(out, n, "%.2f", v);
  else if (a >= 1)    snprintf(out, n, "%.2f", v);
  else if (a >= 0.01) snprintf(out, n, "%.4f", v);
  else                snprintf(out, n, "%.6f", v);
}

// ---------------------------------------------------------------------------
void displayStock(const StockData& d, uint8_t pageIndex, uint8_t pageCount,
                  const Settings& s) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  // No data yet for this symbol.
  if (!d.valid) {
    drawCentered(d.symbol[0] ? d.symbol : "----", 80, 3, C_WHITE);
    drawCentered(d.error ? "fetch error" : "loading...", 120, 2, C_GRAY);
    if (s.showPageDots && pageCount > 1) {
      int total = pageCount * 10 - 4;
      int x0 = (TFT_WIDTH - total) / 2;
      for (uint8_t i = 0; i < pageCount; i++)
        gfx->fillCircle(x0 + i * 10 + 2, 230, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    }
    return;
  }

  // Trend color
  bool up = d.hasChange ? (d.change >= 0) : true;
  uint16_t upC   = s.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = s.colorInverted ? C_GREEN : C_RED;
  uint16_t trendC = !d.hasChange ? C_WHITE : (up ? upC : downC);

  int y = 6;

  // Page dots (top)
  if (s.showPageDots && pageCount > 1) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 3, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 20;                       // extra breathing room below the dots
  }

  // Name / symbol
  if (s.showName) {
    const char* label = d.name[0] ? d.name : d.symbol;
    drawCentered(label, y, fitSize(label, 232, 3), C_WHITE);
    y += 28;
  }

  // Price (big, auto-fit)
  if (s.showPrice) {
    char num[20];
    fmtPrice(d.price, num, sizeof(num));
    char line[28];
    snprintf(line, sizeof(line), "%s%s", d.currency, num);
    uint8_t sz = fitSize(line, 236, 6);
    int ph = 8 * sz;
    int py = s.showName ? 74 : 64;
    drawCentered(line, py, sz, C_WHITE);   // price stays neutral (not trend-colored)
    y = py + ph + 8;
  }

  // Change line: [arrow] +chg (+pct%)
  if (s.showChange && d.hasChange) {
    char chg[40];
    if (d.changePct != 0 || d.change != 0)
      snprintf(chg, sizeof(chg), "%+.2f (%+.2f%%)", d.change, d.changePct);
    else
      snprintf(chg, sizeof(chg), "%+.2f", d.change);
    uint8_t sz = fitSize(chg, 210, 2);
    int tw = textW(chg, sz);
    int ah = 8 * sz;             // arrow box height
    int aw = ah;
    int totalW = aw + 4 + tw;
    int x = (TFT_WIDTH - totalW) / 2;
    if (x < 2) x = 2;
    // arrow triangle
    int ax = x, ay = y;
    if (up)
      gfx->fillTriangle(ax, ay + ah, ax + aw, ay + ah, ax + aw / 2, ay, trendC);
    else
      gfx->fillTriangle(ax, ay, ax + aw, ay, ax + aw / 2, ay + ah, trendC);
    gfx->setTextSize(sz);
    gfx->setTextColor(trendC);
    gfx->setCursor(x + aw + 4, ay);
    gfx->print(chg);
    y = ay + ah + 8;
  }

  // Chart
  if (s.showChart && d.sparkCount >= 2) {
    int top = y < 150 ? 156 : y + 4;
    int bottom = 228;
    if (top < bottom - 10) drawSparkline(d, top, bottom, trendC);
  }

  // Range label (top-right; the very bottom row is overscanned on this panel)
  if (s.showRangeLabel && d.rangeLabel[0]) {
    int sz = 2;
    int tw = textW(d.rangeLabel, sz);
    gfx->setTextSize(sz);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - tw - 4, 4);
    gfx->print(d.rangeLabel);
  }

  // Updated-ago (bottom-left)
  if (s.showUpdatedAgo && d.lastOkMs) {
    uint32_t ago = (millis() - d.lastOkMs) / 1000;
    char buf[12];
    if (ago < 100) snprintf(buf, sizeof(buf), "%lus", (unsigned long)ago);
    else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(ago / 60));
    gfx->setTextSize(2);
    gfx->setTextColor(d.error ? C_RED : C_DGRAY);
    gfx->setCursor(4, 224);
    gfx->print(buf);
  }

  // Stale/error dot (top-left) when last refresh failed but we have old data.
  // (Top-right now holds the range label.)
  if (d.error) gfx->fillCircle(6, 6, 3, C_RED);
}
