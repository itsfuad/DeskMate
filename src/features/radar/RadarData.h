#pragma once
#include <Arduino.h>
#include "config.h"

struct Aircraft {
  float lat;
  float lon;
  float track;
  float gs;
  int32_t altFt;
  char callsign[9];
  char category[3];  // ADS-B emitter class, e.g. A1..A7
  char type[5];      // ICAO type designator when supplied
  float distKm;
  float bearingDeg;
};

