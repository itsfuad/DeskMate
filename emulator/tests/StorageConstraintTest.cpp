#include "EmulatorPlatform.h"
#include "LittleFS.h"
#include "Settings.h"
#include "CrashBreadcrumbs.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

void configure(const char* directory, uint32_t capacity) {
  EmulatorConstraints constraints;
  constraints.filesystemBytes = capacity;
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    directory, 0, "", constraints);
}

void runFileConstraints(const char* directory) {
  configure(directory, 8);
  check(LittleFS.begin(), "filesystem begins");
  const uint8_t bytes[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
  File first = LittleFS.open("/first", "w");
  File second = LittleFS.open("/second", "w");
  check(first.write(bytes, 3) == 3, "first open writer consumes capacity");
  check(second.write(bytes, sizeof(bytes)) == 5, "second writer returns a short write");
  check(emulatorFsUsedBytes() == 8, "open files count toward capacity");
  check(first.write(bytes[0]) == 0, "full disk rejects single-byte writes");
  File alias = second;
  second.close();
  second.close();
  check(!second && !alias, "close invalidates shared handles and is idempotent");
  check(second.write(bytes, 1) == 0 && alias.write(bytes[0]) == 0,
        "closed writable handles reject writes");
  check(second.available() == 0 && second.read() == -1 && second.peek() == -1 &&
            second.readString().length() == 0 && second.size() == 0,
        "closed file has no readable content");
  first.close();

  File reader = LittleFS.open("/second", "r");
  check(reader && reader.size() == 5 && reader.peek() == 'a' && reader.read() == 'a',
        "short write persists its accepted prefix");
  check(reader.readString() == "bcde", "reopened file contains only accepted bytes");
  reader.close();
  check(!reader && reader.available() == 0 && reader.read() == -1 && reader.peek() == -1,
        "read-only close invalidates the handle");

  check(LittleFS.rename("/second", "/first"), "rename replaces destination on full disk");
  check(!LittleFS.exists("/second") && emulatorFsUsedBytes() == 5,
        "rename releases replaced file capacity");
  check(!LittleFS.rename("/missing", "/first") && LittleFS.exists("/first"),
        "failed rename preserves destination");
  {
    File append = LittleFS.open("/first", "a");
    check(append.write(bytes, sizeof(bytes)) == 3, "append respects remaining capacity");
  }
  reader = LittleFS.open("/first", "r");
  check(reader.readString() == "abcdeabc", "destructor closes and persists append");
  reader.close();
  {
    File truncate = LittleFS.open("/first", "w");
    check(truncate && emulatorFsUsedBytes() == 0, "write open immediately truncates");
    check(truncate.write(bytes, sizeof(bytes)) == 8, "truncate reclaims write capacity");
  }
  check(LittleFS.remove("/first") && emulatorFsUsedBytes() == 0,
        "remove reclaims capacity");
}

void runMemoryChecks(const char* directory) {
  for (uint32_t heap : {13119U, 13120U}) {
    for (uint32_t block : {6199U, 6200U}) {
      EmulatorConstraints limits;
      limits.freeHeapBytes = heap;
      limits.maximumBlockBytes = block;
      emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                        directory, 0, "", limits);
      check(platformTlsMemoryReady() == (heap == 13120 && block == 6200),
            "TLS below/at heap and block boundaries");
    }
  }
  for (uint32_t heap : {4095U, 4096U}) {
    for (uint32_t block : {2047U, 2048U}) {
      EmulatorConstraints limits;
      limits.freeHeapBytes = heap;
      limits.maximumBlockBytes = block;
      emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                        directory, 0, "", limits);
      check(platformPageMemoryReady() == (heap == 4096 && block == 2048),
            "page below/at heap and block boundaries");
    }
  }
  for (uint32_t heap : {8191U, 8192U}) {
    for (uint32_t block : {1023U, 1024U}) {
      for (uint32_t stack : {1023U, 1024U}) {
        EmulatorConstraints limits;
        limits.freeHeapBytes = heap;
        limits.maximumBlockBytes = block;
        limits.freeStackBytes = stack;
        emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                          directory, 0, "", limits);
        check(platformMutationMemoryReady() ==
                  (heap == 8192 && block == 1024 && stack == 1024),
              "mutation below/at heap, block and stack boundaries");
        emulatorConfigure(EmulatorBoard::Esp32, EmulatorNetwork::Sta, -56, 640,
                          directory, 0, "", limits);
        check(platformTlsMemoryReady() && platformMutationMemoryReady(),
              "ESP32 does not inherit ESP8266 admission guards");
      }
    }
  }

  Settings base, output;
  base.setDefaults();
  output.setDefaults();
  output.hostname = "unchanged";
  SettingsJsonPresence presence;
  JsonScanner::Error error = JsonScanner::Error::None;
  const char json[] = "{\"hostname\":\"replacement\"}";
  emulatorFailNothrowAfter(0);
  check(!settingsParseJson(output, base, json, sizeof(json) - 1, &presence, &error) &&
            error == JsonScanner::Error::OutOfMemory && output.hostname == "unchanged" &&
            !presence.hostname,
        "real Settings ParseContext allocation failure is transactional");
  check(settingsParseJson(output, base, json, sizeof(json) - 1) &&
            output.hostname == "replacement", "parser recovers after allocation failure");

  String text("prefix");
  const std::string large(4096, 'x');
  emulatorFailStringGrowthAfter(0);
  check(!text.reserve(4096) && text == "prefix", "failed reserve preserves String");
  emulatorFailStringGrowthAfter(0);
  check(!text.concat(large.c_str()) && text == "prefix", "failed concat preserves String");
  emulatorFailStringGrowthAfter(0);
  text = large.c_str();
  check(text.length() == 0, "failed assignment invalidates String content");
  check(text.concat(large.c_str()) && text.length() == large.size(),
        "String recovers after growth failure");

  CrashBreadcrumbs record;
  check(!record.valid(), "unsealed RTC record rejected");
  for (uint32_t i = 0; i < 12; ++i) record.add(5, i, UINT32_MAX - i, 12000, 6000, 2000);
  check(record.valid() && record.entries[(record.count - 1) % 8].detail == 11,
        "RTC ring wraps and seals");
  CrashBreadcrumbs restored = record;
  check(restored.valid(), "reset-copy RTC record validates");
  restored.entries[0].heap ^= 1;
  check(!restored.valid(), "RTC checksum rejects corruption");
  restored = record;
  restored.version = 2;
  restored.checksum = restored.hash();
  check(!restored.valid(), "RTC rejects unsupported version even with valid checksum");
}

