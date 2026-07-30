#pragma once
#include "Mode.h"
#include "config.h"

class RadarMode : public DisplayMode {
 public:
  const char* id() const override { return "radar"; }
  uint8_t modeConst() const override { return MODE_RADAR; }
  void begin(const Settings&) override;
  void service(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override { needRender_ = true; }

 private:
  void render(const Settings&);
  uint32_t renderedOk_ = 0xFFFFFFFF;
  bool renderedError_ = false;
  bool needRender_ = true;
};

extern RadarMode g_radarMode;
