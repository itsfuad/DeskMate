#include "Settings.h"
#include "Platform.h"   // platformChipId() for the unique default hostname
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

// ===========================================================================
// Clock / night mode slice
// ===========================================================================
static uint16_t hhmmToMin(const char* s, uint16_t fallback) {
  if (!s || !s[0]) return fallback;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return (uint16_t)(h * 60 + m);
}
static String minToHhmm(uint16_t v) {
  if (v > 1439) v = 0;
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(v / 60), (unsigned)(v % 60));
  return String(b);
}

void ClockSettings::setDefaults() {
  tz            = DEFAULT_TZ_NAME;
  tzPosix       = DEFAULT_TZ_POSIX;
  nightEnabled  = DEFAULT_NIGHT_ENABLED;
  nightStartMin = DEFAULT_NIGHT_START_MIN;
  nightEndMin   = DEFAULT_NIGHT_END_MIN;
  nightLevel    = DEFAULT_NIGHT_LEVEL;
}

void ClockSettings::toJson(JsonObject o) const {
  o["tz"]           = tz;
  o["tzPosix"]      = tzPosix;
  o["nightEnabled"] = nightEnabled;
  o["nightStart"]   = minToHhmm(nightStartMin);
  o["nightEnd"]     = minToHhmm(nightEndMin);
  o["nightLevel"]   = nightLevel;
}

void ClockSettings::fromJson(JsonObjectConst o) {
  if (o["tz"].is<const char*>())          tz = o["tz"].as<String>();
  if (o["tzPosix"].is<const char*>())     tzPosix = o["tzPosix"].as<String>();
  if (o["nightEnabled"].is<bool>())       nightEnabled = o["nightEnabled"];
  if (o["nightStart"].is<const char*>())  nightStartMin = hhmmToMin(o["nightStart"], nightStartMin);
  if (o["nightEnd"].is<const char*>())    nightEndMin   = hhmmToMin(o["nightEnd"], nightEndMin);
  if (o["nightLevel"].is<int>())          nightLevel = constrain((int)o["nightLevel"], 0, 100);
}

// ===========================================================================
// Radar slice
// ===========================================================================
void RadarSettings::setDefaults() {
  lat = DEFAULT_RADAR_LAT;
  lon = DEFAULT_RADAR_LON;
  source = DEFAULT_RADAR_SRC;
  webhookUrl = "";
  rangeKm = DEFAULT_RADAR_RANGE_KM;
  pollSec = DEFAULT_RADAR_POLL_SEC;
  unitsMi = false;
  showLabels = true;
  showVectors = true;
  showRimDots = true;
  uiScale = 1;            // medium
  minAltFt = 0;           // show all
  airportCount = 0;
  for (uint8_t i = 0; i < MAX_AIRPORTS; i++) {
    airports[i].icao[0] = 0;
    airports[i].lat = airports[i].lon = 0;
  }
}

void RadarSettings::toJson(JsonObject o) const {
  o["lat"]         = lat;
  o["lon"]         = lon;
  o["source"]      = (source == RADAR_SRC_WEBHOOK) ? "webhook" : "direct";
  o["webhookUrl"]  = webhookUrl;
  o["rangeKm"]     = rangeKm;
  o["pollSec"]     = pollSec;
  o["unitsMi"]     = unitsMi;
  o["showLabels"]  = showLabels;
  o["showVectors"] = showVectors;
  o["showRimDots"] = showRimDots;
  o["uiScale"]     = uiScale;
  o["minAltFt"]    = minAltFt;

  JsonArray arr = o["airports"].to<JsonArray>();
  for (uint8_t i = 0; i < airportCount; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["icao"] = airports[i].icao;
    e["lat"]  = airports[i].lat;
    e["lon"]  = airports[i].lon;
  }
}

