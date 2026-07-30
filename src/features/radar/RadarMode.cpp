#include "RadarMode.h"
#include "Gfx.h"
#include "RadarClient.h"
#include "TileRenderer.h"
#include <Arduino_GFX_Library.h>
#include <math.h>

RadarMode g_radarMode;

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BG       = rgb565(4, 12, 17);
constexpr uint16_t GRID     = rgb565(23, 55, 62);
constexpr uint16_t GRID_HI  = rgb565(36, 82, 88);
constexpr uint16_t TEXT     = rgb565(189, 207, 211);
constexpr uint16_t MUTED    = rgb565(93, 119, 125);
constexpr uint16_t SWEEP_1  = rgb565(20, 75, 70);
constexpr uint16_t SWEEP_2  = rgb565(29, 112, 100);
constexpr uint16_t SWEEP_3  = rgb565(52, 178, 150);
constexpr uint16_t CORAL    = rgb565(255, 91, 102);
constexpr uint16_t CYAN     = rgb565(66, 211, 205);
constexpr uint16_t BLUE     = rgb565(69, 145, 210);
constexpr uint16_t AMBER    = rgb565(246, 186, 78);

constexpr int CX = 120;
constexpr int CY = 120;
constexpr int RR = 112;

void polar(float radius, float bearing, int& x, int& y) {
  const float a = bearing * static_cast<float>(PI) / 180.0f;
  x = CX + static_cast<int>(lroundf(radius * sinf(a)));
  y = CY - static_cast<int>(lroundf(radius * cosf(a)));
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

void rotatedPoint(int x, int y, float heading, float localX, float localForward,
                  int& outX, int& outY) {
  const float a = heading * static_cast<float>(PI) / 180.0f;
  const float c = cosf(a);
  const float s = sinf(a);
  outX = x + static_cast<int>(lroundf(localX * c + localForward * s));
  outY = y + static_cast<int>(lroundf(localX * s - localForward * c));
}

float categoryScale(const Aircraft& a) {
  if (a.category[0] == 'A') {
    switch (a.category[1]) {
      case '1': return 0.66f; // light
      case '2': return 0.82f; // small
      case '3': return 1.00f; // large
      case '4': return 1.14f; // high-vortex large
      case '5': return 1.34f; // heavy
      case '6': return 0.92f; // high performance
      case '7': return 0.80f; // rotorcraft
      default: break;
    }
  }
  if (!strncmp(a.type, "A38", 3) || !strncmp(a.type, "B74", 3) ||
      !strncmp(a.type, "B77", 3)) return 1.30f;
  return 0.92f;
}

bool isRotorcraft(const Aircraft& a) {
  return a.category[0] == 'A' && a.category[1] == '7';
}

void drawAircraft(TileCanvas& g, const Aircraft& a, int x, int y,
                  float uiScale, uint16_t color) {
  const float k = uiScale * categoryScale(a);
  const float heading = isnan(a.track) ? a.bearingDeg : a.track;

  if (isRotorcraft(a)) {
    const int arm = max(3, static_cast<int>(5 * k));
    g.drawLine(x - arm, y, x + arm, y, color);
    g.drawLine(x, y - arm, x, y + arm, color);
    g.fillCircle(x, y, max(1, static_cast<int>(2 * k)), color);
    return;
  }

  const float nose = 9.0f * k;
  const float tail = -7.0f * k;
  const float wing = 7.0f * k;
  const float tailWing = 3.4f * k;
  int nx, ny, tx, ty, lwx, lwy, rwx, rwy, ltx, lty, rtx, rty;
  rotatedPoint(x, y, heading, 0, nose, nx, ny);
  rotatedPoint(x, y, heading, 0, tail, tx, ty);
  rotatedPoint(x, y, heading, -wing, 0.5f * k, lwx, lwy);
  rotatedPoint(x, y, heading, wing, 0.5f * k, rwx, rwy);
  rotatedPoint(x, y, heading, -tailWing, -5.0f * k, ltx, lty);
  rotatedPoint(x, y, heading, tailWing, -5.0f * k, rtx, rty);

  g.drawLine(tx, ty, nx, ny, color);
  g.drawLine(lwx, lwy, rwx, rwy, color);
  g.drawLine(ltx, lty, rtx, rty, color);
  g.fillTriangle(nx, ny, x - 1, y + 1, x + 1, y + 1, color);
  if (k >= 1.05f) {
    g.drawLine(tx + 1, ty, nx + 1, ny, color);
    g.fillCircle(x, y, 1, color);
  }
}

struct LabelBox { int16_t x, y, w, h; };
bool intersects(const LabelBox& a, const LabelBox& b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
           a.y + a.h <= b.y || b.y + b.h <= a.y);
}

