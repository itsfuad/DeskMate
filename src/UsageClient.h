// UsageClient.h — pulls Claude usage from the local daemon's HTTP endpoint.
//
// The companion daemon (see daemon/) polls the Claude API rate-limit headers and
// serves the latest snapshot as a tiny JSON object. The device GETs that URL on
// its poll schedule — exactly like the stock webhook, but a different contract.
#pragma once
#include "Settings.h"
#include "UsageData.h"

void usageInit(const Settings& s);
void usageService(const Settings& s);     // call each loop; fetches on the poll schedule
void usageForceRefresh();                 // poll again on the next service() call
const UsageData& usageGet();
bool usageFresh(uint32_t withinMs);       // true if the last good update is recent enough