void RadarSettings::fromJson(JsonObjectConst o) {
  if (o["lat"].is<float>() || o["lat"].is<int>()) lat = o["lat"].as<float>();
  if (o["lon"].is<float>() || o["lon"].is<int>()) lon = o["lon"].as<float>();
  if (o["source"].is<const char*>()) {
    String src = o["source"].as<String>();
    source = src.equalsIgnoreCase("webhook") ? RADAR_SRC_WEBHOOK : RADAR_SRC_DIRECT;
  }
  if (o["webhookUrl"].is<const char*>()) webhookUrl = o["webhookUrl"].as<String>();
  if (o["rangeKm"].is<int>())    rangeKm = constrain((int)o["rangeKm"], 1, 500);
  if (o["pollSec"].is<int>())    pollSec = max(3, (int)o["pollSec"]);
  if (o["unitsMi"].is<bool>())   unitsMi = o["unitsMi"];
  if (o["showLabels"].is<bool>())  showLabels = o["showLabels"];
  if (o["showVectors"].is<bool>()) showVectors = o["showVectors"];
  if (o["showRimDots"].is<bool>()) showRimDots = o["showRimDots"];
  if (o["uiScale"].is<int>())      uiScale = constrain((int)o["uiScale"], 0, 2);
  if (o["minAltFt"].is<int>())     minAltFt = constrain((int)o["minAltFt"], 0, 60000);

  if (o["airports"].is<JsonArrayConst>()) {
    JsonArrayConst arr = o["airports"].as<JsonArrayConst>();
    airportCount = 0;
    for (JsonObjectConst e : arr) {
      if (airportCount >= MAX_AIRPORTS) break;
      const char* ic = e["icao"] | "";
      if (!ic[0]) continue;                  // skip blank rows
      Airport& dst = airports[airportCount];
      strlcpy(dst.icao, ic, MAX_ICAO_LEN);
      dst.lat = e["lat"].as<float>();
      dst.lon = e["lon"].as<float>();
      airportCount++;
    }
  }
}


// ===========================================================================
// Desk-dashboard feature slices
// ===========================================================================
void WeatherSettings::setDefaults(){ lat=23.8103f; lon=90.4125f; city="Dhaka"; pollSec=DEFAULT_WEATHER_POLL_SEC; }
void WeatherSettings::toJson(JsonObject o) const { o["lat"]=lat; o["lon"]=lon; o["city"]=city; o["pollSec"]=pollSec; }
void WeatherSettings::fromJson(JsonObjectConst o){ if(o["lat"].is<float>()||o["lat"].is<int>())lat=o["lat"].as<float>(); if(o["lon"].is<float>()||o["lon"].is<int>())lon=o["lon"].as<float>(); if(o["city"].is<const char*>())city=o["city"].as<String>(); if(o["pollSec"].is<int>())pollSec=constrain((int)o["pollSec"],60,3600); }
void NetworkSettings::setDefaults(){ probeHost="1.1.1.1"; probePort=443; pollSec=DEFAULT_NETWORK_POLL_SEC; }
void NetworkSettings::toJson(JsonObject o) const { o["probeHost"]=probeHost; o["probePort"]=probePort; o["pollSec"]=pollSec; }
void NetworkSettings::fromJson(JsonObjectConst o){ if(o["probeHost"].is<const char*>())probeHost=o["probeHost"].as<String>(); if(o["probePort"].is<int>())probePort=constrain((int)o["probePort"],1,65535); if(o["pollSec"].is<int>())pollSec=constrain((int)o["pollSec"],2,300); }
void GithubSettings::setDefaults(){ user=""; token=""; pollSec=DEFAULT_GITHUB_POLL_SEC; repoCount=0; for(auto &r:repos)r.repo[0]=0; }
void GithubSettings::toJson(JsonObject o,bool includeSecrets) const { o["user"]=user; o["tokenSet"]=token.length()>0; if(includeSecrets)o["token"]=token; o["pollSec"]=pollSec; JsonArray a=o["repos"].to<JsonArray>(); for(uint8_t i=0;i<repoCount;i++)a.add(repos[i].repo); }
void GithubSettings::fromJson(JsonObjectConst o){ if(o["user"].is<const char*>())user=o["user"].as<String>(); if(o["token"].is<const char*>()){String t=o["token"].as<String>(); if(t.length())token=t;} if(o["pollSec"].is<int>())pollSec=constrain((int)o["pollSec"],60,3600); if(o["repos"].is<JsonArrayConst>()){repoCount=0; for(JsonVariantConst v:o["repos"].as<JsonArrayConst>()){if(repoCount>=MAX_GH_REPOS)break; const char* r=v|""; if(!r[0])continue; strlcpy(repos[repoCount++].repo,r,sizeof(repos[0].repo));}} }

