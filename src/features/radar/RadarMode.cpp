#if defined(DESKMATE_PREVIEW)
#include "PreviewApi.h"
#else
#include "RadarMode.h"
#endif
#include "Gfx.h"
#include "RadarClient.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include "StatusHeartbeat.h"
#include <Arduino_GFX_Library.h>
#include <math.h>

#if !defined(DESKMATE_PREVIEW)
RadarMode g_radarMode;
#endif

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BG      = rgb565(4, 12, 17);
constexpr uint16_t GRID    = rgb565(21, 50, 58);
constexpr uint16_t GRID_HI = rgb565(34, 76, 83);
constexpr uint16_t TEXT    = rgb565(194, 211, 215);
constexpr uint16_t MUTED   = rgb565(91, 117, 124);
constexpr uint16_t CORAL   = rgb565(255, 91, 102);
constexpr uint16_t CYAN    = rgb565(66, 211, 205);
constexpr uint16_t BLUE    = rgb565(69, 145, 210);
constexpr uint16_t AMBER   = rgb565(246, 186, 78);
constexpr uint16_t GREEN   = rgb565(79, 205, 128);

constexpr int CX = 120;
constexpr int CY = 120;
constexpr int RR = 108;
static_assert(CX - RR >= DisplayLayout::Left && CY - RR >= DisplayLayout::Top &&
              CX + RR <= DisplayLayout::Right &&
              CY + RR <= DisplayLayout::Bottom,
              "Radar scope must fit the safe display area");
static_assert(226 - 4 >= DisplayLayout::Left &&
              226 + 4 < DisplayLayout::Right &&
              226 - 4 >= DisplayLayout::Top &&
              226 + 4 < DisplayLayout::Bottom,
              "Radar heartbeat must fit the safe display area");

void polar(float radius, float bearing, int& x, int& y) {
  const float angle = bearing * static_cast<float>(PI) / 180.0f;
  x = CX + static_cast<int>(lroundf(radius * sinf(angle)));
  y = CY - static_cast<int>(lroundf(radius * cosf(angle)));
}

void geo(float homeLat, float homeLon, float lat, float lon,
         float& distanceKm, float& bearing) {
  const float north = (lat - homeLat) * 111.0f;
  const float east = (lon - homeLon) * 111.0f *
                     cosf(homeLat * static_cast<float>(PI) / 180.0f);
  distanceKm = sqrtf(north * north + east * east);
  bearing = atan2f(east, north) * 180.0f / static_cast<float>(PI);
  if (bearing < 0) bearing += 360.0f;
}

void rotatedPoint(int x, int y, float heading, float localX,
                  float localForward, int& outX, int& outY) {
  const float angle = heading * static_cast<float>(PI) / 180.0f;
  const float c = cosf(angle);
  const float s = sinf(angle);
  outX = x + static_cast<int>(lroundf(localX * c + localForward * s));
  outY = y + static_cast<int>(lroundf(localX * s - localForward * c));
}

float categoryScale(const Aircraft& aircraft) {
  if (aircraft.category[0] == 'A') {
    switch (aircraft.category[1]) {
      case '1': return 0.66f;  // light
      case '2': return 0.82f;  // small
      case '3': return 1.00f;  // large
      case '4': return 1.14f;  // high-vortex large
      case '5': return 1.34f;  // heavy
      case '6': return 0.92f;  // high performance
      case '7': return 0.80f;  // rotorcraft
      default: break;
    }
  }
  if (!strncmp(aircraft.type, "A38", 3) ||
      !strncmp(aircraft.type, "B74", 3) ||
      !strncmp(aircraft.type, "B77", 3)) {
    return 1.30f;
  }
  return 0.92f;
}

bool isRotorcraft(const Aircraft& aircraft) {
  return aircraft.category[0] == 'A' && aircraft.category[1] == '7';
}

