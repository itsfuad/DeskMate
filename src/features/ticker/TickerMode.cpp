#include "TickerMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "Net.h"
#include "StockClient.h"

TickerMode g_tickerMode;

// ---- sparkline ------------------------------------------------------------
static void drawSparkline(Arduino_GFX* gfx, const StockData& d,
                          int top, int bottom, uint16_t color) {
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

// ---- one ticker page ------------------------------------------------------
static void drawStock(const StockData& d, uint8_t pageIndex, uint8_t pageCount,
                      const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  // No data yet for this symbol.
  if (!d.valid) {
    gfxDrawCentered(d.symbol[0] ? d.symbol : "----", 80, 3, C_WHITE);
    gfxDrawCentered(d.error ? "fetch error" : "loading...", 120, 2, C_GRAY);
    if (s.ticker.showPageDots && pageCount > 1) {
      int total = pageCount * 10 - 4;
      int x0 = (TFT_WIDTH - total) / 2;
      for (uint8_t i = 0; i < pageCount; i++)
        gfx->fillCircle(x0 + i * 10 + 2, 230, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    }
    return;
  }

  // Trend color
  bool up = d.hasChange ? (d.change >= 0) : true;
  uint16_t upC   = s.ticker.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = s.ticker.colorInverted ? C_GREEN : C_RED;
  uint16_t trendC = !d.hasChange ? C_WHITE : (up ? upC : downC);

  int y = 6;

  // Page dots (top)
  if (s.ticker.showPageDots && pageCount > 1) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 3, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 20;                       // extra breathing room below the dots
  }

  // Name / symbol
  if (s.ticker.showName) {
    const char* label = d.name[0] ? d.name : d.symbol;
    gfxDrawCentered(label, y, gfxFitSize(label, 232, 3), C_WHITE);
    y += 28;
  }

  // Price (big, auto-fit)
  if (s.ticker.showPrice) {
    char num[20];
    fmtPrice(d.price, num, sizeof(num));
    char line[28];
    snprintf(line, sizeof(line), "%s%s", d.currency, num);
    uint8_t sz = gfxFitSize(line, 236, 6);
    int ph = 8 * sz;
    int py = s.ticker.showName ? 74 : 64;
    gfxDrawCentered(line, py, sz, C_WHITE);   // price stays neutral (not trend-colored)
    y = py + ph + 8;
  }

  // Change line: [arrow] +chg (+pct%)
  if (s.ticker.showChange && d.hasChange) {
    char chg[40];
    if (d.changePct != 0 || d.change != 0)
      snprintf(chg, sizeof(chg), "%+.2f (%+.2f%%)", d.change, d.changePct);
    else
      snprintf(chg, sizeof(chg), "%+.2f", d.change);
    uint8_t sz = gfxFitSize(chg, 210, 2);
    int tw = gfxTextW(chg, sz);
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
  if (s.ticker.showChart && d.sparkCount >= 2) {
    int top = y < 150 ? 156 : y + 4;
    int bottom = 228;
    if (top < bottom - 10) drawSparkline(gfx, d, top, bottom, trendC);
  }

  // Range label (top-right; the very bottom row is overscanned on this panel)
  if (s.ticker.showRangeLabel && d.rangeLabel[0]) {
    int sz = 2;
    int tw = gfxTextW(d.rangeLabel, sz);
    gfx->setTextSize(sz);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - tw - 4, 4);
    gfx->print(d.rangeLabel);
  }

  // Updated-ago (bottom-left)
  if (s.ticker.showUpdatedAgo && d.lastOkMs) {
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

// ---- DisplayMode ----------------------------------------------------------
void TickerMode::begin(const Settings& s) {
  stocksInit(s);
  curPage_ = 0;
  lastRotate_ = millis();
  renderedLastOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
}

void TickerMode::invalidate(const Settings& s) {
  stocksInit(s);
  stocksForceRefresh();
  renderedLastOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void TickerMode::render(const Settings& s) {
  uint8_t n = stocksCount();
  if (n == 0) {
    gfxMessage("No tickers", netIP().c_str(), C_YELLOW);
    return;
  }
  if (s.ticker.source == SRC_WEBHOOK && s.ticker.webhookUrl.length() < 8) {
    gfxMessage("Set webhook", netIP().c_str(), C_YELLOW);
    return;
  }
  if (curPage_ >= n) curPage_ = 0;
  drawStock(stockAt(curPage_), curPage_, n, s);
}

void TickerMode::service(const Settings& s) {
  stocksService(s);

  uint8_t n = stocksCount();

  // Rotate to next ticker
  if (n > 1 && millis() - lastRotate_ >= (uint32_t)s.ticker.rotateSec * 1000UL) {
    curPage_ = (curPage_ + 1) % n;
    lastRotate_ = millis();
    needRender_ = true;
  }

  // Re-render when the displayed ticker's data changed
  if (n > 0 && curPage_ < n) {
    const StockData& d = stockAt(curPage_);
    if (d.lastOkMs != renderedLastOk_ || d.error != renderedError_) {
      needRender_ = true;
      renderedLastOk_ = d.lastOkMs;
      renderedError_ = d.error;
    }
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}
