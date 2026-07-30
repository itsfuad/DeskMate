#include "WeatherMode.h"
#include "Platform.h"
#include "Gfx.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <time.h>

WeatherMode g_weatherMode;

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t SKY_CLEAR = rgb565(242, 174, 67);
constexpr uint16_t SKY_CLOUD = rgb565(102, 163, 211);
constexpr uint16_t SKY_RAIN  = rgb565(56, 75, 106);
constexpr uint16_t SKY_SNOW  = rgb565(165, 193, 217);
constexpr uint16_t SKY_NIGHT = rgb565(24, 42, 78);
constexpr uint16_t LAND_WARM = rgb565(225, 126, 80);
constexpr uint16_t LAND_COOL = rgb565(65, 91, 122);
constexpr uint16_t PANEL_DAY = rgb565(247, 248, 245);
constexpr uint16_t PANEL_DARK = rgb565(18, 29, 48);
constexpr uint16_t INK_DARK = rgb565(28, 39, 53);
constexpr uint16_t INK_MUTED = rgb565(108, 125, 143);
constexpr uint16_t WHITE_SOFT = rgb565(246, 248, 251);
constexpr uint16_t SUN = rgb565(255, 208, 54);
constexpr uint16_t CLOUD = rgb565(224, 233, 242);
constexpr uint16_t RAIN = rgb565(64, 188, 236);
constexpr uint16_t SNOW = rgb565(235, 246, 252);
constexpr uint16_t ERROR_C = rgb565(255, 112, 112);

struct ForecastPoint {
  bool valid = false;
  int id = 800;
  bool night = false;
  float temp = 0;
  uint32_t stamp = 0;
};

struct WeatherData {
  bool valid = false;
  bool error = false;
  int httpCode = 0;
  char errorText[32] = "";
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
  ForecastPoint forecast[4];
  uint8_t forecastCount = 0;
  uint32_t updatedMs = 0;
};

WeatherData W;

bool conditionIsNight() { return W.icon[2] == 'n'; }
bool isRain(int id) { return id >= 200 && id < 600; }
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
  if (now < 1609459200) {
    now = static_cast<time_t>(W.sunrise ? W.sunrise : 1700000000UL);
  }
  const time_t shifted = now + W.timezone;
  gmtime_r(&shifted, &out);
}

void copyShort(const char* source, char* target, size_t targetSize,
               size_t maxChars) {
  if (!targetSize) return;
  size_t i = 0;
  while (source && source[i] && i < maxChars && i + 1 < targetSize) {
    target[i] = source[i];
    ++i;
  }
  target[i] = 0;
}

void drawSun(TileCanvas& g, int x, int y, int r, uint16_t color) {
  g.fillCircle(x, y, r, color);
  for (int a = 0; a < 360; a += 45) {
    const float q = a * static_cast<float>(PI) / 180.0f;
    g.drawLine(x + static_cast<int>(cosf(q) * (r + 4)),
               y + static_cast<int>(sinf(q) * (r + 4)),
               x + static_cast<int>(cosf(q) * (r + 9)),
               y + static_cast<int>(sinf(q) * (r + 9)), color);
  }
}

void drawMoon(TileCanvas& g, int x, int y, int r, uint16_t background) {
  g.fillCircle(x, y, r, WHITE_SOFT);
  g.fillCircle(x + r / 2, y - r / 3, r, background);
}

void drawCloud(TileCanvas& g, int x, int y, uint16_t color) {
  g.fillCircle(x + 8, y + 8, 7, color);
  g.fillCircle(x + 19, y + 3, 10, color);
  g.fillCircle(x + 33, y + 9, 8, color);
  g.fillRoundRect(x, y + 8, 42, 13, 6, color);
}

