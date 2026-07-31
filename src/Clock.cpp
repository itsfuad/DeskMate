#include "Clock.h"
#include "Platform.h"
#include "config.h"
#include <limits.h>

static String            s_armedTz;
static bool              s_ntpStarted = false;
static volatile uint32_t s_lastSyncMs = 0;
static volatile bool     s_haveSync = false;
static int32_t           s_runtimeOffsetSec = INT32_MIN;

static bool     s_nightLatched = false;
static bool     s_nightActive  = false;
static bool     s_nightHeld    = false;
static uint32_t s_lastResyncMs = 0;

static void onNtpSync() {
  s_lastSyncMs = millis();
  s_haveSync = true;
}

static bool nightWindowContains(int now, int start, int end) {
  if (start == end) return false;
  if (start < end) return now >= start && now < end;
  return now >= start || now < end;
}

// POSIX TZ offsets have the opposite sign to ordinary UTC offsets:
// UTC+06:00 is expressed as UTC-6. This fixed rule is refreshed from the
// browser-resolved location and from OpenWeather's current timezone offset.
static String fixedOffsetPosix(int32_t eastOfUtcSeconds) {
  eastOfUtcSeconds = constrain(eastOfUtcSeconds, -43200L, 50400L);
  const char sign = eastOfUtcSeconds >= 0 ? '-' : '+';
  uint32_t absolute = static_cast<uint32_t>(eastOfUtcSeconds >= 0
      ? eastOfUtcSeconds : -eastOfUtcSeconds);
  const uint16_t hours = absolute / 3600UL;
  const uint8_t minutes = (absolute % 3600UL) / 60UL;
  char value[20];
  if (minutes) snprintf(value, sizeof(value), "UTC%c%u:%02u", sign, hours, minutes);
  else snprintf(value, sizeof(value), "UTC%c%u", sign, hours);
  return String(value);
}

static String effectiveTz(const Settings& settings) {
  if (s_runtimeOffsetSec != INT32_MIN) return fixedOffsetPosix(s_runtimeOffsetSec);
  if (settings.clock.tz.length() || settings.clock.utcOffsetSec != 0)
    return fixedOffsetPosix(settings.clock.utcOffsetSec);
  if (settings.clock.tzPosix.length()) return settings.clock.tzPosix;
  return String("UTC0");
}

void clockBegin(const Settings& settings) {
  platformOnTimeSync(onNtpSync);
  const String tz = effectiveTz(settings);
  platformTimeBegin(tz.c_str(), NTP_SERVER1, NTP_SERVER2);
  s_armedTz = tz;
  s_ntpStarted = true;
}

void clockReapply(const Settings& settings) {
  const String desired = effectiveTz(settings);
  if (!s_ntpStarted || desired != s_armedTz) clockBegin(settings);
}

void clockUpdateUtcOffset(const Settings& settings, int32_t eastOfUtcSeconds) {
  eastOfUtcSeconds = constrain(eastOfUtcSeconds, -43200L, 50400L);
  if (s_runtimeOffsetSec == eastOfUtcSeconds) return;
  s_runtimeOffsetSec = eastOfUtcSeconds;
  clockReapply(settings);
}

void clockForceResync(const Settings& settings) {
  const String tz = effectiveTz(settings);
  platformTimeBegin(tz.c_str(), NTP_SERVER1, NTP_SERVER2);
  s_armedTz = tz;
  s_ntpStarted = true;
}

bool clockSynced() { return platformTimeValid(); }

bool clockTrusted() {
  return s_haveSync && platformTimeValid() &&
         static_cast<uint32_t>(millis() - s_lastSyncMs) <= NIGHT_NTP_TRUST_MS;
}

bool clockNow(struct tm& out) {
  if (!platformTimeValid()) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

void clockService(const Settings& settings) {
  if (!settings.clock.nightEnabled) {
    s_nightLatched = s_nightActive = s_nightHeld = false;
    s_lastResyncMs = 0;
    return;
  }
  struct tm timeInfo;
  const bool inWindow = clockNow(timeInfo) &&
      nightWindowContains(timeInfo.tm_hour * 60 + timeInfo.tm_min,
                          settings.clock.nightStartMin,
                          settings.clock.nightEndMin);
  if (!inWindow) {
    s_nightLatched = s_nightActive = s_nightHeld = false;
    s_lastResyncMs = 0;
    return;
  }
  if (s_nightLatched) {
    s_nightActive = true;
    s_nightHeld = false;
    return;
  }
  if (clockTrusted()) {
    s_nightLatched = true;
    s_nightActive = true;
    s_nightHeld = false;
    return;
  }

  s_nightActive = false;
  s_nightHeld = true;
  if (s_lastResyncMs == 0 ||
      static_cast<uint32_t>(millis() - s_lastResyncMs) >= NIGHT_NTP_RESYNC_MS) {
    s_lastResyncMs = millis() | 1;
    clockForceResync(settings);
  }
}

bool clockNightActive() { return s_nightActive; }
bool clockNightHeld() { return s_nightHeld; }

String clockTimeStr() {
  struct tm value;
  if (!clockNow(value)) return String();
  char text[20];
  if (strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &value) == 0) {
    return String();
  }
  return String(text);
}