void drawAircraft(TileCanvas& g, const Aircraft& aircraft, int x, int y,
                  float uiScale, uint16_t color) {
  const float scale = uiScale * categoryScale(aircraft);
  const float heading = isnan(aircraft.track)
      ? aircraft.bearingDeg : aircraft.track;

  if (isRotorcraft(aircraft)) {
    const int arm = max(3, static_cast<int>(5 * scale));
    g.drawLine(x - arm, y, x + arm, y, color);
    g.drawLine(x, y - arm, x, y + arm, color);
    g.drawCircle(x, y, max(2, static_cast<int>(3 * scale)), color);
    g.fillCircle(x, y, 1, color);
    return;
  }

  const float nose = 9.0f * scale;
  const float tail = -7.0f * scale;
  const float wing = 7.0f * scale;
  const float tailWing = 3.4f * scale;
  int nx, ny, tx, ty, lwx, lwy, rwx, rwy, ltx, lty, rtx, rty;
  rotatedPoint(x, y, heading, 0, nose, nx, ny);
  rotatedPoint(x, y, heading, 0, tail, tx, ty);
  rotatedPoint(x, y, heading, -wing, 0.5f * scale, lwx, lwy);
  rotatedPoint(x, y, heading, wing, 0.5f * scale, rwx, rwy);
  rotatedPoint(x, y, heading, -tailWing, -5.0f * scale, ltx, lty);
  rotatedPoint(x, y, heading, tailWing, -5.0f * scale, rtx, rty);

  g.drawLine(tx, ty, nx, ny, color);
  g.drawLine(lwx, lwy, rwx, rwy, color);
  g.drawLine(ltx, lty, rtx, rty, color);
  g.fillTriangle(nx, ny, x - 1, y + 1, x + 1, y + 1, color);
  if (scale >= 1.05f) {
    g.drawLine(tx + 1, ty, nx + 1, ny, color);
    g.fillCircle(x, y, 1, color);
  }
}

struct LabelBox {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

bool intersects(const LabelBox& a, const LabelBox& b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
           a.y + a.h <= b.y || b.y + b.h <= a.y);
}

struct RadarRenderContext {
  const Settings* settings = nullptr;
  bool heartbeatOn = false;
  bool pollBusy = false;
};

uint16_t radarStatusColor(const Settings& settings) {
  if (radarError()) return CORAL;
  const uint32_t lastOk = radarLastOkMs();
  if (!lastOk) return BLUE;
  const uint32_t staleAfter = max<uint32_t>(30000UL,
      static_cast<uint32_t>(settings.radar.pollSec) * 2500UL);
  if (millis() - lastOk > staleAfter) return AMBER;
  return GREEN;
}

void drawRadarHeartbeat(TileCanvas& g, const Settings& settings,
                        bool on, bool busy) {
  constexpr int x = 226;
  constexpr int y = 226;
  const uint16_t color = busy ? BLUE : radarStatusColor(settings);
  StatusHeartbeat::draw(g, x, y, color, on, busy);
}

#if !defined(DESKMATE_PREVIEW)
struct RadarLedContext {
  const Settings* settings = nullptr;
  bool on = false;
  bool busy = false;
};

void drawRadarLedRegion(TileCanvas& g, void* opaque) {
  const RadarLedContext& context =
      *static_cast<const RadarLedContext*>(opaque);
  g.fillScreen(BG);
  drawRadarHeartbeat(g, *context.settings, context.on, context.busy);
}
#endif

