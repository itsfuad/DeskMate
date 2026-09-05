#include "EmulatorPlatform.h"
#include "LittleFS.h"
#include "Settings.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

class StringPrint : public Print {
 public:
  size_t write(uint8_t value) override {
    text.push_back(static_cast<char>(value));
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    text.append(reinterpret_cast<const char*>(data), size);
    return size;
  }
  std::string text;
};

class StringStream : public Stream {
 public:
  explicit StringStream(const std::string& value) : value_(value) {}
  int available() override { return static_cast<int>(value_.size() - offset_); }
  int read() override {
    return offset_ < value_.size() ? static_cast<uint8_t>(value_[offset_++]) : -1;
  }
  int peek() override {
    return offset_ < value_.size() ? static_cast<uint8_t>(value_[offset_]) : -1;
  }
  size_t write(uint8_t) override { return 0; }

 private:
  std::string value_;
  size_t offset_ = 0;
};

int failures = 0;

void check(bool condition, const char* what) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

Settings baseSettings() {
  Settings settings;
  settings.setDefaults();
  settings.hostname = "base-host";
  settings.wifiCount = 2;
  settings.wifi[0].ssid = "home";
  settings.wifi[0].pass = "home-secret";
  settings.wifi[1].ssid = "office";
  settings.wifi[1].pass = "office-secret";
  settings.apPass = "ap-secret";
  settings.weather.apiKey = "weather-secret";
  settings.github.token = "github-secret";
  return settings;
}

void runPartialAndPresence() {
  const Settings base = baseSettings();
  Settings parsed;
  SettingsJsonPresence presence;
  const char* json =
      R"({"brightness":150,"autoBrightness":true,"mode":"radar","rotation":7,"carouselWeather":false,"carouselNetwork":false,"carouselRadar":false,"carouselGithub":false,"clock":{"nightStart":"bad","nightLevel":120},"weather":{"city":"Paris","timezone":"Europe/Paris","timezoneAbbr":"CET","utcOffsetSec":3600,"apiKey":""},"github":{"pageInbox":false,"pagePulls":false,"pagePulse":false}})";
  check(settingsParseJson(parsed, base, json, std::strlen(json), &presence),
        "partial buffer parses");
  check(parsed.hostname == "base-host", "missing root field inherits base");
  check(parsed.brightness == 100 && parsed.autoBrightness,
        "display values retain bounds behavior");
  check(parsed.mode == MODE_RADAR && parsed.rotation == 3,
        "mode and rotation apply");
  check(parsed.carouselWeather,
        "empty carousel selection falls back to weather");
  check(parsed.clock.nightStartMin == base.clock.nightStartMin &&
            parsed.clock.nightLevel == 100,
        "clock invalid/default and bounds behavior is preserved");
  check(parsed.weather.apiKey == "weather-secret",
        "empty weather secret retains base");
  check(parsed.clock.tz == "Europe/Paris" && parsed.clock.tzAbbr == "CET" &&
            parsed.clock.utcOffsetSec == 3600 && parsed.clock.tzPosix == "",
        "weather updates clock defaults");
  check(parsed.github.pageInbox && parsed.github.pagePulls && parsed.github.pagePulse,
        "empty GitHub page selection falls back to all pages");
  check(presence.brightness && presence.brightnessValue &&
            presence.autoBrightness && presence.mode && presence.rotation &&
            presence.clock && presence.weather && !presence.githubData &&
            presence.githubDisplay && presence.carouselSelection,
        "aggregate WebPortal presence is reported");
  check(presence.clockNightStart && presence.clockNightLevel &&
            presence.weatherCity && presence.weatherTimezone &&
            presence.weatherApiKey,
        "validation field presence is reported");
  check(!presence.brightnessValueValid && !presence.rotationValid &&
            !presence.clockNightStartValid && !presence.clockNightLevelValid &&
            !presence.carouselSelectionValid,
        "normalized request values retain validation status");
}

