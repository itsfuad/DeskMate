#include "RadarClient.h"
#include "Platform.h"
#include "HttpRequest.h"
#include <ArduinoJson.h>
#include <math.h>

static Aircraft g_ac[MAX_AIRCRAFT];   // kept sorted nearest-first
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;

static TlsSession g_radarSession;

struct AircraftTrail {
  char callsign[9];
  float lastLat;
  float lastLon;
  int16_t dLat[16]; // Relative to origin, scaled by 5000
  int16_t dLon[16]; // Relative to origin, scaled by 5000
  uint8_t count;
  uint32_t lastSeenMs;
};

#define MAX_TRAILS 12
constexpr uint8_t kTrailMaxPoints = 16;
constexpr float kTrailMinimumGapKm = 2.0f;
constexpr float kTrailMaximumRadiusKm = 100.0f;
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
      uint8_t count = min<uint8_t>(g_trails[i].count, kTrailMaxPoints);
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
constexpr size_t kRadarUrlCapacity = 512;

static uint16_t rangeNm(uint16_t km) {
  uint16_t nm = (uint16_t)lroundf(km / 1.852f) + 1;   // +1 so the ring edge is covered
  return nm < 1 ? 1 : nm;
}

static bool buildDirectUrl(const Settings& s, char* out, size_t outSize) {
  const int written = snprintf(
      out, outSize, "https://%s%s%.4f/lon/%.4f/dist/%u", ADSB_HOST,
      ADSB_PATH, s.radar.lat, s.radar.lon, rangeNm(s.radar.rangeKm));
  return written > 0 && static_cast<size_t>(written) < outSize;
}

static bool buildWebhookUrl(const Settings& s, char* out, size_t outSize) {
  const char* base = s.radar.webhookUrl.c_str();
  const char sep = strchr(base, '?') ? '&' : '?';
  const int written = snprintf(
      out, outSize, "%s%clat=%.4f&lon=%.4f&dist=%u", base, sep,
      s.radar.lat, s.radar.lon, s.radar.rangeKm);  // webhook uses km
  return written > 0 && static_cast<size_t>(written) < outSize;
}

// ---- parse the adsb.fi / webhook "ac" array --------------------------------
static bool parseAdsb(const Settings& s, Stream& stream) {
  // Filter to just the fields we plot; applied to every element of "ac".
  JsonDocument filter;
  JsonObject fe = filter["ac"][0].to<JsonObject>();
  fe["lat"] = true;
  fe["lon"] = true;
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

    const float lat = a["lat"].as<float>();
    const float lon = a["lon"].as<float>();
    Aircraft t{};
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
        float movementKm = 0;
        float unusedBearing = 0;
        geo(trail.lastLat, trail.lastLon, lat, lon,
            movementKm, unusedBearing);
        if (movementKm >= kTrailMinimumGapKm) {
          // Keep only meaningful movement. This avoids storing near-identical
          // five-second samples while keeping the current aircraft position
          // independent from the compressed trail history.
          const uint8_t limit = kTrailMaxPoints - 1;
          for (int j = limit; j > 0; --j) {
            trail.dLat[j] = trail.dLat[j - 1];
            trail.dLon[j] = trail.dLon[j - 1];
          }
          trail.dLat[0] = (int16_t)roundf((trail.lastLat - s.radar.lat) * 5000.0f);
          trail.dLon[0] = (int16_t)roundf((trail.lastLon - s.radar.lon) * 5000.0f);
          trail.lastLat = lat;
          trail.lastLon = lon;
          if (trail.count < kTrailMaxPoints) trail.count++;

          // Trim old points by geographic radius, not by poll count. The
          // newest point is index zero; once an old point falls outside the
          // configured radius, all subsequent points are older still.
          uint8_t kept = 0;
          for (uint8_t j = 0; j < trail.count; ++j) {
            const float pointLat = s.radar.lat + trail.dLat[j] / 5000.0f;
            const float pointLon = s.radar.lon + trail.dLon[j] / 5000.0f;
            geo(lat, lon, pointLat, pointLon, movementKm, unusedBearing);
            if (movementKm <= kTrailMaximumRadiusKm) {
              trail.dLat[kept] = trail.dLat[j];
              trail.dLon[kept] = trail.dLon[j];
              ++kept;
            }
          }
          trail.count = kept;
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
          trail.lastLat = lat;
          trail.lastLon = lon;
          trail.count = 0;
          trail.lastSeenMs = millis();
        }
      }
    }

    geo(s.radar.lat, s.radar.lon, lat, lon, t.distKm, t.bearingDeg);
    insertNearest(t);
  }

  g_lastOkMs = millis();
  g_error = false;
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
static bool fetchUrl(const Settings& s, const char* url, uint16_t budgetMs, int* responseCode = nullptr) {
  const bool https = strncmp(url, "https://", 8) == 0;

  std::unique_ptr<NetClient> client;
  if (https) {
    if (!platformTlsMemoryReady()) return false;
    client.reset(platformMakeSecureClient(PLATFORM_TLS_RX_BYTES,
                                          &g_radarSession));
  } else {
    client.reset(new WiFiClient());
  }
  if (!client) return false;

  const uint16_t timeoutMs =
      min<uint16_t>(min<uint16_t>(s.httpTimeout, 6000), budgetMs);
  int code = 0;
  int contentLength = -1;
  bool chunked = false;
  if (!httpGet(*client, url, ADSB_USER_AGENT, "application/json", timeoutMs,
               49152, &code, &contentLength, &chunked)) return false;
  if (responseCode) *responseCode = code;
  if (code != 200 || chunked || contentLength < 0) {
    client->stop();
    return false;
  }

  yield();
  bool ok = parseAdsb(s, *client);
  yield();
  client->stop();
  return ok;
}

// ---------------------------------------------------------------------------
bool radarPoll(const Settings& settings, uint16_t budgetMs) {
  if ((settings.radar.lat == 0.0f && settings.radar.lon == 0.0f) ||
      budgetMs < 250) return false;

  const bool useWebhook = settings.radar.source == RADAR_SRC_WEBHOOK &&
                          settings.radar.webhookUrl.length() >= 8;
  char url[kRadarUrlCapacity];
  const bool urlOk = useWebhook
      ? buildWebhookUrl(settings, url, sizeof(url))
      : buildDirectUrl(settings, url, sizeof(url));
  if (!urlOk) return false;
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
  char url[kRadarUrlCapacity];
  const bool urlOk = useWebhook
      ? buildWebhookUrl(settings, url, sizeof(url))
      : buildDirectUrl(settings, url, sizeof(url));
  if (!urlOk) {
    httpCode = 0;
    aircraftCount = 0;
    return false;
  }
  const bool ok = fetchUrl(settings, url, budgetMs, &httpCode);
  aircraftCount = g_count;
  if (!ok) g_error = true;
  return ok;
}
