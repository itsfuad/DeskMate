# DeskMate desktop preview

This target renders DeskMate's real 240 × 240 C++ screen functions on a Linux desktop. It uses the same tile renderer, RGB565 colors, clipping rules, Adafruit GFX primitives, built-in 5 × 7 font, and screen source files used by the firmware.

The preview replaces only hardware, network, and clock inputs with desktop adapters and deterministic fixtures. It does not recreate the UI in HTML or a second graphics implementation.

## Fedora setup

```bash
sudo dnf install gcc-c++ cmake libX11-devel
```

`libX11-devel` is optional for headless screenshot generation, but required for the interactive window.

## Interactive preview

From the project root:

```bash
./preview/run.sh
```

Open a specific fixture:

```bash
./preview/run.sh --screen github-3m
./preview/run.sh --screen weather-rain
./preview/run.sh --screen ota-0
```

The default scale is 4×, so each display pixel appears as a 4 × 4 desktop block. Choose another integer scale from 1 through 10:

```bash
./preview/run.sh --scale 6
```

### Keyboard controls

| Key | Action |
|---|---|
| Left / Right or J / L | Previous or next fixture |
| W | First Weather fixture |
| G | First GitHub fixture |
| N | First Network fixture |
| R | First Radar fixture |
| O | First OTA fixture |
| P | Toggle the enlarged-pixel grid |
| S | Save the current fixture to `preview-output/` |
| A | Save every fixture to `preview-output/` |
| Q or Escape | Quit |

## Headless screenshots

Render one fixture without opening a window:

```bash
./preview/run.sh --headless \
  --screen weather-clear \
  --output weather-clear.bmp \
  --scale 1
```

Render every fixture:

```bash
./preview/run.sh --all preview-output --scale 1
```

List available fixture IDs:

```bash
./preview/run.sh --list
```

## Automatic rebuild while editing

Install the optional watcher:

```bash
sudo dnf install inotify-tools
```

Then run:

```bash
./preview/watch.sh weather-clear
```

The script rebuilds and reopens the selected fixture whenever a source file changes.

## Verification

Run the native build and render every fixture into a temporary directory:

```bash
./preview/test.sh
```

## Source sharing

The preview directly compiles these firmware files:

- `src/TileRenderer.cpp`
- `src/features/weather/WeatherMode.cpp`
- `src/features/github/GithubMode.cpp`
- `src/features/network/NetworkMode.cpp`
- `src/features/radar/RadarMode.cpp`
- `src/FirmwareUi.h`

`DESKMATE_PREVIEW` excludes ESP-specific API clients and provides mock state, but the actual layout and drawing functions remain shared. Changes to coordinates, colors, typography, panels, icons, and clipping therefore appear in both builds.

## Limits

The desktop output is pixel-accurate before it reaches the physical panel. It cannot reproduce ST7789 gamma, backlight brightness, viewing angle, SPI transfer tearing, electrical behavior, or pauses caused by real ESP8266 TLS calls.
