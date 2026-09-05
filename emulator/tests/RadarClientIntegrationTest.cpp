#include "EmulatorPlatform.h"
#include "RadarClient.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr float HomeLat = 23.8103f;
constexpr float HomeLon = 90.4125f;

bool near(float actual, float expected, float tolerance = 0.0004f) {
  return std::fabs(actual - expected) <= tolerance;
}
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fputs("usage: radar-client-test FIXTURE_DIR STATE_DIR\n", stderr);
    return 2;
  }
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                    argv[2], 18082, argv[1]);
  emulatorSetMillis(1000);

  Settings settings{};
  settings.httpTimeout = 3000;
  settings.radar.lat = HomeLat;
  settings.radar.lon = HomeLon;
  settings.radar.rangeKm = 50;
  settings.radar.source = RADAR_SRC_DIRECT;
  settings.radar.minAltFt = 0;
  radarInit(settings);

  for (uint8_t sample = 0; sample < 3; ++sample) {
    emulatorSetMillis(1000 + static_cast<uint32_t>(sample) * 30000UL);
    if (!radarPoll(settings, 3000)) {
      std::fprintf(stderr, "recorded ADS-B poll %u failed\n", sample + 1);
      return 1;
    }
  }

  float latitudes[10] = {};
  float longitudes[10] = {};
  const uint8_t trailCount = getAircraftTrail(
      "CLX7956", HomeLat, HomeLon, latitudes, longitudes, 10);
  if (trailCount != 2 ||
      !near(latitudes[0], 23.708679f) || !near(longitudes[0], 90.047455f) ||
      !near(latitudes[1], 23.778045f) || !near(longitudes[1], 89.932683f)) {
    std::fputs("real ADS-B observations did not produce the expected trail\n", stderr);
    return 1;
  }

  bool foundCurrent = false;
  for (uint8_t index = 0; index < radarCount(); ++index) {
    const Aircraft& aircraft = aircraftAt(index);
    if (std::strcmp(aircraft.callsign, "CLX7956") != 0) continue;
    foundCurrent = near(aircraft.headingDeg, 123.47f, 0.02f);
    break;
  }
  if (!foundCurrent) {
    std::fputs("current real aircraft heading was not preserved\n", stderr);
    return 1;
  }

  const uint8_t countBeforeFailure = radarCount();
  if (radarPoll(settings, 3000)) {
    std::fputs("truncated ADS-B response unexpectedly succeeded\n", stderr);
    return 1;
  }
  float preservedLatitudes[10] = {};
  float preservedLongitudes[10] = {};
  const uint8_t preservedTrailCount = getAircraftTrail(
      "CLX7956", HomeLat, HomeLon, preservedLatitudes, preservedLongitudes, 10);
  if (radarCount() != countBeforeFailure || preservedTrailCount != trailCount ||
      !near(preservedLatitudes[0], latitudes[0]) ||
      !near(preservedLongitudes[0], longitudes[0]) ||
      !near(preservedLatitudes[1], latitudes[1]) ||
      !near(preservedLongitudes[1], longitudes[1])) {
    std::fputs("failed ADS-B response changed the previous snapshot\n", stderr);
    return 1;
  }
  if (radarPoll(settings, 3000) || radarCount() != countBeforeFailure) {
    std::fputs("ADS-B response without an ac array changed the snapshot\n", stderr);
    return 1;
  }

  std::puts("Recorded ADS-B behavior and failed-response rollback passed.");
  return 0;
}
