# DeskMate 4.0

## Product rename

All firmware branding, default AP/hostname, PlatformIO environments, compile-time macros, OTA asset names, web UI labels, configuration export name and documentation were renamed to **DeskMate**.

Primary build:

```bash
pio run -e deskmate
```

## Display redesign

- **Weather** replaces the animated primitive scene with a static, condition-aware modern layout and a four-day forecast card.
- **Network Guardian** measures TCP latency, DNS resolution, availability, outage history and Wi-Fi quality in a compact diagnostics dashboard.
- **Radar** uses a full-screen PPI scope with no overlapping top bar, a rotating sweep, richer aircraft silhouettes, speed vectors, flight labels and category-based size scaling.
- **GitHub** uses the authenticated user from a personal access token rather than one repository. It shows current-year commits, open issues, open pull requests, current streak, weekly activity and a complete 52-week contribution graph.

## API changes

- Weather moved from Open-Meteo to the OpenWeather Current Weather and 5 Day / 3 Hour Forecast endpoints. The API key, coordinates, display label, units and refresh interval are configurable from the web UI.
- GitHub moved from repository REST calls to GraphQL `viewer` data. Open issue/PR totals come from the authenticated user's connections, and contributions come from the user's contribution collections.
- GitHub and OpenWeather requests force HTTP/1.0 responses before streaming JSON to avoid chunked-response truncation on ESP8266. GitHub also retries one transient TLS/stream failure.

## Rendering

All modes use the fixed 3.2 KiB 40 × 40 RGB565 tile compositor. Each final tile is pushed over the retained LCD contents without a visible full-screen clear. The radar sweep marks and redraws only the tiles intersecting the old and new sweep trails.

## Web UI

- Compact embedded single-page control panel with no external assets.
- Separate Display, Weather, Network, Radar, GitHub and System sections.
- Secret fields never return stored API keys or tokens to the browser; leaving them blank preserves the stored value.
- Live status, manual data refresh, reboot and OTA upload remain available.
