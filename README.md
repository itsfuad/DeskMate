# DeskTV for ESP8266 SmallTV / SD Pro

A compact 240x240 desk dashboard firmware based on `smalltv-mod`.

## Modes

- Ambient weather: Open-Meteo current conditions, animated scene, hourly temperature curve
- Network guardian: TCP reachability, latency and ten-minute history
- Aircraft radar: nearby ADS-B targets, heading triangles, vectors, altitude labels, airports and nearest-target highlighting
- GitHub status: latest commit, open issues, open pull requests and five-week commit activity tiles
- Carousel: rotates through any selected modes

All active screens use a static 40x40 RGB565 tile backbuffer. Each completed tile replaces the corresponding LCD area without first clearing the visible screen.

## Configure

Open the device IP or `http://<hostname>.local`. The minimal web UI controls all mode settings, Wi-Fi, display, carousel, refresh, reboot and OTA upload.

## Build

```bash
pio run -e smalltv
```

Firmware output:

```text
.pio/build/smalltv/firmware.bin
```

Upload the binary from the System tab in the web UI.

## Data sources

- Weather: Open-Meteo
- Radar: adsb.fi direct endpoint or a configurable webhook/proxy
- GitHub: GitHub REST API; an optional token raises rate limits
