#include "RadarClient.h"
#include "Platform.h"
#include "HttpRequest.h"
#include <ArduinoJson.h>
#include <math.h>

static Aircraft g_ac[MAX_AIRCRAFT];   // kept sorted nearest-first
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;

// TLS receive-buffer size for the adsb.fi handshake, chosen once by probing the
// server's Maximum Fragment Length support. MFLN at 512/1024 lets BearSSL use a
// tiny buffer (a big heap win on the ESP8266); otherwise we fall back to 4 KB and
// hope the records fit — if they don't in busy airspace, the webhook path is the
// reliable alternative.
static TlsSession g_radarSession;

struct AircraftTrail {
  char callsign[9];
  float lastLat;
  float lastLon;
  int16_t dLat[30]; // Relative to origin, scaled by 5000
  int16_t dLon[30]; // Relative to origin, scaled by 5000
  uint8_t count;
  uint32_t lastSeenMs;
};

#define MAX_TRAILS 12
static AircraftTrail g_trails[MAX_TRAILS];

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_ac[i]; }
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }

void radarInit(const Settings& settings) {
  (void)settings;
  g_count = 0;
  g_error = false;
  g_lastOkMs = 0;
  for (uint8_t i = 0; i < MAX_TRAILS; ++i) {
    g_trails[i].callsign[0] = '\0';
    g_trails[i].count = 0;
    g_trails[i].lastSeenMs = 0;
  }
}

uint8_t getAircraftTrail(const char* callsign, float originLat, float originLon,
                         float* lats, float* lons, uint8_t maxPoints) {
  if (!callsign || !callsign[0]) return 0;
  for (uint8_t i = 0; i < MAX_TRAILS; ++i) {
    if (g_trails[i].callsign[0] != '\0' && strcmp(g_trails[i].callsign, callsign) == 0) {
      uint8_t count = g_trails[i].count;
      if (count > maxPoints) count = maxPoints;
      for (uint8_t j = 0; j < count; ++j) {
        lats[j] = originLat + (g_trails[i].dLat[j] / 5000.0f);
        lons[j] = originLon + (g_trails[i].dLon[j] / 5000.0f);
      }
      return count;
    }
  }
  return 0;
}

// ---- geo: flat-earth projection around home (good enough at radar ranges) --
static void geo(float homeLat, float homeLon, float lat, float lon,
                float& distKm, float& brg) {
  float dLat = (lat - homeLat) * 111.0f;                              // km north
  float dLon = (lon - homeLon) * 111.0f * cosf(homeLat * (float)PI / 180.0f); // km east
  distKm = sqrtf(dLat * dLat + dLon * dLon);
  brg = atan2f(dLon, dLat) * 180.0f / (float)PI;                      // 0 = N, 90 = E
  if (brg < 0) brg += 360.0f;
}

// Keep the array sorted ascending by distance, holding at most MAX_AIRCRAFT.
static void insertNearest(const Aircraft& t) {
  if (g_count == MAX_AIRCRAFT && t.distKm >= g_ac[g_count - 1].distKm) return;
  uint8_t i = (g_count < MAX_AIRCRAFT) ? g_count : (uint8_t)(MAX_AIRCRAFT - 1);
  while (i > 0 && g_ac[i - 1].distKm > t.distKm) { g_ac[i] = g_ac[i - 1]; i--; }
  g_ac[i] = t;
  if (g_count < MAX_AIRCRAFT) g_count++;
}

