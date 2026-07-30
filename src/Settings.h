// Settings.h — persisted configuration (LittleFS /config.json)
//
// Layout is segmented per feature: shared device/network fields live at the top
// level, and each desk feature owns a nested settings slice.
// config.json mirrors this with weather, network, radar, GitHub, and clock objects.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// A home-area airport marker (radar feature), configured in the web UI.
struct Airport {
  char  icao[MAX_ICAO_LEN];
  float lat, lon;
};

// One saved WiFi station network. The device keeps up to MAX_WIFI_NETS and
// joins the strongest visible one at boot (hidden SSIDs are tried last).
struct WifiCred {
  String ssid;
  String pass;
};

// ---- Clock / night mode slice (device-wide) --------------------------------
struct ClockSettings {
  String   tz;            // IANA display name, e.g. "Europe/Rome" (UI round-trip)
  String   tzPosix;       // POSIX TZ rule the device feeds SNTP
  bool     nightEnabled;  // dim/blank on a nightly schedule
  uint16_t nightStartMin; // minutes since local midnight (0..1439)
  uint16_t nightEndMin;
  uint8_t  nightLevel;    // 0..100, 0 = backlight off

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Plane radar feature slice --------------------------------------------
struct RadarSettings {
  float    lat;           // home latitude  (0,0 = not set yet)
  float    lon;           // home longitude
  uint8_t  source;        // RADAR_SRC_DIRECT or RADAR_SRC_WEBHOOK
  String   webhookUrl;    // LAN proxy base URL (when source=webhook)
  uint16_t rangeKm;       // outer ring radius
  uint16_t pollSec;       // refresh period
  bool     unitsMi;       // show distances in miles instead of km

  bool     showLabels;    // callsign + altitude next to each aircraft
  bool     showVectors;   // speed/heading vector line
  bool     showRimDots;   // aircraft beyond the ring as bearing dots on the rim
  uint8_t  uiScale;       // marker/text size: 0 = small, 1 = medium, 2 = large
  uint16_t minAltFt;      // hide aircraft below this altitude (ft); 0 = show all

  Airport airports[MAX_AIRPORTS];
  uint8_t airportCount;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};


struct WeatherSettings {
  float lat, lon;
  String city;
  uint16_t pollSec;
  void setDefaults(); void toJson(JsonObject o) const; void fromJson(JsonObjectConst o);
};

struct NetworkSettings {
  String probeHost;
  uint16_t probePort;
  uint16_t pollSec;
  void setDefaults(); void toJson(JsonObject o) const; void fromJson(JsonObjectConst o);
};

struct GithubRepoCfg { char repo[48]; };
struct GithubSettings {
  String user;
  String token;
  uint16_t pollSec;
  GithubRepoCfg repos[MAX_GH_REPOS];
  uint8_t repoCount;
  void setDefaults(); void toJson(JsonObject o, bool includeSecrets) const; void fromJson(JsonObjectConst o);
};

// ---- Top-level settings ----------------------------------------------------
struct Settings {
  // --- WiFi station networks (the device joins one of these) ---
  WifiCred wifi[MAX_WIFI_NETS];
  uint8_t  wifiCount;

  // --- Access point (config / fallback hotspot) ---
  String apSsid;
  String apPass;        // empty => open network
  String hostname;      // mDNS name => http://<hostname>.local

  // --- Active feature ---
  uint8_t mode;         // MODE_WEATHER / MODE_NETWORK / MODE_RADAR / MODE_GITHUB / MODE_CAROUSEL

  // --- Carousel (mode == MODE_CAROUSEL): dwell + which features rotate ---
  uint16_t carouselSec;
  bool carouselWeather, carouselNetwork, carouselRadar, carouselGithub;

  // --- Shared HTTP / display ---
  uint16_t httpTimeout; // ms
  uint8_t  brightness;        // 0..100 %
  bool     autoBrightness;    // use LDR on A0
  bool     backlightInverted; // active-low backlight
  uint8_t  rotation;          // 0..3 screen orientation

  // --- Feature slices ---
  WeatherSettings weather;
  NetworkSettings network;
  RadarSettings radar;
  GithubSettings github;
  ClockSettings  clock;

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
