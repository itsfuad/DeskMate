// OtaUpdate.h — pull the latest release from GitHub and flash it over the air.
//
// Checks the repo's newest release via the GitHub API, compares its tag to
// FW_VERSION, and (on request) streams the target's firmware asset (UPDATE_ASSET
// in config.h) straight into the OTA partition — ESP8266httpUpdate on the
// ESP8266, HTTPUpdate on the ESP32 targets. The write is atomic: a failed
// download leaves the running firmware untouched, so this cannot brick the device.
//
// HTTPS on the ESP8266 is RAM-tight, so the firmware uses the shared low-RAM
// TLS buffer policy. Some GitHub responses may fail if their TLS records are
// larger than that buffer; manual OTA upload remains the fallback.
#pragma once
#include <Arduino.h>
#include "Settings.h"

struct OtaLatest {
  bool   ok = false;      // the check itself succeeded
  bool   newer = false;   // the latest release is newer than FW_VERSION
  String tag;             // e.g. "v2.1.0"
  String url;             // firmware asset download URL
  String error;           // human-readable reason when ok == false
};

OtaLatest otaCheckLatest(const Settings& s);   // query the GitHub API (blocking)

// Run the update (blocking). Returns "" once the download starts and the device is
// about to reboot into the new image; otherwise a human-readable error.
// ESP32 targets only — the ESP8266 uses the boot-update flow below.
String otaUpdateFromGitHub(const Settings& s);

// Update-at-boot flow (real on the ESP8266; no-op stubs on the ESP32 targets,
// which update directly). The request and the failure message live in LittleFS
// so they survive the reboot.
bool   otaBootRequested();                     // a boot update is queued
bool   otaRequestBootUpdate(const char* tag);  // queue it (false if storage write failed)
void   otaBootUpdate(const Settings& s);       // consume the request; reboots on success
String otaTakeBootResult();                    // last boot attempt's error, "" if none
