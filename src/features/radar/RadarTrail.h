#pragma once

#include <Arduino.h>

constexpr uint8_t RADAR_TRAIL_MAX_POINTS = 10;
constexpr float RADAR_TRAIL_GAP_KM = 5.0f;

void radarTrailReset();
void radarTrailBeginUpdate();
void radarTrailCommitUpdate();
void radarTrailDiscardUpdate();
void radarTrailObserve(const char* callsign, float originLat, float originLon,
                       float lat, float lon, uint32_t seenMs);
uint8_t getAircraftTrail(const char* callsign, float originLat, float originLon,
                         float* lats, float* lons, uint8_t maxPoints);
