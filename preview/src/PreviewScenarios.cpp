#include "PreviewScenarios.h"

#include "PreviewApi.h"
#include "PreviewFramebuffer.h"
#include "StatusDot.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
Settings baseSettings() {
  Settings settings{};
  settings.httpTimeout = 10000;
  settings.brightness = 90;
  settings.rotation = 0;
  settings.weather.metric = true;
  settings.weather.city = "Dhaka";
  settings.weather.apiKey = "preview-key";
  settings.weather.locationVerified = true;
  settings.clock.use24Hour = true;
  settings.radar.lat = 23.8103f;
  settings.radar.lon = 90.4125f;
  settings.radar.rangeKm = 40;
  settings.radar.pollSec = 10;
  settings.radar.showLabels = true;
  settings.radar.showVectors = true;
  settings.radar.showRimDots = true;
  settings.radar.uiScale = 1;
  return settings;
}

constexpr int PREVIEW_TIMEZONE = 6 * 3600;
constexpr int PREVIEW_SUNRISE_MINUTE = 5 * 60 + 30;
constexpr int PREVIEW_SUNSET_MINUTE = 18 * 60 + 30;
constexpr uint32_t PREVIEW_LOCAL_MIDNIGHT_UTC = 1785434400UL;

uint32_t localMinuteUtc(int minute) {
  while (minute < 0) minute += 1440;
  return PREVIEW_LOCAL_MIDNIGHT_UTC +
         static_cast<uint32_t>(minute) * 60UL;
}

uint32_t localTimeUtc(int hour, int minute) {
  return localMinuteUtc(hour * 60 + minute);
}

void setSolarTimes(PreviewWeatherState& state) {
  state.timezone = PREVIEW_TIMEZONE;
  state.sunrise = localMinuteUtc(PREVIEW_SUNRISE_MINUTE);
  state.sunset = localMinuteUtc(PREVIEW_SUNSET_MINUTE);
}

bool previewNightAtMinute(int minute) {
  minute %= 1440;
  if (minute < 0) minute += 1440;
  return minute < PREVIEW_SUNRISE_MINUTE ||
         minute >= PREVIEW_SUNSET_MINUTE;
}

void setForecast(PreviewWeatherState& state, int index, uint32_t hours,
                 int id, float temperature) {
  state.forecast[index].valid = true;
  state.forecast[index].stamp = state.nowUtc + hours * 3600UL;
  state.forecast[index].id = id;
  const int nowLocalMinute = static_cast<int>(
      ((state.nowUtc - PREVIEW_LOCAL_MIDNIGHT_UTC) / 60UL) % 1440UL);
  state.forecast[index].night =
      previewNightAtMinute(nowLocalMinute + static_cast<int>(hours) * 60);
  state.forecast[index].temp = temperature;
}

void renderWeatherClear(uint32_t) {
  Settings settings = baseSettings();
  PreviewWeatherState state;
  state.city = "Dhaka";
  state.icon = "01d";
  state.conditionId = 800;
  state.temp = 33;
  state.feels = 38;
  state.wind = 13;
  state.humidity = 58;
  state.pressure = 1004;
  state.nowUtc = localTimeUtc(12, 30);
  setSolarTimes(state);
  setForecast(state, 0, 3, 800, 34);
  setForecast(state, 1, 6, 802, 32);
  setForecast(state, 2, 9, 804, 29);
  setForecast(state, 3, 12, 800, 27);
  previewRenderWeather(settings, state);
}

void renderWeatherRain(uint32_t) {
  Settings settings = baseSettings();
  settings.clock.use24Hour = false;
  PreviewWeatherState state;
  state.city = "Chattogram";
  state.icon = "10d";
  state.conditionId = 501;
  state.temp = 29;
  state.feels = 33;
  state.wind = 22;
  state.humidity = 89;
  state.pressure = 998;
  state.nowUtc = localTimeUtc(18, 0);
  setSolarTimes(state);
  setForecast(state, 0, 3, 501, 28);
  setForecast(state, 1, 6, 502, 27);
  setForecast(state, 2, 9, 802, 26);
  setForecast(state, 3, 12, 800, 25);
  previewRenderWeather(settings, state);
}

void renderWeatherNight(uint32_t) {
  Settings settings = baseSettings();
  settings.clock.use24Hour = false;
  PreviewWeatherState state;
  state.city = "Sylhet";
  state.icon = "01n";
  state.conditionId = 800;
  state.temp = 24;
  state.feels = 26;
  state.wind = 7;
  state.humidity = 76;
  state.pressure = 1009;
  state.nowUtc = localTimeUtc(23, 30);
  setSolarTimes(state);
  setForecast(state, 0, 3, 800, 23);
  setForecast(state, 1, 6, 801, 22);
  setForecast(state, 2, 9, 802, 25);
  setForecast(state, 3, 12, 800, 29);
  previewRenderWeather(settings, state);
}

