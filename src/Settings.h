// Settings.h — persisted configuration (LittleFS /config.json)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct SymbolCfg {
  char symbol[MAX_SYMBOL_LEN];
  char name[MAX_NAME_LEN];
};

struct Settings {
  // --- WiFi station (the network the device joins) ---
  String staSsid;
  String staPass;

  // --- Access point (config / fallback hotspot) ---
  String apSsid;
  String apPass;        // empty => open network
  String hostname;      // mDNS name => http://<hostname>.local

  // --- Data source ---
  String webhookUrl;    // n8n webhook base URL
  String range;         // chart timeframe token passed to the webhook (e.g. "1d")
  uint16_t points;      // sparkline points requested
  uint16_t pollSec;     // refresh period
  uint16_t rotateSec;   // per-symbol on-screen time
  uint16_t httpTimeout; // ms

  // --- Display ---
  uint8_t  brightness;        // 0..100 %
  bool     autoBrightness;    // use LDR on A0
  bool     backlightInverted; // active-low backlight
  uint8_t  rotation;          // 0..3 screen orientation
  bool     colorInverted;     // false: up=green/down=red ; true: swapped

  // --- What to show ---
  bool showName;
  bool showPrice;
  bool showChange;
  bool showChart;
  bool showRangeLabel;
  bool showUpdatedAgo;
  bool showPageDots;

  // --- Symbols (rotation list) ---
  SymbolCfg symbols[MAX_SYMBOLS];
  uint8_t   symbolCount;

  void setDefaults();
};

// Persistence
bool settingsBegin();                       // mount LittleFS
bool loadSettings(Settings& s);             // false => defaults applied
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);             // wipe file + defaults

// JSON <-> struct. `includeSecrets=false` masks passwords for the web API.
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);
void settingsApplyJson(Settings& s, JsonObjectConst root); // partial update allowed