void drawMainIcon(TileCanvas& g, int id, bool night, int x, int y,
                  uint16_t background) {
  if (id == 800) {
    if (night) drawMoon(g, x + 20, y + 20, 17, background);
    else drawSun(g, x + 20, y + 20, 16, SUN);
    return;
  }
  if (isCloud(id) && !night) drawSun(g, x + 11, y + 8, 9, SUN);
  drawCloud(g, x + 1, y + 11, CLOUD);
  if (id >= 200 && id < 300) {
    g.fillTriangle(x + 22, y + 34, x + 16, y + 47, x + 23, y + 44, SUN);
    g.fillTriangle(x + 23, y + 43, x + 20, y + 53, x + 31, y + 38, SUN);
  } else if (isRain(id)) {
    for (int i = 0; i < 3; ++i) {
      const int rx = x + 10 + i * 13;
      g.drawLine(rx, y + 37, rx - 3, y + 46, RAIN);
    }
  } else if (isSnow(id)) {
    for (int i = 0; i < 3; ++i) {
      const int sx = x + 10 + i * 13;
      const int sy = y + 41 + (i & 1) * 3;
      g.drawFastHLine(sx - 3, sy, 7, SNOW);
      g.drawFastVLine(sx, sy - 3, 7, SNOW);
    }
  } else if (isAtmosphere(id)) {
    for (int i = 0; i < 3; ++i) {
      g.drawFastHLine(x + 3 + i * 4, y + 38 + i * 5, 38 - i * 8,
                      WHITE_SOFT);
    }
  }
}

void drawMiniIcon(TileCanvas& g, int id, bool night, int x, int y,
                  uint16_t panel) {
  if (id == 800) {
    if (night) drawMoon(g, x + 11, y + 10, 7, panel);
    else {
      g.fillCircle(x + 11, y + 10, 6, SUN);
      g.drawFastHLine(x + 2, y + 10, 19, SUN);
      g.drawFastVLine(x + 11, y + 1, 19, SUN);
    }
    return;
  }
  if (isCloud(id) && !night) g.fillCircle(x + 6, y + 5, 5, SUN);
  g.fillCircle(x + 7, y + 10, 5, CLOUD);
  g.fillCircle(x + 14, y + 7, 7, CLOUD);
  g.fillCircle(x + 21, y + 11, 5, CLOUD);
  g.fillRoundRect(x + 3, y + 10, 22, 8, 4, CLOUD);
  if (isRain(id)) {
    g.drawLine(x + 8, y + 20, x + 6, y + 25, RAIN);
    g.drawLine(x + 16, y + 20, x + 14, y + 25, RAIN);
    g.drawLine(x + 23, y + 20, x + 21, y + 25, RAIN);
  } else if (isSnow(id)) {
    g.drawPixel(x + 8, y + 22, SNOW);
    g.drawPixel(x + 16, y + 24, SNOW);
    g.drawPixel(x + 23, y + 22, SNOW);
  }
}

void drawBackdrop(TileCanvas& g, uint16_t sky) {
  const bool dark = conditionIsNight() || isRain(W.conditionId);
  if (conditionIsNight()) {
    for (int i = 0; i < 10; ++i) {
      const int x = (i * 43 + 17) % TFT_WIDTH;
      const int y = 12 + (i * 29) % 103;
      g.drawPixel(x, y, i % 3 ? rgb565(130, 154, 188) : WHITE_SOFT);
    }
  }

  // Broad, static shapes create the layered card-like depth from the visual
  // reference without animation or expensive per-pixel gradients.
  const uint16_t far = dark ? rgb565(37, 57, 84)
                       : (isCloud(W.conditionId) ? rgb565(83, 147, 197)
                                                : rgb565(235, 151, 70));
  const uint16_t near = dark ? LAND_COOL : LAND_WARM;
  g.fillCircle(212, 18, 70, far);
  g.fillCircle(229, 25, 42, sky);
  g.fillTriangle(0, 132, 87, 94, 171, 132, far);
  g.fillTriangle(65, 132, 174, 84, 240, 132, far);
  g.fillTriangle(0, 144, 72, 110, 147, 144, near);
  g.fillTriangle(91, 144, 190, 101, 240, 144, near);
  g.fillRect(0, 128, 240, 20, near);
}