void runSettingsRollback(const char* directory) {
  configure(directory, 65536);
  Settings original;
  original.setDefaults();
  original.hostname = "persisted-config";
  check(saveSettings(original), "initial transactional settings save succeeds");
  File reader = LittleFS.open("/config.json", "r");
  const String persisted = reader.readString();
  const uint32_t configBytes = static_cast<uint32_t>(reader.size());
  reader.close();
  check(configBytes > 32, "saved settings have content");

  configure(directory, configBytes + 32);
  {
    File filler = LittleFS.open("/filler", "w");
    const std::string bytes(32, 'x');
    check(filler.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) == 32,
          "filler reaches exact capacity");
  }
  Settings changed = original;
  changed.hostname = "replacement-config";
  check(!saveSettings(changed), "full disk settings save fails");
  check(!LittleFS.exists("/config.json.tmp"), "full disk failure removes temp file");
  check(LittleFS.remove("/filler"), "release a partial transaction budget");
  check(!saveSettings(changed), "partially written settings transaction fails");
  check(!LittleFS.exists("/config.json.tmp") && emulatorFsUsedBytes() == configBytes,
        "partial transaction rolls back temp file usage");
  reader = LittleFS.open("/config.json", "r");
  check(reader.readString() == persisted, "failed saves preserve config bytes exactly");
  reader.close();

  configure(directory, configBytes + 32);
  check(LittleFS.begin(), "persisted filesystem reopens");
  Settings loaded;
  check(loadSettings(loaded) && loaded.hostname == original.hostname,
        "reopened persisted config survives full disk rollback");
  configure(directory, 65536);
  check(saveSettings(changed), "save succeeds after increasing capacity");
  check(loadSettings(loaded) && loaded.hostname == changed.hostname &&
            !LittleFS.exists("/config.json.tmp"),
        "successful rename persists replacement config without temp file");
}
}

int main() {
  char directory[] = "/tmp/deskmate-storage-test-XXXXXX";
  if (!::mkdtemp(directory)) {
    std::perror("mkdtemp");
    return 2;
  }
  runMemoryChecks(directory);
  runFileConstraints(directory);
  runSettingsRollback(directory);
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  check(!error, "temporary test state is removed");
  if (failures) {
    std::fprintf(stderr, "%d storage constraint check(s) failed\n", failures);
    return 1;
  }
  std::puts("Storage constraint checks passed");
  return 0;
}
