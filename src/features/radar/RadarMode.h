// RadarMode.h — live ADS-B plane radar feature.
//
// Draws a PPI-style radar centred on the configured home lat/lon: range rings,
// aircraft as heading triangles with speed vectors and callsign/altitude labels,
// off-screen traffic as bearing dots on the rim, plus a few home-area airports.
// Owns its fetch (RadarClient) and its render/dirty state.
//
// The look reimplements MatixYo/ESP32-Plane-Radar (a sonar-style ADS-B radar
// for a 1.28" round display): https://github.com/MatixYo/ESP32-Plane-Radar
#pragma once
#include "Mode.h"
#include "config.h"

class RadarMode : public DisplayMode {
 public:
  const char* id() const override { return "radar"; }
  uint8_t     modeConst() const override { return MODE_RADAR; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;

 private:
  void render(const Settings& s);

  uint32_t renderedOk_ = 0xFFFFFFFF;
  bool     renderedError_ = false;
  bool     needRender_ = true;
};

extern RadarMode g_radarMode;
