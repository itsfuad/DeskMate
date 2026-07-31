// Settings.h — persisted DeskMate configuration (LittleFS /config.json).
#pragma once
#include <Arduino.h>
#if defined(DESKMATE_PREVIEW)
class JsonObject {};
class JsonObjectConst {};
#else
#include <ArduinoJson.h>
#endif
#include "config.h"

struct Airport {
  char icao[MAX_ICAO_LEN];
  float lat;
  float lon;
};

struct WifiCred {
  String ssid;
  String pass;
};

struct ClockSettings {
  String tz;             // IANA label resolved by the browser (e.g. Asia/Dhaka)
  String tzAbbr;         // current abbreviation (e.g. BST, CEST)
  String tzPosix;        // optional legacy/manual rule; normally generated from offset
  int32_t utcOffsetSec;  // current UTC offset, refreshed by OpenWeather
  bool use24Hour;        // true = 23:45, false = 11:45 PM
  bool nightEnabled;
  uint16_t nightStartMin;
  uint16_t nightEndMin;
  uint8_t nightLevel;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

struct RadarSettings {
  float lat;
  float lon;
  uint8_t source;
  String webhookUrl;
  uint16_t rangeKm;
  uint16_t pollSec;
  bool unitsMi;
  bool showLabels;
  bool showVectors;
  bool showRimDots;
  uint8_t uiScale;
  uint16_t minAltFt;
  Airport airports[MAX_AIRPORTS];
  uint8_t airportCount;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

struct WeatherSettings {
  float lat;
  float lon;
  String city;
  String country;
  String timezone;
  String timezoneAbbr;
  int32_t utcOffsetSec;
  bool locationVerified;
  String apiKey;
  bool metric;
  uint16_t pollSec;

  void setDefaults();
  void toJson(JsonObject o, bool includeSecrets) const;
  void fromJson(JsonObjectConst o);
};

struct NetworkSettings {
  String probeHost;
  uint16_t probePort;
  String dnsHost;
  uint16_t pollSec;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

struct GithubSettings {
  String token;
  String login;
  uint8_t rangeMonths;   // 1, 3, 6 or 12
  uint16_t pollSec;

  void setDefaults();
  void toJson(JsonObject o, bool includeSecrets) const;
  void fromJson(JsonObjectConst o);
};

struct Settings {
  WifiCred wifi[MAX_WIFI_NETS];
  uint8_t wifiCount;

  String apSsid;
  String apPass;
  String hostname;

  uint8_t mode;
  uint16_t carouselSec;
  bool carouselWeather;
  bool carouselNetwork;
  bool carouselRadar;
  bool carouselGithub;

  uint16_t httpTimeout;
  uint8_t brightness;
  bool autoBrightness;
  bool backlightInverted;
  uint8_t rotation;

  WeatherSettings weather;
  NetworkSettings network;
  RadarSettings radar;
  GithubSettings github;
  ClockSettings clock;

  void setDefaults();
};

bool settingsBegin();
bool loadSettings(Settings& s);
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);
void settingsApplyJson(Settings& s, JsonObjectConst root);