const char* cycleIconFor(int conditionId, bool night) {
  if (conditionId == 800) return night ? "01n" : "01d";
  if (conditionId == 801 || conditionId == 802) return night ? "02n" : "02d";
  if (conditionId == 803 || conditionId == 804) return night ? "04n" : "04d";
  return night ? "10n" : "10d";
}

void renderWeatherCycle(uint32_t nowMs, int conditionId) {
  Settings settings = baseSettings();
  settings.clock.use24Hour = false;

  // One full day takes 72 seconds. Each desktop frame advances only a few
  // simulated minutes, making dawn/dusk interpolation easy to inspect.
  const int minute = (4 * 60 + 30 + static_cast<int>(nowMs / 50UL)) % 1440;
  const bool night = previewNightAtMinute(minute);
  const float angle = static_cast<float>(minute - 9 * 60) *
                      2.0f * 3.14159265f / 1440.0f;

  PreviewWeatherState state;
  state.city = "Dhaka";
  state.icon = cycleIconFor(conditionId, night);
  state.conditionId = conditionId;
  state.temp = 28.0f + 5.0f * std::sin(angle);
  state.feels = state.temp + (conditionId >= 500 && conditionId < 600 ? 3.0f : 2.0f);
  state.wind = conditionId >= 500 && conditionId < 600 ? 18.0f : 11.0f;
  state.humidity = conditionId >= 500 && conditionId < 600 ? 86 : 64;
  state.pressure = conditionId >= 500 && conditionId < 600 ? 1000 : 1006;
  state.nowUtc = localMinuteUtc(minute);
  setSolarTimes(state);

  for (int index = 0; index < 4; ++index) {
    const uint32_t hours = static_cast<uint32_t>((index + 1) * 3);
    const float forecastTemp = state.temp - 0.7f * static_cast<float>(index);
    setForecast(state, index, hours, conditionId, forecastTemp);
  }
  previewRenderWeather(settings, state);
}

void renderWeatherCycleClear(uint32_t nowMs) {
  renderWeatherCycle(nowMs, 800);
}

void renderWeatherCyclePartly(uint32_t nowMs) {
  renderWeatherCycle(nowMs, 802);
}

void renderWeatherCycleCloudy(uint32_t nowMs) {
  renderWeatherCycle(nowMs, 804);
}

void renderWeatherCycleRain(uint32_t nowMs) {
  renderWeatherCycle(nowMs, 501);
}

void renderWeatherLoading(uint32_t) {
  Settings settings = baseSettings();
  PreviewWeatherState state;
  state.valid = false;
  state.error = false;
  previewRenderWeather(settings, state);
}

void renderWeatherError(uint32_t) {
  Settings settings = baseSettings();
  PreviewWeatherState state;
  state.valid = false;
  state.error = true;
  state.httpCode = 401;
  state.errorText = "INVALID API KEY";
  previewRenderWeather(settings, state);
}

void fillGithubGraph(PreviewGithubState& state, uint8_t weeks) {
  state.weekCount = weeks;
  for (uint8_t week = 0; week < weeks; ++week) {
    for (uint8_t day = 0; day < 7; ++day) {
      const unsigned value = (week * 11 + day * 7 + week * day) % 13;
      state.graphLevel[week][day] = value == 0 ? 0 : 1 + value % 4;
    }
  }
}

void renderGithub3m(uint32_t) {
  PreviewGithubState state;
  state.login = "itsfuad";
  state.rangeMonths = 3;
  state.openIssues = 12;
  state.openPullRequests = 8;
  state.commits = 825;
  state.totalContributions = 825;
  state.weekTotal = 38;
  state.streak = 14;
  fillGithubGraph(state, 13);
  previewRenderGithub(state);
}

void renderGithub12m(uint32_t) {
  PreviewGithubState state;
  state.login = "very-long-login-name";
  state.rangeMonths = 12;
  state.openIssues = 1284;
  state.openPullRequests = 984;
  state.commits = 28429;
  state.totalContributions = 128532;
  state.weekTotal = 142;
  state.streak = 365;
  fillGithubGraph(state, 53);
  previewRenderGithub(state);
}

void renderGithubError(uint32_t) {
  PreviewGithubState state;
  state.valid = false;
  state.error = true;
  state.httpCode = 403;
  state.errorText = "TOKEN PERMISSION";
  previewRenderGithub(state);
}