void drawSweep(TileCanvas& g, float angle) {
  const uint16_t colors[5] = {SWEEP_1, SWEEP_1, SWEEP_2, SWEEP_2, SWEEP_3};
  for (int i = 0; i < 5; ++i) {
    int ex, ey;
    polar(RR - 2, angle - (4 - i) * 3.5f, ex, ey);
    g.drawLine(CX, CY, ex, ey, colors[i]);
  }
}

struct RadarRenderContext { const Settings* settings; float sweepAngle; };

void drawRadar(TileCanvas& g, void* opaque) {
  const RadarRenderContext& context = *static_cast<const RadarRenderContext*>(opaque);
  const Settings& s = *context.settings;
  g.fillScreen(BG);
  g.setTextWrap(false);

  // Full-screen PPI scope. There is deliberately no header bar: corner labels
  // sit in the unused square corners while the radar circle owns the display.
  g.drawCircle(CX, CY, RR, GRID_HI);
  g.drawCircle(CX, CY, RR * 3 / 4, GRID);
  g.drawCircle(CX, CY, RR / 2, GRID);
  g.drawCircle(CX, CY, RR / 4, GRID);
  for (int a = 0; a < 360; a += 30) {
    int x, y;
    polar(RR, static_cast<float>(a), x, y);
    g.drawLine(CX, CY, x, y, (a % 90 == 0) ? GRID_HI : GRID);
  }
  for (int a = 0; a < 360; a += 10) {
    int x1, y1, x2, y2;
    polar(RR, static_cast<float>(a), x1, y1);
    polar(RR - (a % 30 == 0 ? 5 : 2), static_cast<float>(a), x2, y2);
    g.drawLine(x1, y1, x2, y2, GRID_HI);
  }

  drawSweep(g, context.sweepAngle);

  g.setTextSize(1);
  g.setTextColor(MUTED);
  g.setCursor(116, 10); g.print('N');
  g.setCursor(224, 117); g.print('E');
  g.setCursor(116, 224); g.print('S');
  g.setCursor(9, 117); g.print('W');

  const float configuredRange = static_cast<float>(s.radar.rangeKm);
  const float range = configuredRange < 1.0f ? 1.0f : configuredRange;
  const float ui = s.radar.uiScale == 0 ? 0.82f
                   : s.radar.uiScale == 2 ? 1.25f : 1.0f;

  // Airport reference points.
  for (uint8_t i = 0; i < s.radar.airportCount; ++i) {
    float distance, bearing;
    geo(s.radar.lat, s.radar.lon, s.radar.airports[i].lat,
        s.radar.airports[i].lon, distance, bearing);
    if (distance > range) continue;
    int x, y;
    polar(distance / range * RR, bearing, x, y);
    g.drawLine(x, y - 4, x + 4, y, BLUE);
    g.drawLine(x + 4, y, x, y + 4, BLUE);
    g.drawLine(x, y + 4, x - 4, y, BLUE);
    g.drawLine(x - 4, y, x, y - 4, BLUE);
    if (s.radar.airports[i].icao[0]) {
      g.setTextColor(BLUE);
      g.setCursor(x + 6, y - 3);
      g.print(s.radar.airports[i].icao);
    }
  }

  LabelBox labels[10];
  uint8_t labelCount = 0;
  const uint8_t aircraftCount = radarCount();
  for (uint8_t i = 0; i < aircraftCount; ++i) {
    const Aircraft& a = aircraftAt(i);
    if (a.distKm > range) {
      if (!s.radar.showRimDots) continue;
      int x, y;
      polar(RR - 1, a.bearingDeg, x, y);
      g.fillCircle(x, y, i == 0 ? 3 : 2, i == 0 ? CYAN : CORAL);
      continue;
    }

    int x, y;
    polar(a.distKm / range * RR, a.bearingDeg, x, y);
    const uint16_t color = i == 0 ? CYAN : CORAL;

    if (s.radar.showVectors && !isnan(a.track) && !isnan(a.gs)) {
      const float length = constrain(a.gs * 0.045f, 5.0f, 18.0f);
      int vx, vy;
      polar(length, a.track, vx, vy);
      // polar() is centered on the scope; translate the vector to the aircraft.
      vx = x + (vx - CX);
      vy = y + (vy - CY);
      g.drawLine(x, y, vx, vy, i == 0 ? CYAN : AMBER);
    }

    drawAircraft(g, a, x, y, ui, color);

    if (!s.radar.showLabels || !a.callsign[0] || labelCount >= 10) continue;
    const int textW = min(48, static_cast<int>(strlen(a.callsign)) * 6);
    int lx = x + 8;
    if (lx + textW > TFT_WIDTH - 3) lx = x - textW - 8;
    lx = constrain(lx, 2, TFT_WIDTH - textW - 2);
    int ly = constrain(y - 9, 2, TFT_HEIGHT - 19);
    LabelBox box = {static_cast<int16_t>(lx), static_cast<int16_t>(ly),
                    static_cast<int16_t>(textW), 17};
    bool clash = false;
    for (uint8_t j = 0; j < labelCount; ++j) {
      if (intersects(box, labels[j])) { clash = true; break; }
    }
    if (clash) continue;
    labels[labelCount++] = box;

    g.setTextSize(1);
    g.setTextColor(i == 0 ? CYAN : TEXT);
    g.setCursor(lx, ly);
    g.print(a.callsign);
    if (a.altFt > 0) {
      char flightLevel[12];
      snprintf(flightLevel, sizeof(flightLevel), "FL%03d",
               constrain(static_cast<int>(a.altFt / 100), 0, 999));
      g.setTextColor(MUTED);
      g.setCursor(lx, ly + 9);
      g.print(flightLevel);
    }
  }

  // Home marker and corner telemetry.
  g.drawCircle(CX, CY, 4, CYAN);
  g.fillCircle(CX, CY, 1, CYAN);

  char rangeText[14];
  if (s.radar.unitsMi)
    snprintf(rangeText, sizeof(rangeText), "%d MI",
             static_cast<int>(lroundf(s.radar.rangeKm * 0.621371f)));
  else
    snprintf(rangeText, sizeof(rangeText), "%d KM", s.radar.rangeKm);
  g.setTextColor(TEXT);
  g.setCursor(4, 4);
  g.print(rangeText);

  char countText[12];
  snprintf(countText, sizeof(countText), "%u AC", aircraftCount);
  g.setCursor(TFT_WIDTH - gfxTextW(countText, 1) - 4, 4);
  g.print(countText);

  if (aircraftCount) {
    const Aircraft& nearest = aircraftAt(0);
    char nearestText[28];
    snprintf(nearestText, sizeof(nearestText), "%s %.1f%s",
             nearest.callsign[0] ? nearest.callsign : "NEAREST",
             s.radar.unitsMi ? nearest.distKm * 0.621371f : nearest.distKm,
             s.radar.unitsMi ? "mi" : "km");
    g.setTextColor(CYAN);
    g.setCursor(4, 229);
    g.print(nearestText);
  } else {
    g.setTextColor(MUTED);
    g.setCursor(4, 229);
    g.print("ADSB.FI  NO TARGETS");
  }

  if (radarError()) {
    g.fillCircle(233, 233, 4, CORAL);
  } else {
    g.fillCircle(233, 233, 3, CYAN);
  }
}

