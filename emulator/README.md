# DeskMate ESP emulator

This is a desktop hardware-resource implementation for the DeskMate firmware.
It runs the real application lifecycle, scheduler, settings, provider clients,
web portal, OTA state machine, display modes, and renderers. There are no
feature-state fixtures or parallel preview implementations.

The emulator substitutes only resources that are physically attached to an
ESP: display transport, clock, Wi-Fi/socket access, flash storage, ADC/PWM,
board limits, reset, and firmware-update storage.

## Requirements

Fedora packages:

```bash
sudo dnf install gcc-c++ cmake libX11-devel openssl-devel
```

ArduinoJson uses the same pinned PlatformIO dependency as firmware. Install the
project dependencies once if `.pio/libdeps/deskmate/ArduinoJson` is absent:

```bash
pio pkg install -e deskmate
```

## Run

```bash
./emulator/run.sh --board esp8266
./emulator/run.sh --board esp32c2
./emulator/run.sh --board esp32
```

The device window is the actual 240×240 RGB565 framebuffer. The real firmware
portal is available at <http://127.0.0.1:8080> and persists configuration under
`emulator/.state/<board>/`.

Useful resource controls:

```bash
./emulator/run.sh --board esp8266 --network offline
./emulator/run.sh --board esp8266 --network ap
./emulator/run.sh --board esp8266 --rssi -82 --ldr 120
./emulator/run.sh --board esp32c2 --web-port 8081
```

`--network sta` is the default and maps the PC connection to an associated
station. `ap` and `offline` exercise device failure/setup behavior without
controlling the host Wi-Fi interface.

For a headless framebuffer capture:

```bash
./emulator/run.sh --headless --duration-ms 1000 \
  --board esp8266 --output deskmate.bmp --scale 1
```

## Automatic rebuild

```bash
./emulator/run.sh --watch --board esp8266
```

The watcher rebuilds and restarts on changes under `src/` or `emulator/` while
retaining virtual flash. It uses `inotifywait` when available and a polling
fallback otherwise.

## Networking and recorded tests

Interactive runs use real desktop TCP/TLS connections. The compatibility client
implements the same `Stream` interface consumed by `HttpRequest`, Weather,
GitHub, Radar, and OTA, so provider parsing remains firmware code.

`--responses DIR` replaces socket bytes with recorded raw provider responses.
It is intended for deterministic tests, not interactive sample screens. The
checked-in radar sequence under `tests/fixtures/` is an ungenerated recording
from the ADS-B provider; the real `RadarClient` parses every snapshot.

## OTA and restart

Manual and GitHub OTA flows write to the selected board profile's bounded
virtual slot. A successful update becomes `virtual-flash.bin` in the state
directory and requests an emulated software restart. The host executable is
never overwritten. `run.sh` relaunches exit status 75 so static firmware state
is reset exactly as it is after a device reboot.

## Verification

```bash
./emulator/test.sh
```

The test builds the full emulator, verifies 5 km/10-point radar retention,
replays recorded real ADS-B responses through `RadarClient`, and boots the real
application under all three board profiles.

The emulator models hardware-visible limits and behavior. It is not a
cycle-accurate CPU, RF, electrical, ST7789 gamma, or general heap-allocation
emulator.
