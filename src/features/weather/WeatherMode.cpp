#include "WeatherMode.h"
#include "Platform.h"
#include "HttpRequest.h"
#include <ArduinoJson.h>
#include "Gfx.h"
#include "TileRenderer.h"
#include "DisplayLayout.h"
#include "WeatherScene.h"
#include "Clock.h"
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <time.h>

WeatherMode g_weatherMode;

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
constexpr uint16_t TEXT_BLACK = rgb565(0, 0, 0);
constexpr uint16_t TEXT_WHITE = rgb565(255, 255, 255);
constexpr uint16_t SUN = rgb565(255, 208, 54);
constexpr uint16_t CLOUD = rgb565(224, 233, 242);
constexpr uint16_t RAIN = rgb565(64, 188, 236);
constexpr uint16_t SNOW = rgb565(235, 246, 252);
constexpr uint16_t ERROR_C = rgb565(255, 112, 112);
constexpr int WEATHER_PANEL_Y = 157;
constexpr int WEATHER_SCENE_SHIFT_Y = -6;

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

constexpr uint16_t WINDOW_LIGHT = rgb565(255, 198, 102);

// Extra dawn and dusk keys are deliberately present between the five named
// day phases. They are transition colours, not abrupt themes.
constexpr TimePalette TIME_NIGHT = {
    rgb565(11, 18, 52), rgb565(57, 79, 127), rgb565(52, 68, 108),
    rgb565(19, 25, 61), rgb565(40, 72, 119), rgb565(184, 207, 232),
    82, 255, 0};
constexpr TimePalette TIME_DAWN = {
    rgb565(48, 58, 101), rgb565(232, 160, 139), rgb565(113, 91, 116),
    rgb565(47, 48, 83), rgb565(110, 114, 149), rgb565(255, 216, 190),
    76, 118, 255};
constexpr TimePalette TIME_MORNING = {
    rgb565(88, 167, 215), rgb565(212, 239, 224), rgb565(91, 167, 202),
    rgb565(35, 105, 143), rgb565(118, 194, 211), rgb565(23, 57, 91),
    66, 0, 35};
constexpr TimePalette TIME_NOON = {
    rgb565(60, 155, 213), rgb565(190, 232, 226), rgb565(69, 151, 197),
    rgb565(26, 91, 130), rgb565(112, 195, 215), rgb565(12, 43, 73),
    68, 0, 0};
constexpr TimePalette TIME_AFTERNOON = {
    rgb565(92, 150, 193), rgb565(220, 213, 194), rgb565(110, 145, 169),
    rgb565(57, 96, 126), rgb565(142, 175, 190), rgb565(17, 46, 72),
    68, 0, 72};
constexpr TimePalette TIME_DUSK = {
    rgb565(56, 66, 108), rgb565(232, 139, 116), rgb565(116, 79, 105),
    rgb565(48, 43, 76), rgb565(157, 119, 134), rgb565(255, 190, 154),
    78, 92, 255};