void drawRadar(TileCanvas& g, void* opaque) {
  const RadarRenderContext& context =
      *static_cast<const RadarRenderContext*>(opaque);
  const Settings& settings = *context.settings;
  g.fillScreen(BG);
  g.setTextWrap(false);

  // Static PPI scope. The synchronous HTTPS poll no longer has an animation to
  // visibly freeze; the display remains a stable retained radar between data
  // updates and is redrawn only when the target set actually changes.
  g.drawCircle(CX, CY, RR, GRID_HI);
  g.drawCircle(CX, CY, RR * 3 / 4, GRID);
  g.drawCircle(CX, CY, RR / 2, GRID);
  g.drawCircle(CX, CY, RR / 4, GRID);
  for (int angle = 0; angle < 360; angle += 30) {
    int x, y;
    polar(RR, static_cast<float>(angle), x, y);
    g.drawLine(CX, CY, x, y, (angle % 90 == 0) ? GRID_HI : GRID);
  }
  for (int angle = 0; angle < 360; angle += 10) {
    int x1, y1, x2, y2;
    polar(RR, static_cast<float>(angle), x1, y1);
    polar(RR - (angle % 30 == 0 ? 5 : 2), static_cast<float>(angle), x2, y2);
    g.drawLine(x1, y1, x2, y2, GRID_HI);
  }

  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(117, 9); g.print('N');
  g.setCursor(226, 116); g.print('E');
  g.setCursor(117, 222); g.print('S');
  g.setCursor(8, 116); g.print('W');

  const float configuredRange = static_cast<float>(settings.radar.rangeKm);
  const float range = configuredRange < 1.0f ? 1.0f : configuredRange;
  const float ui = settings.radar.uiScale == 0 ? 0.82f
                   : settings.radar.uiScale == 2 ? 1.25f : 1.0f;

  for (uint8_t i = 0; i < settings.radar.airportCount; ++i) {
    float distance, bearing;
    geo(settings.radar.lat, settings.radar.lon,
        settings.radar.airports[i].lat, settings.radar.airports[i].lon,
        distance, bearing);
    if (distance > range) continue;
    int x, y;
    polar(distance / range * RR, bearing, x, y);
    g.drawLine(x, y - 4, x + 4, y, BLUE);
    g.drawLine(x + 4, y, x, y + 4, BLUE);
    g.drawLine(x, y + 4, x - 4, y, BLUE);
    g.drawLine(x - 4, y, x, y - 4, BLUE);
    if (settings.radar.airports[i].icao[0]) {
      const int width = gfxTextW(settings.radar.airports[i].icao, 1);
      const int labelX = constrain(x + 6, DisplayLayout::Left,
                                   DisplayLayout::Right - width);
      const int labelY = constrain(y - 3, DisplayLayout::Top,
                                   DisplayLayout::Bottom - 8);
      g.setTextColor(BLUE);
      g.setCursor(labelX, labelY);
      g.print(settings.radar.airports[i].icao);
    }
  }

  LabelBox labels[10];
  uint8_t labelCount = 0;
  const uint8_t aircraftCount = radarCount();
  for (uint8_t i = 0; i < aircraftCount; ++i) {
    const Aircraft& aircraft = aircraftAt(i);
    if (aircraft.distKm > range) {
      if (!settings.radar.showRimDots) continue;
      int x, y;
      polar(RR - 1, aircraft.bearingDeg, x, y);
      g.fillCircle(x, y, i == 0 ? 3 : 2, i == 0 ? CYAN : CORAL);
      continue;
    }

    int x, y;
    polar(aircraft.distKm / range * RR, aircraft.bearingDeg, x, y);
    const uint16_t color = i == 0 ? CYAN : CORAL;

    if (settings.radar.showVectors && !isnan(aircraft.track) &&
        !isnan(aircraft.gs)) {
      const float length = constrain(aircraft.gs * 0.045f, 5.0f, 18.0f);
      int vx, vy;
      polar(length, aircraft.track, vx, vy);
      vx = x + (vx - CX);
      vy = y + (vy - CY);
      g.drawLine(x, y, vx, vy, i == 0 ? CYAN : AMBER);
    }

    drawAircraft(g, aircraft, x, y, ui, color);

    if (!settings.radar.showLabels || !aircraft.callsign[0] ||
        labelCount >= 10) {
      continue;
    }
    const int textWidth = min(48, static_cast<int>(strlen(aircraft.callsign)) * 6);
    int labelX = x + 8;
    if (labelX + textWidth > DisplayLayout::Right) {
      labelX = x - textWidth - 8;
    }
    labelX = constrain(labelX, DisplayLayout::Left,
                        DisplayLayout::Right - textWidth);
    const int labelY = constrain(y - 9, DisplayLayout::Top,
                                 DisplayLayout::Bottom - 17);
    LabelBox box = {static_cast<int16_t>(labelX),
                    static_cast<int16_t>(labelY),
                    static_cast<int16_t>(textWidth), 17};
    bool clash = false;
    for (uint8_t j = 0; j < labelCount; ++j) {
      if (intersects(box, labels[j])) {
        clash = true;
        break;
      }
    }
    if (clash) continue;
    labels[labelCount++] = box;

    g.setTextSize(1);
    g.setTextColor(i == 0 ? CYAN : TEXT);
    g.setCursor(labelX, labelY);
    g.print(aircraft.callsign);
    if (aircraft.altFt > 0) {
      char flightLevel[12];
      snprintf(flightLevel, sizeof(flightLevel), "FL%03d",
               constrain(static_cast<int>(aircraft.altFt / 100), 0, 999));
      g.setTextColor(MUTED);
      g.setCursor(labelX, labelY + 9);
      g.print(flightLevel);
    }
  }

  g.drawCircle(CX, CY, 4, CYAN);
  g.fillCircle(CX, CY, 1, CYAN);

  char rangeText[14];
  if (settings.radar.unitsMi) {
    snprintf(rangeText, sizeof(rangeText), "%d MI",
             static_cast<int>(lroundf(settings.radar.rangeKm * 0.621371f)));
  } else {
    snprintf(rangeText, sizeof(rangeText), "%d KM", settings.radar.rangeKm);
  }
  g.setTextColor(TEXT);
  g.setCursor(8, 8);
  g.print(rangeText);

  char countText[12];
  snprintf(countText, sizeof(countText), "%u AC", aircraftCount);
  g.setCursor(DisplayLayout::Right - gfxTextW(countText, 1), 8);
  g.print(countText);

  if (aircraftCount) {
    const Aircraft& nearest = aircraftAt(0);
    char nearestText[30];
    snprintf(nearestText, sizeof(nearestText), "%s %.1f%s",
             nearest.callsign[0] ? nearest.callsign : "NEAREST",
             settings.radar.unitsMi ? nearest.distKm * 0.621371f
                                    : nearest.distKm,
             settings.radar.unitsMi ? "mi" : "km");
    g.setTextColor(CYAN);
    g.setCursor(8, 222);
    g.print(nearestText);
  } else {
    g.setTextColor(MUTED);
    g.setCursor(8, 222);
    g.print("ADSB.FI  NO TARGETS");
  }

  drawRadarHeartbeat(g, settings, context.heartbeatOn, context.pollBusy);
}
}  // namespace

