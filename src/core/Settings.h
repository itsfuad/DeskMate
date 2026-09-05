// Settings.h — persisted DeskMate configuration (LittleFS /config.json).
#pragma once
#include <Arduino.h>
#include "JsonScanner.h"
#include "JsonWriter.h"
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
  bool showRimDots;
  bool showTrails;
  uint8_t uiScale;
  uint16_t minAltFt;
  Airport airports[MAX_AIRPORTS];
  uint8_t airportCount;

  void setDefaults();
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
};

struct NetworkSettings {
  String probeHost;
  uint16_t probePort;
  String dnsHost;
  uint16_t pollSec;

  void setDefaults();
};

struct GithubSettings {
  String token;
  String login;
  uint8_t rangeMonths;   // fixed at 3 months
  uint16_t pollSec;
  // Which of the screen's pages take part in its rotation. At least one stays
  // selected; the screen's share of display time is divided between them.
  bool pageInbox;
  bool pagePulls;
  bool pagePulse;

  uint8_t pageCount() const {
    return static_cast<uint8_t>(pageInbox) + static_cast<uint8_t>(pagePulls) +
           static_cast<uint8_t>(pagePulse);
  }

  void setDefaults();
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

// Presence describes only correctly typed schema values. Section and aggregate
// flags make partial requests straightforward for WebPortal; field flags retain
// the distinctions its validation currently needs.
struct SettingsJsonPresence {
  bool configVersion = false;
  uint16_t configVersionValue = 0;
  bool hostname = false;
  bool wifi = false;
  bool wifiEntriesValid = true;
  bool staSsid = false;
  bool staPass = false;
  bool apSsid = false;
  bool apPass = false;

  bool mode = false;
  bool carouselSec = false;
  bool carouselSecValid = true;
  bool carouselSelection = false;
  bool carouselSelectionValid = true;
  bool httpTimeout = false;
  bool brightness = false;
  bool brightnessValue = false;
  bool brightnessValueValid = true;
  bool autoBrightness = false;
  bool backlightInverted = false;
  bool rotation = false;
  bool rotationValid = true;

  bool clock = false;
  bool clockNightStart = false;
  bool clockNightStartValid = true;
  bool clockNightEnd = false;
  bool clockNightEndValid = true;
  bool clockUse24Hour = false;
  bool clockUse24HourInvalid = false;
  bool clockNightLevel = false;
  bool clockNightLevelValid = true;

  bool weather = false;
  bool weatherLat = false;
  bool weatherLon = false;
  bool weatherCity = false;
  bool weatherTimezone = false;
  bool weatherLocationVerified = false;
  bool weatherPollSec = false;
  bool weatherPollSecValid = true;
  bool weatherApiKey = false;
  bool weatherApiKeyValid = true;

  bool network = false;
  bool networkProbeHost = false;
  bool networkProbePort = false;
  bool networkProbePortValid = true;
  bool networkDnsHost = false;
  bool networkPollSec = false;
  bool networkPollSecValid = true;

  bool radar = false;
  bool radarLat = false;
  bool radarLon = false;
  bool radarSource = false;
  bool radarWebhookUrl = false;
  bool radarRangeKm = false;
  bool radarRangeKmValid = true;
  bool radarPollSec = false;
  bool radarPollSecValid = true;
  bool radarAirports = false;
  bool radarAirportsValid = true;

  bool githubData = false;
  bool githubDisplay = false;
  bool githubRangeMonths = false;
  bool githubRangeMonthsValid = true;
  bool githubPollSec = false;
  bool githubPollSecValid = true;
  bool githubToken = false;
  bool githubTokenValid = true;
};

bool settingsBegin();
bool loadSettings(Settings& s);
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);

// Parse exactly one complete JSON object. `out` and `presence` are changed only
// on success; missing fields inherit from `base`.
bool settingsParseJson(Settings& out, const Settings& base,
                       const char* json, size_t length,
                       SettingsJsonPresence* presence = nullptr,
                       JsonScanner::Error* error = nullptr);
bool settingsParseJson(Settings& out, const Settings& base, Stream& stream,
                       int contentLength = JsonScanner::UnknownLength,
                       size_t maxBytes = JsonScanner::DefaultMaxBytes,
                       SettingsJsonPresence* presence = nullptr,
                       JsonScanner::Error* error = nullptr);

// Write settings fields into an open object, allowing WebPortal to append its
// response-only fields. settingsWriteJson writes and closes a complete object.
bool settingsWriteJsonFields(JsonWriter& writer, const Settings& settings,
                             bool includeSecrets);
bool settingsWriteJson(JsonWriter& writer, const Settings& settings,
                       bool includeSecrets);
