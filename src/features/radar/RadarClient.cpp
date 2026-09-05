#include "RadarClient.h"
#include "Platform.h"
#include "HttpRequest.h"
#include "JsonScanner.h"
#include <math.h>
#include <new>
#include <stdlib.h>

static Aircraft g_ac[MAX_AIRCRAFT];   // kept sorted nearest-first
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;
static bool     g_lowMemory = false;

static TlsSession g_radarSession;

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_ac[i]; }
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }
bool            radarLowMemory()  { return g_lowMemory; }

void radarInit(const Settings& settings) {
  (void)settings;
  g_count = 0;
  g_error = false;
  g_lowMemory = false;
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
namespace {
struct ParsedAircraft {
  Aircraft aircraft;
  float lat;
  float lon;
};

static ParsedAircraft candidates[MAX_AIRCRAFT];

struct RadarParse {
  const Settings& settings;
  ParsedAircraft current{};
  int16_t index = -1;
  uint8_t count = 0;
  bool hasLat = false;
  bool hasLon = false;
  uint8_t depth = 0;
  bool rootObject = false;
  bool aircraftArray = false;
  bool aircraftRow = false;
  bool invalidSchema = false;
};

void radarJsonContainer(void* context, const JsonScanner& scanner,
                        JsonScanner::Container event) {
  RadarParse& parse = *static_cast<RadarParse*>(context);
  const bool start = event == JsonScanner::Container::ObjectStart ||
                     event == JsonScanner::Container::ArrayStart;
  const bool object = event == JsonScanner::Container::ObjectStart ||
                      event == JsonScanner::Container::ObjectEnd;
  if (start) {
    if (parse.depth == 0) {
      parse.rootObject = object;
      if (!object) parse.invalidSchema = true;
    } else if (parse.depth == 1 && !object && parse.rootObject &&
               !strcmp(scanner.container(), "ac")) {
      parse.aircraftArray = true;
    } else if (parse.depth == 2 && parse.aircraftArray) {
      if (object) parse.aircraftRow = true;
      else parse.invalidSchema = true;
    }
    ++parse.depth;
    return;
  }
  if (object && parse.depth == 3 && parse.aircraftRow) parse.aircraftRow = false;
  if (parse.depth) --parse.depth;
}

void commitRadarRow(RadarParse& parse) {
  ParsedAircraft& row = parse.current;
  if (!parse.hasLat || !parse.hasLon ||
      (parse.settings.radar.minAltFt > 0 &&
       row.aircraft.altFt < static_cast<int32_t>(parse.settings.radar.minAltFt))) return;

  radarTrailObserve(row.aircraft.callsign, parse.settings.radar.lat,
                    parse.settings.radar.lon, row.lat, row.lon, millis());
  geo(parse.settings.radar.lat, parse.settings.radar.lon, row.lat, row.lon,
      row.aircraft.distKm, row.aircraft.bearingDeg);
  if (row.aircraft.headingDeg < 0.0f || row.aircraft.headingDeg > 360.0f)
    row.aircraft.headingDeg = row.aircraft.bearingDeg;

  if (parse.count == MAX_AIRCRAFT &&
      row.aircraft.distKm >= candidates[parse.count - 1].aircraft.distKm) return;
  uint8_t i = parse.count < MAX_AIRCRAFT ? parse.count : MAX_AIRCRAFT - 1;
  while (i > 0 && candidates[i - 1].aircraft.distKm > row.aircraft.distKm) {
    candidates[i] = candidates[i - 1];
    --i;
  }
  candidates[i] = row;
  if (parse.count < MAX_AIRCRAFT) ++parse.count;
}

void radarJsonValue(void* context, const JsonScanner& scanner,
                    JsonScanner::Value type, const char* text, uint32_t number) {
  (void)number;
  RadarParse& parse = *static_cast<RadarParse*>(context);
  const int16_t index = scanner.indexUnder("ac");
  if (!parse.aircraftArray || !parse.aircraftRow || parse.depth != 3 || index < 0) {
    if (parse.aircraftArray && parse.depth == 2) parse.invalidSchema = true;
    return;
  }
  if (index != parse.index) {
    if (parse.index >= 0) commitRadarRow(parse);
    parse.current = ParsedAircraft{};
    parse.current.aircraft.headingDeg = -1.0f;
    parse.hasLat = false;
    parse.hasLon = false;
    parse.index = index;
  }

  const char* key = scanner.key();
  if (type != JsonScanner::Value::String) {
    char* end = nullptr;
    const float value = strtof(text, &end);
    if (end == text || *end) return;
    if (!strcmp(key, "lat")) {
      parse.current.lat = value;
      parse.hasLat = true;
    } else if (!strcmp(key, "lon")) {
      parse.current.lon = value;
      parse.hasLon = true;
    } else if (!strcmp(key, "track")) {
      parse.current.aircraft.headingDeg = value;
    } else if (!strcmp(key, "alt_baro")) {
      parse.current.aircraft.altFt = static_cast<int32_t>(value);
    }
  } else {
    if (!strcmp(key, "flight")) {
      strlcpy(parse.current.aircraft.callsign, text,
              sizeof(parse.current.aircraft.callsign));
      trimTail(parse.current.aircraft.callsign);
    } else if (!strcmp(key, "hex") && !parse.current.aircraft.callsign[0]) {
      strlcpy(parse.current.aircraft.callsign, text,
              sizeof(parse.current.aircraft.callsign));
    } else if (!strcmp(key, "category")) {
      strlcpy(parse.current.aircraft.category, text,
              sizeof(parse.current.aircraft.category));
    } else if (!strcmp(key, "t")) {
      strlcpy(parse.current.aircraft.type, text,
              sizeof(parse.current.aircraft.type));
    }
  }
}
}  // namespace

static bool parseAdsb(const Settings& s, Stream& stream, NetClient& client,
                      int contentLength, uint32_t timeoutMs) {
  std::unique_ptr<RadarParse> parse(new (std::nothrow) RadarParse{s});
  if (!parse) {
    g_lowMemory = true;
    return false;
  }
  JsonScanner& scanner = JsonScanner::shared(stream, client, contentLength, timeoutMs);
  scanner.setContainerHandler(radarJsonContainer, parse.get());
  radarTrailBeginUpdate();
  if (!scanner.walk(radarJsonValue, parse.get()) || !parse->rootObject ||
      !parse->aircraftArray || parse->invalidSchema) {
    radarTrailDiscardUpdate();
    return false;
  }
  if (parse->index >= 0) commitRadarRow(*parse);
  radarTrailCommitUpdate();

  // Publish only after the complete document validates. A timeout or malformed
  // response leaves both the previous aircraft snapshot and trails intact.
  g_count = parse->count;
  for (uint8_t i = 0; i < parse->count; ++i) g_ac[i] = candidates[i].aircraft;
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
  // The streaming JSON reader stops at the end of the document, so an unframed
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
  bool ok = parseAdsb(s, *client, *client, contentLength, timeoutMs);
  yield();
  client->stop();
  return ok;
}

// ---------------------------------------------------------------------------
bool radarPoll(const Settings& settings, uint16_t budgetMs) {
  g_lowMemory = false;
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