constexpr TimePalette TIME_EVENING = {
    rgb565(31, 39, 79), rgb565(111, 75, 105), rgb565(76, 60, 91),
    rgb565(28, 31, 66), rgb565(72, 73, 111), rgb565(232, 183, 172),
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
  time_t now = time(nullptr);
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

bool isNightMinute(int minute) {
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);
  return minute < sunriseMinute || minute >= sunsetMinute;
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
  uint8_t cloudLevel = 0;
  uint8_t precipitation = 0;

  if (isPartlyCloudy(W.conditionId)) {
    conditionTint = rgb565(117, 143, 165);
    topTint = 24;
    horizonTint = 18;
    terrainTint = 26;
    cloudLevel = 2;
  } else if (isCloud(W.conditionId)) {
    conditionTint = rgb565(83, 104, 127);
    topTint = 138;
    horizonTint = 112;
    terrainTint = 130;
    extraPanelAlpha = 16;
    cloudLevel = 4;
  } else if (isRain(W.conditionId)) {
    conditionTint = rgb565(34, 48, 68);
    topTint = 178;
    horizonTint = 152;
    terrainTint = 170;
    extraPanelAlpha = 28;
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
  theme.water = blend565(palette.water, conditionTint,
                         static_cast<uint8_t>(terrainTint * 3 / 4));

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

  theme.primary = blend565(TEXT_BLACK, TEXT_WHITE, textNight);
  theme.secondary = blend565(theme.skyTop, theme.primary, 205);
  theme.panelOverlay = blend565(WHITE_SOFT, GLASS_DARK, textNight);
  theme.panelAlpha = static_cast<uint8_t>(
      min<int>(132, palette.panelAlpha + extraPanelAlpha + 20));
  theme.panelBorder = blend565(theme.skyHorizon, theme.primary,
                               textNight > 140 ? 98 : 72);
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

uint32_t colorLuminance(uint16_t color) {
  const uint32_t red = ((color >> 11) & 0x1F) * 255 / 31;
  const uint32_t green = ((color >> 5) & 0x3F) * 255 / 63;
  const uint32_t blue = (color & 0x1F) * 255 / 31;
  return 2126UL * red * red + 7152UL * green * green + 722UL * blue * blue;
}

uint16_t regionTextColor(const uint16_t* backgrounds, uint8_t count) {
  uint32_t darkest = colorLuminance(backgrounds[0]);
  uint32_t brightest = darkest;
  for (uint8_t i = 1; i < count; ++i) {
    const uint32_t luminance = colorLuminance(backgrounds[i]);
    darkest = min(darkest, luminance);
    brightest = max(brightest, luminance);
  }

  // Compare each candidate's weakest WCAG contrast: black over the darkest
  // background versus white over the brightest background.
  constexpr uint32_t full = 650250000UL;
  constexpr uint32_t offset = full / 20;
  const uint64_t blackWorst =
      static_cast<uint64_t>(darkest + offset) * (brightest + offset);
  const uint64_t whiteWorst =
      static_cast<uint64_t>(offset) * (full + offset);
  return blackWorst > whiteWorst ? TEXT_BLACK : TEXT_WHITE;
}

void drawContrastText(TileCanvas& g, const char* text, int x, int y,
                      uint8_t size, uint16_t color,
                      uint8_t shadowOffset = 0) {
  g.setTextSize(size);
  if (shadowOffset) {
    g.setTextColor(color == TEXT_WHITE ? TEXT_BLACK : TEXT_WHITE);
    g.setCursor(x + shadowOffset, y + shadowOffset);
    g.print(text);
  }
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(text);
}

void drawSun(TileCanvas& g, int x, int y, int r, uint16_t color) {
  constexpr float rotation = static_cast<float>(PI) / 8.0f;
  for (int ray = 0; ray < 8; ++ray) {
    const float q = rotation + ray * static_cast<float>(PI) / 4.0f;
    g.drawLine(x + static_cast<int>(cosf(q) * (r + 4)),
               y + static_cast<int>(sinf(q) * (r + 4)),
               x + static_cast<int>(cosf(q) * (r + 9)),
               y + static_cast<int>(sinf(q) * (r + 9)), color);
  }
  g.fillCircle(x, y, r, color);
}

void drawMoon(TileCanvas& g, int x, int y, int r) {
  // Draw only the crescent scanlines. Painting a background-colored circle
  // over a full moon leaves a visible mask when the sky is gradient-filled.
  const int innerX = x + r / 2;
  const int innerY = y - r / 3;
  const int radiusSquared = r * r;
  for (int dy = -r; dy <= r; ++dy) {
    const int outerSquared = radiusSquared - dy * dy;
    if (outerSquared < 0) continue;
    const int outer = static_cast<int>(sqrtf(static_cast<float>(outerSquared)));
    const int left = x - outer;
    const int right = x + outer;

    const int innerDy = y + dy - innerY;
    const int innerSquared = radiusSquared - innerDy * innerDy;
    if (innerSquared <= 0) {
      g.drawFastHLine(left, y + dy, right - left + 1, WHITE_SOFT);
      continue;
    }

    const int inner = static_cast<int>(sqrtf(static_cast<float>(innerSquared)));
    const int cutLeft = innerX - inner;
    const int cutRight = innerX + inner;
    if (cutLeft > left) {
      g.drawFastHLine(left, y + dy, min(right, cutLeft - 1) - left + 1,
                      WHITE_SOFT);
    }
    if (cutRight < right) {
      g.drawFastHLine(max(left, cutRight + 1), y + dy,
                      right - max(left, cutRight + 1) + 1, WHITE_SOFT);
    }
  }
}

void drawCloud(TileCanvas& g, int x, int y, uint16_t color) {
  g.fillCircle(x + 8, y + 8, 7, color);
  g.fillCircle(x + 19, y + 3, 10, color);
  g.fillCircle(x + 33, y + 9, 8, color);
  g.fillRoundRect(x, y + 8, 42, 13, 6, color);
}

void drawMainIcon(TileCanvas& g, int id, bool night, int x, int y,
                  uint16_t cloudColor, uint16_t rainColor) {
  if (id == 800) {
    if (night) drawMoon(g, x + 20, y + 20, 17);
    else drawSun(g, x + 20, y + 20, 16, SUN);
    return;
  }
  if (isPartlyCloudy(id)) {
    if (night) drawMoon(g, x + 12, y + 10, 9);
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

void drawMiniIcon(TileCanvas& g, int id, bool night, int x, int y) {
  if (id == 800) {
    if (night) drawMoon(g, x + 11, y + 10, 7);
    else drawSun(g, x + 11, y + 10, 5, SUN);
    return;
  }
  if (isPartlyCloudy(id)) {
    if (night) drawMoon(g, x + 7, y + 6, 5);
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
  constexpr int skyDepth = 170;
  const int clampedY = constrain(y, 0, skyDepth);
  const uint8_t amount = static_cast<uint8_t>(
      static_cast<uint32_t>(clampedY) * 255U / skyDepth);
  return blend565(theme.skyTop, theme.skyHorizon, amount);
}

void drawSkyGradient(TileCanvas& g, const WeatherTheme& theme) {
  constexpr int skyHeight = 171;
  const int top = max<int>(0, g.tileY());
  const int bottom = min<int>(skyHeight, g.tileY() + g.tileH());
  for (int y = top; y < bottom; ++y) {
    g.drawFastHLine(0, y, TFT_WIDTH, skyColorAt(theme, y));
  }
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

void drawStars(TileCanvas& g, const WeatherTheme& theme,
               uint32_t animationMs) {
  if (theme.nightAmount < 18) return;
  const uint16_t phase = static_cast<uint16_t>(animationMs / 180UL);
  uint16_t seed = 0xA53CU;
  for (uint8_t i = 0; i < 62; ++i) {
    seed = static_cast<uint16_t>(seed * 2053U + 13849U);
    const int x = 1 + seed % 238U;
    seed = static_cast<uint16_t>(seed * 2053U + 13849U);
    const int y = 3 + seed % 104U + WEATHER_SCENE_SHIFT_Y;
    const uint8_t pulse = static_cast<uint8_t>((phase + i * 7U) % 13U);
    const uint8_t twinkle = pulse < 3 ? 65 : (pulse > 10 ? 24 : 0);
    const uint8_t alpha = static_cast<uint8_t>(min<int>(235,
        (theme.nightAmount - 18) * 3 / 4 + twinkle));
    const uint16_t star = blend565(skyColorAt(theme, y), WHITE_SOFT, alpha);
    g.drawPixel(x, y, star);
    if ((i % 9) == 1 && pulse < 3 && theme.nightAmount > 150) {
      g.drawPixel(x + 1, y, star);
      g.drawPixel(x, y + 1, star);
    }
  }
}

bool sunPosition(int minute, int& x, int& y, float& arc) {
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);

  const int sunStart = sunriseMinute - 48;
  const int sunEnd = sunsetMinute + 48;
  if (minute < sunStart || minute > sunEnd) return false;

  const float progress = constrain(static_cast<float>(minute - sunStart) /
                                       static_cast<float>(sunEnd - sunStart),
                                   0.0f, 1.0f);
  arc = sinf(progress * static_cast<float>(PI));
  x = 232 - static_cast<int>(224.0f * progress);
  y = 116 - static_cast<int>(52.0f * arc);
  return true;
}

bool moonPosition(int minute, int& x, int& y, float& arc) {
  int sunriseMinute;
  int sunsetMinute;
  solarMinutes(sunriseMinute, sunsetMinute);

  const int moonStart = sunsetMinute - 48;
  const int moonEnd = sunriseMinute + 1440 + 48;
  int wrappedMinute = minute;
  if (wrappedMinute < moonStart) wrappedMinute += 1440;
  if (wrappedMinute > moonEnd) return false;

  const float progress = constrain(
      static_cast<float>(wrappedMinute - moonStart) /
          static_cast<float>(moonEnd - moonStart),
      0.0f, 1.0f);
  arc = sinf(progress * static_cast<float>(PI));
  x = 232 - static_cast<int>(224.0f * progress);
  y = 116 - static_cast<int>(52.0f * arc);
  return true;
}

void drawCelestial(TileCanvas& g, const WeatherTheme& theme, int minute) {
  int x;
  int y;
  float arc;

  if (moonPosition(minute, x, y, arc)) {
    y += WEATHER_SCENE_SHIFT_Y;
    const uint16_t background = skyColorAt(theme, y);
    const uint8_t cloudDim = static_cast<uint8_t>(theme.cloudLevel * 18);
    const uint16_t outer = blend565(
        background, WHITE_SOFT,
        static_cast<uint8_t>(max<int>(14, 42 - cloudDim / 3)));
    const uint16_t middle = blend565(
        background, WHITE_SOFT,
        static_cast<uint8_t>(max<int>(28, 82 - cloudDim / 2)));
    const uint16_t moon = blend565(
        background, WHITE_SOFT,
        static_cast<uint8_t>(max<int>(105, 220 - cloudDim)));
    const uint16_t crater = blend565(moon, background, 70);
    g.fillCircle(x, y, 15, outer);
    g.fillCircle(x, y, 11, middle);
    g.fillCircle(x, y, 7, moon);
    g.fillCircle(x - 2, y - 2, 1, crater);
    g.drawPixel(x + 3, y + 2, crater);
  }

  if (!sunPosition(minute, x, y, arc)) return;
  y += WEATHER_SCENE_SHIFT_Y;

  const uint16_t background = skyColorAt(theme, y);
  const uint8_t cloudDim = static_cast<uint8_t>(theme.cloudLevel * 18);
  const uint16_t outer = blend565(
      background, SUN, static_cast<uint8_t>(max<int>(16, 48 - cloudDim / 3)));
  const uint16_t middle = blend565(
      background, WHITE_SOFT,
      static_cast<uint8_t>(max<int>(24, 78 - cloudDim / 2)));
  const uint16_t inner = blend565(
      background, blend565(WHITE_SOFT, SUN, theme.warmAmount / 2),
      static_cast<uint8_t>(max<int>(72, 156 - cloudDim)));
  g.fillCircle(x, y, 15, outer);
  g.fillCircle(x, y, 11, middle);
  g.fillCircle(x, y, 7, inner);
  g.fillCircle(x, y, 3, blend565(inner, WHITE_SOFT, 190));
}

void blendFlareHexagon(TileCanvas& g, int x, int y, int radius,
                       uint16_t overlay, uint8_t alpha) {
  const int left = max<int>(x - radius, g.tileX());
  const int top = max<int>(y - radius, g.tileY());
  const int right = min<int>(x + radius, g.tileX() + g.tileW() - 1);
  const int bottom = min<int>(y + radius, g.tileY() + g.tileH() - 1);
  uint16_t* pixels = g.pixels();

  for (int py = top; py <= bottom; ++py) {
    const int dy = abs(py - y);
    const int halfWidth = radius - dy / 2;
    for (int px = left; px <= right; ++px) {
      if (abs(px - x) > halfWidth) continue;
      const int index = (py - g.tileY()) * g.tileW() + px - g.tileX();
      pixels[index] = blend565(pixels[index], overlay, alpha);
    }
  }
}

void drawLensFlare(TileCanvas& g, const WeatherTheme& theme, int minute) {
  if (theme.cloudLevel || theme.nightAmount > 18) return;

  int sunX;
  int sunY;
  float arc;
  if (!sunPosition(minute, sunX, sunY, arc) || arc < 0.38f) return;
  sunY += WEATHER_SCENE_SHIFT_Y;

  constexpr float opticalX = 120.0f;
  constexpr float opticalY = 96.0f;
  const float dx = sunX - opticalX;
  const float dy = sunY - opticalY;
  const float length = sqrtf(dx * dx + dy * dy);
  if (length < 4.0f) return;

  const float ux = dx / length;
  const float uy = dy / length;
  const uint8_t strength = static_cast<uint8_t>(
      min<int>(58, 22 + static_cast<int>(arc * 38.0f)));

  const int upperX1 = sunX + static_cast<int>(ux * 26.0f);
  const int upperY1 = sunY + static_cast<int>(uy * 26.0f);
  const int upperX2 = sunX + static_cast<int>(ux * 48.0f);
  const int upperY2 = sunY + static_cast<int>(uy * 48.0f);
  blendFlareHexagon(g, upperX1, upperY1, 5, WHITE_SOFT, strength);
  blendFlareHexagon(g, upperX2, upperY2, 3, WHITE_SOFT, strength / 2);

  const int lowerX = sunX - static_cast<int>(ux * 92.0f);
  const int lowerY = sunY - static_cast<int>(uy * 92.0f);
  blendFlareHexagon(g, lowerX, lowerY, 14, SUN, strength / 2);
}

void drawMovingClouds(TileCanvas& g, const WeatherTheme& theme,
                      uint32_t animationMs) {
  if (!theme.cloudLevel) return;
  static const int seed[] = {15, 126, 242, 68, 188};
  static const int yPos[] = {19, 48, 72, 82, 102};
  static const int scale[] = {1, 1, 1, 1, 1};
  static const uint16_t speedMs[] = {1450, 1950, 1250, 2500, 3100};
  const uint8_t count = constrain(theme.cloudLevel, 1, 5);

  for (uint8_t i = 0; i < count; ++i) {
    const int travel = static_cast<int>(animationMs / speedMs[i]);
    const int x = ((seed[i] + travel) % 330) - 70;
    int y = yPos[i] + WEATHER_SCENE_SHIFT_Y;
    const uint8_t visibility = count == 1 ? 30
        : static_cast<uint8_t>(min<int>(120, 50 + count * 12 + i * 2));
    const uint16_t localSky = skyColorAt(theme, y + 7 * scale[i]);
    const uint16_t light = blend565(localSky, theme.cloudLight, visibility);
    const uint16_t shade = blend565(localSky, theme.cloudShade,
                                    static_cast<uint8_t>(visibility * 3 / 4));
    drawSceneCloud(g, x, y, scale[i], light, shade);
  }
}

void drawAlpineValley(TileCanvas& g, const WeatherTheme& theme) {
  const uint16_t lake = blend565(theme.water, theme.skyHorizon, 150);
  const uint16_t foreground = blend565(theme.near, rgb565(5, 15, 35), 78);
  const uint16_t colors[] = {
      0,
      blend565(lake, WHITE_SOFT, 110),
      blend565(lake, WHITE_SOFT, 46),
      blend565(theme.skyHorizon, theme.far, 34),
      blend565(theme.skyHorizon, theme.far, 68),
      blend565(theme.skyHorizon, theme.far, 104),
      blend565(theme.skyHorizon, theme.far, 138),
      theme.far,
      blend565(theme.far, theme.near, 34),
      blend565(theme.far, theme.near, 62),
      blend565(theme.far, theme.near, 91),
      blend565(theme.far, theme.near, 122),
      blend565(theme.far, theme.near, 154),
      blend565(theme.far, theme.near, 188),
      blend565(theme.near, foreground, 74),
      foreground,
  };

  if (WEATHER_SCENE_SHIFT_Y < 0) {
    g.fillRect(0, WEATHER_PANEL_Y + WEATHER_SCENE_SHIFT_Y, TFT_WIDTH,
               -WEATHER_SCENE_SHIFT_Y, theme.near);
  }

  const int left = max<int>(0, g.tileX());
  const int top = max<int>(0, g.tileY());
  const int right = min<int>(WEATHER_SCENE_WIDTH, g.tileX() + g.tileW());
  const int bottom = min<int>(WEATHER_SCENE_HEIGHT, g.tileY() + g.tileH());
  for (int y = top; y < bottom; ++y) {
    const int sourceY = y - WEATHER_SCENE_SHIFT_Y;
    if (sourceY < 0 || sourceY >= WEATHER_SCENE_HEIGHT) continue;
    for (int x = left; x < right; ++x) {
      const int pixel = sourceY * WEATHER_SCENE_WIDTH + x;
      const uint8_t packed = pgm_read_byte(&WEATHER_SCENE[pixel / 2]);
      const uint8_t index = pixel & 1 ? packed & 0x0F : packed >> 4;
      if (!index) continue;
      const uint16_t color = index <= 2 && sourceY < 116
          ? blend565(skyColorAt(theme, sourceY), theme.far, 62)
          : colors[index];
      g.drawPixel(x, y, color);
    }
  }

  // The tent sits one third across the frame, just above the foreground.
  const uint16_t tentEdge = blend565(foreground, rgb565(4, 12, 24), 116);
  const uint16_t tentLeft = blend565(rgb565(31, 72, 67),
                                     rgb565(93, 126, 78),
                                     static_cast<uint8_t>(
                                         215 - theme.nightAmount / 3));
  const uint16_t tentRight = blend565(tentLeft, foreground, 115);
  const uint16_t tentGlow = blend565(tentRight, WINDOW_LIGHT,
                                     static_cast<uint8_t>(
                                         58 + theme.nightAmount / 2));
  g.fillTriangle(57, 160 + WEATHER_SCENE_SHIFT_Y,
                 78, 128 + WEATHER_SCENE_SHIFT_Y,
                 99, 160 + WEATHER_SCENE_SHIFT_Y, tentEdge);
  g.fillTriangle(61, 157 + WEATHER_SCENE_SHIFT_Y,
                 78, 133 + WEATHER_SCENE_SHIFT_Y,
                 78, 157 + WEATHER_SCENE_SHIFT_Y, tentLeft);
  g.fillTriangle(78, 133 + WEATHER_SCENE_SHIFT_Y,
                 95, 157 + WEATHER_SCENE_SHIFT_Y,
                 78, 157 + WEATHER_SCENE_SHIFT_Y, tentRight);
  g.fillTriangle(78, 157 + WEATHER_SCENE_SHIFT_Y,
                 84, 145 + WEATHER_SCENE_SHIFT_Y,
                 91, 157 + WEATHER_SCENE_SHIFT_Y, tentGlow);
  g.drawFastHLine(52, 160 + WEATHER_SCENE_SHIFT_Y, 52, foreground);
  g.drawLine(78, 128 + WEATHER_SCENE_SHIFT_Y,
             78, 159 + WEATHER_SCENE_SHIFT_Y, tentEdge);
  g.drawLine(57, 160 + WEATHER_SCENE_SHIFT_Y,
             51, 162 + WEATHER_SCENE_SHIFT_Y, tentEdge);
  g.drawLine(99, 160 + WEATHER_SCENE_SHIFT_Y,
             105, 162 + WEATHER_SCENE_SHIFT_Y, tentEdge);
}

void drawPrecipitation(TileCanvas& g, const WeatherTheme& theme,
                       uint32_t animationMs) {
  if (!theme.precipitation) return;
  // Rain was visually crawling because both its simulation step and the
  // retained upper-screen redraw were too slow for a 240x240 panel.
  const uint32_t tick = animationMs / 80UL;
  if (theme.precipitation == 2) {
    static const uint8_t rainSeeds[][2] = {
        {4, 12},   {19, 91},  {31, 45},  {47, 132}, {58, 19},
        {73, 108}, {86, 63},  {101, 3},  {113, 118}, {126, 72},
        {139, 35}, {151, 143}, {164, 88}, {178, 14}, {191, 125},
        {205, 54}, {219, 101}, {233, 28}, {12, 61},  {42, 7},
        {67, 149}, {96, 99},  {132, 16}, {171, 67}, {211, 145},
        {238, 83}};
    for (uint8_t i = 0; i < sizeof(rainSeeds) / sizeof(rainSeeds[0]); ++i) {
      const int drift = static_cast<int>(
          tick * static_cast<uint32_t>(1 + i % 3) % 248UL);
      const int x = (rainSeeds[i][0] + 248 - drift) % 248 - 4;
      const int y = 10 + (rainSeeds[i][1] +
                          tick * static_cast<uint32_t>(5 + i % 4)) % 153 +
                    WEATHER_SCENE_SHIFT_Y;
      const int length = 3 + i % 3;
      g.drawLine(x, y, x - 1, y + length, theme.rainColor);
    }
  } else {
    const int phase = static_cast<int>(tick % 9UL);
    for (int i = 0; i < 18; ++i) {
      const int x = (i * 37 + phase * 4) % 240;
      const int y = 14 + (i * 23 + phase * 3) % 150 +
                    WEATHER_SCENE_SHIFT_Y;
      g.drawPixel(x, y, SNOW);
      if ((i & 3) == 0) g.drawPixel(x + 1, y, SNOW);
    }
  }
}

void drawBackdrop(TileCanvas& g, const WeatherTheme& theme, int minute,
                  uint32_t animationMs) {
  drawSkyGradient(g, theme);
  drawStars(g, theme, animationMs);
  drawCelestial(g, theme, minute);
  drawMovingClouds(g, theme, animationMs);
  drawAlpineValley(g, theme);
  drawLensFlare(g, theme, minute);
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
  const bool night = isNightMinute(context.minute);

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
  g.fillRect(0, WEATHER_PANEL_Y, TFT_WIDTH, TFT_HEIGHT - WEATHER_PANEL_Y,
             theme.near);

  const struct tm& nowTm = context.nowTm;
  const uint16_t scenicBackgrounds[] = {
      theme.skyTop, theme.skyHorizon, theme.far,
      theme.near, theme.water, theme.cloudLight};
  const uint16_t scenicText = regionTextColor(
      scenicBackgrounds,
      sizeof(scenicBackgrounds) / sizeof(scenicBackgrounds[0]));

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
            cityText, sizeof(cityText), 10);

  constexpr uint8_t headerSize = 2;
  drawContrastText(g, cityText, DisplayLayout::Left + 2, 5,
                   headerSize, scenicText, 1);
  drawContrastText(g, dateText,
                   DisplayLayout::Right - gfxTextW(dateText, headerSize) - 2,
                   5, headerSize, scenicText, 1);

  constexpr uint8_t clockSize = 4;
  constexpr int clockX = 8;
  constexpr int clockY = 23;
  drawContrastText(g, timeText, clockX, clockY, clockSize, scenicText, 2);

  if (meridiem[0]) {
    drawContrastText(g, meridiem,
                     clockX + gfxTextW(timeText, clockSize) + 4, 42,
                     2, scenicText, 1);
  }

  char tempText[10];
  snprintf(tempText, sizeof(tempText), "%.0f", W.temp);
  constexpr int tempX = 10;
  constexpr int tempY = 60;
  constexpr uint8_t tempSize = 3;
  drawContrastText(g, tempText, tempX, tempY, tempSize, scenicText, 1);
  const int degreeX = tempX + gfxTextW(tempText, tempSize) + 5;
  g.drawCircle(degreeX + 1, tempY + 8, 4,
               scenicText == TEXT_WHITE ? TEXT_BLACK : TEXT_WHITE);
  g.drawCircle(degreeX, tempY + 7, 4, scenicText);

  // The celestial bodies belong to the landscape; this icon always identifies
  // the current condition, including clear weather after the body has set.
  int iconY = isCloud(W.conditionId) ? 40 : 35;
  if (W.conditionId >= 200 && W.conditionId < 300) iconY = 24;
  drawMainIcon(g, W.conditionId, night, 181, iconY, theme.cloudLight,
               theme.rainColor);

  const ConditionLabel condition = conditionLabel(W.conditionId);
  constexpr uint8_t conditionSize = 2;
  if (condition.second) {
    drawContrastText(g, condition.first,
                     228 - gfxTextW(condition.first, conditionSize), 88,
                     conditionSize, scenicText, 1);
    drawContrastText(g, condition.second,
                     228 - gfxTextW(condition.second, conditionSize), 106,
                     conditionSize, scenicText, 1);
  } else {
    drawContrastText(g, condition.first,
                     228 - gfxTextW(condition.first, conditionSize), 96,
                     conditionSize, scenicText, 1);
  }

  const char unit = s.weather.metric ? 'C' : 'F';

  constexpr int panelX = DisplayLayout::Left;
  constexpr int panelY = WEATHER_PANEL_Y;
  constexpr int panelW = DisplayLayout::Width;
  constexpr int panelH = 75;
  constexpr int panelRadius = 11;
  static_assert(DisplayLayout::fitsSafe(panelX, panelY, panelW, panelH),
                "Weather forecast panel must fit the safe display area");

  const uint16_t panelOverlay = theme.panelOverlay;
  const uint16_t panelBorder = theme.panelBorder;
  const uint16_t separator = theme.separator;
  const uint8_t panelAlpha = static_cast<uint8_t>(
      min<int>(120, theme.panelAlpha + 10));
  blendRoundedPanel(g, panelX, panelY, panelW, panelH, panelRadius,
                    panelOverlay, panelAlpha, panelBorder);
  const uint16_t panelBackground =
      blend565(theme.near, panelOverlay, panelAlpha);
  const uint16_t panelText = regionTextColor(&panelBackground, 1);

  char detail[40];
  snprintf(detail, sizeof(detail), "FEELS %.0f%c  HUM%d%%  W%.0f%c  %d",
           W.feels, unit, W.humidity, W.wind,
           s.weather.metric ? 'K' : 'M', W.pressure);
  drawContrastText(g, detail, 120 - gfxTextW(detail, 1) / 2, 163,
                   1, panelText);

  for (uint8_t i = 0; i < 4; ++i) {
    const int left = 9 + i * 56;
    if (i) g.drawFastVLine(left - 3, 178, 47, separator);
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

    drawContrastText(g, forecastTime,
                     left + (52 - gfxTextW(forecastTime, 1)) / 2, 177,
                     1, panelText);

    drawMiniIcon(g, W.forecast[i].id, W.forecast[i].night, left + 12, 188);

    char forecastTemp[12];
    snprintf(forecastTemp, sizeof(forecastTemp), "%.0f%c",
             W.forecast[i].temp, unit);
    drawContrastText(g, forecastTemp,
                     left + (52 - gfxTextW(forecastTemp, 2)) / 2, 215,
                     2, panelText);
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

void renderWeatherAnimatedTop(const Settings& settings) {
  WeatherRenderContext context = makeWeatherContext(settings);
  // Recompose only the scenic/header area. The forecast panel stays
  // in LCD RAM, keeping the animation affordable on the ESP8266.
  gfxRenderRegion(drawScreen, &context, context.theme.near,
                  0, 0, TFT_WIDTH, WEATHER_PANEL_Y);
}

static TlsSession g_weatherSession;
constexpr size_t kWeatherUrlCapacity = 320;

static bool buildWeatherUrl(const Settings& s, bool forecast,
                            char* out, size_t outSize) {
  const char* path = forecast ? "forecast" : "weather";
  const char* count = forecast ? "&cnt=8" : "";
  const int written = snprintf(
      out, outSize,
      "https://api.openweathermap.org/data/2.5/%s?lat=%.5f&lon=%.5f%s&units=%s&appid=%s",
      path, s.weather.lat, s.weather.lon, count,
      s.weather.metric ? "metric" : "imperial", s.weather.apiKey.c_str());
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool beginGet(const Settings& s, const char* url,
              std::unique_ptr<SecureClient>& client, uint16_t budgetMs,
              int& code, int& contentLength, bool& chunked) {
  if (!platformTlsMemoryReady()) return false;
  client.reset(platformMakeSecureClient(PLATFORM_TLS_RX_BYTES,
                                        &g_weatherSession));
  if (!client) return false;
  const uint16_t timeoutMs = min<uint16_t>(budgetMs, s.httpTimeout);
  return httpGet(*client, url, FW_NAME, "application/json", timeoutMs,
                 24576, &code, &contentLength, &chunked);
}

bool fetchCurrent(const Settings& s, uint16_t budgetMs) {
  char url[kWeatherUrlCapacity];
  if (!buildWeatherUrl(s, false, url, sizeof(url))) {
    strlcpy(W.errorText, "URL TOO LONG", sizeof(W.errorText));
    return false;
  }

  std::unique_ptr<SecureClient> client;
  int code = 0;
  int contentLength = -1;
  bool chunked = false;
  if (!beginGet(s, url, client, budgetMs, code, contentLength, chunked)) {
    strlcpy(W.errorText, "CONNECTION FAILED", sizeof(W.errorText));
    return false;
  }

  W.httpCode = code;
  if (code != 200 || chunked || contentLength < 0) {
    client->stop();
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
      doc, *client, DeserializationOption::Filter(filter));
  client->stop();
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
  char url[kWeatherUrlCapacity];
  if (!buildWeatherUrl(s, true, url, sizeof(url))) {
    strlcpy(W.errorText, "URL TOO LONG", sizeof(W.errorText));
    return false;
  }

  std::unique_ptr<SecureClient> client;
  int code = 0;
  int contentLength = -1;
  bool chunked = false;
  if (!beginGet(s, url, client, budgetMs, code, contentLength, chunked)) return false;
  if (code != 200 || chunked || contentLength < 0) {
    client->stop();
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
      doc, *client, DeserializationOption::Filter(filter));
  client->stop();
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
    nextAnimationMs_ = nowMs + (isRain(W.conditionId) ? 1000UL : 2500UL);
    return;
  }

  // The ESP8266 does not redraw the whole 240x240 display for weather motion.
  // Rain gets a 1-second upper-region redraw; slower cloud/snow motion keeps
  // the cheaper 2.5-second cadence.
  if (W.valid && static_cast<int32_t>(nowMs - nextAnimationMs_) >= 0) {
    renderWeatherAnimatedTop(settings);
    nextAnimationMs_ = nowMs + (isRain(W.conditionId) ? 1000UL : 2500UL);
  }
}
