#include "RadarTrail.h"

#include <math.h>
#include <string.h>

namespace {
constexpr uint8_t kMaxTrails = 12;
constexpr float kMaximumRadiusKm = 100.0f;
constexpr float kCoordinateScale = 5000.0f;

struct AircraftTrail {
  char callsign[9];
  float lastLat;
  float lastLon;
  int16_t dLat[RADAR_TRAIL_MAX_POINTS];
  int16_t dLon[RADAR_TRAIL_MAX_POINTS];
  uint8_t count;
  uint32_t lastSeenMs;
};

AircraftTrail trails[kMaxTrails];

float distanceKm(float homeLat, float homeLon, float lat, float lon) {
  const float north = (lat - homeLat) * 111.0f;
  const float east = (lon - homeLon) * 111.0f *
                     cosf(homeLat * static_cast<float>(PI) / 180.0f);
  return sqrtf(north * north + east * east);
}
}  // namespace

void radarTrailReset() {
  for (uint8_t i = 0; i < kMaxTrails; ++i) {
    trails[i].callsign[0] = '\0';
    trails[i].count = 0;
    trails[i].lastSeenMs = 0;
  }
}

uint8_t getAircraftTrail(const char* callsign, float originLat, float originLon,
                         float* lats, float* lons, uint8_t maxPoints) {
  if (!callsign || !callsign[0] || !lats || !lons || maxPoints == 0) return 0;

  for (uint8_t i = 0; i < kMaxTrails; ++i) {
    const AircraftTrail& trail = trails[i];
    if (!trail.callsign[0] || strcmp(trail.callsign, callsign) != 0) continue;

    const uint8_t count = trail.count < maxPoints ? trail.count : maxPoints;
    for (uint8_t j = 0; j < count; ++j) {
      lats[j] = originLat + trail.dLat[j] / kCoordinateScale;
      lons[j] = originLon + trail.dLon[j] / kCoordinateScale;
    }
    return count;
  }
  return 0;
}

void radarTrailObserve(const char* callsign, float originLat, float originLon,
                       float lat, float lon, uint32_t seenMs) {
  if (!callsign || !callsign[0]) return;

  int found = -1;
  for (uint8_t i = 0; i < kMaxTrails; ++i) {
    if (trails[i].callsign[0] && strcmp(trails[i].callsign, callsign) == 0) {
      found = i;
      break;
    }
  }

  if (found < 0) {
    int slot = -1;
    uint32_t oldestMs = 0xFFFFFFFFUL;
    for (uint8_t i = 0; i < kMaxTrails; ++i) {
      if (!trails[i].callsign[0]) {
        slot = i;
        break;
      }
      if (trails[i].lastSeenMs < oldestMs) {
        oldestMs = trails[i].lastSeenMs;
        slot = i;
      }
    }
    if (slot < 0) return;

    AircraftTrail& trail = trails[slot];
    strlcpy(trail.callsign, callsign, sizeof(trail.callsign));
    trail.lastLat = lat;
    trail.lastLon = lon;
    trail.count = 0;
    trail.lastSeenMs = seenMs;
    return;
  }

  AircraftTrail& trail = trails[found];
  const float movementKm = distanceKm(trail.lastLat, trail.lastLon, lat, lon);
  if (movementKm >= RADAR_TRAIL_GAP_KM) {
    for (int j = RADAR_TRAIL_MAX_POINTS - 1; j > 0; --j) {
      trail.dLat[j] = trail.dLat[j - 1];
      trail.dLon[j] = trail.dLon[j - 1];
    }
    trail.dLat[0] = static_cast<int16_t>(
        roundf((trail.lastLat - originLat) * kCoordinateScale));
    trail.dLon[0] = static_cast<int16_t>(
        roundf((trail.lastLon - originLon) * kCoordinateScale));
    trail.lastLat = lat;
    trail.lastLon = lon;
    if (trail.count < RADAR_TRAIL_MAX_POINTS) ++trail.count;

    uint8_t kept = 0;
    for (uint8_t j = 0; j < trail.count; ++j) {
      const float pointLat = originLat + trail.dLat[j] / kCoordinateScale;
      const float pointLon = originLon + trail.dLon[j] / kCoordinateScale;
      if (distanceKm(lat, lon, pointLat, pointLon) <= kMaximumRadiusKm) {
        trail.dLat[kept] = trail.dLat[j];
        trail.dLon[kept] = trail.dLon[j];
        ++kept;
      }
    }
    trail.count = kept;
  }
  trail.lastSeenMs = seenMs;
}