void drawScreen(TileCanvas& g, void* opaque) {
  const Settings& s = *static_cast<const Settings*>(opaque);
  const uint16_t sky = skyColor();
  const bool night = conditionIsNight();
  g.fillScreen(sky);
  g.setTextWrap(false);

  if (!W.valid) {
    g.fillRoundRect(12, 16, 216, 208, 16, PANEL_DARK);
    g.setTextColor(WHITE_SOFT);
    g.setTextSize(2);
    g.setCursor(26, 40);
    g.print("DESKMATE WEATHER");
    g.setTextSize(1);
    g.setTextColor(rgb565(155, 170, 187));
    g.setCursor(26, 82);
    if (!s.weather.apiKey.length()) {
      g.print("OPENWEATHER KEY REQUIRED");
      g.setCursor(26, 100);
      g.print("Add it in the web UI.");
    } else if (W.error) {
      g.setTextColor(ERROR_C);
      g.print(W.errorText[0] ? W.errorText : "WEATHER API ERROR");
      if (W.httpCode) {
        g.setCursor(26, 100);
        g.print("HTTP ");
        g.print(W.httpCode);
      }
    } else {
      g.print("LOADING WEATHER...");
    }
    g.setTextColor(rgb565(115, 136, 158));
    g.setCursor(26, 180);
    g.print("Current weather + four");
    g.setCursor(26, 196);
    g.print("3-hour forecast points.");
    return;
  }

  drawBackdrop(g, sky);

  const bool dark = night || isRain(W.conditionId) || isAtmosphere(W.conditionId);
  const uint16_t primary = dark ? WHITE_SOFT : INK_DARK;
  const uint16_t secondary = dark ? rgb565(187, 204, 221)
                                  : rgb565(72, 88, 103);

  struct tm nowTm;
  currentLocalTm(nowTm);
  char timeText[8];
  snprintf(timeText, sizeof(timeText), "%02d:%02d",
           constrain(nowTm.tm_hour, 0, 23), constrain(nowTm.tm_min, 0, 59));
  static const char* dayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  char dateText[16];
  snprintf(dateText, sizeof(dateText), "%s %02d", dayNames[nowTm.tm_wday % 7],
           constrain(nowTm.tm_mday, 1, 31));

  char cityText[24];
  copyShort(s.weather.city.length() ? s.weather.city.c_str() : W.city,
            cityText, sizeof(cityText), 21);
  g.setTextSize(1);
  g.setTextColor(secondary);
  g.setCursor(DisplayLayout::Left + 2, 8);
  g.print(cityText);
  g.setCursor(DisplayLayout::Right - gfxTextW(dateText, 1) - 2, 8);
  g.print(dateText);

  // Time and temperature share visual priority. Both remain readable from
  // across a desk while the icon/condition occupy the right-hand column.
  g.setTextColor(primary);
  g.setTextSize(4);
  g.setCursor(8, 24);
  g.print(timeText);

  char tempText[10];
  snprintf(tempText, sizeof(tempText), "%.0f", W.temp);
  g.setTextSize(5);
  g.setCursor(8, 65);
  g.print(tempText);
  const int degreeX = 8 + gfxTextW(tempText, 5) + 6;
  g.drawCircle(degreeX, 72, 4, primary);

  drawMainIcon(g, W.conditionId, night, 176, 25, sky);
  g.setTextSize(2);
  g.setTextColor(primary);
  const char* condition = conditionLabel(W.conditionId);
  g.setCursor(226 - gfxTextW(condition, 2), 81);
  g.print(condition);

  char detail[38];
  const char unit = s.weather.metric ? 'C' : 'F';
  g.setTextSize(1);
  g.setTextColor(secondary);
  snprintf(detail, sizeof(detail), "FEELS %.0f%c  HUM %d%%", W.feels, unit,
           W.humidity);
  g.setCursor(10, 111);
  g.print(detail);
  if (s.weather.metric) {
    snprintf(detail, sizeof(detail), "WIND %.0f km/h  %d hPa", W.wind, W.pressure);
  } else {
    snprintf(detail, sizeof(detail), "WIND %.0f mph  %d hPa", W.wind, W.pressure);
  }
  g.setCursor(10, 124);
  g.print(detail);

  const uint16_t panel = dark ? PANEL_DARK : PANEL_DAY;
  const uint16_t panelText = dark ? WHITE_SOFT : INK_DARK;
  const uint16_t panelMuted = dark ? rgb565(137, 158, 183) : INK_MUTED;
  constexpr int panelX = 6;
  constexpr int panelY = 145;
  constexpr int panelW = 228;
  constexpr int panelH = 87;  // ends exactly at the 232 px safe bottom
  static_assert(DisplayLayout::fitsSafe(8, panelY, 224, panelH),
                "Weather forecast panel must fit the safe display area");
  g.fillRoundRect(panelX, panelY, panelW, panelH, 13, panel);

  g.setTextSize(1);
  g.setTextColor(panelMuted);
  g.setCursor(12, 153);
  g.print("NEXT 12 HOURS");

  for (uint8_t i = 0; i < 4; ++i) {
    const int left = 9 + i * 56;
    if (i) {
      g.drawFastVLine(left - 3, 166, 56,
                      dark ? rgb565(43, 60, 81) : rgb565(224, 229, 226));
    }
    if (i >= W.forecastCount || !W.forecast[i].valid) continue;

    struct tm ft;
    localTm(W.forecast[i].stamp, ft);
    char forecastTime[8];
    snprintf(forecastTime, sizeof(forecastTime), "%02d:%02d",
             constrain(ft.tm_hour, 0, 23), constrain(ft.tm_min, 0, 59));
    g.setTextColor(panelMuted);
    g.setCursor(left + (52 - gfxTextW(forecastTime, 1)) / 2, 167);
    g.print(forecastTime);
    drawMiniIcon(g, W.forecast[i].id, W.forecast[i].night,
                 left + 12, 181, panel);

    char forecastTemp[12];
    snprintf(forecastTemp, sizeof(forecastTemp), "%.0f%c", W.forecast[i].temp,
             unit);
    g.setTextSize(2);
    g.setTextColor(panelText);
    g.setCursor(left + (52 - gfxTextW(forecastTemp, 2)) / 2, 211);
    g.print(forecastTemp);
    g.setTextSize(1);
  }
}

