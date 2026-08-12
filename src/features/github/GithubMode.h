#pragma once
#include "Mode.h"

class GithubMode : public DisplayMode {
 public:
  const char* id() const override { return "github"; }
  uint8_t modeConst() const override { return MODE_GITHUB; }
  void begin(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override;
  uint32_t pollIntervalMs(const Settings&) const override;
  uint16_t pollBudgetMs(const Settings&) const override;
  uint8_t pollCost() const override { return 5; }
  bool requiresInternet() const override { return true; }
  PollResult poll(const Settings&, uint16_t budgetMs) override;
  void displayTick(const Settings&) override;

 private:
  bool dirty_ = true;
  void render(const Settings&);
};

extern GithubMode g_githubMode;