// ===========================================================================
// Top-level settings
// ===========================================================================
void Settings::setDefaults() {
  wifiCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_NETS; i++) {
    wifi[i].ssid = "";
    wifi[i].pass = "";
  }
  apSsid  = DEFAULT_AP_SSID;
  apPass  = DEFAULT_AP_PASS;
  // Unique per device so several SmallTVs on one network don't collide on
  // mDNS out of the box. A hostname saved in config.json overrides this.
  hostname = String(DEFAULT_HOSTNAME) + "-" + String(platformChipId() & 0xFFFF, HEX);

  mode = DEFAULT_MODE;
  carouselSec = DEFAULT_CAROUSEL_SEC;
  carouselWeather = carouselNetwork = carouselRadar = carouselGithub = true;
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
}

// ---------------------------------------------------------------------------
bool settingsBegin() {
  if (LittleFS.begin()) return true;
  // First boot on a fresh chip: format then mount.
  if (LittleFS.format() && LittleFS.begin()) return true;
  return false;
}

bool loadSettings(Settings& s) {
  s.setDefaults();
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  settingsApplyJson(s, doc.as<JsonObjectConst>());
  return true;
}

bool saveSettings(const Settings& s) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(s, root, /*includeSecrets=*/true);

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

// ---------------------------------------------------------------------------
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["hostname"]   = s.hostname;

  // WiFi networks. Passwords only reach the config file, never the web API.
  JsonArray wf = root["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    JsonObject e = wf.add<JsonObject>();
    e["ssid"]    = s.wifi[i].ssid;
    e["passSet"] = s.wifi[i].pass.length() > 0;
    if (includeSecrets) e["pass"] = s.wifi[i].pass;
  }
  // Legacy mirror of the primary network, kept for one release so a firmware
  // downgrade still finds its WiFi in config.json.
  root["staSsid"]    = s.wifiCount ? s.wifi[0].ssid : "";
  root["staPassSet"] = s.wifiCount && s.wifi[0].pass.length() > 0;
  root["apSsid"]     = s.apSsid;
  root["apPassSet"]  = s.apPass.length() > 0;
  if (includeSecrets) {
    root["staPass"]  = s.wifiCount ? s.wifi[0].pass : "";
    root["apPass"]   = s.apPass;
  }

  // Mode + shared HTTP/display
  root["mode"] = (s.mode == MODE_NETWORK) ? "network" : (s.mode == MODE_RADAR) ? "radar" : (s.mode == MODE_GITHUB) ? "github" : (s.mode == MODE_CAROUSEL) ? "carousel" : "weather";
  root["carouselSec"] = s.carouselSec;
  root["carouselWeather"] = s.carouselWeather;
  root["carouselNetwork"] = s.carouselNetwork;
  root["carouselRadar"] = s.carouselRadar;
  root["carouselGithub"] = s.carouselGithub;
  root["httpTimeout"]       = s.httpTimeout;
  root["brightness"]        = s.brightness;
  root["autoBrightness"]    = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"]          = s.rotation;

  // Feature slices
  s.weather.toJson(root["weather"].to<JsonObject>());
  s.network.toJson(root["network"].to<JsonObject>());
  s.radar.toJson(root["radar"].to<JsonObject>());
  s.github.toJson(root["github"].to<JsonObject>(), includeSecrets);
  s.clock.toJson(root["clock"].to<JsonObject>());
}