bool beginGet(const Settings& s, const String& url,
              std::unique_ptr<SecureClient>& client, HTTPClient& http) {
  client.reset(platformMakeSecureClient(4096, nullptr, 512, false));
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
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
  strlcpy(W.description, doc["weather"][0]["description"] | "",
          sizeof(W.description));
  strlcpy(W.icon, doc["weather"][0]["icon"] | "01d", sizeof(W.icon));
  return true;
}

bool fetchForecast(const Settings& s) {
  String url = F("https://api.openweathermap.org/data/2.5/forecast?lat=");
  url += String(s.weather.lat, 5);
  url += F("&lon=");
  url += String(s.weather.lon, 5);
  url += F("&cnt=8&units=");
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
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["weather"][0]["id"] = true;
  filter["list"][0]["weather"][0]["icon"] = true;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;

  if (doc["city"]["timezone"].is<int>()) W.timezone = doc["city"]["timezone"];
  for (ForecastPoint& point : W.forecast) point = ForecastPoint();
  W.forecastCount = 0;

  time_t now = time(nullptr);
  if (now < 1609459200) now = 1700000000;
  for (JsonObjectConst item : doc["list"].as<JsonArrayConst>()) {
    if (W.forecastCount >= 4) break;
    const uint32_t stamp = item["dt"] | 0UL;
    if (stamp <= static_cast<uint32_t>(now + 900)) continue;
    ForecastPoint& point = W.forecast[W.forecastCount++];
    point.valid = true;
    point.stamp = stamp;
    point.temp = item["main"]["temp"] | 0.0f;
    point.id = item["weather"][0]["id"] | 800;
    const char* icon = item["weather"][0]["icon"] | "01d";
    point.night = icon[2] == 'n';
  }
  return W.forecastCount > 0;
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
    if (!forecastOk) W.forecastCount = 0;
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
