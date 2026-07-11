// StockClient.h — fetches ticker data from the configured source
// (Yahoo Finance, cash.ch, or a custom webhook)
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "StockData.h"

void  stocksInit(const Settings& s);        // (re)build list from settings
void  stocksService(const Settings& s);     // call often; self-times the polling
void  stocksForceRefresh();                 // poll ASAP (e.g. after config save)

uint8_t          stocksCount();
const StockData& stockAt(uint8_t i);
bool             stocksAnyValid();

// Effective change for display. With ticker.changeOnRange it is the move over
// the charted timeframe (live price vs the first spark point) so the sign
// agrees with the chart; otherwise (or without chart data) it is the
// provider's 1-day change. Returns false when neither is available.
// onRange (optional) reports which basis was actually used.
bool stockDisplayChange(const StockData& d, const TickerSettings& t,
                        float& chg, float& pct, bool* onRange = nullptr);
