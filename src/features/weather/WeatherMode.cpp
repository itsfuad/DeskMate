#include "WeatherMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <time.h>

WeatherMode g_weatherMode;

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t SKY_CLEAR = rgb565(245, 190, 72);
constexpr uint16_t SKY_CLOUD = rgb565(128, 181, 220);
constexpr uint16_t SKY_RAIN  = rgb565(91, 115, 143);
constexpr uint16_t SKY_SNOW  = rgb565(184, 205, 224);
constexpr uint16_t SKY_NIGHT = rgb565(36, 53, 78);
constexpr uint16_t LAND_LIGHT = rgb565(223, 173, 72);
constexpr uint16_t LAND_COOL  = rgb565(89, 117, 134);
constexpr uint16_t PANEL_DAY  = rgb565(246, 247, 242);
constexpr uint16_t PANEL_DARK = rgb565(18, 28, 43);
constexpr uint16_t INK_DARK   = rgb565(29, 42, 55);
constexpr uint16_t INK_MUTED  = rgb565(112, 129, 146);
constexpr uint16_t WHITE_SOFT = rgb565(244, 247, 250);
constexpr uint16_t SUN        = rgb565(255, 205, 40);
constexpr uint16_t CLOUD      = rgb565(220, 229, 238);
constexpr uint16_t RAIN       = rgb565(57, 179, 232);
constexpr uint16_t SNOW       = rgb565(228, 242, 250);

struct ForecastDay {
  bool valid = false;
  int id = 800;
  float minTemp = 0;
  float maxTemp = 0;
  uint32_t stamp = 0;
};

struct WeatherData {
  bool valid = false;
  bool error = false;
  int httpCode = 0;
  char errorText[28] = "";
  char city[28] = "";
  char description[36] = "";
  char icon[5] = "";
  float temp = 0;
  float feels = 0;
  float wind = 0;
  int humidity = 0;
  int pressure = 0;
  int conditionId = 800;
  int32_t timezone = 0;
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  ForecastDay days[4];
  uint8_t dayCount = 0;
  uint32_t updatedMs = 0;
};

WeatherData W;

bool conditionIsNight() {
  return W.icon[2] == 'n';
}

bool isRain(int id) { return (id >= 200 && id < 600); }
bool isSnow(int id) { return id >= 600 && id < 700; }
bool isAtmosphere(int id) { return id >= 700 && id < 800; }
bool isCloud(int id) { return id >= 801 && id <= 804; }

const char* conditionLabel(int id) {
  if (id >= 200 && id < 300) return "THUNDER";
  if (id >= 300 && id < 400) return "DRIZZLE";
  if (id >= 500 && id < 600) return "RAIN";
  if (id >= 600 && id < 700) return "SNOW";
  if (id >= 700 && id < 800) return "MIST";
  if (id == 800) return "CLEAR";
  return "CLOUDS";
}

uint16_t skyColor() {
  if (conditionIsNight()) return SKY_NIGHT;
  if (isSnow(W.conditionId)) return SKY_SNOW;
  if (isRain(W.conditionId) || isAtmosphere(W.conditionId)) return SKY_RAIN;
  if (isCloud(W.conditionId)) return SKY_CLOUD;
  return SKY_CLEAR;
}

void localTm(uint32_t utc, struct tm& out) {
  const time_t shifted = static_cast<time_t>(utc) + W.timezone;
  gmtime_r(&shifted, &out);
}

void currentLocalTm(struct tm& out) {
  time_t now = time(nullptr);
  if (now < 1609459200) now = static_cast<time_t>(W.sunrise ? W.sunrise : 1700000000UL);
  const time_t shifted = now + W.timezone;
  gmtime_r(&shifted, &out);
}

void drawSun(TileCanvas& g, int x, int y, int r, uint16_t color) {
  g.fillCircle(x, y, r, color);
  for (int a = 0; a < 360; a += 45) {
    const float q = a * static_cast<float>(PI) / 180.0f;
    g.drawLine(x + static_cast<int>(cosf(q) * (r + 5)),
               y + static_cast<int>(sinf(q) * (r + 5)),
               x + static_cast<int>(cosf(q) * (r + 11)),
               y + static_cast<int>(sinf(q) * (r + 11)), color);
  }
}

