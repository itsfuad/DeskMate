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

constexpr uint16_t PANEL_DARK = rgb565(18, 29, 48);
constexpr uint16_t GLASS_DARK = rgb565(3, 12, 22);
constexpr uint16_t WHITE_SOFT = rgb565(246, 248, 251);
constexpr uint16_t SUN = rgb565(255, 208, 54);
constexpr uint16_t CLOUD = rgb565(224, 233, 242);
constexpr uint16_t RAIN = rgb565(64, 188, 236);
constexpr uint16_t SNOW = rgb565(235, 246, 252);
constexpr uint16_t ERROR_C = rgb565(255, 112, 112);

struct TimePalette {
  uint16_t skyTop;
  uint16_t skyHorizon;
  uint16_t far;
  uint16_t near;
  uint16_t water;
  uint16_t secondary;
  uint8_t panelAlpha;
  uint8_t nightAmount;
  uint8_t warmAmount;
};

struct WeatherTheme {
  uint16_t skyTop;
  uint16_t skyHorizon;
  uint16_t far;
  uint16_t near;
  uint16_t water;
  uint16_t primary;
  uint16_t secondary;
  uint16_t panelOverlay;
  uint16_t panelBorder;
  uint16_t panelText;
  uint16_t panelMuted;
  uint16_t separator;
  uint16_t cloudLight;
  uint16_t cloudShade;
  uint16_t rainColor;
  uint16_t lightColor;
  uint8_t panelAlpha;
  uint8_t nightAmount;
  uint8_t warmAmount;
  uint8_t cloudLevel;
  uint8_t precipitation;
};

constexpr uint16_t INK_DARK = rgb565(13, 34, 58);
constexpr uint16_t WINDOW_LIGHT = rgb565(255, 198, 102);

// Extra dawn and dusk keys are deliberately present between the five named
// day phases. They are transition colours, not abrupt themes.
constexpr TimePalette TIME_NIGHT = {
    rgb565(6, 20, 42), rgb565(24, 51, 78), rgb565(24, 48, 70),
    rgb565(9, 29, 52), rgb565(12, 45, 70), rgb565(184, 207, 232),
    82, 255, 0};
constexpr TimePalette TIME_DAWN = {
    rgb565(55, 76, 112), rgb565(226, 145, 121), rgb565(95, 91, 112),
    rgb565(48, 55, 78), rgb565(62, 91, 118), rgb565(255, 216, 190),
    76, 118, 255};
constexpr TimePalette TIME_MORNING = {
    rgb565(120, 183, 221), rgb565(207, 235, 246), rgb565(112, 163, 188),
    rgb565(69, 122, 149), rgb565(74, 148, 181), rgb565(23, 57, 91),
    66, 0, 35};
constexpr TimePalette TIME_NOON = {
    rgb565(47, 148, 211), rgb565(139, 207, 236), rgb565(83, 157, 194),
    rgb565(43, 111, 151), rgb565(40, 135, 177), rgb565(12, 43, 73),
    68, 0, 0};
constexpr TimePalette TIME_AFTERNOON = {
    rgb565(88, 157, 198), rgb565(190, 208, 202), rgb565(103, 151, 167),
    rgb565(66, 113, 132), rgb565(59, 128, 157), rgb565(17, 46, 72),
    68, 0, 72};
constexpr TimePalette TIME_DUSK = {
    rgb565(57, 70, 109), rgb565(220, 119, 102), rgb565(103, 76, 98),
    rgb565(45, 46, 73), rgb565(57, 72, 100), rgb565(255, 190, 154),
    78, 92, 255};
constexpr TimePalette TIME_EVENING = {
    rgb565(27, 45, 78), rgb565(94, 65, 91), rgb565(58, 55, 80),
    rgb565(23, 35, 59), rgb565(29, 56, 82), rgb565(232, 183, 172),
    82, 198, 150};

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

bool isRain(int id) { return id >= 200 && id < 600; }
bool isSnow(int id) { return id >= 600 && id < 700; }
bool isAtmosphere(int id) { return id >= 700 && id < 800; }
bool isCloud(int id) { return id >= 801 && id <= 804; }
bool isPartlyCloudy(int id) { return id == 801 || id == 802; }

