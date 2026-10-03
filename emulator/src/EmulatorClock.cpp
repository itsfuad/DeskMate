#include "EmulatorPlatform.h"

#include <chrono>
#include <ctime>

namespace {
uint32_t startEpoch = 0;
std::chrono::steady_clock::time_point startAt;
}

void emulatorSetEpoch(uint32_t epoch) {
  startEpoch = epoch;
  startAt = std::chrono::steady_clock::now();
}

extern "C" time_t __real_time(time_t* output);

// Linker wrapping substitutes the firmware's clock resource without changing
// shared renderers or the host clock used by desktop networking libraries.
extern "C" time_t __wrap_time(time_t* output) {
  if (!startEpoch) return __real_time(output);
  const uint64_t elapsedMs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startAt).count());
  const time_t now = static_cast<time_t>(startEpoch) +
      elapsedMs * emulatorTimeScale() / 1000UL;
  if (output) *output = now;
  return now;
}
