#pragma once
#include "Mode.h"

class GithubMode : public DisplayMode {
 public:
  const char* id() const override { return "github"; }
  uint8_t modeConst() const override { return MODE_GITHUB; }
  void begin(const Settings&) override;
  void service(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override;

 private:
  uint32_t nextPoll_ = 0;
  bool dirty_ = true;
  void fetch(const Settings&);
  void render(const Settings&);
};

extern GithubMode g_githubMode;