struct ConditionLabel {
  const char* first;
  const char* second;
};

ConditionLabel conditionLabel(int id) {
  if (id >= 200 && id < 300) return {"THUNDER", nullptr};
  if (id >= 300 && id < 400) return {"DRIZZLE", nullptr};
  if (id >= 500 && id < 600) return {"RAIN", nullptr};
  if (id >= 600 && id < 700) return {"SNOW", nullptr};
  if (id >= 700 && id < 800) return {"MIST", nullptr};
  if (id == 800) return {"CLEAR", nullptr};
  if (isPartlyCloudy(id)) return {"PARTLY", "CLOUDY"};
  return {"CLOUDY", nullptr};
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

uint8_t lerpByte(uint8_t from, uint8_t to, uint8_t amount) {
  const uint16_t inverse = 255U - amount;
  return static_cast<uint8_t>((from * inverse + to * amount + 127U) / 255U);
}

uint8_t smoothAmount(int value, int start, int end) {
  if (value <= start) return 0;
  if (value >= end || end <= start) return 255;
  const uint32_t x = static_cast<uint32_t>(value - start) * 255U /
                     static_cast<uint32_t>(end - start);
  const uint32_t smooth = x * x * (765U - 2U * x) / 65025U;
  return static_cast<uint8_t>(smooth);
}

TimePalette blendPalette(const TimePalette& from, const TimePalette& to,
                         uint8_t amount) {
  TimePalette result;
  result.skyTop = blend565(from.skyTop, to.skyTop, amount);
  result.skyHorizon = blend565(from.skyHorizon, to.skyHorizon, amount);
  result.far = blend565(from.far, to.far, amount);
  result.near = blend565(from.near, to.near, amount);
  result.water = blend565(from.water, to.water, amount);
  result.secondary = blend565(from.secondary, to.secondary, amount);
  result.panelAlpha = lerpByte(from.panelAlpha, to.panelAlpha, amount);
  result.nightAmount = lerpByte(from.nightAmount, to.nightAmount, amount);
  result.warmAmount = lerpByte(from.warmAmount, to.warmAmount, amount);
  return result;
}

int localMinuteForUtc(uint32_t utc) {
  struct tm value;
  localTm(utc, value);
  return value.tm_hour * 60 + value.tm_min;
}

void solarMinutes(int& sunriseMinute, int& sunsetMinute) {
  sunriseMinute = 6 * 60;
  sunsetMinute = 18 * 60;
  if (W.sunrise > 1609459200UL && W.sunset > W.sunrise) {
    sunriseMinute = localMinuteForUtc(W.sunrise);
    sunsetMinute = localMinuteForUtc(W.sunset);
  }

  // Keep malformed provider data from collapsing the visual cycle.
  sunriseMinute = constrain(sunriseMinute, 4 * 60, 9 * 60);
  sunsetMinute = constrain(sunsetMinute, 16 * 60, 21 * 60);
  if (sunsetMinute - sunriseMinute < 7 * 60) {
    sunriseMinute = 6 * 60;
    sunsetMinute = 18 * 60;
  }
}

TimePalette timePaletteForMinute(int minute) {
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);

  const int dawnStart = max(0, sunriseMinute - 90);
  const int morningEnd = min(sunriseMinute + 100, sunsetMinute - 390);
  const int solarNoon = (sunriseMinute + sunsetMinute) / 2;
  const int duskStart = max(solarNoon + 120, sunsetMinute - 100);
  const int eveningEnd = min(1439, sunsetMinute + 80);
  const int nightStart = min(1439, sunsetMinute + 155);

  if (minute < dawnStart) return TIME_NIGHT;
  if (minute < sunriseMinute) {
    return blendPalette(TIME_NIGHT, TIME_DAWN,
                        smoothAmount(minute, dawnStart, sunriseMinute));
  }
  if (minute < morningEnd) {
    return blendPalette(TIME_DAWN, TIME_MORNING,
                        smoothAmount(minute, sunriseMinute, morningEnd));
  }
  if (minute < solarNoon) {
    return blendPalette(TIME_MORNING, TIME_NOON,
                        smoothAmount(minute, morningEnd, solarNoon));
  }
  if (minute < duskStart) {
    return blendPalette(TIME_NOON, TIME_AFTERNOON,
                        smoothAmount(minute, solarNoon, duskStart));
  }
  if (minute < sunsetMinute) {
    return blendPalette(TIME_AFTERNOON, TIME_DUSK,
                        smoothAmount(minute, duskStart, sunsetMinute));
  }
  if (minute < eveningEnd) {
    return blendPalette(TIME_DUSK, TIME_EVENING,
                        smoothAmount(minute, sunsetMinute, eveningEnd));
  }
  if (minute < nightStart) {
    return blendPalette(TIME_EVENING, TIME_NIGHT,
                        smoothAmount(minute, eveningEnd, nightStart));
  }
  return TIME_NIGHT;
}

