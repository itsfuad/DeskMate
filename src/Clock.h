// Clock.h — SNTP wall-clock + scheduled night-mode window.
//
// Owns the device's only notion of real time: arms SNTP with the configured
// POSIX TZ rule (DST handled by the rule), and answers whether the current
// local time falls inside the night-mode window. Until SNTP has synced,
// clockNightActive() returns false so the screen fails safe to ON.
#pragma once
#include "Settings.h"
#include <time.h>

void   clockBegin(const Settings& s);        // arm SNTP (call once WiFi is up)
void   clockReapply(const Settings& s);      // re-arm iff tzPosix changed
bool   clockSynced();                        // SNTP has set the clock
bool   clockNow(struct tm& out);             // local broken-down time; false if unsynced
bool   clockNightActive(const Settings& s);  // true inside the night window
String clockTimeStr();                        // "YYYY-MM-DD HH:MM" or "" if unsynced
