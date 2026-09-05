#include "RadarMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "RadarClient.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include "StatusDot.h"
#include "Icons.h"
#include <Arduino_GFX_Library.h>
#include <math.h>

RadarMode g_radarMode;

namespace {
PollResult indicatorResult = PollResult::Skipped;

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
constexpr uint16_t VIOLET  = rgb565(168, 130, 255);
constexpr uint16_t GROUND  = rgb565(140, 158, 163);

// Altitude bands, the convention every ADS-B display uses. Colour carries the
// one thing a flat plan view cannot: how high a target is. The previous scheme
// painted every aircraft the same coral and reserved cyan for the nearest,
// which encoded almost nothing and turned a busy sky into one red mass.
uint16_t altitudeColor(int32_t altFt) {
  if (altFt <= 0) return GROUND;        // on the ground, or no altitude given
  if (altFt < 5000) return CORAL;
  if (altFt < 10000) return AMBER;
  if (altFt < 20000) return GREEN;
  if (altFt < 30000) return CYAN;
  if (altFt < 40000) return BLUE;
  return VIOLET;
}

uint16_t blend565(uint16_t background, uint16_t foreground, uint8_t alpha) {
  const uint16_t inverse = 255U - alpha;
  const uint16_t red = (((background >> 11) & 0x1F) * inverse +
                        ((foreground >> 11) & 0x1F) * alpha) / 255U;
  const uint16_t green = (((background >> 5) & 0x3F) * inverse +
                          ((foreground >> 5) & 0x3F) * alpha) / 255U;
  const uint16_t blue = ((background & 0x1F) * inverse +
                         (foreground & 0x1F) * alpha) / 255U;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

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

void drawTrailCurve(TileCanvas& g, const int* xs, const int* ys,
                    uint8_t pointCount, uint8_t segment,
                    uint16_t color) {
  if (pointCount < 2 || segment + 1 >= pointCount) return;
  const uint8_t i = segment;
  const int previousX = i == 0 ? xs[i] : xs[i - 1];
  const int previousY = i == 0 ? ys[i] : ys[i - 1];
  const int nextX = i + 2 < pointCount ? xs[i + 2] : xs[i + 1];
  const int nextY = i + 2 < pointCount ? ys[i + 2] : ys[i + 1];
  const float c1x = xs[i] + (xs[i + 1] - previousX) / 6.0f;
  const float c1y = ys[i] + (ys[i + 1] - previousY) / 6.0f;
  const float c2x = xs[i + 1] - (nextX - xs[i]) / 6.0f;
  const float c2y = ys[i + 1] - (nextY - ys[i]) / 6.0f;

  int lastX = xs[i];
  int lastY = ys[i];
  for (uint8_t step = 1; step <= 4; ++step) {
    const float t = step / 4.0f;
    const float inverse = 1.0f - t;
    const float x = inverse * inverse * inverse * xs[i] +
                    3.0f * inverse * inverse * t * c1x +
                    3.0f * inverse * t * t * c2x + t * t * t * xs[i + 1];
    const float y = inverse * inverse * inverse * ys[i] +
                    3.0f * inverse * inverse * t * c1y +
                    3.0f * inverse * t * t * c2y + t * t * t * ys[i + 1];
    const int currentX = static_cast<int>(lroundf(x));
    const int currentY = static_cast<int>(lroundf(y));
    g.drawLine(lastX, lastY, currentX, currentY, color);
    lastX = currentX;
    lastY = currentY;
  }
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
  const float heading = aircraft.headingDeg;

  if (isRotorcraft(aircraft)) {
    // A rotor disc seen from above is the same at every heading, so this one
    // stays drawn rather than iconified; there is no orientation to convey.
    const int arm = max(3, static_cast<int>(5 * scale));
    g.drawLine(x - arm, y, x + arm, y, color);
    g.drawLine(x, y - arm, x, y + arm, color);
    g.drawCircle(x, y, max(2, static_cast<int>(3 * scale)), color);
    g.fillCircle(x, y, 1, color);
    return;
  }

  // Two icon sizes stand in for the continuous category scaling the drawn
  // silhouette used, which is all the panel can resolve anyway.
  gfxDrawIconRotated(g, scale >= 1.05f ? Icon::Plane16 : Icon::Plane12,
                     x, y, heading, color);
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

uint16_t radarStatusColor(const Settings&) {
  switch (indicatorResult) {
    case PollResult::Success: return GREEN;
    case PollResult::Failed: return C_RED;
    case PollResult::Skipped: return C_WHITE;
    case PollResult::MoreWork: return AMBER;
  }
  return C_WHITE;
}

void drawRadarHeartbeat(TileCanvas& g, const Settings& settings,
                        bool on, bool busy) {
  constexpr int x = 226;
  constexpr int y = 226;
  const uint16_t color = busy ? BLUE : radarStatusColor(settings);
  StatusDot::draw(g, x, y, color, on, busy);
}

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
      const uint16_t rimColor = altitudeColor(aircraft.altFt);
      g.fillCircle(x, y, i == 0 ? 3 : 2, rimColor);
      if (i == 0) g.drawCircle(x, y, 5, rimColor);
      continue;
    }

    int x, y;
    polar(aircraft.distKm / range * RR, aircraft.bearingDeg, x, y);
    const uint16_t color = altitudeColor(aircraft.altFt);

    // Draw fading trail paths
    if (settings.radar.showTrails) {
      float tLats[RADAR_TRAIL_MAX_POINTS];
      float tLons[RADAR_TRAIL_MAX_POINTS];
      uint8_t count = getAircraftTrail(
          aircraft.callsign, settings.radar.lat, settings.radar.lon,
          tLats, tLons, RADAR_TRAIL_MAX_POINTS);
      int trailX[RADAR_TRAIL_MAX_POINTS + 1] = {x};
      int trailY[RADAR_TRAIL_MAX_POINTS + 1] = {y};
      uint8_t pointCount = 1;
      for (uint8_t j = 0; j < count; ++j) {
        float tDist, tBrg;
        geo(settings.radar.lat, settings.radar.lon, tLats[j], tLons[j], tDist, tBrg);
        if (tDist > range) continue;
        int tx, ty;
        polar(tDist / range * RR, tBrg, tx, ty);
        trailX[pointCount] = tx;
        trailY[pointCount] = ty;
        ++pointCount;
      }
      for (uint8_t j = 0; j + 1 < pointCount; ++j) {
        const uint8_t oldestSegment = pointCount - 2;
        const uint8_t scale = oldestSegment == 0
            ? 255
            : 255 - static_cast<uint8_t>(j * 215 / oldestSegment);
        drawTrailCurve(g, trailX, trailY, pointCount, j,
                       blend565(BG, color, scale));
      }
    }

    drawAircraft(g, aircraft, x, y, ui, color);
    // The nearest target keeps its own marker instead of its own colour, so
    // that highlighting it costs no altitude information.
    if (i == 0) g.drawCircle(x, y, max(7, static_cast<int>(9 * ui)), color);

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

  // Altitude key. Target colour is the only place the plan view can carry
  // height, so the scale that decodes it belongs on the screen rather than in
  // the documentation. It sits in the top-left corner, outside the scope.
  constexpr int keyX = 8;
  constexpr int keyY = 18;
  constexpr int keyW = 5;
  constexpr int keyGap = 1;
  const uint16_t bands[7] = {GROUND, CORAL, AMBER, GREEN, CYAN, BLUE, VIOLET};
  for (uint8_t band = 0; band < 7; ++band) {
    g.fillRect(keyX + band * (keyW + keyGap), keyY, keyW, 5, bands[band]);
  }
  constexpr int keyRight = keyX + 7 * (keyW + keyGap) - keyGap;
  g.setTextColor(MUTED);
  g.setCursor(keyX, keyY + 7);
  g.print("GND");
  g.setCursor(keyRight - gfxTextW("40K", 1), keyY + 7);
  g.print("40K");

  if (aircraftCount) {
    const Aircraft& nearest = aircraftAt(0);
    char nearestText[30];
    snprintf(nearestText, sizeof(nearestText), "%s %.1f%s",
             nearest.callsign[0] ? nearest.callsign : "NEAREST",
             settings.radar.unitsMi ? nearest.distKm * 0.621371f
                                    : nearest.distKm,
             settings.radar.unitsMi ? "mi" : "km");
    // The same ring the nearest target wears on the scope, so the highlight
    // and the readout below it read as one thing.
    const uint16_t nearestColor = altitudeColor(nearest.altFt);
    g.drawCircle(11, 225, 3, nearestColor);
    g.setTextColor(TEXT);
    g.setCursor(18, 222);
    g.print(nearestText);
  } else {
    g.setTextColor(MUTED);
    g.setCursor(8, 222);
    g.print("ADSB.FI  NO TARGETS");
  }

  drawRadarHeartbeat(g, settings, context.heartbeatOn, context.pollBusy);
}
}  // namespace

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
  const bool https = settings.radar.source != RADAR_SRC_WEBHOOK ||
                     settings.radar.webhookUrl.startsWith("https://");
  if (https && !platformTlsMemoryReady()) return PollResult::Skipped;
  const bool ok = radarPoll(settings, budgetMs);
  needRender_ = true;
  if (!ok && radarLowMemory()) return PollResult::Skipped;
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

void RadarMode::pollResultChanged(const Settings&, PollResult result) {
  indicatorResult = result;
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
      : StatusDot::onAt(millis(), heartbeatEpochMs_);
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
