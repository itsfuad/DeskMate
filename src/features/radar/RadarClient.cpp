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

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_ac[i]; }
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }

void radarInit(const Settings& settings) {
  (void)settings;
  g_count = 0;
  g_error = false;
  g_lastOkMs = 0;
  radarTrailReset();
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
  fe["track"] = true;
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

    radarTrailObserve(t.callsign, s.radar.lat, s.radar.lon, lat, lon,
                      millis());

    geo(s.radar.lat, s.radar.lon, lat, lon, t.distKm, t.bearingDeg);
    const float track = (a["track"].is<float>() || a["track"].is<int>())
        ? a["track"].as<float>() : -1.0f;
    t.headingDeg = track >= 0.0f && track <= 360.0f
        ? track : t.bearingDeg;
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
  // adsb.fi sits behind a CDN that answers an HTTP/1.0 request by closing the
  // connection instead of sending Content-Length or Transfer-Encoding. The
  // ArduinoJson reader below stops at the end of the document, so an unframed
  // body is perfectly parseable; refusing one made a healthy endpoint look
  // dead.
  if (!httpGet(*client, url, ADSB_USER_AGENT, "application/json", timeoutMs,
               49152, &code, &contentLength, &chunked, true)) return false;
  if (responseCode) *responseCode = code;
  if (code != 200 || chunked) {
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
