# DeskMate 4.1 changes

## Display-safe layouts

Every feature is now authored against an 8 px safe inset. The network footer,
status cards, radar telemetry, weather forecast panel, and GitHub contribution
graph all end at or above y=231 on the 240 x 240 panel. This prevents the bottom
row from being clipped by the display viewport.

## Weather

- Larger local time and temperature with equal visual priority.
- Four upcoming forecast timestamps instead of day-based cards.
- Uses OpenWeather's 5 day / 3 hour feed, so the forecast row shows the next
  four available 3-hour points.
- Static modern scene; no cloud/rain/sun animation.

## Network guardian

- Recalculated vertical layout: graph, cards, and footer fit the panel.
- Existing TCP, DNS, availability, outage, Wi-Fi, best/average/peak metrics are
  retained.
- Render callbacks no longer allocate temporary Strings once per tile.

## Aircraft radar

- Removed the sweep animation and its partial-frame updates.
- The PPI scope is static and redraws only when aircraft data or error state
  changes, so a synchronous HTTPS poll no longer looks like a frozen animation.
- Aircraft category/type scaling, heading silhouettes, airports, vectors,
  callsigns, flight levels, range rings, and rim targets remain.
- Direct ADS-B requests use a bounded timeout to reduce long UI stalls.

## GitHub

- Fixed the GraphQL issue/PR connection query by supplying `first: 1` while
  still reading each connection's complete `totalCount`.
- Replaced the large ArduinoJson contribution-calendar document with a small
  streaming parser. The 52-week graph is consumed directly from the HTTP stream
  instead of being expanded into hundreds of JSON objects in ESP8266 heap.
- Added heap/max-block guards and delayed the first request after boot to avoid
  GitHub-mode reset loops.
- Last good data remains visible when a later request fails.

## Version

Firmware version: 4.1.0