void drawMoon(TileCanvas& g, int x, int y, int r, uint16_t sky) {
  g.fillCircle(x, y, r, WHITE_SOFT);
  g.fillCircle(x + r / 2, y - r / 3, r, sky);
}

void drawCloud(TileCanvas& g, int x, int y, uint16_t color, bool outline = false) {
  if (!outline) {
    g.fillCircle(x + 9, y + 7, 8, color);
    g.fillCircle(x + 20, y + 2, 11, color);
    g.fillCircle(x + 34, y + 8, 8, color);
    g.fillRoundRect(x, y + 7, 44, 14, 7, color);
  } else {
    g.drawCircle(x + 9, y + 7, 8, color);
    g.drawCircle(x + 20, y + 2, 11, color);
    g.drawCircle(x + 34, y + 8, 8, color);
    g.drawFastHLine(x + 3, y + 20, 37, color);
  }
}

void drawWeatherIcon(TileCanvas& g, int id, bool night, int x, int y,
                     float scale, uint16_t background) {
  const int r = static_cast<int>(12 * scale);
  if (id == 800) {
    if (night) drawMoon(g, x + r, y + r, r, background);
    else drawSun(g, x + r, y + r, r, SUN);
    return;
  }

  if (id >= 200 && id < 300) {
    drawCloud(g, x, y + 2, CLOUD);
    const int cx = x + 22;
    g.fillTriangle(cx, y + 25, cx - 5, y + 36, cx + 1, y + 34, SUN);
    g.fillTriangle(cx + 1, y + 33, cx - 2, y + 43, cx + 8, y + 30, SUN);
    return;
  }

  if (id >= 600 && id < 700) {
    drawCloud(g, x, y, CLOUD);
    for (int i = 0; i < 3; ++i) {
      const int sx = x + 9 + i * 13;
      const int sy = y + 29 + (i & 1) * 4;
      g.drawFastHLine(sx - 3, sy, 7, SNOW);
      g.drawFastVLine(sx, sy - 3, 7, SNOW);
    }
    return;
  }

  if (id >= 300 && id < 600) {
    drawCloud(g, x, y, CLOUD);
    for (int i = 0; i < 3; ++i) {
      const int rx = x + 9 + i * 13;
      g.drawLine(rx, y + 29, rx - 3, y + 37, RAIN);
    }
    return;
  }

  if (id >= 700 && id < 800) {
    drawCloud(g, x, y - 2, CLOUD);
    for (int i = 0; i < 3; ++i)
      g.drawFastHLine(x + 2 + i * 5, y + 27 + i * 5, 39 - i * 10, WHITE_SOFT);
    return;
  }

  if (!night) drawSun(g, x + 10, y + 8, static_cast<int>(7 * scale), SUN);
  drawCloud(g, x + 4, y + 8, CLOUD);
}