WeatherTheme weatherThemeForMinute(int minute) {
  const TimePalette palette = timePaletteForMinute(minute);

  uint16_t conditionTint = palette.skyTop;
  uint8_t topTint = 0;
  uint8_t horizonTint = 0;
  uint8_t terrainTint = 0;
  uint8_t extraPanelAlpha = 0;
  uint8_t cloudLevel = 1;
  uint8_t precipitation = 0;

  if (isPartlyCloudy(W.conditionId)) {
    conditionTint = rgb565(117, 143, 165);
    topTint = 14;
    horizonTint = 8;
    terrainTint = 18;
    cloudLevel = 2;
  } else if (isCloud(W.conditionId)) {
    conditionTint = rgb565(99, 122, 145);
    topTint = 35;
    horizonTint = 22;
    terrainTint = 42;
    extraPanelAlpha = 5;
    cloudLevel = 4;
  } else if (isRain(W.conditionId)) {
    conditionTint = rgb565(42, 58, 79);
    topTint = 64;
    horizonTint = 45;
    terrainTint = 72;
    extraPanelAlpha = 13;
    cloudLevel = 5;
    precipitation = 2;
  } else if (isAtmosphere(W.conditionId)) {
    conditionTint = rgb565(103, 116, 126);
    topTint = 48;
    horizonTint = 35;
    terrainTint = 55;
    extraPanelAlpha = 8;
    cloudLevel = 3;
  } else if (isSnow(W.conditionId)) {
    conditionTint = rgb565(181, 204, 220);
    topTint = 34;
    horizonTint = 25;
    terrainTint = 43;
    extraPanelAlpha = 5;
    cloudLevel = 4;
    precipitation = 1;
  }

  WeatherTheme theme;
  theme.skyTop = blend565(palette.skyTop, conditionTint, topTint);
  theme.skyHorizon = blend565(palette.skyHorizon, conditionTint, horizonTint);
  theme.far = blend565(palette.far, conditionTint, terrainTint);
  theme.near = blend565(palette.near, conditionTint, terrainTint);
  theme.water = blend565(palette.water, conditionTint, terrainTint / 2);

  // Text/glass contrast follows sunrise and sunset directly. This avoids the
  // low-contrast grey phase that occurs when a colour palette is halfway
  // between dawn and morning.
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);
  uint8_t textNight = 255;
  if (minute >= sunriseMinute - 10 && minute < sunriseMinute + 20) {
    textNight = static_cast<uint8_t>(
        255 - smoothAmount(minute, sunriseMinute - 10, sunriseMinute + 20));
  } else if (minute >= sunriseMinute + 20 && minute < sunsetMinute - 50) {
    textNight = 0;
  } else if (minute >= sunsetMinute - 50 && minute < sunsetMinute + 10) {
    textNight = smoothAmount(minute, sunsetMinute - 50, sunsetMinute + 10);
  }

  theme.primary = blend565(INK_DARK, WHITE_SOFT, textNight);
  theme.secondary = blend565(theme.skyTop, theme.primary, 205);
  theme.panelOverlay = blend565(WHITE_SOFT, GLASS_DARK, textNight);
  theme.panelAlpha = static_cast<uint8_t>(
      min<int>(132, palette.panelAlpha + extraPanelAlpha + 20));
  theme.panelBorder = blend565(theme.skyHorizon, theme.primary,
                               textNight > 140 ? 98 : 72);
  theme.panelText = theme.primary;
  theme.panelMuted = blend565(theme.panelOverlay, theme.primary,
                              textNight > 140 ? 188 : 205);
  theme.separator = blend565(theme.panelOverlay, theme.primary,
                             textNight > 140 ? 76 : 60);

  const uint16_t dayCloud = rgb565(235, 241, 246);
  const uint16_t nightCloud = rgb565(129, 143, 165);
  const uint16_t rainCloud = rgb565(87, 98, 120);
  theme.cloudLight = blend565(dayCloud, nightCloud, palette.nightAmount);
  if (isRain(W.conditionId)) {
    theme.cloudLight = blend565(theme.cloudLight, rainCloud, 150);
  }
  theme.cloudShade = blend565(theme.cloudLight, theme.near,
                              isRain(W.conditionId) ? 115 : 58);
  theme.rainColor = blend565(rgb565(65, 190, 236), rgb565(103, 151, 210),
                            palette.nightAmount / 2);
  theme.lightColor = blend565(theme.near, WINDOW_LIGHT,
                              static_cast<uint8_t>(palette.nightAmount * 4 / 5));
  theme.nightAmount = palette.nightAmount;
  theme.warmAmount = palette.warmAmount;
  theme.cloudLevel = cloudLevel;
  theme.precipitation = precipitation;
  return theme;
}