#if defined(DESKMATE_PREVIEW)
void previewRenderRadar(const Settings& settings,
                        const PreviewRadarState& state) {
  previewSetRadarState(state);
  RadarRenderContext context;
  context.settings = &settings;
  context.heartbeatOn = state.heartbeatOn;
  context.pollBusy = state.pollBusy;
  gfxRenderTiled(drawRadar, &context, BG);
}
#else

void RadarMode::begin(const Settings& settings) {
  radarInit(settings);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

void RadarMode::invalidate(const Settings& settings) {
  radarInit(settings);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

void RadarMode::wake(const Settings&) {
  needRender_ = true;
  heartbeatEpochMs_ = millis();
  heartbeatOn_ = true;
  pollBusy_ = false;
}

uint32_t RadarMode::pollIntervalMs(const Settings& settings) const {
  return static_cast<uint32_t>(settings.radar.pollSec) * 1000UL;
}

uint16_t RadarMode::pollBudgetMs(const Settings& settings) const {
  return min<uint16_t>(settings.httpTimeout, 4500);
}

PollResult RadarMode::poll(const Settings& settings, uint16_t budgetMs) {
  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f)
    return PollResult::Skipped;
  const bool ok = radarPoll(settings, budgetMs);
  needRender_ = true;
  return ok ? PollResult::Success : PollResult::Failed;
}

void RadarMode::render(const Settings& settings) {
  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) {
    gfxMessage("AIRCRAFT RADAR", "SET HOME LOCATION", AMBER);
    return;
  }
  RadarRenderContext context;
  context.settings = &settings;
  context.heartbeatOn = heartbeatOn_;
  context.pollBusy = pollBusy_;
  gfxRenderTiled(drawRadar, &context, BG);
}

void RadarMode::renderHeartbeat(const Settings& settings) {
  RadarLedContext context;
  context.settings = &settings;
  context.on = heartbeatOn_;
  context.busy = pollBusy_;
  // Exact 11x11 retained region with a dedicated callback: minimal CPU and SPI.
  gfxRenderRegion(drawRadarLedRegion, &context, BG, 221, 221, 11, 11);
}

void RadarMode::pollActivityChanged(const Settings& settings, bool busy) {
  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) return;
  if (pollBusy_ == busy) return;
  pollBusy_ = busy;
  if (!busy) {
    heartbeatEpochMs_ = millis();
    heartbeatOn_ = true;
  }
  renderHeartbeat(settings);
}

void RadarMode::displayTick(const Settings& settings) {
  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) {
    if (needRender_) {
      render(settings);
      needRender_ = false;
    }
    return;
  }

  const bool nextOn = pollBusy_ ? true
      : StatusHeartbeat::onAt(millis(), heartbeatEpochMs_);
  const bool heartbeatChanged = nextOn != heartbeatOn_;
  if (heartbeatChanged) heartbeatOn_ = nextOn;

  const uint32_t ok = radarLastOkMs();
  const bool error = radarError();
  if (ok != renderedOk_ || error != renderedError_) {
    renderedOk_ = ok;
    renderedError_ = error;
    needRender_ = true;
  }
  if (needRender_) {
    render(settings);
    needRender_ = false;
  } else if (heartbeatChanged) {
    renderHeartbeat(settings);
  }
}

#endif  // DESKMATE_PREVIEW
