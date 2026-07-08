// TickerMode.h — stock/crypto ticker feature.
//
// Rotates through the configured symbols, showing price, change and a sparkline.
// Owns its fetch (StockClient), its rendering, and its rotation/dirty state.
#pragma once
#include "Mode.h"
#include "config.h"

class TickerMode : public DisplayMode {
 public:
  const char* id() const override { return "stocks"; }
  uint8_t     modeConst() const override { return MODE_STOCKS; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;

 private:
  void render(const Settings& s);   // draw the current page

  uint8_t  curPage_ = 0;
  uint32_t lastRotate_ = 0;
  uint32_t renderedLastOk_ = 0xFFFFFFFF;
  bool     renderedError_ = false;
  bool     needRender_ = true;
};

extern TickerMode g_tickerMode;