void runSectionOrderingAndStream() {
  const Settings base = baseSettings();
  const std::string json =
      R"({"clock":{"tz":"Manual","utcOffsetSec":7200},"weather":{"timezone":"Weather","timezoneAbbr":"W","utcOffsetSec":3600},"network":{"probePort":0},"radar":{"pollSec":1},"github":{"token":"","pollSec":99999}})";
  StringStream stream(json);
  Settings parsed;
  SettingsJsonPresence presence;
  check(settingsParseJson(parsed, base, stream, static_cast<int>(json.size()),
                          json.size(), &presence),
        "stream patch parses");
  check(parsed.clock.tz == "Manual" && parsed.clock.tzAbbr == "W" &&
            parsed.clock.utcOffsetSec == 7200,
        "explicit clock values override weather regardless of JSON order");
  check(parsed.network.probePort == 1 && parsed.radar.pollSec == 3 &&
            parsed.github.pollSec == 3600,
        "nested numeric bounds are preserved");
  check(parsed.github.token == "github-secret",
        "empty GitHub secret retains base");
  check(presence.network && presence.radar && presence.githubData &&
            !presence.githubDisplay && presence.githubPollSec,
        "feature data presence is reported");
  check(!presence.networkProbePortValid && !presence.radarPollSecValid &&
            !presence.githubPollSecValid,
        "nested bounds remain available to request validation");
}

void runWifiAndLegacy() {
  const Settings base = baseSettings();
  Settings parsed;
  const char* wifi =
      R"({"staSsid":"ignored","staPass":"ignored-pass","wifi":[{"ssid":"office","pass":""},{"ssid":"new","pass":"new-secret"},{"ssid":""}]})";
  check(settingsParseJson(parsed, base, wifi, std::strlen(wifi)),
        "Wi-Fi array parses");
  check(parsed.wifiCount == 2 && parsed.wifi[0].ssid == "office" &&
            parsed.wifi[0].pass == "office-secret" &&
            parsed.wifi[1].ssid == "new" && parsed.wifi[1].pass == "new-secret",
        "Wi-Fi array replaces list and retains matching secrets");

  const char* legacy = R"({"staSsid":"legacy","staPass":""})";
  check(settingsParseJson(parsed, base, legacy, std::strlen(legacy)),
        "legacy station fields parse");
  check(parsed.wifiCount == 2 && parsed.wifi[0].ssid == "legacy" &&
            parsed.wifi[0].pass == "home-secret",
        "legacy station fields preserve count and empty secret behavior");
}

void runInvalidContainerShapes() {
  const Settings base = baseSettings();
  Settings parsed;
  SettingsJsonPresence presence;
  const char* wifi = R"({"wifi":[[]]})";
  check(settingsParseJson(parsed, base, wifi, std::strlen(wifi), &presence) &&
            presence.wifi && !presence.wifiEntriesValid,
        "nested Wi-Fi array is marked invalid");

  const char* airports = R"({"radar":{"airports":[[]]}})";
  check(settingsParseJson(parsed, base, airports, std::strlen(airports), &presence) &&
            presence.radarAirports && !presence.radarAirportsValid,
        "nested airport array is marked invalid");
}

void runTransactionalFailure() {
  Settings output = baseSettings();
  output.hostname = "unchanged";
  SettingsJsonPresence presence;
  presence.mode = true;
  JsonScanner::Error error = JsonScanner::Error::None;
  const char* invalid = R"({"hostname":"changed")";
  check(!settingsParseJson(output, baseSettings(), invalid, std::strlen(invalid),
                           &presence, &error),
        "truncated document fails");
  check(output.hostname == "unchanged" && presence.mode,
        "failed parse changes neither settings nor presence");
  check(error == JsonScanner::Error::UnexpectedEnd,
        "scanner error is returned");

  const char* array = "[]";
  check(!settingsParseJson(output, baseSettings(), array, std::strlen(array),
                           nullptr, &error) &&
            error == JsonScanner::Error::InvalidSyntax,
        "settings root must be an object");
}

