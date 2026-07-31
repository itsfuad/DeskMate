#include "Clock.h"

#include <cstdio>
#include <cstring>

void clockBegin(const Settings&) {}
void clockReapply(const Settings&) {}
void clockUpdateUtcOffset(const Settings&, int32_t) {}
void clockForceResync(const Settings&) {}
void clockService(const Settings&) {}
bool clockSynced() { return true; }
bool clockTrusted() { return true; }
bool clockNow(struct tm&) { return false; }
bool clockNightActive() { return false; }
bool clockNightHeld() { return false; }

void clockFormatTime(const Settings& settings, const struct tm& value,
                     char* timeText, size_t timeTextSize,
                     char* meridiem, size_t meridiemSize) {
  if (settings.clock.use24Hour) {
    std::snprintf(timeText, timeTextSize, "%02d:%02d", value.tm_hour,
                  value.tm_min);
    if (meridiemSize) meridiem[0] = '\0';
    return;
  }

  const int hour = value.tm_hour % 12 ? value.tm_hour % 12 : 12;
  std::snprintf(timeText, timeTextSize, "%d:%02d", hour, value.tm_min);
  std::snprintf(meridiem, meridiemSize, "%s", value.tm_hour < 12 ? "AM" : "PM");
}

String clockTimeStr(const Settings&) { return String("12:00"); }
