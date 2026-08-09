#include "RadarTrail.h"

#include <cmath>
#include <cstdio>

namespace {
constexpr float kOriginLat = 23.8103f;
constexpr float kOriginLon = 90.4125f;

void observeNorth(float northKm, uint32_t seenMs) {
  radarTrailObserve("TEST01", kOriginLat, kOriginLon,
                    kOriginLat + northKm / 111.0f, kOriginLon, seenMs);
}

bool near(float actual, float expected, float tolerance) {
  return std::fabs(actual - expected) <= tolerance;
}
}  // namespace

int main() {
  float latitudes[20];
  float longitudes[20];

  radarTrailReset();
  observeNorth(0.0f, 1);
  observeNorth(4.9f, 2);
  if (getAircraftTrail("TEST01", kOriginLat, kOriginLon,
                       latitudes, longitudes, 20) != 0) {
    std::fputs("trail accepted movement below 5 km\n", stderr);
    return 1;
  }

  observeNorth(5.1f, 3);
  if (getAircraftTrail("TEST01", kOriginLat, kOriginLon,
                       latitudes, longitudes, 20) != 1 ||
      !near(latitudes[0], kOriginLat, 0.0003f)) {
    std::fputs("trail rejected movement above 5 km\n", stderr);
    return 1;
  }

  for (uint8_t point = 2; point <= 20; ++point) {
    observeNorth(point * 5.1f, point + 2);
  }
  const uint8_t count = getAircraftTrail(
      "TEST01", kOriginLat, kOriginLon, latitudes, longitudes, 20);
  const float newestNorthKm = (latitudes[0] - kOriginLat) * 111.0f;
  if (count != 10 || !near(newestNorthKm, 19.0f * 5.1f, 0.05f)) {
    std::fputs("trail did not retain the newest 10 observations\n", stderr);
    return 1;
  }

  std::puts("Radar trail behavior passed.");
  return 0;
}
