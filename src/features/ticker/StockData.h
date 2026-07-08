// StockData.h — runtime (volatile) data for one ticker
#pragma once
#include <Arduino.h>
#include "config.h"

struct StockData {
  char  symbol[MAX_SYMBOL_LEN];
  char  name[MAX_NAME_LEN];
  char  currency[6];
  char  rangeLabel[8];

  float price;
  float change;       // absolute change in price units
  float changePct;    // percentage change
  bool  hasChange;    // a change value was provided/derived

  float   spark[MAX_SPARK_POINTS];
  uint8_t sparkCount;

  bool     valid;     // has been successfully populated at least once
  bool     error;     // most recent fetch failed
  bool     userNamed; // user supplied a custom name (don't override from source)
  uint32_t lastOkMs;  // millis() of last good update

  void clear() {
    symbol[0] = name[0] = currency[0] = rangeLabel[0] = 0;
    price = change = changePct = 0;
    hasChange = false;
    sparkCount = 0;
    valid = false;
    error = false;
    userNamed = false;
    lastOkMs = 0;
  }
};
