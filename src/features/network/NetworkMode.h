#pragma once
#include "Mode.h"

class NetworkMode : public DisplayMode {
 public:
  const char* id() const override { return "network"; }
  uint8_t modeConst() const override { return MODE_NETWORK; }
  void begin(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override;
  uint32_t pollIntervalMs(const Settings&) const override;
  uint16_t pollBudgetMs(const Settings&) const override { return 1500; }
  uint8_t pollCost() const override { return 1; }
  PollResult poll(const Settings&, uint16_t budgetMs) override;
  void pollActivityChanged(const Settings&, bool busy) override;
  void pollResultChanged(const Settings&, PollResult result) override;
  void displayTick(const Settings&) override;

 private:
  bool dirty_ = true;
  bool heartbeatOn_ = false;
  bool pollBusy_ = false;
  uint32_t heartbeatEpochMs_ = 0;
  void probe(const Settings&, uint16_t budgetMs);
  void render(const Settings&);
  void renderHeartbeat(const Settings&);
};

extern NetworkMode g_networkMode;