struct WeatherRenderContext {
  const Settings* settings = nullptr;
  struct tm nowTm{};
  WeatherTheme theme{};
  int minute = 0;
  uint32_t animationMs = 0;
};

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
                  uint16_t background, uint16_t cloudColor,
                  uint16_t rainColor) {
  if (id == 800) {
    if (night) drawMoon(g, x + 20, y + 20, 17, background);
    else drawSun(g, x + 20, y + 20, 16, SUN);
    return;
  }
  if (isPartlyCloudy(id)) {
    if (night) drawMoon(g, x + 12, y + 10, 9, background);
    else drawSun(g, x + 11, y + 8, 9, SUN);
  }
  drawCloud(g, x + 1, y + 11, cloudColor);
  if (id >= 200 && id < 300) {
    g.fillTriangle(x + 22, y + 34, x + 16, y + 47, x + 23, y + 44, SUN);
    g.fillTriangle(x + 23, y + 43, x + 20, y + 53, x + 31, y + 38, SUN);
  } else if (isRain(id)) {
    for (int i = 0; i < 3; ++i) {
      const int rx = x + 10 + i * 13;
      g.drawLine(rx, y + 37, rx - 3, y + 46, rainColor);
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
  if (isPartlyCloudy(id)) {
    if (night) drawMoon(g, x + 7, y + 6, 5, panel);
    else g.fillCircle(x + 6, y + 5, 5, SUN);
  }
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

uint16_t skyColorAt(const WeatherTheme& theme, int y) {
  const int clampedY = constrain(y, 0, 126);
  const uint8_t amount = static_cast<uint8_t>(
      static_cast<uint32_t>(clampedY) * 255U / 126U);
  return blend565(theme.skyTop, theme.skyHorizon, amount);
}

void drawSkyGradient(TileCanvas& g, const WeatherTheme& theme) {
  constexpr int bandHeight = 9;
  for (int y = 0; y < 126; y += bandHeight) {
    const int sampleY = min(126, y + bandHeight / 2);
    g.fillRect(0, y, TFT_WIDTH, min(bandHeight, 126 - y),
               skyColorAt(theme, sampleY));
  }
  g.fillRect(0, 126, TFT_WIDTH, 19, theme.water);
  g.drawFastHLine(0, 126, TFT_WIDTH,
                  blend565(theme.water, theme.skyHorizon, 118));
}

void drawSceneCloud(TileCanvas& g, int x, int y, int scale,
                    uint16_t light, uint16_t shade) {
  const int s = max(1, scale);
  g.fillCircle(x + 7 * s, y + 7 * s, 5 * s, light);
  g.fillCircle(x + 15 * s, y + 4 * s, 7 * s, light);
  g.fillCircle(x + 24 * s, y + 8 * s, 5 * s, light);
  g.fillRoundRect(x + 2 * s, y + 7 * s, 28 * s, 9 * s, 4 * s, light);
  g.drawFastHLine(x + 5 * s, y + 15 * s, 22 * s, shade);
}

void drawStars(TileCanvas& g, const WeatherTheme& theme) {
  if (theme.nightAmount < 18) return;
  const uint8_t alpha = static_cast<uint8_t>(
      min<int>(210, (theme.nightAmount - 18) * 9 / 10));
  const uint16_t star = blend565(theme.skyTop, WHITE_SOFT, alpha);
  static const uint8_t stars[][2] = {
      {12, 18}, {34, 42}, {61, 15}, {88, 54}, {113, 24},
      {139, 13}, {161, 48}, {191, 21}, {218, 52}, {231, 15},
      {24, 76}, {124, 71}, {203, 78}};
  for (uint8_t i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
    g.drawPixel(stars[i][0], stars[i][1], star);
    if ((i % 4) == 1 && theme.nightAmount > 175) {
      g.drawPixel(stars[i][0] + 1, stars[i][1], star);
    }
  }
}

void drawCelestial(TileCanvas& g, const WeatherTheme& theme, int minute) {
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);

  const int sunStart = sunriseMinute - 48;
  const int sunEnd = sunsetMinute + 48;
  if (minute >= sunStart && minute <= sunEnd) {
    const float t = constrain(static_cast<float>(minute - sunStart) /
                                  static_cast<float>(sunEnd - sunStart),
                              0.0f, 1.0f);
    const float arc = sinf(t * static_cast<float>(PI));
    const int x = 8 + static_cast<int>(224.0f * t);
    const int y = 116 - static_cast<int>(86.0f * arc);
    const uint16_t bg = skyColorAt(theme, y);
    const uint8_t cloudDim = static_cast<uint8_t>(theme.cloudLevel * 20);
    const uint16_t glow = blend565(bg, SUN,
        static_cast<uint8_t>(max<int>(22, 92 - cloudDim / 2)));
    const uint16_t disc = blend565(bg, SUN,
        static_cast<uint8_t>(max<int>(118, 255 - cloudDim)));
    g.fillCircle(x, y, 13, glow);
    g.fillCircle(x, y, 8, disc);

    // A small horizon reflection makes sunrise and sunset feel connected to
    // the skyscape without requiring another framebuffer.
    if (y > 72) {
      const uint8_t reflectionAlpha = static_cast<uint8_t>(
          min<int>(120, (y - 72) * 3));
      const uint16_t reflection = blend565(theme.water, SUN, reflectionAlpha);
      for (int row = 0; row < 4; ++row) {
        const int half = max(1, 7 - row * 2);
        g.drawFastHLine(x - half, 130 + row * 3, half * 2 + 1, reflection);
      }
    }
  }

  if (theme.nightAmount < 24) return;
  const int nightLength = (1440 - sunsetMinute) + sunriseMinute;
  int elapsed = minute >= sunsetMinute ? minute - sunsetMinute
                                      : minute + 1440 - sunsetMinute;
  elapsed = constrain(elapsed, 0, nightLength);
  const float t = constrain(static_cast<float>(elapsed) /
                                static_cast<float>(nightLength),
                            0.0f, 1.0f);
  const float arc = sinf(t * static_cast<float>(PI));
  const int x = 10 + static_cast<int>(220.0f * t);
  const int y = 108 - static_cast<int>(70.0f * arc);
  const uint16_t bg = skyColorAt(theme, y);
  const uint8_t moonAlpha = static_cast<uint8_t>(
      min<int>(235, 72 + theme.nightAmount * 3 / 5));
  const uint16_t moon = blend565(bg, WHITE_SOFT, moonAlpha);
  g.fillCircle(x, y, 8, moon);
  g.fillCircle(x + 4, y - 3, 8, bg);
}

void drawMovingClouds(TileCanvas& g, const WeatherTheme& theme,
                      uint32_t animationMs) {
  static const int seed[] = {15, 126, 242, 68, 188};
  static const int yPos[] = {18, 50, 76, 72, 88};
  static const int scale[] = {1, 1, 1, 1, 1};
  static const uint16_t speedMs[] = {1450, 1950, 1250, 2500, 3100};
  const uint8_t count = constrain(theme.cloudLevel, 1, 5);

  for (uint8_t i = 0; i < count; ++i) {
    const int travel = static_cast<int>(animationMs / speedMs[i]);
    const int x = ((seed[i] + travel) % 330) - 70;
    int y = yPos[i];
    const int cloudWidth = 32 * scale[i];
    const int cloudHeight = 17 * scale[i];
    // Preserve the weather word on the right. The cloud remains in motion but
    // takes a higher lane while crossing that small readability-critical area.
    if (x + cloudWidth > 158 && x < 240 &&
        y + cloudHeight > 64 && y < 103) {
      y = 12 + static_cast<int>(i) * 6;
    }
    const uint8_t visibility = count == 1 ? 30
        : static_cast<uint8_t>(min<int>(120, 50 + count * 12 + i * 2));
    const uint16_t localSky = skyColorAt(theme, y + 7 * scale[i]);
    const uint16_t light = blend565(localSky, theme.cloudLight, visibility);
    const uint16_t shade = blend565(localSky, theme.cloudShade,
                                    static_cast<uint8_t>(visibility * 3 / 4));
    drawSceneCloud(g, x, y, scale[i], light, shade);
  }
}

void drawSkylineAndBridge(TileCanvas& g, const WeatherTheme& theme) {
  // Water texture stays low contrast so the forecast text remains dominant.
  const uint16_t waterLine = blend565(theme.water, theme.skyHorizon, 76);
  g.drawFastHLine(0, 132, TFT_WIDTH, waterLine);
  g.drawFastHLine(8, 139, 57, waterLine);
  g.drawFastHLine(83, 136, 72, waterLine);
  g.drawFastHLine(172, 141, 61, waterLine);

  static const uint8_t buildings[][3] = {
      {3, 13, 18}, {19, 10, 27}, {33, 16, 15}, {82, 12, 23},
      {98, 16, 31}, {117, 11, 19}, {132, 18, 36}, {153, 11, 24},
      {190, 12, 21}, {207, 18, 30}, {229, 9, 17}};
  for (uint8_t i = 0; i < sizeof(buildings) / sizeof(buildings[0]); ++i) {
    const int x = buildings[i][0];
    const int w = buildings[i][1];
    const int h = buildings[i][2];
    const int y = 125 - h;
    g.fillRect(x, y, w, h, theme.far);
    if ((i % 3) == 1) g.fillRect(x + w / 2, y - 4, 1, 4, theme.far);
  }

  const uint16_t bridge = blend565(theme.far, theme.secondary, 24);
  constexpr int deckY = 122;
  constexpr int towerTop = 88;
  constexpr int leftTower = 58;
  constexpr int rightTower = 175;
  g.fillRect(0, deckY, TFT_WIDTH, 3, bridge);
  g.fillRect(leftTower - 2, towerTop, 5, deckY - towerTop, bridge);
  g.fillRect(rightTower - 2, towerTop, 5, deckY - towerTop, bridge);
  g.drawFastHLine(leftTower - 6, towerTop, 13, bridge);
  g.drawFastHLine(rightTower - 6, towerTop, 13, bridge);

  // Two cable fans are enough to read as a bridge at 240x240.
  for (int step = 0; step < 4; ++step) {
    const int deckXLeft = 8 + step * 10;
    const int deckXMidLeft = 70 + step * 20;
    g.drawLine(leftTower, towerTop + 2, deckXLeft, deckY, bridge);
    g.drawLine(leftTower, towerTop + 2, deckXMidLeft, deckY, bridge);

    const int deckXRight = 232 - step * 10;
    const int deckXMidRight = 163 - step * 20;
    g.drawLine(rightTower, towerTop + 2, deckXRight, deckY, bridge);
    g.drawLine(rightTower, towerTop + 2, deckXMidRight, deckY, bridge);
  }

  if (theme.nightAmount < 52) return;
  const uint8_t glowAmount = smoothAmount(theme.nightAmount, 52, 220);
  const uint16_t lamp = blend565(bridge, WINDOW_LIGHT, glowAmount);
  for (int x = 7; x < 238; x += 14) g.drawPixel(x, deckY - 2, lamp);
  g.drawPixel(leftTower, towerTop + 6, lamp);
  g.drawPixel(rightTower, towerTop + 6, lamp);

  for (uint8_t i = 0; i < sizeof(buildings) / sizeof(buildings[0]); ++i) {
    const int x = buildings[i][0];
    const int w = buildings[i][1];
    const int h = buildings[i][2];
    const int y = 125 - h;
    if ((i & 1) == 0) g.drawPixel(x + max(2, w / 3), y + 6, lamp);
    if ((i % 3) == 1 && h > 22) g.drawPixel(x + w - 3, y + 13, lamp);
  }

  const uint16_t reflection = blend565(theme.water, lamp, glowAmount / 2);
  for (int x = 7; x < 238; x += 28) {
    g.drawPixel(x, 130, reflection);
    g.drawFastVLine(x, 134, 3, reflection);
  }
}

void drawPrecipitation(TileCanvas& g, const WeatherTheme& theme,
                       uint32_t animationMs) {
  if (!theme.precipitation) return;
  const int phase = static_cast<int>((animationMs / 160UL) % 9UL);
  if (theme.precipitation == 2) {
    for (int i = 0; i < 15; ++i) {
      const int x = (i * 29 + phase * 7) % 250 - 5;
      const int y = 16 + (i * 17 + phase * 5) % 103;
      g.drawLine(x, y, x - 2, y + 6, theme.rainColor);
    }
  } else {
    for (int i = 0; i < 13; ++i) {
      const int x = (i * 37 + phase * 4) % 240;
      const int y = 14 + (i * 23 + phase * 3) % 108;
      g.drawPixel(x, y, SNOW);
      if ((i & 3) == 0) g.drawPixel(x + 1, y, SNOW);
    }
  }
}

void drawBackdrop(TileCanvas& g, const WeatherTheme& theme, int minute,
                  uint32_t animationMs) {
  drawSkyGradient(g, theme);
  drawStars(g, theme);
  drawCelestial(g, theme, minute);
  drawMovingClouds(g, theme, animationMs);
  drawSkylineAndBridge(g, theme);
  drawPrecipitation(g, theme, animationMs);
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
  const WeatherRenderContext& context =
      *static_cast<const WeatherRenderContext*>(opaque);
  const Settings& s = *context.settings;
  const WeatherTheme& theme = context.theme;
  const bool night = theme.nightAmount >= 132;

  // Outside the scenic top half, the near-scene colour becomes the substrate
  // visible through the translucent cards.
  g.fillScreen(theme.near);
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

  drawBackdrop(g, theme, context.minute, context.animationMs);

  const uint16_t primary = theme.primary;
  const uint16_t secondary = theme.secondary;
  const struct tm& nowTm = context.nowTm;

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

  // Clear conditions use the moving celestial body itself. Other conditions
  // keep a compact icon on the right without hiding the bridge scene.
  if (W.conditionId != 800) {
    int drift = static_cast<int>((context.animationMs / 900UL) % 12UL);
    if (drift > 6) drift = 12 - drift;
    drift -= 3;
    drawMainIcon(g, W.conditionId, night, 178 + drift, 18,
                 skyColorAt(theme, 42), theme.cloudLight, theme.rainColor);
  }

  const ConditionLabel condition = conditionLabel(W.conditionId);
  g.setTextColor(primary);
  if (condition.second) {
    g.setTextSize(1);
    g.setCursor(226 - gfxTextW(condition.first, 1), 78);
    g.print(condition.first);
    g.setTextSize(2);
    g.setCursor(226 - gfxTextW(condition.second, 2), 88);
    g.print(condition.second);
  } else {
    g.setTextSize(2);
    g.setCursor(226 - gfxTextW(condition.first, 2), 81);
    g.print(condition.first);
  }

  const char unit = s.weather.metric ? 'C' : 'F';

  constexpr int detailX = 8;
  constexpr int detailY = 103;
  constexpr int detailW = 224;
  constexpr int detailH = 34;
  constexpr int detailRadius = 7;
  static_assert(DisplayLayout::fitsSafe(detailX, detailY, detailW, detailH),
                "Weather telemetry panel must fit the safe display area");

  const uint16_t detailOverlay = theme.panelOverlay;
  const uint16_t detailBorder = theme.panelBorder;
  const uint16_t detailText = theme.panelText;
  const uint8_t detailAlpha = theme.panelAlpha;
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

  const uint16_t panelOverlay = theme.panelOverlay;
  const uint16_t panelBorder = theme.panelBorder;
  const uint16_t panelText = theme.panelText;
  const uint16_t panelMuted = theme.panelMuted;
  const uint16_t separator = theme.separator;
  const uint8_t panelAlpha = static_cast<uint8_t>(
      min<int>(120, theme.panelAlpha + 10));
  blendRoundedPanel(g, panelX, panelY, panelW, panelH, panelRadius,
                    panelOverlay, panelAlpha, panelBorder);

  g.setTextSize(1);
  g.setTextColor(panelMuted);
  g.setCursor(22, 153);
  g.print("NEXT 12 HOURS");

  const uint16_t forecastIconBg =
      blend565(theme.near, panelOverlay, panelAlpha);

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

WeatherRenderContext makeWeatherContext(const Settings& settings) {
  WeatherRenderContext context;
  context.settings = &settings;
  currentLocalTm(context.nowTm);
  context.minute = context.nowTm.tm_hour * 60 + context.nowTm.tm_min;
  context.animationMs = millis();
  context.theme = weatherThemeForMinute(context.minute);
  return context;
}

void renderWeatherScreen(const Settings& settings) {
  WeatherRenderContext context = makeWeatherContext(settings);
  gfxRenderTiled(drawScreen, &context, context.theme.near);
}

#if !defined(DESKMATE_PREVIEW)
void renderWeatherAnimatedTop(const Settings& settings) {
  WeatherRenderContext context = makeWeatherContext(settings);
  // Recompose only the scenic/header/telemetry area. The forecast panel stays
  // in LCD RAM, keeping the animation affordable on the ESP8266.
  gfxRenderRegion(drawScreen, &context, context.theme.near,
                  0, 0, TFT_WIDTH, 145);
}
#endif

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
  W.sunrise = state.sunrise;
  W.sunset = state.sunset;
  W.forecastCount = min<uint8_t>(state.forecastCount, 4);
  for (uint8_t i = 0; i < W.forecastCount; ++i) {
    W.forecast[i].valid = state.forecast[i].valid;
    W.forecast[i].id = state.forecast[i].id;
    W.forecast[i].night = state.forecast[i].night;
    W.forecast[i].temp = state.forecast[i].temp;
    W.forecast[i].stamp = state.forecast[i].stamp;
  }
  previewNowUtc = state.nowUtc;
  renderWeatherScreen(settings);
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
  nextAnimationMs_ = millis();
  dirty_ = true;
}

void WeatherMode::invalidate(const Settings& settings) {
  pollStage_ = 0;
  W.timezone = settings.weather.utcOffsetSec;
  nextAnimationMs_ = millis();
  dirty_ = true;
}

void WeatherMode::wake(const Settings&) {
  nextAnimationMs_ = millis();
  dirty_ = true;
}

void WeatherMode::render(const Settings& settings) {
  renderWeatherScreen(settings);
}

void WeatherMode::displayTick(const Settings& settings) {
  struct tm current;
  currentLocalTm(current);
  const int32_t minute = static_cast<int32_t>(current.tm_yday) * 1440L +
                         static_cast<int32_t>(current.tm_hour) * 60L +
                         current.tm_min;
  const uint32_t nowMs = millis();
  if (minute != renderedMinute_) {
    renderedMinute_ = minute;
    dirty_ = true;
  }
  if (dirty_) {
    render(settings);
    dirty_ = false;
    nextAnimationMs_ = nowMs + 2500UL;
    return;
  }

  // The ESP8266 does not redraw the whole 240x240 display for cloud motion.
  // Every 2.5 seconds only the upper 145 rows are recomposed tile-by-tile.
  if (W.valid && static_cast<int32_t>(nowMs - nextAnimationMs_) >= 0) {
    renderWeatherAnimatedTop(settings);
    nextAnimationMs_ = nowMs + 2500UL;
  }
}

#endif  // DESKMATE_PREVIEW