const char* dayName(uint8_t weekday) {
  static const char* k[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return k[weekday % 7];
}

void drawLandscape(TileCanvas& g, uint16_t sky) {
  if (conditionIsNight()) {
    for (int i = 0; i < 12; ++i) {
      const int x = (i * 37 + 13) % TFT_WIDTH;
      const int y = 16 + (i * 23) % 88;
      g.drawPixel(x, y, i % 3 ? rgb565(155, 176, 205) : WHITE_SOFT);
    }
  }

  const uint16_t far = conditionIsNight() ? rgb565(47, 69, 94)
                       : (isRain(W.conditionId) ? rgb565(81, 103, 119)
                                               : LAND_LIGHT);
  const uint16_t near = conditionIsNight() ? rgb565(31, 47, 67)
                        : (isRain(W.conditionId) ? LAND_COOL
                                                : rgb565(204, 146, 58));
  g.fillTriangle(0, 118, 80, 88, 155, 118, far);
  g.fillTriangle(80, 118, 165, 79, 240, 118, far);
  g.fillTriangle(0, 130, 66, 101, 126, 130, near);
  g.fillTriangle(78, 130, 176, 94, 240, 130, near);
  g.fillRect(0, 118, 240, 34, near);
  (void)sky;
}

void drawScreen(TileCanvas& g, void* opaque) {
  const Settings& s = *static_cast<const Settings*>(opaque);
  const uint16_t sky = skyColor();
  const bool night = conditionIsNight();
  g.fillScreen(sky);
  g.setTextWrap(false);

  if (!W.valid) {
    g.fillRoundRect(12, 17, 216, 206, 16, PANEL_DARK);
    g.setTextColor(WHITE_SOFT);
    g.setTextSize(2);
    g.setCursor(28, 42);
    g.print("DESKMATE WEATHER");
    g.setTextSize(1);
    g.setTextColor(rgb565(155, 170, 187));
    g.setCursor(28, 82);
    if (!s.weather.apiKey.length()) {
      g.print("OPENWEATHER KEY REQUIRED");
      g.setCursor(28, 101);
      g.print("Add it in the web UI.");
    } else if (W.error) {
      g.setTextColor(rgb565(255, 112, 112));
      g.print(W.errorText[0] ? W.errorText : "WEATHER API ERROR");
      if (W.httpCode) {
        g.setCursor(28, 101);
        g.print("HTTP ");
        g.print(W.httpCode);
      }
    } else {
      g.print("LOADING WEATHER...");
    }
    g.setTextColor(rgb565(115, 136, 158));
    g.setCursor(28, 174);
    g.print("Current + 5 day / 3 hour");
    g.setCursor(28, 190);
    g.print("forecast from OpenWeather.");
    return;
  }

  drawLandscape(g, sky);

  const uint16_t primary = night || isRain(W.conditionId) ? WHITE_SOFT : INK_DARK;
  const uint16_t secondary = night || isRain(W.conditionId)
      ? rgb565(185, 200, 217) : rgb565(76, 91, 105);

  struct tm nowTm;
  currentLocalTm(nowTm);
  const unsigned hour = static_cast<unsigned>(constrain(nowTm.tm_hour, 0, 23));
  const unsigned minute = static_cast<unsigned>(constrain(nowTm.tm_min, 0, 59));
  const unsigned monthDay = static_cast<unsigned>(constrain(nowTm.tm_mday, 1, 31));
  char timeText[8];
  snprintf(timeText, sizeof(timeText), "%02u:%02u", hour, minute);
  char dateText[20];
  snprintf(dateText, sizeof(dateText), "%s %02u", dayName(nowTm.tm_wday), monthDay);

  g.setTextColor(primary);
  g.setTextSize(1);
  g.setCursor(10, 9);
  String city = s.weather.city.length() ? s.weather.city : String(W.city);
  g.print(city.substring(0, 22));
  g.setTextColor(secondary);
  g.setCursor(TFT_WIDTH - gfxTextW(timeText, 1) - 10, 9);
  g.print(timeText);

  char temp[12];
  snprintf(temp, sizeof(temp), "%.0f", W.temp);
  g.setTextColor(primary);
  g.setTextSize(5);
  g.setCursor(10, 35);
  g.print(temp);
  const int degreeX = 10 + gfxTextW(temp, 5) + 7;
  g.drawCircle(degreeX, 42, 4, primary);

  drawWeatherIcon(g, W.conditionId, night, 174, 39, 1.0f, sky);

  g.setTextSize(2);
  g.setTextColor(primary);
  g.setCursor(11, 89);
  g.print(conditionLabel(W.conditionId));
  g.setTextSize(1);
  g.setTextColor(secondary);
  g.setCursor(12, 108);
  char details[36];
  const char unit = s.weather.metric ? 'C' : 'F';
  snprintf(details, sizeof(details), "FEELS %.0f%c  HUM %d%%", W.feels, unit,
           W.humidity);
  g.print(details);
  g.setCursor(12, 121);
  if (s.weather.metric)
    snprintf(details, sizeof(details), "WIND %.0f km/h  %d hPa", W.wind, W.pressure);
  else
    snprintf(details, sizeof(details), "WIND %.0f mph  %d hPa", W.wind, W.pressure);
  g.print(details);

  const uint16_t panel = night || isRain(W.conditionId) ? PANEL_DARK : PANEL_DAY;
  const uint16_t panelText = night || isRain(W.conditionId) ? WHITE_SOFT : INK_DARK;
  const uint16_t panelMuted = night || isRain(W.conditionId)
      ? rgb565(135, 153, 174) : INK_MUTED;
  g.fillRoundRect(6, 148, 228, 86, 13, panel);

  for (uint8_t i = 0; i < 4; ++i) {
    const int left = 9 + i * 56;
    if (i && i < 4) g.drawFastVLine(left - 3, 162, 57, night ? rgb565(42, 58, 76) : rgb565(226, 230, 226));
    if (i >= W.dayCount || !W.days[i].valid) continue;
    struct tm ft;
    localTm(W.days[i].stamp, ft);
    g.setTextSize(1);
    g.setTextColor(panelMuted);
    g.setCursor(left + 11, 157);
    g.print(dayName(ft.tm_wday));
    drawWeatherIcon(g, W.days[i].id, false, left + 10, 174, 0.55f, panel);
    char range[16];
    snprintf(range, sizeof(range), "%.0f/%.0f", W.days[i].maxTemp, W.days[i].minTemp);
    g.setTextColor(panelText);
    g.setCursor(left + 7, 216);
    g.print(range);
  }

  g.setTextSize(1);
  g.setTextColor(panelMuted);
  g.setCursor(12, 137);
  g.print(dateText);
}

bool beginGet(const Settings& s, const String& url,
              std::unique_ptr<SecureClient>& client, HTTPClient& http) {
  client.reset(platformMakeSecureClient(4096, nullptr, 512, false));
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  // Streaming ArduinoJson directly from HTTPClient is most reliable when the
  // server sends a Content-Length body rather than chunked transfer encoding.
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  http.setUserAgent(FW_NAME);
  return true;
}

bool fetchCurrent(const Settings& s) {
  String url = F("https://api.openweathermap.org/data/2.5/weather?lat=");
  url += String(s.weather.lat, 5);
  url += F("&lon=");
  url += String(s.weather.lon, 5);
  url += F("&units=");
  url += s.weather.metric ? F("metric") : F("imperial");
  url += F("&appid=");
  url += s.weather.apiKey;

  std::unique_ptr<SecureClient> client;
  HTTPClient http;
  if (!beginGet(s, url, client, http)) {
    strlcpy(W.errorText, "CONNECTION FAILED", sizeof(W.errorText));
    return false;
  }

  const int code = http.GET();
  W.httpCode = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    strlcpy(W.errorText, code == 401 ? "INVALID API KEY" : "CURRENT API ERROR",
            sizeof(W.errorText));
    return false;
  }

  JsonDocument filter;
  filter["name"] = true;
  filter["timezone"] = true;
  filter["main"]["temp"] = true;
  filter["main"]["feels_like"] = true;
  filter["main"]["humidity"] = true;
  filter["main"]["pressure"] = true;
  filter["wind"]["speed"] = true;
  filter["sys"]["sunrise"] = true;
  filter["sys"]["sunset"] = true;
  filter["weather"][0]["id"] = true;
  filter["weather"][0]["description"] = true;
  filter["weather"][0]["icon"] = true;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    strlcpy(W.errorText, "BAD CURRENT DATA", sizeof(W.errorText));
    return false;
  }

  W.temp = doc["main"]["temp"] | 0.0f;
  W.feels = doc["main"]["feels_like"] | 0.0f;
  W.humidity = doc["main"]["humidity"] | 0;
  W.pressure = doc["main"]["pressure"] | 0;
  const float speed = doc["wind"]["speed"] | 0.0f;
  W.wind = s.weather.metric ? speed * 3.6f : speed;
  W.conditionId = doc["weather"][0]["id"] | 800;
  W.timezone = doc["timezone"] | 0;
  W.sunrise = doc["sys"]["sunrise"] | 0UL;
  W.sunset = doc["sys"]["sunset"] | 0UL;
  strlcpy(W.city, doc["name"] | "", sizeof(W.city));
  strlcpy(W.description, doc["weather"][0]["description"] | "", sizeof(W.description));
  strlcpy(W.icon, doc["weather"][0]["icon"] | "01d", sizeof(W.icon));
  return true;
}