static void trimTail(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

// ---- URL builders ----------------------------------------------------------
static uint16_t rangeNm(uint16_t km) {
  uint16_t nm = (uint16_t)lroundf(km / 1.852f) + 1;   // +1 so the ring edge is covered
  return nm < 1 ? 1 : nm;
}

static String buildDirectUrl(const Settings& s) {
  String u = F("https://");
  u += F(ADSB_HOST);
  u += F(ADSB_PATH);
  u += String(s.radar.lat, 4);
  u += F("/lon/");
  u += String(s.radar.lon, 4);
  u += F("/dist/");
  u += String(rangeNm(s.radar.rangeKm));
  return u;
}

static String buildWebhookUrl(const Settings& s) {
  String u = s.radar.webhookUrl;
  char sep = (u.indexOf('?') >= 0) ? '&' : '?';
  u += sep;
  u += "lat=" + String(s.radar.lat, 4);
  u += "&lon=" + String(s.radar.lon, 4);
  u += "&dist=" + String(s.radar.rangeKm);   // webhook works in km
  return u;
}

// ---- parse the adsb.fi / webhook "ac" array --------------------------------
static bool parseAdsb(const Settings& s, Stream& stream) {
  // Filter to just the fields we plot; applied to every element of "ac".
  JsonDocument filter;
  JsonObject fe = filter["ac"][0].to<JsonObject>();
  fe["lat"] = true;
  fe["lon"] = true;
  fe["track"] = true;
  fe["gs"] = true;
  fe["flight"] = true;
  fe["hex"] = true;
  fe["alt_baro"] = true;
  fe["category"] = true;
  fe["t"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
  if (ac.isNull()) return false;

  g_count = 0;
  for (JsonObjectConst a : ac) {
    if (!a["lat"].is<float>() && !a["lat"].is<int>()) continue;
    if (!a["lon"].is<float>() && !a["lon"].is<int>()) continue;

    Aircraft t{};
    t.lat = a["lat"].as<float>();
    t.lon = a["lon"].as<float>();
    t.track = (a["track"].is<float>() || a["track"].is<int>()) ? a["track"].as<float>() : NAN;
    t.gs    = (a["gs"].is<float>()    || a["gs"].is<int>())    ? a["gs"].as<float>()    : NAN;
    t.altFt = a["alt_baro"].is<int>() ? a["alt_baro"].as<int>() : 0;  // "ground" => 0

    // Optional: drop ground/low traffic below the configured altitude threshold.
    if (s.radar.minAltFt > 0 && t.altFt < (int32_t)s.radar.minAltFt) continue;

    const char* fl = a["flight"] | (a["hex"] | "");
    strlcpy(t.callsign, fl, sizeof(t.callsign));
    trimTail(t.callsign);
    strlcpy(t.category, a["category"] | "", sizeof(t.category));
    strlcpy(t.type, a["t"] | "", sizeof(t.type));

    // Update or insert into global trail pool
    if (t.callsign[0] != '\0') {
      int foundIdx = -1;
      for (uint8_t i = 0; i < MAX_TRAILS; ++i) {
        if (g_trails[i].callsign[0] != '\0' && strcmp(g_trails[i].callsign, t.callsign) == 0) {
          foundIdx = i;
          break;
        }
      }

      if (foundIdx >= 0) {
        AircraftTrail& trail = g_trails[foundIdx];
        if (t.lat != trail.lastLat || t.lon != trail.lastLon) {
          // Shift history
          for (int j = 29; j > 0; j--) {
            trail.dLat[j] = trail.dLat[j - 1];
            trail.dLon[j] = trail.dLon[j - 1];
          }
          // Push previous point relative to origin
          trail.dLat[0] = (int16_t)roundf((trail.lastLat - s.radar.lat) * 5000.0f);
          trail.dLon[0] = (int16_t)roundf((trail.lastLon - s.radar.lon) * 5000.0f);
          
          trail.lastLat = t.lat;
          trail.lastLon = t.lon;
          if (trail.count < 30) trail.count++;
        }
        trail.lastSeenMs = millis();
      } else {
        // Evict least-recently-seen or use empty slot
        int slot = -1;
        uint32_t oldestMs = 0xFFFFFFFFUL;
        for (uint8_t i = 0; i < MAX_TRAILS; ++i) {
          if (g_trails[i].callsign[0] == '\0') {
            slot = i;
            break;
          }
          if (g_trails[i].lastSeenMs < oldestMs) {
            oldestMs = g_trails[i].lastSeenMs;
            slot = i;
          }
        }
        if (slot >= 0) {
          AircraftTrail& trail = g_trails[slot];
          strlcpy(trail.callsign, t.callsign, sizeof(trail.callsign));
          trail.lastLat = t.lat;
          trail.lastLon = t.lon;
          trail.count = 0;
          trail.lastSeenMs = millis();
        }
      }
    }

    geo(s.radar.lat, s.radar.lon, t.lat, t.lon, t.distKm, t.bearingDeg);
    insertNearest(t);
  }

  g_lastOkMs = millis();
  g_error = false;
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
static bool fetchUrl(const Settings& s, const String& url, uint16_t budgetMs, int* responseCode = nullptr) {
  const bool https = url.startsWith("https://");
  HttpRequest request;
  HttpRequestOptions options;
  options.host = https ? ADSB_HOST : nullptr;
  options.timeoutMs = min<uint16_t>(min<uint16_t>(s.httpTimeout, 6000), budgetMs);
  options.workingSetBytes = 6000;
  options.responseLimitBytes = 49152;
  options.session = https ? &g_radarSession : nullptr;
  if (!request.begin(url, options)) return false;

  HTTPClient& http = request.http();
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(ADSB_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  if (responseCode) *responseCode = code;
  if (code != HTTP_CODE_OK) {
    request.end();
    return false;
  }

  yield();
  bool ok = parseAdsb(s, request.stream());
  yield();
  request.end();
  return ok;
}

// ---------------------------------------------------------------------------
bool radarPoll(const Settings& settings, uint16_t budgetMs) {
  if ((settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) ||
      budgetMs < 250) return false;

  const bool useWebhook = settings.radar.source == RADAR_SRC_WEBHOOK &&
                          settings.radar.webhookUrl.length() >= 8;
  const String url = useWebhook ? buildWebhookUrl(settings) : buildDirectUrl(settings);
  const bool ok = fetchUrl(settings, url, budgetMs);
  if (!ok) g_error = true;  // keep the previous snapshot visible
  return ok;
}

bool radarTest(const Settings& settings, uint16_t budgetMs,
               uint8_t& aircraftCount, int& httpCode) {
  if (settings.radar.lat < -90.0f || settings.radar.lat > 90.0f ||
      settings.radar.lon < -180.0f || settings.radar.lon > 180.0f) {
    httpCode = 0;
    aircraftCount = 0;
    return false;
  }
  const bool useWebhook = settings.radar.source == RADAR_SRC_WEBHOOK &&
                          settings.radar.webhookUrl.length() >= 8;
  const String url = useWebhook ? buildWebhookUrl(settings) : buildDirectUrl(settings);
  const bool ok = fetchUrl(settings, url, budgetMs, &httpCode);
  aircraftCount = g_count;
  if (!ok) g_error = true;
  return ok;
}
