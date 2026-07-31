#include "PreviewApi.h"
#include "RadarClient.h"

#include <algorithm>

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
