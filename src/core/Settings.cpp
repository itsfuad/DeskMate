#include "Settings.h"
#include "Platform.h"
#include <LittleFS.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

static const char* CONFIG_PATH = "/config.json";
static const char* CONFIG_TEMP_PATH = "/config.json.tmp";
static constexpr uint16_t CONFIG_VERSION = 1;

static bool isLegacySetupSsid(const String& value) {
  static const char legacy[] = {
      83, 109, 97, 108, 108, 84, 86, 45, 83, 101, 116, 117, 112, 0};
  if (value.length() != sizeof(legacy) - 1) return false;
  for (size_t i = 0; i < sizeof(legacy) - 1; ++i) {
    if (value[i] != legacy[i]) return false;
  }
  return true;
}

static uint16_t hhmmToMin(const char* text, uint16_t fallback) {
  if (!text || !text[0]) return fallback;
  int hour = 0;
  int minute = 0;
  if (sscanf(text, "%d:%d", &hour, &minute) != 2) return fallback;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return fallback;
  return static_cast<uint16_t>(hour * 60 + minute);
}

static bool validHhmm(const char* value) {
  if (!value || strlen(value) != 5 || value[2] != ':') return false;
  if (!isdigit(static_cast<unsigned char>(value[0])) ||
      !isdigit(static_cast<unsigned char>(value[1])) ||
      !isdigit(static_cast<unsigned char>(value[3])) ||
      !isdigit(static_cast<unsigned char>(value[4]))) return false;
  const int hour = (value[0] - '0') * 10 + value[1] - '0';
  const int minute = (value[3] - '0') * 10 + value[4] - '0';
  return hour <= 23 && minute <= 59;
}

