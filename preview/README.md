# DeskMate desktop preview

This target renders DeskMate's real 240 × 240 C++ screen functions on a Linux desktop. It uses the same tile renderer, RGB565 colors, clipping rules, Adafruit GFX primitives, built-in 5 × 7 font, and screen source files used by the firmware.

The preview replaces only hardware, network, and clock inputs with desktop adapters and deterministic fixtures. It does not recreate the UI in HTML or a second graphics implementation.

## Fedora setup

```bash
sudo dnf install gcc-c++ cmake libX11-devel
```

`libX11-devel` is optional for headless screenshot generation, but required for the interactive window.

## Interactive preview

From the project root, launch the animated clear-weather day cycle. One
simulated 24-hour cycle takes 72 seconds and uses the same C++ renderer as the
firmware:

```bash
./preview/run.sh
```

Open a specific weather cycle or another fixture:

```bash
./preview/run.sh --screen weather-cycle-partly
./preview/run.sh --screen weather-cycle-cloudy
./preview/run.sh --screen weather-cycle-rain
./preview/run.sh --screen github-3m
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
| 1 / 2 / 3 / 4 | Clear / Partly cloudy / Cloudy / Rain animated cycle |
| G | First GitHub fixture |
| N | First Network fixture |
| R | First Radar fixture |
| O | First OTA fixture |
| Space | Pause or resume an animated fixture |
| Up / Down | Step an animated weather cycle forward/back by 30 simulated minutes |
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

For an animated fixture, `--frame-ms` selects the animation position. The
weather cycle begins at 04:30 and advances one simulated minute per 50 ms. For
example, this captures clear weather at 18:30:

```bash
./preview/run.sh --headless \
  --screen weather-cycle-clear \
  --frame-ms 42000 \
  --output weather-evening.bmp \
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

## Weather transition preview

A ready-made animation is included at `preview/weather-cycle-demo.gif`. The interactive preview remains the authoritative version because it renders live and lets you pause or step through the cycle.


The four animated fixtures keep the condition fixed while time moves through
morning, noon, afternoon, evening, and night:

- `weather-cycle-clear`
- `weather-cycle-partly`
- `weather-cycle-cloudy`
- `weather-cycle-rain`

The palette is continuously interpolated around the configured sunrise and
sunset, including dedicated dawn and dusk transition ranges. The same scene
draws the moving sun/moon, moving cloud lanes, precipitation, bridge/skyline,
water reflections, stars, and lights after dark. Panels are rendered as true
per-pixel translucent glass: a faint light wash during bright hours and a faint
dark wash at dusk/night, so they inherit the current backdrop automatically.

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
