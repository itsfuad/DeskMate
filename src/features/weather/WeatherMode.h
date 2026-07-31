#pragma once
#include "Mode.h"

class WeatherMode : public DisplayMode {
 public:
  const char* id() const override { return "weather"; }
  uint8_t modeConst() const override { return MODE_WEATHER; }
  void begin(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override;
  uint32_t pollIntervalMs(const Settings&) const override;
  uint16_t pollBudgetMs(const Settings&) const override;
  uint8_t pollCost() const override { return 4; }
  PollResult poll(const Settings&, uint16_t budgetMs) override;
  void displayTick(const Settings&) override;

 private:
  uint8_t pollStage_ = 0;  // current conditions, then forecast in a separate job
  int32_t renderedMinute_ = -1;
  bool dirty_ = true;
  void render(const Settings&);
};

extern WeatherMode g_weatherMode;
