#pragma once
#include "Settings.h"
#include <time.h>

void clockBegin(const Settings& settings);
void clockReapply(const Settings& settings);
void clockUpdateUtcOffset(const Settings& settings, int32_t eastOfUtcSeconds);
void clockForceResync(const Settings& settings);
void clockService(const Settings& settings);

bool clockSynced();
bool clockTrusted();
bool clockNow(struct tm& out);
bool clockNightActive();
bool clockNightHeld();
void clockFormatTime(const Settings& settings, const struct tm& value,
                     char* timeText, size_t timeTextSize,
                     char* meridiem, size_t meridiemSize);
bool clockTimeStr(const Settings& settings, char* out, size_t outSize);
String clockTimeStr(const Settings& settings);
