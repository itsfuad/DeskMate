#include "Clock.h"
#include "Platform.h"
#include "config.h"

static String s_armedTz;   // last tzPosix armed, so clockReapply only re-arms on change

// Pure window predicate (minutes since local midnight). Handles a window that
// wraps midnight (start > end). An empty window (start == end) is never active.
//   start < end : [start, end)         e.g. 09:00..17:00
//   start > end : [start, 24h) ∪ [0,end)  e.g. 22:00..07:00
static bool nightWindowContains(int now, int start, int end) {
  if (start == end) return false;
  if (start < end)  return now >= start && now < end;
  return now >= start || now < end;
}

void clockBegin(const Settings& s) {
  const char* tz = s.clock.tzPosix.length() ? s.clock.tzPosix.c_str() : "UTC0";
  platformTimeBegin(tz, NTP_SERVER1, NTP_SERVER2);
  s_armedTz = s.clock.tzPosix;
}

void clockReapply(const Settings& s) {
  if (s.clock.tzPosix != s_armedTz) clockBegin(s);
}

bool clockSynced() { return platformTimeValid(); }

bool clockNow(struct tm& out) {
  if (!platformTimeValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

bool clockNightActive(const Settings& s) {
  if (!s.clock.nightEnabled) return false;
  struct tm t;
  if (!clockNow(t)) return false;
  int now = t.tm_hour * 60 + t.tm_min;
  return nightWindowContains(now, (int)s.clock.nightStartMin, (int)s.clock.nightEndMin);
}

String clockTimeStr() {
  struct tm t;
  if (!clockNow(t)) return String();
  char b[20];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
  return String(b);
}
