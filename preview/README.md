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

The interactive preview starts at the device's physical 240×240 size. Drag any window corner to resize it; the square device image scales to fit while preserving its aspect ratio. Use `--scale N` to choose a larger initial window size:

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
./preview/run.sh --watch weather-clear
```

(`./preview/watch.sh weather-clear` remains equivalent.)

The script rebuilds and reopens the selected fixture whenever firmware or preview
source changes. It watches all `src/` files, preview sources, CMake configuration,
and `platformio.ini`; if `inotifywait` is unavailable it uses a portable polling
fallback. Failed rebuilds are retried instead of terminating the watcher.

## Verification

Run the native build and render every fixture into a temporary directory:

```bash
./preview/test.sh
```

## Weather transition preview

A ready-made animation is included at `preview/weather-cycle-demo.gif`. The interactive preview remains the authoritative version because it renders live and lets you pause or step through the cycle.

Regenerate the bundled clear-weather GIF:

```bash
./preview/generate-weather-gif.sh
```

Pass a weather-cycle fixture and output path to generate another condition:

```bash
./preview/generate-weather-gif.sh \
  weather-cycle-rain \
  preview/weather-rain-demo.gif
```

The four animated fixtures keep the condition fixed while time moves through
morning, noon, afternoon, evening, and night:

- `weather-cycle-clear`
- `weather-cycle-partly`
- `weather-cycle-cloudy`
- `weather-cycle-rain`

The palette is continuously interpolated around the configured sunrise and
sunset, including dedicated dawn and dusk transition ranges. A compact 4-bit
terrain map retains the reference GIF's exact mountain, lake, forest,
foreground, and tent silhouette while the runtime recolors it for time and
weather. The right-to-left sun and moon share the same skyline arc, and the
sun's optical-axis flare follows the reference. A separate sun/moon marker
keeps clear weather identifiable at every hour; cloud lanes, precipitation,
and stars remain condition-driven. Two-tone pixel text reverses contrast across
the day/night boundary, while telemetry shares one uniform forecast-card
surface so it cannot cover the lake or become a separate strip.

## Source sharing

The preview directly compiles these firmware files:

- `src/display/TileRenderer.cpp`
- `src/features/weather/WeatherMode.cpp`
- `src/features/weather/WeatherScene.h`
- `src/features/github/GithubMode.cpp`
- `src/features/network/NetworkMode.cpp`
- `src/features/radar/RadarMode.cpp`
- `src/FirmwareUi.h`
- `src/display/SystemUi.h`

`DESKMATE_PREVIEW` excludes ESP-specific API clients and provides mock state, but the actual layout and drawing functions remain shared. Changes to coordinates, colors, typography, panels, icons, and clipping therefore appear in both builds.

Lifecycle fixtures include boot progress, open/password setup mode, system
messages, crash recovery diagnostics, all firmware-update stages, and the
provider/network/radar states. Use `./preview/run.sh --list` to see the complete
set.

## Limits

The desktop output is pixel-accurate before it reaches the physical panel. It cannot reproduce ST7789 gamma, backlight brightness, viewing angle, SPI transfer tearing, electrical behavior, or pauses caused by real ESP8266 TLS calls.
