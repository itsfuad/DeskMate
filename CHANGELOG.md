# Changelog

## 4.7.1 — 2026-08-08

- Centralized bounded HTTP/1.0 GET handling for Weather, Radar, and OTA release checks.
- Removed per-request `HTTPClient` URL and header `String` allocations.
- Added fixed response-header validation with timeout, content-length limits, and rejection of unknown/chunked bodies.
- Verified all 39 preview fixtures and produced the ESP8266 OTA binary successfully.
