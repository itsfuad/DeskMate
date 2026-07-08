// RadarClient.h — fetches nearby aircraft (adsb.fi direct, or a LAN webhook).
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RadarData.h"

void radarInit(const Settings& s);       // reset + poll ASAP
void radarService(const Settings& s);    // call often; self-times the polling
void radarForceRefresh();                // poll on the next service() call

uint8_t         radarCount();            // aircraft currently held (nearest first)
const Aircraft& aircraftAt(uint8_t i);
uint32_t        radarLastOkMs();         // millis() of last good fetch (0 = never)
bool            radarError();            // most recent fetch failed
