#pragma once
#include "Mode.h"
#include "config.h"

class RadarMode : public DisplayMode {
 public:
  const char* id() const override { return "radar"; }
  uint8_t modeConst() const override { return MODE_RADAR; }
  void begin(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override { needRender_ = true; }
  uint32_t pollIntervalMs(const Settings&) const override;
  uint16_t pollBudgetMs(const Settings&) const override;
  uint8_t pollCost() const override { return 3; }
  PollResult poll(const Settings&, uint16_t budgetMs) override;
  void displayTick(const Settings&) override;

 private:
  void render(const Settings&);
  uint32_t renderedOk_ = 0xFFFFFFFF;
  bool renderedError_ = false;
  bool needRender_ = true;
};

extern RadarMode g_radarMode;