PreviewNetworkState networkState(uint32_t nowMs) {
  PreviewNetworkState state;
  state.sampleCount = 60;
  state.heartbeatOn = StatusDot::onAt(nowMs, 0);
  for (uint8_t i = 0; i < state.sampleCount; ++i) {
    const float wave = 17.0f * std::sin(i * 0.31f) +
                       8.0f * std::sin(i * 0.83f);
    state.samples[i].tcpMs = static_cast<uint16_t>(44 + std::fabs(wave));
    state.samples[i].dnsMs = static_cast<uint16_t>(16 + (i * 7) % 18);
    state.samples[i].tcpOk = true;
    state.samples[i].dnsOk = true;
  }
  return state;
}

void renderNetworkHealthy(uint32_t nowMs) {
  Settings settings = baseSettings();
  PreviewNetworkState state = networkState(nowMs);
  previewRenderNetwork(settings, state);
}

void renderNetworkDegraded(uint32_t nowMs) {
  Settings settings = baseSettings();
  PreviewNetworkState state = networkState(nowMs);
  state.rssi = -78;
  state.lastOutageSec = 43;
  state.outageCount = 2;
  for (uint8_t i = 6; i < state.sampleCount; i += 11) {
    state.samples[i].dnsOk = false;
  }
  for (uint8_t i = 17; i < state.sampleCount; i += 19) {
    state.samples[i].tcpOk = false;
    state.samples[i].dnsOk = false;
  }
  previewRenderNetwork(settings, state);
}

void renderNetworkOffline(uint32_t nowMs) {
  Settings settings = baseSettings();
  PreviewNetworkState state = networkState(nowMs);
  state.online = false;
  state.outageStartMs = nowMs > 17000 ? nowMs - 17000 : 1;
  state.outageCount = 3;
  for (uint8_t i = 48; i < state.sampleCount; ++i) {
    state.samples[i].tcpOk = false;
    state.samples[i].dnsOk = false;
    state.samples[i].tcpMs = 0;
    state.samples[i].dnsMs = 0;
  }
  previewRenderNetwork(settings, state);
}

void renderNetworkBusy(uint32_t nowMs) {
  Settings settings = baseSettings();
  PreviewNetworkState state = networkState(nowMs);
  state.pollBusy = true;
  state.heartbeatOn = true;
  previewRenderNetwork(settings, state);
}

void setAircraft(Aircraft& aircraft, const char* callsign, float distance,
                 float bearing, float track, float speed, int altitude,
                 const char* category, const char* type) {
  aircraft = Aircraft{};
  strlcpy(aircraft.callsign, callsign, sizeof(aircraft.callsign));
  strlcpy(aircraft.category, category, sizeof(aircraft.category));
  strlcpy(aircraft.type, type, sizeof(aircraft.type));
  aircraft.distKm = distance;
  aircraft.bearingDeg = bearing;
  aircraft.track = track;
  aircraft.gs = speed;
  aircraft.altFt = altitude;
}

PreviewRadarState radarState(uint32_t nowMs) {
  PreviewRadarState state;
  state.aircraftCount = 6;
  state.lastOkMs = nowMs > 2000 ? nowMs - 2000 : 1;
  state.heartbeatOn = StatusDot::onAt(nowMs, 0);
  setAircraft(state.aircraft[0], "BGD071", 7.4f, 36, 74, 410, 13200, "A3",
              "B738");
  setAircraft(state.aircraft[1], "UAE584", 15.2f, 118, 102, 465, 27500,
              "A5", "B77W");
  setAircraft(state.aircraft[2], "NOVOAIR", 22.8f, 224, 192, 280, 9100,
              "A2", "AT76");
  setAircraft(state.aircraft[3], "HELI01", 11.0f, 310, 15, 95, 1800, "A7",
              "EC35");
  setAircraft(state.aircraft[4], "CPA098", 34.5f, 170, 154, 490, 34000,
              "A5", "A359");
  setAircraft(state.aircraft[5], "RIMDOT", 52.0f, 274, 260, 430, 31000,
              "A3", "A320");
  return state;
}

Settings radarSettings() {
  Settings settings = baseSettings();
  settings.radar.airportCount = 2;
  strlcpy(settings.radar.airports[0].icao, "DAC",
          sizeof(settings.radar.airports[0].icao));
  settings.radar.airports[0].lat = 23.8433f;
  settings.radar.airports[0].lon = 90.3978f;
  strlcpy(settings.radar.airports[1].icao, "VGTJ",
          sizeof(settings.radar.airports[1].icao));
  settings.radar.airports[1].lat = 23.7788f;
  settings.radar.airports[1].lon = 90.3827f;
  return settings;
}