static bool validAirportCode(const char* value) {
  if (!value || !value[0] || strlen(value) >= MAX_ICAO_LEN) return false;
  for (size_t i = 0; value[i]; ++i) {
    if (!isalnum(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

static void minToHhmm(uint16_t value, char output[6]) {
  if (value > 1439) value = 0;
  snprintf(output, 6, "%02u:%02u", static_cast<unsigned>(value / 60),
           static_cast<unsigned>(value % 60));
}

void ClockSettings::setDefaults() {
  tz = DEFAULT_TZ_NAME;
  tzAbbr = "UTC";
  tzPosix = "";
  utcOffsetSec = 0;
  use24Hour = DEFAULT_24_HOUR;
  nightEnabled = DEFAULT_NIGHT_ENABLED;
  nightStartMin = DEFAULT_NIGHT_START_MIN;
  nightEndMin = DEFAULT_NIGHT_END_MIN;
  nightLevel = DEFAULT_NIGHT_LEVEL;
}

void RadarSettings::setDefaults() {
  lat = DEFAULT_RADAR_LAT;
  lon = DEFAULT_RADAR_LON;
  source = DEFAULT_RADAR_SRC;
  webhookUrl = "";
  rangeKm = DEFAULT_RADAR_RANGE_KM;
  pollSec = DEFAULT_RADAR_POLL_SEC;
  unitsMi = false;
  showLabels = true;
  showRimDots = true;
  showTrails = true;
  uiScale = 1;
  minAltFt = 0;
  airportCount = 0;
  for (uint8_t i = 0; i < MAX_AIRPORTS; ++i) {
    airports[i].icao[0] = 0;
    airports[i].lat = 0;
    airports[i].lon = 0;
  }
}

void WeatherSettings::setDefaults() {
  lat = 23.8103f;
  lon = 90.4125f;
  city = "Dhaka";
  country = "Bangladesh";
  timezone = "Asia/Dhaka";
  timezoneAbbr = "+06";
  utcOffsetSec = 21600;
  locationVerified = true;
  apiKey = "";
  metric = true;
  pollSec = DEFAULT_WEATHER_POLL_SEC;
}

void NetworkSettings::setDefaults() {
  probeHost = "1.1.1.1";
  probePort = 443;
  dnsHost = "github.com";
  pollSec = DEFAULT_NETWORK_POLL_SEC;
}

void GithubSettings::setDefaults() {
  token = "";
  login = "";
  rangeMonths = 3;
  pollSec = DEFAULT_GITHUB_POLL_SEC;
  pageInbox = true;
  pagePulls = true;
  pagePulse = true;
}

void Settings::setDefaults() {
  wifiCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_NETS; ++i) {
    wifi[i].ssid = "";
    wifi[i].pass = "";
  }

  apSsid = DEFAULT_AP_SSID;
  apPass = DEFAULT_AP_PASS;
  hostname = String(DEFAULT_HOSTNAME) + "-" + String(platformChipId() & 0xFFFF, HEX);

  mode = DEFAULT_MODE;
  carouselSec = DEFAULT_CAROUSEL_SEC;
  carouselWeather = true;
  carouselNetwork = true;
  carouselRadar = true;
  carouselGithub = true;

  httpTimeout = DEFAULT_HTTP_TIMEOUT;
  brightness = DEFAULT_BRIGHTNESS;
  autoBrightness = false;
  backlightInverted = TFT_BL_DEFAULT_INVERTED;
  rotation = 0;

  weather.setDefaults();
  network.setDefaults();
  radar.setDefaults();
  github.setDefaults();
  clock.setDefaults();
  clock.tz = weather.timezone;
  clock.tzAbbr = weather.timezoneAbbr;
  clock.utcOffsetSec = weather.utcOffsetSec;
}

namespace {

bool keyIs(const JsonScanner& scanner, const char* key) {
  return !strcmp(scanner.key(), key);
}

bool stringValue(JsonScanner::Value type) {
  return type == JsonScanner::Value::String;
}

bool boolValue(JsonScanner::Value type, const char* text, bool& output) {
  if (type != JsonScanner::Value::Boolean) return false;
  output = text[0] == 't';
  return true;
}

bool intValue(JsonScanner::Value type, const char* text, int32_t& output) {
  if (type != JsonScanner::Value::Number || strchr(text, '.') ||
      strchr(text, 'e') || strchr(text, 'E')) return false;
  errno = 0;
  char* end = nullptr;
  const long long value = strtoll(text, &end, 10);
  if (errno == ERANGE || !end || *end || value < INT32_MIN || value > INT32_MAX)
    return false;
  output = static_cast<int32_t>(value);
  return true;
}

bool floatValue(JsonScanner::Value type, const char* text, float& output) {
  if (type != JsonScanner::Value::Number) return false;
  errno = 0;
  char* end = nullptr;
  const double value = strtod(text, &end);
  if (errno == ERANGE || !end || *end || !isfinite(value)) return false;
  output = static_cast<float>(value);
  return isfinite(output);
}

template <typename T>
T clampInt(int32_t value, int32_t low, int32_t high) {
  if (value < low) value = low;
  if (value > high) value = high;
  return static_cast<T>(value);
}

struct ParseContext {
  explicit ParseContext(const Settings& source)
      : base(source), candidate(source), weather(candidate.weather),
        network(candidate.network), radar(candidate.radar),
        github(candidate.github), clock(source.clock) {}

  const Settings& base;
  Settings candidate;
  WeatherSettings& weather;
  NetworkSettings& network;
  RadarSettings& radar;
  GithubSettings& github;
  ClockSettings clock;
  SettingsJsonPresence presence;

  String legacySsid;
  String legacyPass;
  bool legacySsidValue = false;
  bool legacyPassValue = false;

  WifiCred wifiEntry;
  Airport airportEntry{};
  bool inWifiEntry = false;
  bool inAirportEntry = false;
  bool airportIcao = false;
  bool airportLat = false;
  bool airportLon = false;
  bool rootObject = false;
  bool wrongRoot = false;
  bool truncatedValue = false;
  uint8_t depth = 0;

  bool clockTz = false;
  bool clockTzAbbr = false;
  bool clockTzPosix = false;
  bool clockUtcOffset = false;
  bool clockUse24 = false;
  bool clockNightEnabled = false;
  bool clockNightStart = false;
  bool clockNightEnd = false;
  bool clockNightLevel = false;
};

void retainWifiSecret(ParseContext& context, WifiCred& entry) {
  if (entry.pass.length()) return;
  for (uint8_t i = 0; i < context.base.wifiCount; ++i) {
    if (context.base.wifi[i].ssid == entry.ssid) {
      entry.pass = context.base.wifi[i].pass;
      return;
    }
  }
}

void onContainer(void* opaque, const JsonScanner& scanner,
                 JsonScanner::Container event) {
  ParseContext& context = *static_cast<ParseContext*>(opaque);
  const bool start = event == JsonScanner::Container::ObjectStart ||
                     event == JsonScanner::Container::ArrayStart;
  const bool object = event == JsonScanner::Container::ObjectStart ||
                      event == JsonScanner::Container::ObjectEnd;

  if (start) {
    if (context.depth == 0) {
      context.rootObject = object;
      context.wrongRoot = !object;
    } else if (context.depth == 1 && object) {
      const char* section = scanner.container();
      if (!strcmp(section, "clock")) context.presence.clock = true;
      else if (!strcmp(section, "weather")) context.presence.weather = true;
      else if (!strcmp(section, "network")) context.presence.network = true;
      else if (!strcmp(section, "radar")) context.presence.radar = true;
      else if (!strcmp(section, "github")) {
        context.presence.githubDisplay = true;
      }
    } else if (context.depth == 1 && !object &&
               !strcmp(scanner.container(), "wifi")) {
      context.presence.wifi = true;
      context.candidate.wifiCount = 0;
    } else if (context.depth == 2 && !object &&
               !strcmp(scanner.container(), "airports") &&
               !strcmp(scanner.container(1), "radar")) {
      context.presence.radarAirports = true;
      context.radar.airportCount = 0;
    } else if (context.depth == 2 && !object &&
               !strcmp(scanner.container(1), "wifi")) {
      context.presence.wifiEntriesValid = false;
    } else if (context.depth == 2 && object &&
               !strcmp(scanner.container(1), "wifi")) {
      context.wifiEntry.ssid = "";
      context.wifiEntry.pass = "";
      context.inWifiEntry = true;
    } else if (context.depth == 3 && !object &&
               !strcmp(scanner.container(1), "airports") &&
               !strcmp(scanner.container(2), "radar")) {
      context.presence.radarAirportsValid = false;
    } else if (context.depth == 3 && object &&
               !strcmp(scanner.container(1), "airports") &&
               !strcmp(scanner.container(2), "radar")) {
      context.airportEntry.icao[0] = 0;
      context.airportEntry.lat = 0;
      context.airportEntry.lon = 0;
      context.airportIcao = context.airportLat = context.airportLon = false;
      context.inAirportEntry = true;
    }
    ++context.depth;
    return;
  }

  if (object && context.depth == 3 && context.inWifiEntry &&
      !strcmp(scanner.container(1), "wifi")) {
    if (!context.wifiEntry.ssid.length() || context.wifiEntry.ssid.length() > 32 ||
        context.wifiEntry.pass.length() > 64)
      context.presence.wifiEntriesValid = false;
    if (context.wifiEntry.ssid.length() &&
        context.candidate.wifiCount < MAX_WIFI_NETS) {
      retainWifiSecret(context, context.wifiEntry);
      context.candidate.wifi[context.candidate.wifiCount++] = context.wifiEntry;
    }
    context.inWifiEntry = false;
  } else if (object && context.depth == 4 && context.inAirportEntry &&
             !strcmp(scanner.container(1), "airports") &&
             !strcmp(scanner.container(2), "radar")) {
    if (!context.airportIcao || !context.airportLat || !context.airportLon ||
        !validAirportCode(context.airportEntry.icao) ||
        context.airportEntry.lat < -90.0f || context.airportEntry.lat > 90.0f ||
        context.airportEntry.lon < -180.0f || context.airportEntry.lon > 180.0f ||
        context.radar.airportCount >= MAX_AIRPORTS)
      context.presence.radarAirportsValid = false;
    if (context.airportEntry.icao[0] && context.radar.airportCount < MAX_AIRPORTS)
      context.radar.airports[context.radar.airportCount++] = context.airportEntry;
    context.inAirportEntry = false;
  }
  if (context.depth) --context.depth;
}

void parseRootValue(ParseContext& context, const JsonScanner& scanner,
                    JsonScanner::Value type, const char* text) {
  int32_t integer;
  bool boolean;

  if (keyIs(scanner, "configVersion") && intValue(type, text, integer)) {
    context.presence.configVersion = true;
    context.presence.configVersionValue = clampInt<uint16_t>(integer, 0, 65535);
  } else if (keyIs(scanner, "hostname") && stringValue(type)) {
    context.presence.hostname = true;
    context.candidate.hostname = text;
  } else if (keyIs(scanner, "staSsid") && stringValue(type)) {
    context.presence.staSsid = true;
    context.legacySsidValue = true;
    context.legacySsid = text;
  } else if (keyIs(scanner, "staPass") && stringValue(type)) {
    context.presence.staPass = true;
    context.legacyPassValue = true;
    context.legacyPass = text;
  } else if (keyIs(scanner, "apSsid") && stringValue(type)) {
    context.presence.apSsid = true;
    context.candidate.apSsid = text;
  } else if (keyIs(scanner, "apPass") && stringValue(type)) {
    context.presence.apPass = true;
    context.candidate.apPass = text;
  } else if (keyIs(scanner, "mode") && stringValue(type)) {
    context.presence.mode = true;
    const String mode(text);
    context.candidate.mode = mode.equalsIgnoreCase("network") ? MODE_NETWORK
        : mode.equalsIgnoreCase("radar") ? MODE_RADAR
        : mode.equalsIgnoreCase("github") ? MODE_GITHUB
        : mode.equalsIgnoreCase("carousel") ? MODE_CAROUSEL : MODE_WEATHER;
  } else if (keyIs(scanner, "carouselSec") && intValue(type, text, integer)) {
    context.presence.carouselSec = true;
    context.presence.carouselSecValid = integer >= 5 && integer <= 3600;
    context.candidate.carouselSec = clampInt<uint16_t>(integer, 5, 3600);
  } else if (keyIs(scanner, "carouselWeather") &&
             boolValue(type, text, boolean)) {
    context.presence.carouselSelection = true;
    context.candidate.carouselWeather = boolean;
  } else if (keyIs(scanner, "carouselNetwork") &&
             boolValue(type, text, boolean)) {
    context.presence.carouselSelection = true;
    context.candidate.carouselNetwork = boolean;
  } else if (keyIs(scanner, "carouselRadar") &&
             boolValue(type, text, boolean)) {
    context.presence.carouselSelection = true;
    context.candidate.carouselRadar = boolean;
  } else if (keyIs(scanner, "carouselGithub") &&
             boolValue(type, text, boolean)) {
    context.presence.carouselSelection = true;
    context.candidate.carouselGithub = boolean;
  } else if (keyIs(scanner, "httpTimeout") && intValue(type, text, integer)) {
    context.presence.httpTimeout = true;
    context.candidate.httpTimeout = clampInt<uint16_t>(integer, 1000, 20000);
  } else if (keyIs(scanner, "brightness") && intValue(type, text, integer)) {
    context.presence.brightness = true;
    context.presence.brightnessValue = true;
    context.presence.brightnessValueValid = integer >= 0 && integer <= 100;
    context.candidate.brightness = clampInt<uint8_t>(integer, 0, 100);
  } else if (keyIs(scanner, "autoBrightness") &&
             boolValue(type, text, boolean)) {
    context.presence.brightness = true;
    context.presence.autoBrightness = true;
    context.candidate.autoBrightness = boolean;
  } else if (keyIs(scanner, "backlightInverted") &&
             boolValue(type, text, boolean)) {
    context.presence.brightness = true;
    context.presence.backlightInverted = true;
    context.candidate.backlightInverted = boolean;
  } else if (keyIs(scanner, "rotation") && intValue(type, text, integer)) {
    context.presence.rotation = true;
    context.presence.rotationValid = integer >= 0 && integer <= 3;
    context.candidate.rotation = static_cast<uint8_t>(integer & 3);
  }
}

void parseClockValue(ParseContext& context, const JsonScanner& scanner,
                     JsonScanner::Value type, const char* text) {
  int32_t integer;
  bool boolean;
  if (keyIs(scanner, "tz") && stringValue(type)) {
    context.clock.tz = text;
    context.clockTz = true;
  } else if (keyIs(scanner, "tzAbbr") && stringValue(type)) {
    context.clock.tzAbbr = text;
    context.clockTzAbbr = true;
  } else if (keyIs(scanner, "tzPosix") && stringValue(type)) {
    context.clock.tzPosix = text;
    context.clockTzPosix = true;
  } else if (keyIs(scanner, "utcOffsetSec") && intValue(type, text, integer)) {
    context.clock.utcOffsetSec = clampInt<int32_t>(integer, -43200, 50400);
    context.clockUtcOffset = true;
  } else if (keyIs(scanner, "use24Hour")) {
    if (boolValue(type, text, boolean)) {
      context.clock.use24Hour = boolean;
      context.clockUse24 = true;
      context.presence.clockUse24Hour = true;
    } else if (type != JsonScanner::Value::Null) {
      context.presence.clockUse24HourInvalid = true;
    }
  } else if (keyIs(scanner, "nightEnabled") && boolValue(type, text, boolean)) {
    context.clock.nightEnabled = boolean;
    context.clockNightEnabled = true;
  } else if (keyIs(scanner, "nightStart") && stringValue(type)) {
    context.clock.nightStartMin = hhmmToMin(text, context.clock.nightStartMin);
    context.clockNightStart = true;
    context.presence.clockNightStart = true;
    context.presence.clockNightStartValid = validHhmm(text);
  } else if (keyIs(scanner, "nightEnd") && stringValue(type)) {
    context.clock.nightEndMin = hhmmToMin(text, context.clock.nightEndMin);
    context.clockNightEnd = true;
    context.presence.clockNightEnd = true;
    context.presence.clockNightEndValid = validHhmm(text);
  } else if (keyIs(scanner, "nightLevel") && intValue(type, text, integer)) {
    context.clock.nightLevel = clampInt<uint8_t>(integer, 0, 100);
    context.clockNightLevel = true;
    context.presence.clockNightLevel = true;
    context.presence.clockNightLevelValid = integer >= 0 && integer <= 100;
  }
}

void parseWeatherValue(ParseContext& context, const JsonScanner& scanner,
                       JsonScanner::Value type, const char* text) {
  int32_t integer;
  bool boolean;
  float number;
  if (keyIs(scanner, "lat") && floatValue(type, text, number)) {
    context.weather.lat = number;
    context.presence.weatherLat = true;
  } else if (keyIs(scanner, "lon") && floatValue(type, text, number)) {
    context.weather.lon = number;
    context.presence.weatherLon = true;
  } else if (keyIs(scanner, "city") && stringValue(type)) {
    context.weather.city = text;
    context.presence.weatherCity = true;
  } else if (keyIs(scanner, "country") && stringValue(type)) {
    context.weather.country = text;
  } else if (keyIs(scanner, "timezone") && stringValue(type)) {
    context.weather.timezone = text;
    context.presence.weatherTimezone = true;
  } else if (keyIs(scanner, "timezoneAbbr") && stringValue(type)) {
    context.weather.timezoneAbbr = text;
  } else if (keyIs(scanner, "utcOffsetSec") && intValue(type, text, integer)) {
    context.weather.utcOffsetSec = clampInt<int32_t>(integer, -43200, 50400);
  } else if (keyIs(scanner, "locationVerified") &&
             boolValue(type, text, boolean)) {
    context.weather.locationVerified = boolean;
    context.presence.weatherLocationVerified = true;
  } else if (keyIs(scanner, "apiKey") && stringValue(type)) {
    context.presence.weatherApiKey = true;
    context.presence.weatherApiKeyValid = strlen(text) <= 160;
    if (text[0]) context.weather.apiKey = text;
  } else if (keyIs(scanner, "metric") && boolValue(type, text, boolean)) {
    context.weather.metric = boolean;
  } else if (keyIs(scanner, "pollSec") && intValue(type, text, integer)) {
    context.weather.pollSec = clampInt<uint16_t>(integer, 300, 3600);
    context.presence.weatherPollSec = true;
    context.presence.weatherPollSecValid = integer >= 300 && integer <= 3600;
  }
}

void parseNetworkValue(ParseContext& context, const JsonScanner& scanner,
                       JsonScanner::Value type, const char* text) {
  int32_t integer;
  if (keyIs(scanner, "probeHost") && stringValue(type)) {
    context.network.probeHost = text;
    context.presence.networkProbeHost = true;
  } else if (keyIs(scanner, "probePort") && intValue(type, text, integer)) {
    context.network.probePort = clampInt<uint16_t>(integer, 1, 65535);
    context.presence.networkProbePort = true;
    context.presence.networkProbePortValid = integer >= 1 && integer <= 65535;
  } else if (keyIs(scanner, "dnsHost") && stringValue(type)) {
    context.network.dnsHost = text;
    context.presence.networkDnsHost = true;
  } else if (keyIs(scanner, "pollSec") && intValue(type, text, integer)) {
    context.network.pollSec = clampInt<uint16_t>(integer, 3, 300);
    context.presence.networkPollSec = true;
    context.presence.networkPollSecValid = integer >= 3 && integer <= 300;
  }
}

void parseRadarValue(ParseContext& context, const JsonScanner& scanner,
                     JsonScanner::Value type, const char* text) {
  int32_t integer;
  bool boolean;
  float number;
  if (keyIs(scanner, "lat") && floatValue(type, text, number)) {
    context.radar.lat = number;
    context.presence.radarLat = true;
  } else if (keyIs(scanner, "lon") && floatValue(type, text, number)) {
    context.radar.lon = number;
    context.presence.radarLon = true;
  } else if (keyIs(scanner, "source") && stringValue(type)) {
    const String source(text);
    context.radar.source = source.equalsIgnoreCase("webhook")
        ? RADAR_SRC_WEBHOOK : RADAR_SRC_DIRECT;
    context.presence.radarSource = true;
  } else if (keyIs(scanner, "webhookUrl") && stringValue(type)) {
    context.radar.webhookUrl = text;
    context.presence.radarWebhookUrl = true;
  } else if (keyIs(scanner, "rangeKm") && intValue(type, text, integer)) {
    context.radar.rangeKm = clampInt<uint16_t>(integer, 1, 500);
    context.presence.radarRangeKm = true;
    context.presence.radarRangeKmValid = integer >= 1 && integer <= 500;
  } else if (keyIs(scanner, "pollSec") && intValue(type, text, integer)) {
    context.radar.pollSec = clampInt<uint16_t>(integer, 3, 65535);
    context.presence.radarPollSec = true;
    context.presence.radarPollSecValid = integer >= 3 && integer <= 3600;
  } else if (keyIs(scanner, "unitsMi") && boolValue(type, text, boolean)) {
    context.radar.unitsMi = boolean;
  } else if (keyIs(scanner, "showLabels") && boolValue(type, text, boolean)) {
    context.radar.showLabels = boolean;
  } else if (keyIs(scanner, "showRimDots") && boolValue(type, text, boolean)) {
    context.radar.showRimDots = boolean;
  } else if (keyIs(scanner, "showTrails") && boolValue(type, text, boolean)) {
    context.radar.showTrails = boolean;
  } else if (keyIs(scanner, "uiScale") && intValue(type, text, integer)) {
    context.radar.uiScale = clampInt<uint8_t>(integer, 0, 2);
  } else if (keyIs(scanner, "minAltFt") && intValue(type, text, integer)) {
    context.radar.minAltFt = clampInt<uint16_t>(integer, 0, 60000);
  }
}

void parseGithubValue(ParseContext& context, const JsonScanner& scanner,
                      JsonScanner::Value type, const char* text) {
  int32_t integer;
  bool boolean;
  if (keyIs(scanner, "token") && stringValue(type)) {
    context.presence.githubToken = true;
    context.presence.githubTokenValid = strlen(text) <= 512;
    context.presence.githubData = true;
    if (text[0]) context.github.token = text;
  } else if (keyIs(scanner, "login") && stringValue(type)) {
    context.github.login = text;
    context.presence.githubData = true;
  } else if (keyIs(scanner, "rangeMonths") && intValue(type, text, integer)) {
    context.presence.githubRangeMonths = true;
    context.presence.githubRangeMonthsValid = integer == 3;
  } else if (keyIs(scanner, "pollSec") && intValue(type, text, integer)) {
    context.github.pollSec = clampInt<uint16_t>(integer, 300, 3600);
    context.presence.githubPollSec = true;
    context.presence.githubPollSecValid = integer >= 300 && integer <= 3600;
    context.presence.githubData = true;
  } else if (keyIs(scanner, "pageInbox") && boolValue(type, text, boolean)) {
    context.github.pageInbox = boolean;
  } else if (keyIs(scanner, "pagePulls") && boolValue(type, text, boolean)) {
    context.github.pagePulls = boolean;
  } else if (keyIs(scanner, "pagePulse") && boolValue(type, text, boolean)) {
    context.github.pagePulse = boolean;
  }
}

void onValue(void* opaque, const JsonScanner& scanner, JsonScanner::Value type,
             const char* text, uint32_t) {
  ParseContext& context = *static_cast<ParseContext*>(opaque);
  if (scanner.valueTruncated()) {
    context.truncatedValue = true;
    return;
  }
  if (context.depth == 1) {
    parseRootValue(context, scanner, type, text);
    return;
  }
  if (context.depth == 2) {
    const char* section = scanner.container();
    if (!strcmp(section, "wifi") && context.presence.wifi) {
      context.presence.wifiEntriesValid = false;
      return;
    }
    if (!strcmp(section, "clock") && context.presence.clock)
      parseClockValue(context, scanner, type, text);
    else if (!strcmp(section, "weather") && context.presence.weather)
      parseWeatherValue(context, scanner, type, text);
    else if (!strcmp(section, "network") && context.presence.network)
      parseNetworkValue(context, scanner, type, text);
    else if (!strcmp(section, "radar") && context.presence.radar)
      parseRadarValue(context, scanner, type, text);
    else if (!strcmp(section, "github") && context.presence.githubDisplay)
      parseGithubValue(context, scanner, type, text);
    return;
  }
  if (context.depth == 3 && !context.inAirportEntry &&
      !strcmp(scanner.container(), "airports") &&
      !strcmp(scanner.container(1), "radar")) {
    context.presence.radarAirportsValid = false;
    return;
  }
  if (context.depth == 3 && context.inWifiEntry &&
      !strcmp(scanner.container(1), "wifi")) {
    if (keyIs(scanner, "ssid") && stringValue(type)) context.wifiEntry.ssid = text;
    else if (keyIs(scanner, "pass") && stringValue(type)) context.wifiEntry.pass = text;
    return;
  }
  if (context.depth == 4 && context.inAirportEntry &&
      !strcmp(scanner.container(1), "airports") &&
      !strcmp(scanner.container(2), "radar")) {
    float number;
    if (keyIs(scanner, "icao") && stringValue(type)) {
      if (strlen(text) >= sizeof(context.airportEntry.icao))
        context.presence.radarAirportsValid = false;
      strlcpy(context.airportEntry.icao, text, sizeof(context.airportEntry.icao));
      context.airportIcao = true;
    } else if (keyIs(scanner, "lat") && floatValue(type, text, number)) {
      context.airportEntry.lat = number;
      context.airportLat = true;
    } else if (keyIs(scanner, "lon") && floatValue(type, text, number)) {
      context.airportEntry.lon = number;
      context.airportLon = true;
    }
  }
}

void finishParse(ParseContext& context) {
  if (!context.presence.wifi && context.legacySsidValue &&
      context.legacySsid.length()) {
    context.candidate.wifi[0].ssid = context.legacySsid;
    if (context.legacyPassValue && context.legacyPass.length())
      context.candidate.wifi[0].pass = context.legacyPass;
    if (!context.candidate.wifiCount) context.candidate.wifiCount = 1;
  }

  const bool noCarouselPage = !context.candidate.carouselWeather &&
      !context.candidate.carouselNetwork && !context.candidate.carouselRadar &&
      !context.candidate.carouselGithub;
  if (context.presence.carouselSelection && noCarouselPage)
    context.presence.carouselSelectionValid = false;
  if (noCarouselPage) context.candidate.carouselWeather = true;

  if (context.presence.weather) {
    context.candidate.weather = context.weather;
    if (context.weather.timezone.length()) context.candidate.clock.tz = context.weather.timezone;
    if (context.weather.timezoneAbbr.length()) context.candidate.clock.tzAbbr = context.weather.timezoneAbbr;
    context.candidate.clock.utcOffsetSec = context.weather.utcOffsetSec;
    context.candidate.clock.tzPosix = "";
  }
  if (context.presence.network) context.candidate.network = context.network;
  if (context.presence.radar) context.candidate.radar = context.radar;
  if (context.presence.githubDisplay) {
    context.github.rangeMonths = 3;
    if (!context.github.pageCount())
      context.github.pageInbox = context.github.pagePulls = context.github.pagePulse = true;
    context.candidate.github = context.github;
    context.presence.githubDisplay = !context.presence.githubData;
  }
  if (context.presence.clock) {
    if (context.clockTz) context.candidate.clock.tz = context.clock.tz;
    if (context.clockTzAbbr) context.candidate.clock.tzAbbr = context.clock.tzAbbr;
    if (context.clockTzPosix) context.candidate.clock.tzPosix = context.clock.tzPosix;
    if (context.clockUtcOffset) context.candidate.clock.utcOffsetSec = context.clock.utcOffsetSec;
    if (context.clockUse24) context.candidate.clock.use24Hour = context.clock.use24Hour;
    if (context.clockNightEnabled) context.candidate.clock.nightEnabled = context.clock.nightEnabled;
    if (context.clockNightStart) context.candidate.clock.nightStartMin = context.clock.nightStartMin;
    if (context.clockNightEnd) context.candidate.clock.nightEndMin = context.clock.nightEndMin;
    if (context.clockNightLevel) context.candidate.clock.nightLevel = context.clock.nightLevel;
  }
}

bool parseScanner(Settings& out, const Settings& base, JsonScanner& scanner,
                  SettingsJsonPresence* presence, JsonScanner::Error* error) {
  static char valueBuffer[513];
  // The shared scanner removes the largest stack/heap object. Keep the
  // transactional settings copy temporary so TLS gets this memory back.
  ParseContext* context = new (std::nothrow) ParseContext(base);
  if (!context) {
    if (error) *error = JsonScanner::Error::OutOfMemory;
    return false;
  }
  scanner.setValueBuffer(valueBuffer, sizeof(valueBuffer));
  scanner.setContainerHandler(onContainer, context);
  const bool walked = scanner.walk(onValue, context);
  const bool valid = walked && context->rootObject && !context->wrongRoot &&
                     !context->truncatedValue;
  if (error) *error = valid ? JsonScanner::Error::None
                            : (scanner.error() == JsonScanner::Error::None
                                   ? JsonScanner::Error::InvalidSyntax
                                   : scanner.error());
  if (valid) {
    finishParse(*context);
    out = context->candidate;
    if (presence) *presence = context->presence;
  }
  delete context;
  return valid;
}

bool member(JsonWriter& writer, const char* key, const String& value) {
  return writer.key(key) && writer.value(value);
}
bool member(JsonWriter& writer, const char* key, const char* value) {
  return writer.key(key) && writer.value(value);
}
bool member(JsonWriter& writer, const char* key, bool value) {
  return writer.key(key) && writer.value(value);
}
template <typename T>
bool member(JsonWriter& writer, const char* key, T value) {
  return writer.key(key) && writer.value(value);
}

}  // namespace

bool settingsParseJson(Settings& out, const Settings& base,
                       const char* json, size_t length,
                       SettingsJsonPresence* presence,
                       JsonScanner::Error* error) {
  JsonScanner& scanner = JsonScanner::shared(json, length);
  return parseScanner(out, base, scanner, presence, error);
}

bool settingsParseJson(Settings& out, const Settings& base, Stream& stream,
                       int contentLength, size_t maxBytes,
                       SettingsJsonPresence* presence,
                       JsonScanner::Error* error) {
  JsonScanner& scanner = JsonScanner::shared(stream, contentLength, maxBytes);
  return parseScanner(out, base, scanner, presence, error);
}

bool settingsWriteJsonFields(JsonWriter& writer, const Settings& settings,
                             bool includeSecrets) {
  if (!member(writer, "configVersion", CONFIG_VERSION) ||
      !member(writer, "hostname", settings.hostname) ||
      !writer.key("wifi") || !writer.beginArray()) return false;
  for (uint8_t i = 0; i < settings.wifiCount; ++i) {
    if (!writer.beginObject() ||
        !member(writer, "ssid", settings.wifi[i].ssid) ||
        !member(writer, "passSet", settings.wifi[i].pass.length() > 0) ||
        (includeSecrets && !member(writer, "pass", settings.wifi[i].pass)) ||
        !writer.endObject()) return false;
  }
  if (!writer.endArray() ||
      !member(writer, "staSsid", settings.wifiCount ? settings.wifi[0].ssid : String("")) ||
      !member(writer, "staPassSet", settings.wifiCount && settings.wifi[0].pass.length() > 0) ||
      !member(writer, "apSsid", settings.apSsid) ||
      !member(writer, "apPassSet", settings.apPass.length() > 0) ||
      (includeSecrets &&
       (!member(writer, "staPass", settings.wifiCount ? settings.wifi[0].pass : String("")) ||
        !member(writer, "apPass", settings.apPass)))) return false;

  const char* mode = settings.mode == MODE_NETWORK ? "network"
      : settings.mode == MODE_RADAR ? "radar"
      : settings.mode == MODE_GITHUB ? "github"
      : settings.mode == MODE_CAROUSEL ? "carousel" : "weather";
  if (!member(writer, "mode", mode) ||
      !member(writer, "carouselSec", settings.carouselSec) ||
      !member(writer, "carouselWeather", settings.carouselWeather) ||
      !member(writer, "carouselNetwork", settings.carouselNetwork) ||
      !member(writer, "carouselRadar", settings.carouselRadar) ||
      !member(writer, "carouselGithub", settings.carouselGithub) ||
      !member(writer, "httpTimeout", settings.httpTimeout) ||
      !member(writer, "brightness", settings.brightness) ||
      !member(writer, "autoBrightness", settings.autoBrightness) ||
      !member(writer, "backlightInverted", settings.backlightInverted) ||
      !member(writer, "rotation", settings.rotation)) return false;

  if (!writer.key("weather") || !writer.beginObject() ||
      !member(writer, "lat", static_cast<double>(settings.weather.lat)) ||
      !member(writer, "lon", static_cast<double>(settings.weather.lon)) ||
      !member(writer, "city", settings.weather.city) ||
      !member(writer, "country", settings.weather.country) ||
      !member(writer, "timezone", settings.weather.timezone) ||
      !member(writer, "timezoneAbbr", settings.weather.timezoneAbbr) ||
      !member(writer, "utcOffsetSec", static_cast<long>(settings.weather.utcOffsetSec)) ||
      !member(writer, "locationVerified", settings.weather.locationVerified) ||
      !member(writer, "apiKeySet", settings.weather.apiKey.length() > 0) ||
      (includeSecrets && !member(writer, "apiKey", settings.weather.apiKey)) ||
      !member(writer, "metric", settings.weather.metric) ||
      !member(writer, "pollSec", settings.weather.pollSec) || !writer.endObject())
    return false;

  if (!writer.key("network") || !writer.beginObject() ||
      !member(writer, "probeHost", settings.network.probeHost) ||
      !member(writer, "probePort", settings.network.probePort) ||
      !member(writer, "dnsHost", settings.network.dnsHost) ||
      !member(writer, "pollSec", settings.network.pollSec) || !writer.endObject())
    return false;

  if (!writer.key("radar") || !writer.beginObject() ||
      !member(writer, "lat", static_cast<double>(settings.radar.lat)) ||
      !member(writer, "lon", static_cast<double>(settings.radar.lon)) ||
      !member(writer, "source", settings.radar.source == RADAR_SRC_WEBHOOK ? "webhook" : "direct") ||
      !member(writer, "webhookUrl", settings.radar.webhookUrl) ||
      !member(writer, "rangeKm", settings.radar.rangeKm) ||
      !member(writer, "pollSec", settings.radar.pollSec) ||
      !member(writer, "unitsMi", settings.radar.unitsMi) ||
      !member(writer, "showLabels", settings.radar.showLabels) ||
      !member(writer, "showRimDots", settings.radar.showRimDots) ||
      !member(writer, "showTrails", settings.radar.showTrails) ||
      !member(writer, "uiScale", settings.radar.uiScale) ||
      !member(writer, "minAltFt", settings.radar.minAltFt) ||
      !writer.key("airports") || !writer.beginArray()) return false;
  for (uint8_t i = 0; i < settings.radar.airportCount; ++i) {
    if (!writer.beginObject() ||
        !member(writer, "icao", settings.radar.airports[i].icao) ||
        !member(writer, "lat", static_cast<double>(settings.radar.airports[i].lat)) ||
        !member(writer, "lon", static_cast<double>(settings.radar.airports[i].lon)) ||
        !writer.endObject()) return false;
  }
  if (!writer.endArray() || !writer.endObject()) return false;

  if (!writer.key("github") || !writer.beginObject() ||
      !member(writer, "tokenSet", settings.github.token.length() > 0) ||
      (includeSecrets && !member(writer, "token", settings.github.token)) ||
      !member(writer, "login", settings.github.login) ||
      !member(writer, "rangeMonths", settings.github.rangeMonths) ||
      !member(writer, "pollSec", settings.github.pollSec) ||
      !member(writer, "pageInbox", settings.github.pageInbox) ||
      !member(writer, "pagePulls", settings.github.pagePulls) ||
      !member(writer, "pagePulse", settings.github.pagePulse) || !writer.endObject())
    return false;

  char nightStart[6];
  char nightEnd[6];
  minToHhmm(settings.clock.nightStartMin, nightStart);
  minToHhmm(settings.clock.nightEndMin, nightEnd);
  return writer.key("clock") && writer.beginObject() &&
      member(writer, "tz", settings.clock.tz) &&
      member(writer, "tzAbbr", settings.clock.tzAbbr) &&
      member(writer, "tzPosix", settings.clock.tzPosix) &&
      member(writer, "utcOffsetSec", static_cast<long>(settings.clock.utcOffsetSec)) &&
      member(writer, "use24Hour", settings.clock.use24Hour) &&
      member(writer, "nightEnabled", settings.clock.nightEnabled) &&
      member(writer, "nightStart", nightStart) &&
      member(writer, "nightEnd", nightEnd) &&
      member(writer, "nightLevel", settings.clock.nightLevel) &&
      writer.endObject();
}

bool settingsWriteJson(JsonWriter& writer, const Settings& settings,
                       bool includeSecrets) {
  return writer.beginObject() &&
         settingsWriteJsonFields(writer, settings, includeSecrets) &&
         writer.endObject() && writer.complete();
}

bool settingsBegin() {
  if (LittleFS.begin()) return true;
  return LittleFS.format() && LittleFS.begin();
}

bool loadSettings(Settings& settings) {
  settings.setDefaults();
  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) return false;

  Settings loaded;
  SettingsJsonPresence presence;
  const bool parsed = settingsParseJson(loaded, settings, file,
      static_cast<int>(file.size()), file.size(), &presence);
  file.close();
  if (!parsed) return false;

  const bool legacyApName = isLegacySetupSsid(loaded.apSsid);
  if (legacyApName) loaded.apSsid = DEFAULT_AP_SSID;
  settings = loaded;
  if (!presence.configVersion || presence.configVersionValue < CONFIG_VERSION ||
      legacyApName) saveSettings(settings);
  return true;
}

bool saveSettings(const Settings& settings) {
#if defined(DESKMATE_EMULATOR)
  File file = LittleFS.open(CONFIG_PATH, "w");
#else
  LittleFS.remove(CONFIG_TEMP_PATH);
  File file = LittleFS.open(CONFIG_TEMP_PATH, "w");
#endif
  if (!file) return false;
  JsonWriter writer(file);
  const bool written = settingsWriteJson(writer, settings, true);
  file.close();
#if defined(DESKMATE_EMULATOR)
  return written;
#else
  if (!written) {
    LittleFS.remove(CONFIG_TEMP_PATH);
    return false;
  }
  if (LittleFS.rename(CONFIG_TEMP_PATH, CONFIG_PATH)) return true;
  LittleFS.remove(CONFIG_TEMP_PATH);
  return false;
#endif
}

void factoryReset(Settings& settings) {
  LittleFS.remove(CONFIG_PATH);
  LittleFS.remove(CONFIG_TEMP_PATH);
  settings.setDefaults();
}