void markSweepTiles(TileMask& mask, float angle) {
  // Include the full visible trail. Marking both the old and new trail before a
  // partial render erases every stale ray while drawing the new one.
  for (int i = 0; i < 5; ++i) {
    int ex, ey;
    polar(RR - 1, angle - (4 - i) * 3.5f, ex, ey);
    gfxMarkLineTiles(mask, CX, CY, ex, ey, 2);
  }
}

void renderTile(TileCanvas& canvas, void* opaque) { drawRadar(canvas, opaque); }
}  // namespace

void RadarMode::begin(const Settings& settings) {
  radarInit(settings);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  lastFrame_ = 0;
  sweepAngle_ = 0.0f;
  needRender_ = true;
}

void RadarMode::invalidate(const Settings& settings) {
  radarInit(settings);
  radarForceRefresh();
  renderedOk_ = 0xFFFFFFFF;
  lastFrame_ = 0;
  sweepAngle_ = 0.0f;
  needRender_ = true;
}

void RadarMode::render(const Settings& settings) {
  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) {
    gfxMessage("AIRCRAFT RADAR", "SET HOME LOCATION", AMBER);
    return;
  }
  RadarRenderContext context{&settings, sweepAngle_};
  gfxRenderTiled(renderTile, &context, BG);
}

void RadarMode::service(const Settings& settings) {
  radarService(settings);

  if (settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) {
    if (needRender_) {
      render(settings);
      needRender_ = false;
    }
    return;
  }

  const uint32_t ok = radarLastOkMs();
  const bool error = radarError();
  if (ok != renderedOk_ || error != renderedError_) {
    renderedOk_ = ok;
    renderedError_ = error;
    needRender_ = true;  // target set changed: every tile may be affected
  }

  const uint32_t now = millis();
  if (needRender_) {
    sweepAngle_ = fmodf((now / static_cast<float>(RADAR_FRAME_MS)) * 7.5f,
                        360.0f);
    lastFrame_ = now;
    render(settings);
    needRender_ = false;
    return;
  }

  if (now - lastFrame_ < RADAR_FRAME_MS) return;
  lastFrame_ = now;

  const float nextAngle = fmodf(
      (now / static_cast<float>(RADAR_FRAME_MS)) * 7.5f, 360.0f);
  TileMask dirty = 0;
  markSweepTiles(dirty, sweepAngle_);
  markSweepTiles(dirty, nextAngle);
  sweepAngle_ = nextAngle;

  RadarRenderContext context{&settings, sweepAngle_};
  gfxRenderTileMask(renderTile, &context, BG, dirty);
}
