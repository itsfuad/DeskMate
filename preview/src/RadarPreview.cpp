#include "PreviewApi.h"
#include "RadarClient.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
PreviewRadarState currentState;
Aircraft emptyAircraft{};
}

void previewSetRadarState(const PreviewRadarState& state) {
  currentState = state;
}

void radarInit(const Settings&) {}
bool radarPoll(const Settings&, uint16_t) { return true; }
bool radarTest(const Settings&, uint16_t, uint8_t& aircraftCount,
               int& httpCode) {
  aircraftCount = currentState.aircraftCount;
  httpCode = 200;
  return true;
}

uint8_t radarCount() { return currentState.aircraftCount; }

const Aircraft& aircraftAt(uint8_t index) {
  return index < currentState.aircraftCount ? currentState.aircraft[index]
                                             : emptyAircraft;
}

uint32_t radarLastOkMs() { return currentState.lastOkMs; }
bool radarError() { return currentState.error; }

uint8_t getAircraftTrail(const char* callsign, float originLat, float originLon,
                         float* latitudes, float* longitudes,
                         unsigned char maxPoints) {
  if (!callsign || !latitudes || !longitudes || maxPoints == 0) return 0;

  const Aircraft* target = nullptr;
  for (uint8_t index = 0; index < currentState.aircraftCount; ++index) {
    if (std::strncmp(currentState.aircraft[index].callsign, callsign,
                     sizeof(currentState.aircraft[index].callsign)) == 0) {
      target = &currentState.aircraft[index];
      break;
    }
  }
  if (!target) return 0;

  // The preview has no historical feed. Generate a short deterministic trail
  // behind each target so the trails presentation is still testable.
  const uint8_t count = std::min<uint8_t>(maxPoints, 6);
  constexpr float kPi = 3.14159265f;
  const float bearing = target->bearingDeg * kPi / 180.0f;
  const float latitudeScale = 111.0f;
  const float longitudeScale = 111.0f * std::max(0.2f, std::cos(originLat * kPi / 180.0f));
  for (uint8_t point = 0; point < count; ++point) {
    const float distanceKm = target->distKm + 1.5f * static_cast<float>(point);
    const float northKm = std::cos(bearing) * distanceKm;
    const float eastKm = std::sin(bearing) * distanceKm;
    latitudes[point] = originLat - northKm / latitudeScale;
    longitudes[point] = originLon - eastKm / longitudeScale;
  }
  return count;
}