void runPersistenceAndMigration() {
  check(LittleFS.format() && LittleFS.begin(), "settings filesystem initializes");
  const Settings source = baseSettings();
  check(saveSettings(source), "saveSettings writes configuration");
  Settings loaded;
  check(loadSettings(loaded), "loadSettings reads configuration");
  check(loaded.hostname == source.hostname && loaded.wifiCount == source.wifiCount &&
            loaded.wifi[0].pass == source.wifi[0].pass &&
            loaded.weather.apiKey == source.weather.apiKey &&
            loaded.github.token == source.github.token,
        "save/load preserves settings and secrets");

  std::ifstream fixture("emulator/tests/fixtures/legacy-config.json",
                        std::ios::binary);
  const std::string legacy((std::istreambuf_iterator<char>(fixture)),
                           std::istreambuf_iterator<char>());
  check(!legacy.empty(), "legacy settings fixture is readable");
  File config = LittleFS.open("/config.json", "w");
  check(config && config.write(reinterpret_cast<const uint8_t*>(legacy.data()),
                               legacy.size()) == legacy.size(),
        "legacy fixture is installed");
  config.close();

  Settings migrated;
  check(loadSettings(migrated), "legacy configuration loads");
  check(migrated.apSsid == DEFAULT_AP_SSID &&
            migrated.weather.apiKey == "fixture-key" &&
            migrated.radar.airportCount == 1 &&
            !std::strcmp(migrated.radar.airports[0].icao, "VGHS"),
        "legacy AP identity migrates without losing schema data");
  File saved = LittleFS.open("/config.json", "r");
  const String savedText = saved.readString();
  saved.close();
  check(savedText.indexOf("SmallTV-Setup") < 0 &&
            savedText.indexOf("DeskMate-Setup") >= 0,
        "migration is persisted through JsonWriter");
}

void runWriterRoundTrips() {
  const Settings source = baseSettings();
  StringPrint fullOutput;
  JsonWriter fullWriter(fullOutput);
  check(settingsWriteJson(fullWriter, source, true), "full settings write succeeds");
  check(fullOutput.text.find("\"staPass\":\"home-secret\"") != std::string::npos &&
            fullOutput.text.find("\"apiKey\":\"weather-secret\"") != std::string::npos &&
            fullOutput.text.find("\"token\":\"github-secret\"") != std::string::npos,
        "full settings output includes secrets and legacy station fields");

  Settings roundTrip;
  SettingsJsonPresence presence;
  check(settingsParseJson(roundTrip, Settings{}, fullOutput.text.data(),
                          fullOutput.text.size(), &presence),
        "written settings parse back");
  check(roundTrip.hostname == source.hostname && roundTrip.wifiCount == 2 &&
            roundTrip.wifi[0].pass == "home-secret" &&
            roundTrip.weather.apiKey == "weather-secret" &&
            roundTrip.github.token == "github-secret",
        "full settings round trip preserves values");
  check(presence.configVersion && presence.configVersionValue == 1,
        "config version is returned for migration");

  StringPrint publicOutput;
  JsonWriter publicWriter(publicOutput);
  check(settingsWriteJson(publicWriter, source, false),
        "redacted settings write succeeds");
  check(publicOutput.text.find("\"pass\":") == std::string::npos &&
            publicOutput.text.find("\"staPass\":") == std::string::npos &&
            publicOutput.text.find("\"apPass\":") == std::string::npos &&
            publicOutput.text.find("\"apiKey\":") == std::string::npos &&
            publicOutput.text.find("\"token\":") == std::string::npos &&
            publicOutput.text.find("\"passSet\":true") != std::string::npos &&
            publicOutput.text.find("\"apiKeySet\":true") != std::string::npos &&
            publicOutput.text.find("\"tokenSet\":true") != std::string::npos,
        "redacted output hides secrets but preserves set markers");

  StringPrint extendedOutput;
  JsonWriter extendedWriter(extendedOutput);
  check(extendedWriter.beginObject() &&
            settingsWriteJsonFields(extendedWriter, source, false) &&
            extendedWriter.key("chip") && extendedWriter.value("emulator") &&
            extendedWriter.endObject() && extendedWriter.complete(),
        "settings fields compose with WebPortal response fields");

  Settings retained;
  check(settingsParseJson(retained, source, publicOutput.text.data(),
                          publicOutput.text.size()),
        "redacted output parses over a secret base");
  check(retained.wifi[0].pass == "home-secret" &&
            retained.weather.apiKey == "weather-secret" &&
            retained.github.token == "github-secret",
        "redacted round trip retains supplied secrets");
}

}  // namespace

int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    "/tmp/deskmate-settings-state", 0, ".");
  runPartialAndPresence();
  runSectionOrderingAndStream();
  runWifiAndLegacy();
  runInvalidContainerShapes();
  runTransactionalFailure();
  runWriterRoundTrips();
  runPersistenceAndMigration();

  if (failures) {
    std::fprintf(stderr, "%d settings check(s) failed\n", failures);
    return 1;
  }
  std::puts("Settings tests passed");
  return 0;
}