// Apply only the keys that are present (partial update friendly). Accepts both
// the nested layout and the legacy flat layout (feature keys at the top level).
void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();

  if (root["wifi"].is<JsonArrayConst>()) {
    // The list is authoritative when present (order = try priority, missing
    // row = deletion). A blank password keeps the stored one, matched by SSID
    // so rows survive reordering.
    WifiCred old[MAX_WIFI_NETS];
    uint8_t oldCount = s.wifiCount;
    for (uint8_t i = 0; i < oldCount; i++) old[i] = s.wifi[i];

    s.wifiCount = 0;
    for (JsonObjectConst e : root["wifi"].as<JsonArrayConst>()) {
      if (s.wifiCount >= MAX_WIFI_NETS) break;
      const char* ssid = e["ssid"] | "";
      if (!ssid[0]) continue;                // skip blank rows
      WifiCred& dst = s.wifi[s.wifiCount];
      dst.ssid = ssid;
      const char* pass = e["pass"] | "";
      dst.pass = pass;
      if (!pass[0])
        for (uint8_t i = 0; i < oldCount; i++)
          if (old[i].ssid == dst.ssid) { dst.pass = old[i].pass; break; }
      s.wifiCount++;
    }
  } else if (root["staSsid"].is<const char*>()) {
    // Legacy single-network layout (pre-2.4 config.json or an old cached web
    // page): it becomes/updates the primary network, extras stay untouched.
    String ssid = root["staSsid"].as<String>();
    if (ssid.length()) {
      s.wifi[0].ssid = ssid;
      if (root["staPass"].is<const char*>()) {
        String p = root["staPass"].as<String>();
        if (p.length() > 0) s.wifi[0].pass = p;   // blank = keep
      }
      if (s.wifiCount < 1) s.wifiCount = 1;
    }
  }
  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  // AP password: apply as-is when present (empty allowed => open AP).
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();

  if (root["mode"].is<const char*>()) {
    String m = root["mode"].as<String>();
    s.mode = m.equalsIgnoreCase("network") ? MODE_NETWORK : m.equalsIgnoreCase("radar") ? MODE_RADAR : m.equalsIgnoreCase("github") ? MODE_GITHUB : m.equalsIgnoreCase("carousel") ? MODE_CAROUSEL : MODE_WEATHER;
  }
  if (root["carouselSec"].is<int>()) s.carouselSec=constrain((int)root["carouselSec"],5,3600);
  if (root["carouselWeather"].is<bool>()) s.carouselWeather=root["carouselWeather"];
  if (root["carouselNetwork"].is<bool>()) s.carouselNetwork=root["carouselNetwork"];
  if (root["carouselRadar"].is<bool>()) s.carouselRadar=root["carouselRadar"];
  if (root["carouselGithub"].is<bool>()) s.carouselGithub=root["carouselGithub"];

  if (root["httpTimeout"].is<int>())        s.httpTimeout = constrain((int)root["httpTimeout"], 1000, 20000);
  if (root["brightness"].is<int>())         s.brightness = constrain((int)root["brightness"], 0, 100);
  if (root["autoBrightness"].is<bool>())    s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>())           s.rotation = (uint8_t)(((int)root["rotation"]) & 3);

  // Feature slices.
  if (root["weather"].is<JsonObjectConst>()) s.weather.fromJson(root["weather"].as<JsonObjectConst>());
  if (root["network"].is<JsonObjectConst>()) s.network.fromJson(root["network"].as<JsonObjectConst>());
  if (root["radar"].is<JsonObjectConst>()) s.radar.fromJson(root["radar"].as<JsonObjectConst>());
  if (root["github"].is<JsonObjectConst>()) s.github.fromJson(root["github"].as<JsonObjectConst>());
  if (root["clock"].is<JsonObjectConst>()) s.clock.fromJson(root["clock"].as<JsonObjectConst>());
}
