#include "Settings.h"
#include "Platform.h"
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";
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

static uint16_t hhmmToMin(const char* s, uint16_t fallback) {
  if (!s || !s[0]) return fallback;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return static_cast<uint16_t>(h * 60 + m);
}

static String minToHhmm(uint16_t v) {
  if (v > 1439) v = 0;
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", static_cast<unsigned>(v / 60),
           static_cast<unsigned>(v % 60));
  return String(b);
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

void ClockSettings::toJson(JsonObject o) const {
  o["tz"] = tz;
  o["tzAbbr"] = tzAbbr;
  o["tzPosix"] = tzPosix;
  o["utcOffsetSec"] = utcOffsetSec;
  o["use24Hour"] = use24Hour;
  o["nightEnabled"] = nightEnabled;
  o["nightStart"] = minToHhmm(nightStartMin);
  o["nightEnd"] = minToHhmm(nightEndMin);
  o["nightLevel"] = nightLevel;
}

void ClockSettings::fromJson(JsonObjectConst o) {
  if (o["tz"].is<const char*>()) tz = o["tz"].as<String>();
  if (o["tzAbbr"].is<const char*>()) tzAbbr = o["tzAbbr"].as<String>();
  if (o["tzPosix"].is<const char*>()) tzPosix = o["tzPosix"].as<String>();
  if (o["utcOffsetSec"].is<long>() || o["utcOffsetSec"].is<int>())
    utcOffsetSec = constrain(o["utcOffsetSec"].as<long>(), -43200L, 50400L);
  if (o["use24Hour"].is<bool>()) use24Hour = o["use24Hour"];
  if (o["nightEnabled"].is<bool>()) nightEnabled = o["nightEnabled"];
  if (o["nightStart"].is<const char*>())
    nightStartMin = hhmmToMin(o["nightStart"], nightStartMin);
  if (o["nightEnd"].is<const char*>())
    nightEndMin = hhmmToMin(o["nightEnd"], nightEndMin);
  if (o["nightLevel"].is<int>())
    nightLevel = constrain(static_cast<int>(o["nightLevel"]), 0, 100);
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

void RadarSettings::toJson(JsonObject o) const {
  o["lat"] = lat;
  o["lon"] = lon;
  o["source"] = source == RADAR_SRC_WEBHOOK ? "webhook" : "direct";
  o["webhookUrl"] = webhookUrl;
  o["rangeKm"] = rangeKm;
  o["pollSec"] = pollSec;
  o["unitsMi"] = unitsMi;
  o["showLabels"] = showLabels;
  o["showRimDots"] = showRimDots;
  o["showTrails"] = showTrails;
  o["uiScale"] = uiScale;
  o["minAltFt"] = minAltFt;

  JsonArray arr = o["airports"].to<JsonArray>();
  for (uint8_t i = 0; i < airportCount; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["icao"] = airports[i].icao;
    e["lat"] = airports[i].lat;
    e["lon"] = airports[i].lon;
  }
}

void RadarSettings::fromJson(JsonObjectConst o) {
  if (o["lat"].is<float>() || o["lat"].is<int>()) lat = o["lat"].as<float>();
  if (o["lon"].is<float>() || o["lon"].is<int>()) lon = o["lon"].as<float>();
  if (o["source"].is<const char*>()) {
    const String src = o["source"].as<String>();
    source = src.equalsIgnoreCase("webhook") ? RADAR_SRC_WEBHOOK : RADAR_SRC_DIRECT;
  }
  if (o["webhookUrl"].is<const char*>()) webhookUrl = o["webhookUrl"].as<String>();
  if (o["rangeKm"].is<int>()) rangeKm = constrain(static_cast<int>(o["rangeKm"]), 1, 500);
  if (o["pollSec"].is<int>()) pollSec = max(3, static_cast<int>(o["pollSec"]));
  if (o["unitsMi"].is<bool>()) unitsMi = o["unitsMi"];
  if (o["showLabels"].is<bool>()) showLabels = o["showLabels"];
  if (o["showRimDots"].is<bool>()) showRimDots = o["showRimDots"];
  if (o["showTrails"].is<bool>()) showTrails = o["showTrails"];
  if (o["uiScale"].is<int>()) uiScale = constrain(static_cast<int>(o["uiScale"]), 0, 2);
  if (o["minAltFt"].is<int>()) minAltFt = constrain(static_cast<int>(o["minAltFt"]), 0, 60000);

  if (o["airports"].is<JsonArrayConst>()) {
    airportCount = 0;
    for (JsonObjectConst e : o["airports"].as<JsonArrayConst>()) {
      if (airportCount >= MAX_AIRPORTS) break;
      const char* icao = e["icao"] | "";
      if (!icao[0]) continue;
      Airport& dst = airports[airportCount++];
      strlcpy(dst.icao, icao, sizeof(dst.icao));
      dst.lat = e["lat"] | 0.0f;
      dst.lon = e["lon"] | 0.0f;
    }
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

void WeatherSettings::toJson(JsonObject o, bool includeSecrets) const {
  o["lat"] = lat;
  o["lon"] = lon;
  o["city"] = city;
  o["country"] = country;
  o["timezone"] = timezone;
  o["timezoneAbbr"] = timezoneAbbr;
  o["utcOffsetSec"] = utcOffsetSec;
  o["locationVerified"] = locationVerified;
  o["apiKeySet"] = apiKey.length() > 0;
  if (includeSecrets) o["apiKey"] = apiKey;
  o["metric"] = metric;
  o["pollSec"] = pollSec;
}

void WeatherSettings::fromJson(JsonObjectConst o) {
  if (o["lat"].is<float>() || o["lat"].is<int>()) lat = o["lat"].as<float>();
  if (o["lon"].is<float>() || o["lon"].is<int>()) lon = o["lon"].as<float>();
  if (o["city"].is<const char*>()) city = o["city"].as<String>();
  if (o["country"].is<const char*>()) country = o["country"].as<String>();
  if (o["timezone"].is<const char*>()) timezone = o["timezone"].as<String>();
  if (o["timezoneAbbr"].is<const char*>()) timezoneAbbr = o["timezoneAbbr"].as<String>();
  if (o["utcOffsetSec"].is<long>() || o["utcOffsetSec"].is<int>())
    utcOffsetSec = constrain(o["utcOffsetSec"].as<long>(), -43200L, 50400L);
  if (o["locationVerified"].is<bool>()) locationVerified = o["locationVerified"];
  if (o["apiKey"].is<const char*>()) {
    const String value = o["apiKey"].as<String>();
    if (value.length()) apiKey = value;
  }
  if (o["metric"].is<bool>()) metric = o["metric"];
  if (o["pollSec"].is<int>()) pollSec = constrain(static_cast<int>(o["pollSec"]), 300, 3600);
}

void NetworkSettings::setDefaults() {
  probeHost = "1.1.1.1";
  probePort = 443;
  dnsHost = "github.com";
  pollSec = DEFAULT_NETWORK_POLL_SEC;
}

void NetworkSettings::toJson(JsonObject o) const {
  o["probeHost"] = probeHost;
  o["probePort"] = probePort;
  o["dnsHost"] = dnsHost;
  o["pollSec"] = pollSec;
}

void NetworkSettings::fromJson(JsonObjectConst o) {
  if (o["probeHost"].is<const char*>()) probeHost = o["probeHost"].as<String>();
  if (o["probePort"].is<int>()) probePort = constrain(static_cast<int>(o["probePort"]), 1, 65535);
  if (o["dnsHost"].is<const char*>()) dnsHost = o["dnsHost"].as<String>();
  if (o["pollSec"].is<int>()) pollSec = constrain(static_cast<int>(o["pollSec"]), 3, 300);
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

void GithubSettings::toJson(JsonObject o, bool includeSecrets) const {
  o["tokenSet"] = token.length() > 0;
  if (includeSecrets) o["token"] = token;
  o["login"] = login;
  o["rangeMonths"] = rangeMonths;
  o["pollSec"] = pollSec;
  o["pageInbox"] = pageInbox;
  o["pagePulls"] = pagePulls;
  o["pagePulse"] = pagePulse;
}

void GithubSettings::fromJson(JsonObjectConst o) {
  if (o["token"].is<const char*>()) {
    const String value = o["token"].as<String>();
    if (value.length()) token = value;
  }
  if (o["login"].is<const char*>()) login = o["login"].as<String>();
  // Keep old configuration files compatible, but make the request window
  // fixed so a large historical graph cannot reintroduce TLS pressure.
  rangeMonths = 3;
  if (o["pollSec"].is<int>()) pollSec = constrain(static_cast<int>(o["pollSec"]), 300, 3600);
  if (o["pageInbox"].is<bool>()) pageInbox = o["pageInbox"];
  if (o["pagePulls"].is<bool>()) pagePulls = o["pagePulls"];
  if (o["pagePulse"].is<bool>()) pagePulse = o["pagePulse"];
  // A screen with nothing to show would be a blank panel. Configuration files
  // predating these keys, and any request that clears all three, fall back to
  // the whole rotation.
  if (!pageCount()) pageInbox = pagePulls = pagePulse = true;
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

bool settingsBegin() {
  if (LittleFS.begin()) return true;
  return LittleFS.format() && LittleFS.begin();
}

bool saveSettings(const Settings& s);

bool loadSettings(Settings& s) {
  s.setDefaults();
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  const uint16_t savedVersion = doc["configVersion"] | 0;
  const bool legacyApName = isLegacySetupSsid(s.apSsid) ||
                            isLegacySetupSsid(doc["apSsid"] | "");
  settingsApplyJson(s, doc.as<JsonObjectConst>());

  // Migrate only the obsolete recovery identity; other persisted AP values
  // remain intact.
  if (legacyApName) {
    s.apSsid = DEFAULT_AP_SSID;
  }
  if (savedVersion < CONFIG_VERSION || legacyApName) {
    saveSettings(s);
  }
  return true;
}

bool saveSettings(const Settings& s) {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), true);
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  const bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["configVersion"] = CONFIG_VERSION;
  root["hostname"] = s.hostname;

  JsonArray wifi = root["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < s.wifiCount; ++i) {
    JsonObject e = wifi.add<JsonObject>();
    e["ssid"] = s.wifi[i].ssid;
    e["passSet"] = s.wifi[i].pass.length() > 0;
    if (includeSecrets) e["pass"] = s.wifi[i].pass;
  }

  root["staSsid"] = s.wifiCount ? s.wifi[0].ssid : "";
  root["staPassSet"] = s.wifiCount && s.wifi[0].pass.length() > 0;
  root["apSsid"] = s.apSsid;
  root["apPassSet"] = s.apPass.length() > 0;
  if (includeSecrets) {
    root["staPass"] = s.wifiCount ? s.wifi[0].pass : "";
    root["apPass"] = s.apPass;
  }

  root["mode"] = s.mode == MODE_NETWORK ? "network"
                  : s.mode == MODE_RADAR ? "radar"
                  : s.mode == MODE_GITHUB ? "github"
                  : s.mode == MODE_CAROUSEL ? "carousel"
                  : "weather";
  root["carouselSec"] = s.carouselSec;
  root["carouselWeather"] = s.carouselWeather;
  root["carouselNetwork"] = s.carouselNetwork;
  root["carouselRadar"] = s.carouselRadar;
  root["carouselGithub"] = s.carouselGithub;
  root["httpTimeout"] = s.httpTimeout;
  root["brightness"] = s.brightness;
  root["autoBrightness"] = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"] = s.rotation;

  s.weather.toJson(root["weather"].to<JsonObject>(), includeSecrets);
  s.network.toJson(root["network"].to<JsonObject>());
  s.radar.toJson(root["radar"].to<JsonObject>());
  s.github.toJson(root["github"].to<JsonObject>(), includeSecrets);
  s.clock.toJson(root["clock"].to<JsonObject>());
}

void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();

  if (root["wifi"].is<JsonArrayConst>()) {
    WifiCred old[MAX_WIFI_NETS];
    const uint8_t oldCount = s.wifiCount;
    for (uint8_t i = 0; i < oldCount; ++i) old[i] = s.wifi[i];

    s.wifiCount = 0;
    for (JsonObjectConst e : root["wifi"].as<JsonArrayConst>()) {
      if (s.wifiCount >= MAX_WIFI_NETS) break;
      const char* ssid = e["ssid"] | "";
      if (!ssid[0]) continue;
      WifiCred& dst = s.wifi[s.wifiCount++];
      dst.ssid = ssid;
      dst.pass = e["pass"] | "";
      if (!dst.pass.length()) {
        for (uint8_t i = 0; i < oldCount; ++i) {
          if (old[i].ssid == dst.ssid) {
            dst.pass = old[i].pass;
            break;
          }
        }
      }
    }
  } else if (root["staSsid"].is<const char*>()) {
    const String ssid = root["staSsid"].as<String>();
    if (ssid.length()) {
      s.wifi[0].ssid = ssid;
      if (root["staPass"].is<const char*>()) {
        const String pass = root["staPass"].as<String>();
        if (pass.length()) s.wifi[0].pass = pass;
      }
      if (!s.wifiCount) s.wifiCount = 1;
    }
  }

  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();

  if (root["mode"].is<const char*>()) {
    const String mode = root["mode"].as<String>();
    s.mode = mode.equalsIgnoreCase("network") ? MODE_NETWORK
             : mode.equalsIgnoreCase("radar") ? MODE_RADAR
             : mode.equalsIgnoreCase("github") ? MODE_GITHUB
             : mode.equalsIgnoreCase("carousel") ? MODE_CAROUSEL
             : MODE_WEATHER;
  }
  if (root["carouselSec"].is<int>()) s.carouselSec = constrain(static_cast<int>(root["carouselSec"]), 5, 3600);
  if (root["carouselWeather"].is<bool>()) s.carouselWeather = root["carouselWeather"];
  if (root["carouselNetwork"].is<bool>()) s.carouselNetwork = root["carouselNetwork"];
  if (root["carouselRadar"].is<bool>()) s.carouselRadar = root["carouselRadar"];
  if (root["carouselGithub"].is<bool>()) s.carouselGithub = root["carouselGithub"];
  if (!s.carouselWeather && !s.carouselNetwork &&
      !s.carouselRadar && !s.carouselGithub) {
    // A carousel with no members has no sensible active/upcoming source. Keep
    // one safe default selected even when a direct API client bypasses the UI.
    s.carouselWeather = true;
  }
  if (root["httpTimeout"].is<int>()) s.httpTimeout = constrain(static_cast<int>(root["httpTimeout"]), 1000, 20000);
  if (root["brightness"].is<int>()) s.brightness = constrain(static_cast<int>(root["brightness"]), 0, 100);
  if (root["autoBrightness"].is<bool>()) s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>()) s.rotation = static_cast<uint8_t>(static_cast<int>(root["rotation"]) & 3);

  if (root["weather"].is<JsonObjectConst>()) {
    s.weather.fromJson(root["weather"].as<JsonObjectConst>());
    if (s.weather.timezone.length()) s.clock.tz = s.weather.timezone;
    if (s.weather.timezoneAbbr.length()) s.clock.tzAbbr = s.weather.timezoneAbbr;
    s.clock.utcOffsetSec = s.weather.utcOffsetSec;
    s.clock.tzPosix = "";  // browser-resolved fixed offset is the source of truth
  }
  if (root["network"].is<JsonObjectConst>()) s.network.fromJson(root["network"].as<JsonObjectConst>());
  if (root["radar"].is<JsonObjectConst>()) s.radar.fromJson(root["radar"].as<JsonObjectConst>());
  if (root["github"].is<JsonObjectConst>()) s.github.fromJson(root["github"].as<JsonObjectConst>());
  if (root["clock"].is<JsonObjectConst>()) s.clock.fromJson(root["clock"].as<JsonObjectConst>());
}
