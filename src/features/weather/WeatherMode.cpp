#if defined(DESKMATE_PREVIEW)
#include "PreviewApi.h"
#else
#include "WeatherMode.h"
#include "Platform.h"
#include <ArduinoJson.h>
#endif
#include "Gfx.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include "Clock.h"
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <time.h>

#if !defined(DESKMATE_PREVIEW)
WeatherMode g_weatherMode;
#endif

namespace {
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t blend565(uint16_t base, uint16_t overlay, uint8_t alpha) {
  const uint16_t br = (base >> 11) & 0x1F;
  const uint16_t bg = (base >> 5) & 0x3F;
  const uint16_t bb = base & 0x1F;
  const uint16_t or_ = (overlay >> 11) & 0x1F;
  const uint16_t og = (overlay >> 5) & 0x3F;
  const uint16_t ob = overlay & 0x1F;
  const uint16_t inv = 255U - alpha;
  return static_cast<uint16_t>(
      (((br * inv + or_ * alpha) / 255U) << 11) |
      (((bg * inv + og * alpha) / 255U) << 5) |
      ((bb * inv + ob * alpha) / 255U));
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
#if defined(DESKMATE_PREVIEW)
time_t previewNowUtc = 1785501000;
#endif

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
#if defined(DESKMATE_PREVIEW)
  time_t now = previewNowUtc;
#else
  time_t now = time(nullptr);
#endif
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

void blendRoundedPanel(TileCanvas& g, int x, int y, int width, int height,
                       int radius, uint16_t overlay, uint8_t alpha,
                       uint16_t border) {
  const int right = x + width - 1;
  const int bottom = y + height - 1;
  const int innerLeft = x + radius;
  const int innerRight = right - radius;
  const int innerTop = y + radius;
  const int innerBottom = bottom - radius;
  const int tileLeft = g.tileX();
  const int tileTop = g.tileY();
  const int tileRight = tileLeft + g.tileW() - 1;
  const int tileBottom = tileTop + g.tileH() - 1;
  const int x0 = max(x, tileLeft);
  const int y0 = max(y, tileTop);
  const int x1 = min(right, tileRight);
  const int y1 = min(bottom, tileBottom);

  if (x0 <= x1 && y0 <= y1) {
    uint16_t* pixels = g.pixels();
    const int stride = g.tileW();

    for (int py = y0; py <= y1; ++py) {
      for (int px = x0; px <= x1; ++px) {
        bool inside = px >= innerLeft && px <= innerRight;
        if (!inside) inside = py >= innerTop && py <= innerBottom;
        if (!inside) {
          const int centerX = px < innerLeft ? innerLeft : innerRight;
          const int centerY = py < innerTop ? innerTop : innerBottom;
          const int dx = px - centerX;
          const int dy = py - centerY;
          inside = dx * dx + dy * dy <= radius * radius;
        }
        if (!inside) continue;

        const int localX = px - tileLeft;
        const int localY = py - tileTop;
        const int index = localY * stride + localX;
        pixels[index] = blend565(pixels[index], overlay, alpha);
      }
    }
  }

  g.drawRoundRect(x, y, width, height, radius, border);
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

  const bool dark = night || isRain(W.conditionId) ||
                    isAtmosphere(W.conditionId);
  const uint16_t primary = dark ? WHITE_SOFT : INK_DARK;
  const uint16_t secondary = dark ? rgb565(187, 204, 221)
                                  : rgb565(72, 88, 103);

  struct tm nowTm;
  currentLocalTm(nowTm);

  char timeText[8];
  char meridiem[3];
  clockFormatTime(s, nowTm, timeText, sizeof(timeText), meridiem,
                  sizeof(meridiem));

  static const char* dayNames[] = {
      "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  char dateText[16];
  snprintf(dateText, sizeof(dateText), "%s %02d",
           dayNames[nowTm.tm_wday % 7], constrain(nowTm.tm_mday, 1, 31));

  char cityText[24];
  copyShort(s.weather.city.length() ? s.weather.city.c_str() : W.city,
            cityText, sizeof(cityText), 21);

  g.setTextSize(1);
  g.setTextColor(secondary);
  g.setCursor(DisplayLayout::Left + 2, 8);
  g.print(cityText);
  g.setCursor(DisplayLayout::Right - gfxTextW(dateText, 1) - 2, 8);
  g.print(dateText);

  const uint8_t clockSize = s.clock.use24Hour ? 5 : 4;
  g.setTextColor(primary);
  g.setTextSize(clockSize);
  g.setCursor(8, 24);
  g.print(timeText);

  if (meridiem[0]) {
    g.setTextSize(2);
    g.setCursor(8 + gfxTextW(timeText, clockSize) + 5, 43);
    g.print(meridiem);
  }

  char tempText[10];
  snprintf(tempText, sizeof(tempText), "%.0f", W.temp);
  constexpr int tempX = 20;
  constexpr int tempY = 65;
  constexpr uint8_t tempSize = 3;
  g.setTextSize(tempSize);
  g.setCursor(tempX, tempY);
  g.print(tempText);
  const int degreeX = tempX + gfxTextW(tempText, tempSize) + 5;
  g.drawCircle(degreeX, tempY + 7, 4, primary);

  drawMainIcon(g, W.conditionId, night, 176, 25, sky);

  const char* condition = conditionLabel(W.conditionId);
  g.setTextSize(2);
  g.setTextColor(primary);
  g.setCursor(226 - gfxTextW(condition, 2), 81);
  g.print(condition);

  const char unit = s.weather.metric ? 'C' : 'F';

  constexpr int detailX = 8;
  constexpr int detailY = 103;
  constexpr int detailW = 224;
  constexpr int detailH = 34;
  constexpr int detailRadius = 7;
  static_assert(DisplayLayout::fitsSafe(detailX, detailY, detailW, detailH),
                "Weather telemetry panel must fit the safe display area");

  const uint16_t detailOverlay = dark ? rgb565(3, 12, 22)
                                      : rgb565(247, 250, 250);
  const uint16_t detailBorder = dark ? rgb565(83, 107, 130)
                                     : rgb565(179, 197, 202);
  const uint16_t detailText = dark ? WHITE_SOFT : rgb565(34, 52, 65);
  const uint8_t detailAlpha = dark ? 112 : 128;
  blendRoundedPanel(g, detailX, detailY, detailW, detailH, detailRadius,
                    detailOverlay, detailAlpha, detailBorder);

  char detail[38];
  g.setTextSize(1);
  g.setTextColor(detailText);
  snprintf(detail, sizeof(detail), "FEELS %.0f%c  HUM %d%%", W.feels,
           unit, W.humidity);
  g.setCursor(14, 109);
  g.print(detail);

  if (s.weather.metric) {
    snprintf(detail, sizeof(detail), "WIND %.0f km/h  %d hPa", W.wind,
             W.pressure);
  } else {
    snprintf(detail, sizeof(detail), "WIND %.0f mph  %d hPa", W.wind,
             W.pressure);
  }
  g.setCursor(14, 122);
  g.print(detail);

  constexpr int panelX = DisplayLayout::Left;
  constexpr int panelY = 145;
  constexpr int panelW = DisplayLayout::Width;
  constexpr int panelH = 87;
  constexpr int panelRadius = 13;
  static_assert(DisplayLayout::fitsSafe(panelX, panelY, panelW, panelH),
                "Weather forecast panel must fit the safe display area");

  const uint16_t panelOverlay = dark ? rgb565(3, 12, 22)
                                     : rgb565(247, 250, 250);
  const uint16_t panelBorder = dark ? rgb565(67, 88, 110)
                                    : rgb565(179, 197, 202);
  const uint16_t panelText = dark ? WHITE_SOFT : INK_DARK;
  const uint16_t panelMuted = dark ? rgb565(166, 184, 205)
                                   : rgb565(61, 78, 90);
  const uint16_t separator = dark ? rgb565(65, 83, 105)
                                  : rgb565(181, 198, 201);
  const uint8_t panelAlpha = dark ? 138 : 158;
  blendRoundedPanel(g, panelX, panelY, panelW, panelH, panelRadius,
                    panelOverlay, panelAlpha, panelBorder);

  g.setTextSize(1);
  g.setTextColor(panelMuted);
  g.setCursor(22, 153);
  g.print("NEXT 12 HOURS");

  const uint16_t forecastIconBg = blend565(sky, panelOverlay, panelAlpha);

  for (uint8_t i = 0; i < 4; ++i) {
    const int left = 9 + i * 56;
    if (i) g.drawFastVLine(left - 3, 166, 56, separator);
    if (i >= W.forecastCount || !W.forecast[i].valid) continue;

    struct tm ft;
    localTm(W.forecast[i].stamp, ft);

    char forecastClock[8];
    char forecastMeridiem[3];
    clockFormatTime(s, ft, forecastClock, sizeof(forecastClock),
                    forecastMeridiem, sizeof(forecastMeridiem));

    char forecastTime[12];
    if (!forecastMeridiem[0]) {
      strlcpy(forecastTime, forecastClock, sizeof(forecastTime));
    } else if (ft.tm_min == 0) {
      const int hour12 = ft.tm_hour % 12 ? ft.tm_hour % 12 : 12;
      snprintf(forecastTime, sizeof(forecastTime), "%d %s", hour12,
               forecastMeridiem);
    } else {
      snprintf(forecastTime, sizeof(forecastTime), "%s%c", forecastClock,
               forecastMeridiem[0]);
    }

    g.setTextSize(1);
    g.setTextColor(panelMuted);
    g.setCursor(left + (52 - gfxTextW(forecastTime, 1)) / 2, 167);
    g.print(forecastTime);

    drawMiniIcon(g, W.forecast[i].id, W.forecast[i].night, left + 12, 181,
                 forecastIconBg);

    char forecastTemp[12];
    snprintf(forecastTemp, sizeof(forecastTemp), "%.0f%c",
             W.forecast[i].temp, unit);
    g.setTextSize(2);
    g.setTextColor(panelText);
    g.setCursor(left + (52 - gfxTextW(forecastTemp, 2)) / 2, 211);
    g.print(forecastTemp);
  }
}

#if !defined(DESKMATE_PREVIEW)
bool beginGet(const Settings& s, const String& url,
              std::unique_ptr<SecureClient>& client, HTTPClient& http,
              uint16_t budgetMs) {
  client.reset(platformMakeSecureClient(4096, nullptr, 512, false));
  if (!client) return false;
  http.setTimeout(min<uint16_t>(budgetMs, s.httpTimeout));
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  http.setUserAgent(FW_NAME);
  return true;
}

bool fetchCurrent(const Settings& s, uint16_t budgetMs) {
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
  if (!beginGet(s, url, client, http, budgetMs)) {
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

bool fetchForecast(const Settings& s, uint16_t budgetMs) {
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
  if (!beginGet(s, url, client, http, budgetMs)) return false;
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
#endif
}  // namespace

#if defined(DESKMATE_PREVIEW)
void previewRenderWeather(const Settings& settings,
                          const PreviewWeatherState& state) {
  W = WeatherData();
  W.valid = state.valid;
  W.error = state.error;
  W.httpCode = state.httpCode;
  strlcpy(W.errorText, state.errorText ? state.errorText : "",
          sizeof(W.errorText));
  strlcpy(W.city, state.city ? state.city : "", sizeof(W.city));
  strlcpy(W.icon, state.icon ? state.icon : "01d", sizeof(W.icon));
  W.temp = state.temp;
  W.feels = state.feels;
  W.wind = state.wind;
  W.humidity = state.humidity;
  W.pressure = state.pressure;
  W.conditionId = state.conditionId;
  W.timezone = state.timezone;
  W.forecastCount = min<uint8_t>(state.forecastCount, 4);
  for (uint8_t i = 0; i < W.forecastCount; ++i) {
    W.forecast[i].valid = state.forecast[i].valid;
    W.forecast[i].id = state.forecast[i].id;
    W.forecast[i].night = state.forecast[i].night;
    W.forecast[i].temp = state.forecast[i].temp;
    W.forecast[i].stamp = state.forecast[i].stamp;
  }
  previewNowUtc = state.nowUtc;
  gfxRenderTiled(drawScreen, const_cast<Settings*>(&settings), skyColor());
}
#else

uint32_t WeatherMode::pollIntervalMs(const Settings& settings) const {
  return static_cast<uint32_t>(settings.weather.pollSec) * 1000UL;
}

uint16_t WeatherMode::pollBudgetMs(const Settings& settings) const {
  return min<uint16_t>(settings.httpTimeout, 5000);
}

PollResult WeatherMode::poll(const Settings& settings, uint16_t budgetMs) {
  if (!settings.weather.locationVerified || !settings.weather.apiKey.length()) {
    W.valid = false;
    W.error = false;
    pollStage_ = 0;
    dirty_ = true;
    return PollResult::Skipped;
  }

  W.errorText[0] = 0;
  W.httpCode = 0;
  if (pollStage_ == 0) {
    const bool hadValidSnapshot = W.valid;
    const bool ok = fetchCurrent(settings, budgetMs);
    W.error = !ok;
    dirty_ = true;
    if (!ok) {
      // Preserve the last complete dashboard through transient provider/TLS
      // failures. Only a never-configured device falls back to the error card.
      W.valid = hadValidSnapshot;
      return PollResult::Failed;
    }

    W.valid = true;
    W.updatedMs = millis();
    clockUpdateUtcOffset(settings, W.timezone);
    pollStage_ = 1;
    return PollResult::MoreWork;
  }

  const bool forecastOk = fetchForecast(settings, budgetMs);
  pollStage_ = 0;
  // fetchForecast commits only after a complete parse, so a failed refresh
  // naturally leaves the previous forecast cached and visible.
  dirty_ = true;
  return forecastOk ? PollResult::Success : PollResult::Failed;
}

void WeatherMode::begin(const Settings& settings) {
  pollStage_ = 0;
  W.timezone = settings.weather.utcOffsetSec;
  renderedMinute_ = -1;
  dirty_ = true;
}

void WeatherMode::invalidate(const Settings& settings) {
  pollStage_ = 0;
  W.timezone = settings.weather.utcOffsetSec;
  dirty_ = true;
}

void WeatherMode::wake(const Settings&) { dirty_ = true; }

void WeatherMode::render(const Settings& settings) {
  gfxRenderTiled(drawScreen, const_cast<Settings*>(&settings), skyColor());
}

void WeatherMode::displayTick(const Settings& settings) {
  struct tm current;
  currentLocalTm(current);
  const int32_t minute = static_cast<int32_t>(current.tm_yday) * 1440L +
                         static_cast<int32_t>(current.tm_hour) * 60L + current.tm_min;
  if (minute != renderedMinute_) {
    renderedMinute_ = minute;
    dirty_ = true;
  }
  if (dirty_) {
    render(settings);
    dirty_ = false;
  }
}

#endif  // DESKMATE_PREVIEW
