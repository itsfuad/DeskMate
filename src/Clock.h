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
String clockTimeStr();