bool fetchForecast(const Settings& s) {
  String url = F("https://api.openweathermap.org/data/2.5/forecast?lat=");
  url += String(s.weather.lat, 5);
  url += F("&lon=");
  url += String(s.weather.lon, 5);
  url += F("&cnt=40&units=");
  url += s.weather.metric ? F("metric") : F("imperial");
  url += F("&appid=");
  url += s.weather.apiKey;

  std::unique_ptr<SecureClient> client;
  HTTPClient http;
  if (!beginGet(s, url, client, http)) return false;
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["city"]["timezone"] = true;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp_min"] = true;
  filter["list"][0]["main"]["temp_max"] = true;
  filter["list"][0]["weather"][0]["id"] = true;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;

  if (doc["city"]["timezone"].is<int>()) W.timezone = doc["city"]["timezone"];
  for (ForecastDay& d : W.days) d = ForecastDay();
  W.dayCount = 0;

  time_t now = time(nullptr);
  if (now < 1609459200) now = 1700000000;
  const int32_t today = static_cast<int32_t>((now + W.timezone) / 86400);
  int32_t keys[4] = {-1, -1, -1, -1};
  int bestNoon[4] = {99, 99, 99, 99};

  for (JsonObjectConst item : doc["list"].as<JsonArrayConst>()) {
    const uint32_t stamp = item["dt"] | 0UL;
    const int32_t key = static_cast<int32_t>((stamp + W.timezone) / 86400);
    if (key <= today) continue;

    int slot = -1;
    for (uint8_t i = 0; i < W.dayCount; ++i) if (keys[i] == key) slot = i;
    if (slot < 0) {
      if (W.dayCount >= 4) continue;
      slot = W.dayCount++;
      keys[slot] = key;
      W.days[slot].valid = true;
      W.days[slot].stamp = stamp;
      W.days[slot].minTemp = item["main"]["temp_min"] | 0.0f;
      W.days[slot].maxTemp = item["main"]["temp_max"] | 0.0f;
    } else {
      W.days[slot].minTemp = min(W.days[slot].minTemp,
                                 item["main"]["temp_min"].as<float>());
      W.days[slot].maxTemp = max(W.days[slot].maxTemp,
                                 item["main"]["temp_max"].as<float>());
    }

    struct tm local;
    localTm(stamp, local);
    const int noonDelta = abs(local.tm_hour - 12);
    if (noonDelta < bestNoon[slot]) {
      bestNoon[slot] = noonDelta;
      W.days[slot].id = item["weather"][0]["id"] | 800;
      W.days[slot].stamp = stamp;
    }
  }
  return W.dayCount > 0;
}
}  // namespace

