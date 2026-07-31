#pragma once

#include <cstdint>
#include <ctime>

#include "Gfx.h"
#include "RadarData.h"
#include "Settings.h"

struct PreviewForecastPoint {
  bool valid = false;
  int id = 800;
  bool night = false;
  float temp = 0;
  uint32_t stamp = 0;
};

struct PreviewWeatherState {
  bool valid = true;
  bool error = false;
  int httpCode = 0;
  const char* errorText = "";
  const char* city = "Dhaka";
  const char* icon = "01d";
  float temp = 31;
  float feels = 36;
  float wind = 12;
  int humidity = 68;
  int pressure = 1006;
  int conditionId = 800;
  int32_t timezone = 21600;
  time_t nowUtc = 1785501000;
  uint32_t sunrise = 1785454200UL;
  uint32_t sunset = 1785501000UL;
  PreviewForecastPoint forecast[4];
  uint8_t forecastCount = 4;
};

struct PreviewGithubState {
  bool valid = true;
  bool error = false;
  int httpCode = 0;
  const char* errorText = "";
  const char* login = "itsfuad";
  uint32_t commits = 825;
  uint32_t openIssues = 12;
  uint32_t openPullRequests = 8;
  uint32_t totalContributions = 825;
  uint16_t streak = 14;
  uint16_t weekTotal = 38;
  uint8_t weekCount = 13;
  uint8_t rangeMonths = 3;
  uint8_t graphLevel[GITHUB_GRAPH_WEEKS][7] = {{0}};
};

struct PreviewNetworkSample {
  uint16_t tcpMs = 0;
  uint16_t dnsMs = 0;
  bool tcpOk = false;
  bool dnsOk = false;
};

struct PreviewNetworkState {
  PreviewNetworkSample samples[60];
  uint8_t sampleCount = 60;
  bool online = true;
  bool haveState = true;
  uint32_t outageStartMs = 0;
  uint32_t lastOutageSec = 0;
  uint16_t outageCount = 0;
  int rssi = -56;
  const char* host = "cloudflare.com:443";
  const char* ip = "192.168.1.42";
  bool heartbeatOn = true;
  bool pollBusy = false;
};

struct PreviewRadarState {
  Aircraft aircraft[MAX_AIRCRAFT];
  uint8_t aircraftCount = 0;
  bool error = false;
  uint32_t lastOkMs = 1000;
  bool heartbeatOn = true;
  bool pollBusy = false;
};

void previewRenderWeather(const Settings& settings,
                          const PreviewWeatherState& state);
void previewRenderGithub(const PreviewGithubState& state);
void previewRenderNetwork(const Settings& settings,
                          const PreviewNetworkState& state);
void previewRenderRadar(const Settings& settings,
                        const PreviewRadarState& state);
void previewRenderFirmware(GfxFirmwareState state, const char* artifact,
                           uint32_t writtenBytes, uint32_t totalBytes,
                           const char* detail = nullptr);

void previewSetNetworkRssi(int rssi);
void previewSetRadarState(const PreviewRadarState& state);
