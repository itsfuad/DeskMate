// OtaUpdate.h — pull the latest release from GitHub and flash it over the air.
//
// Checks the repo's newest release via the GitHub API, compares its tag to
// FW_VERSION, and (on request) streams the release's firmware asset straight into
// the OTA partition with ESP8266httpUpdate. The write is atomic: a failed download
// leaves the running firmware untouched, so this cannot brick the device.
//
// Caveat: HTTPS on the ESP8266 is RAM-tight. GitHub's asset CDN can send large TLS
// records; if it does not negotiate a small fragment length, the download needs a
// big BearSSL buffer that may not fit. Manual OTA (upload the .bin in the Update
// tab) stays as the fallback.
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
String otaUpdateFromGitHub(const Settings& s);
