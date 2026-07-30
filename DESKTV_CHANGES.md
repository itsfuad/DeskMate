# DeskTV 3.0

This fork removes the ticker and Claude usage features and ships four desk modes:

1. Ambient weather via Open-Meteo
2. Network guardian using a TCP reachability/latency probe
3. Improved aircraft radar using the existing adsb.fi/webhook client
4. GitHub status with latest commit, open issue/PR counts, and 5-week commit-activity tiles

All display modes render through the static 40x40 RGB565 tile backbuffer. The physical LCD is never cleared before a new frame is ready.

The web UI was replaced with a compact 8.6 KB single-page configuration interface. It configures all four modes, carousel selection, Wi-Fi, brightness, rotation, reboot, refresh, and OTA upload.

Build:

```bash
pio run -e smalltv
```

OTA artifact:

```text
.pio/build/smalltv/firmware.bin
```


## 3.0.1 build fixes

- Fixed the ESP8266 `millis()`/`std::min` type mismatch in Network Guardian.
- Removed the remaining dead ticker/usage portal and mDNS code.
- Increased timestamp and flight-level formatting buffers.
- Removed obsolete ticker/usage build flags and constants.