void WeatherMode::fetch(const Settings& s) {
  if (!s.weather.apiKey.length()) {
    W.valid = false;
    W.error = false;
    dirty_ = true;
    return;
  }

  W.errorText[0] = 0;
  W.httpCode = 0;
  const bool currentOk = fetchCurrent(s);
  const bool forecastOk = currentOk && fetchForecast(s);
  W.valid = currentOk;
  W.error = !currentOk;
  if (currentOk) {
    W.updatedMs = millis();
    if (!forecastOk) W.dayCount = 0;
  }
  dirty_ = true;
}

void WeatherMode::begin(const Settings&) {
  nextPoll_ = 0;
  renderedMinute_ = -1;
  dirty_ = true;
}

void WeatherMode::invalidate(const Settings&) {
  nextPoll_ = 0;
  dirty_ = true;
}

void WeatherMode::wake(const Settings&) { dirty_ = true; }

void WeatherMode::render(const Settings& s) {
  gfxRenderTiled(drawScreen, const_cast<Settings*>(&s), skyColor());
}

void WeatherMode::service(const Settings& s) {
  const uint32_t nowMs = millis();
  if (static_cast<int32_t>(nowMs - nextPoll_) >= 0) {
    nextPoll_ = nowMs + static_cast<uint32_t>(s.weather.pollSec) * 1000UL;
    fetch(s);
  }

  struct tm t;
  currentLocalTm(t);
  const int32_t minute = static_cast<int32_t>(t.tm_yday) * 1440L +
                         static_cast<int32_t>(t.tm_hour) * 60L + t.tm_min;
  if (minute != renderedMinute_) {
    renderedMinute_ = minute;
    dirty_ = true;
  }

  if (dirty_) {
    render(s);
    dirty_ = false;
  }
}