void renderRadarTargets(uint32_t nowMs) {
  Settings settings = radarSettings();
  PreviewRadarState state = radarState(nowMs);
  previewRenderRadar(settings, state);
}

void renderRadarBusy(uint32_t nowMs) {
  Settings settings = radarSettings();
  PreviewRadarState state = radarState(nowMs);
  state.pollBusy = true;
  state.heartbeatOn = true;
  previewRenderRadar(settings, state);
}

void renderRadarError(uint32_t nowMs) {
  Settings settings = radarSettings();
  PreviewRadarState state = radarState(nowMs);
  state.error = true;
  previewRenderRadar(settings, state);
}

void renderRadarEmpty(uint32_t nowMs) {
  Settings settings = radarSettings();
  PreviewRadarState state;
  state.aircraftCount = 0;
  state.lastOkMs = nowMs > 1000 ? nowMs - 1000 : 1;
  state.heartbeatOn = StatusDot::onAt(nowMs, 0);
  previewRenderRadar(settings, state);
}

void renderOta0(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Writing,
                        "deskmate-4.4.0-esp8266.bin", 0, 1048576,
                        "Waiting for first upload block");
}

void renderOta1(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Writing,
                        "deskmate-4.4.0-esp8266.bin", 10485, 1048576);
}

void renderOta50(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Writing,
                        "deskmate-4.4.0-esp8266.bin", 524288, 1048576);
}

void renderOtaUnknown(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Downloading,
                        "deskmate-4.4.0-esp8266.bin", 188416, 0);
}

void renderOtaFailed(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Failed,
                        "deskmate-4.4.0-esp8266.bin", 311296, 1048576,
                        "Flash write failed");
}

void renderOtaComplete(uint32_t) {
  previewRenderFirmware(GfxFirmwareState::Complete,
                        "deskmate-4.4.0-esp8266.bin", 1048576, 1048576);
}

const std::vector<PreviewScenario> scenarios = {
    {"weather-cycle-clear", "Weather / animated day cycle / clear", true,
     renderWeatherCycleClear},
    {"weather-cycle-partly",
     "Weather / animated day cycle / partly cloudy", true,
     renderWeatherCyclePartly},
    {"weather-cycle-cloudy", "Weather / animated day cycle / cloudy", true,
     renderWeatherCycleCloudy},
    {"weather-cycle-rain", "Weather / animated day cycle / rain", true,
     renderWeatherCycleRain},
    {"weather-clear", "Weather / clear noon", false, renderWeatherClear},
    {"weather-rain", "Weather / evening rain and 12-hour clock", false,
     renderWeatherRain},
    {"weather-night", "Weather / clear night", false, renderWeatherNight},
    {"weather-loading", "Weather / loading", false, renderWeatherLoading},
    {"weather-error", "Weather / API error", false, renderWeatherError},
    {"github-3m", "GitHub / three months", false, renderGithub3m},
    {"github-12m", "GitHub / twelve months and large values", false,
     renderGithub12m},
    {"github-error", "GitHub / API error", false, renderGithubError},
    {"network-healthy", "Network / healthy", true, renderNetworkHealthy},
    {"network-degraded", "Network / degraded", true, renderNetworkDegraded},
    {"network-offline", "Network / outage", true, renderNetworkOffline},
    {"network-busy", "Network / API request busy", false, renderNetworkBusy},
    {"radar-targets", "Radar / targets", true, renderRadarTargets},
    {"radar-busy", "Radar / API request busy", false, renderRadarBusy},
    {"radar-error", "Radar / stale API error", true, renderRadarError},
    {"radar-empty", "Radar / no targets", true, renderRadarEmpty},
    {"ota-0", "Firmware update / zero percent", false, renderOta0},
    {"ota-1", "Firmware update / one percent", false, renderOta1},
    {"ota-50", "Firmware update / fifty percent", false, renderOta50},
    {"ota-unknown", "Firmware update / unknown total", false,
     renderOtaUnknown},
    {"ota-failed", "Firmware update / failure", false, renderOtaFailed},
    {"ota-complete", "Firmware update / complete", false,
     renderOtaComplete},
};
}  // namespace

const std::vector<PreviewScenario>& previewScenarios() { return scenarios; }

int previewScenarioIndex(const std::string& id) {
  for (size_t index = 0; index < scenarios.size(); ++index) {
    if (scenarios[index].id == id) return static_cast<int>(index);
  }
  return -1;
}
