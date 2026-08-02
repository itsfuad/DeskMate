#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RadarData.h"

void radarInit(const Settings& settings);
bool radarPoll(const Settings& settings, uint16_t budgetMs);
bool radarTest(const Settings& settings, uint16_t budgetMs,
               uint8_t& aircraftCount, int& httpCode);

uint8_t radarCount();
const Aircraft& aircraftAt(uint8_t index);
uint32_t radarLastOkMs();
bool radarError();
uint8_t getAircraftTrail(const char* callsign, float originLat, float originLon,
                         float* lats, float* lons, uint8_t maxPoints);
