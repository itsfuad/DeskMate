# Graph Report - /home/itsfuad/Dev/Arduino/ESP8266/DeskMonitor/Deskmate  (2026-08-01)

## Corpus Check
- cluster-only mode — file stats not available

## Summary
- 885 nodes · 1604 edges · 60 communities (45 shown, 15 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 145 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `a8f9c885`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_GitHub Contribution Data|GitHub Contribution Data]]
- [[_COMMUNITY_OTA Update Logic|OTA Update Logic]]
- [[_COMMUNITY_GFX Drawing Primitives|GFX Drawing Primitives]]
- [[_COMMUNITY_Firmware Preview Scenarios|Firmware Preview Scenarios]]
- [[_COMMUNITY_Radar Rendering Logic|Radar Rendering Logic]]
- [[_COMMUNITY_Async Job Management|Async Job Management]]
- [[_COMMUNITY_Network Status Rendering|Network Status Rendering]]
- [[_COMMUNITY_Desktop Simulator Main|Desktop Simulator Main]]
- [[_COMMUNITY_Hardware Clock Management|Hardware Clock Management]]
- [[_COMMUNITY_Application Lifecycle Management|Application Lifecycle Management]]
- [[_COMMUNITY_Weather API Client|Weather API Client]]
- [[_COMMUNITY_Weather Data Models|Weather Data Models]]
- [[_COMMUNITY_Weather Theme Configuration|Weather Theme Configuration]]
- [[_COMMUNITY_Radar Data Fetching|Radar Data Fetching]]
- [[_COMMUNITY_Preview Framebuffer Management|Preview Framebuffer Management]]
- [[_COMMUNITY_UI Component Rendering|UI Component Rendering]]
- [[_COMMUNITY_Graphics Initialization|Graphics Initialization]]
- [[_COMMUNITY_Tiled Rendering Engine|Tiled Rendering Engine]]
- [[_COMMUNITY_Platform Hardware Abstraction|Platform Hardware Abstraction]]
- [[_COMMUNITY_Arduino String Utilities|Arduino String Utilities]]
- [[_COMMUNITY_Clock Preview Logic|Clock Preview Logic]]
- [[_COMMUNITY_Weather Icon Rendering|Weather Icon Rendering]]
- [[_COMMUNITY_Atmospheric Color Calculations|Atmospheric Color Calculations]]
- [[_COMMUNITY_Polling Scheduler|Polling Scheduler]]
- [[_COMMUNITY_Network Preview Logic|Network Preview Logic]]
- [[_COMMUNITY_Adafruit GFX Core|Adafruit GFX Core]]
- [[_COMMUNITY_Project Component Overview|Project Component Overview]]
- [[_COMMUNITY_Radar Preview Logic|Radar Preview Logic]]
- [[_COMMUNITY_Sky and Scene Rendering|Sky and Scene Rendering]]
- [[_COMMUNITY_Time-based Color Palettes|Time-based Color Palettes]]
- [[_COMMUNITY_Firmware Boot Context|Firmware Boot Context]]
- [[_COMMUNITY_Display Driver Setup|Display Driver Setup]]
- [[_COMMUNITY_System Status Rendering|System Status Rendering]]
- [[_COMMUNITY_Tile Masking Utilities|Tile Masking Utilities]]
- [[_COMMUNITY_Project Documentation|Project Documentation]]
- [[_COMMUNITY_Access Point Context|Access Point Context]]
- [[_COMMUNITY_Crash Debugging Context|Crash Debugging Context]]
- [[_COMMUNITY_System Message Context|System Message Context]]
- [[_COMMUNITY_BearSSL Configuration|BearSSL Configuration]]
- [[_COMMUNITY_GitHub Mode Definition|GitHub Mode Definition]]
- [[_COMMUNITY_I2C Device Interface|I2C Device Interface]]
- [[_COMMUNITY_SPI Device Interface|SPI Device Interface]]
- [[_COMMUNITY_Network Mode Definition|Network Mode Definition]]
- [[_COMMUNITY_Radar Mode Definition|Radar Mode Definition]]
- [[_COMMUNITY_Weather Mode Definition|Weather Mode Definition]]
- [[_COMMUNITY_Firmware UI Namespace|Firmware UI Namespace]]
- [[_COMMUNITY_Status Heartbeat Logic|Status Heartbeat Logic]]
- [[_COMMUNITY_Build Shell Script|Build Shell Script]]
- [[_COMMUNITY_Run Shell Script|Run Shell Script]]
- [[_COMMUNITY_Test Shell Script|Test Shell Script]]
- [[_COMMUNITY_Watch Shell Script|Watch Shell Script]]
- [[_COMMUNITY_UI Documentation Headers|UI Documentation Headers]]
- [[_COMMUNITY_Graphics Device Instance|Graphics Device Instance]]

## God Nodes (most connected - your core abstractions)
1. `millis()` - 39 edges
2. `WeatherTheme` - 32 edges
3. `GithubData` - 22 edges
4. `WeatherData` - 22 edges
5. `startWrite()` - 18 edges
6. `endWrite()` - 18 edges
7. `Settings` - 16 edges
8. `Settings` - 15 edges
9. `TileCanvas` - 15 edges
10. `gfxRenderTiled()` - 14 edges

## Surprising Connections (you probably didn't know these)
- `onNtpSync()` --calls--> `millis()`  [INFERRED]
  src/Clock.cpp → preview/src/ArduinoCompat.cpp
- `loop()` --calls--> `yield()`  [INFERRED]
  src/loader.cpp → preview/src/ArduinoCompat.cpp
- `previewRenderNetwork()` --calls--> `previewSetNetworkRssi()`  [INFERRED]
  src/features/network/NetworkMode.cpp → preview/src/NetPreview.cpp
- `Preview CMake Configuration` --references--> `GitHub Mode Renderer`  [EXTRACTED]
  preview/CMakeLists.txt → src/features/github/GithubMode.cpp
- `Preview CMake Configuration` --references--> `Network Mode Renderer`  [EXTRACTED]
  preview/CMakeLists.txt → src/features/network/NetworkMode.cpp

## Import Cycles
- None detected.

## Communities (60 total, 15 thin omitted)

### Community 0 - "GitHub Contribution Data"
Cohesion: 0.06
Nodes (57): advanceCalendarWeek(), begin(), calculateDerived(), CalendarBuilder, lastWeekday, pendingCount, pendingWeekday, sawDay (+49 more)

### Community 1 - "OTA Update Logic"
Cohesion: 0.06
Nodes (58): JsonDocument, JsonObject, OtaLatest, delay(), loop(), setup(), setup(), Settings (+50 more)

### Community 2 - "GFX Drawing Primitives"
Cohesion: 0.08
Nodes (52): charBounds(), drawBitmap(), drawButton(), drawChar(), drawCircle(), drawCircleHelper(), drawEllipse(), drawFastHLine() (+44 more)

### Community 3 - "Firmware Preview Scenarios"
Cohesion: 0.08
Nodes (55): previewRenderGithub(), previewRenderNetwork(), GfxFirmwareState, Aircraft, PreviewGithubState, PreviewNetworkState, PreviewRadarState, PreviewWeatherState (+47 more)

### Community 4 - "Radar Rendering Logic"
Cohesion: 0.07
Nodes (46): begin(), categoryScale(), displayTick(), drawAircraft(), drawRadar(), drawRadarHeartbeat(), drawRadarLedRegion(), geo() (+38 more)

### Community 5 - "Async Job Management"
Cohesion: 0.08
Nodes (38): async, budget, catch, dBm, failed, function, lat, lon (+30 more)

### Community 6 - "Network Status Rendering"
Cohesion: 0.09
Nodes (35): begin(), displayTick(), drawCardValue(), drawNetwork(), drawNetworkHeartbeat(), drawNetworkLedRegion(), invalidate(), NetworkLedContext (+27 more)

### Community 7 - "Desktop Simulator Main"
Cohesion: 0.11
Nodes (31): Display, path, string, string, PreviewScenario, previewSetMillis(), drawFramebufferToImage(), firstScenarioWithPrefix() (+23 more)

### Community 8 - "Hardware Clock Management"
Cohesion: 0.11
Nodes (21): clockBegin(), clockForceResync(), clockFormatTime(), clockNow(), clockReapply(), clockService(), clockTimeStr(), clockTrusted() (+13 more)

### Community 9 - "Application Lifecycle Management"
Cohesion: 0.11
Nodes (28): activeMode(), appApplyBrightness(), appEffectiveBrightness(), appForceRefresh(), appInvalidate(), appPollAverageDuration(), appPollCoalesced(), appPollCompleted() (+20 more)

### Community 10 - "Weather API Client"
Cohesion: 0.11
Nodes (27): HTTPClient, SecureClient, PollResult, Settings, String, unique_ptr, begin(), beginGet() (+19 more)

### Community 11 - "Weather Data Models"
Cohesion: 0.08
Nodes (26): ForecastPoint, id, night, stamp, temp, valid, WeatherData, city (+18 more)

### Community 12 - "Weather Theme Configuration"
Cohesion: 0.09
Nodes (22): WeatherTheme, cloudLevel, cloudLight, cloudShade, far, lightColor, near, nightAmount (+14 more)

### Community 13 - "Radar Data Fetching"
Cohesion: 0.21
Nodes (16): buildDirectUrl(), buildWebhookUrl(), fetchUrl(), geo(), insertNearest(), parseAdsb(), probeTls(), radarInit() (+8 more)

### Community 14 - "Preview Framebuffer Management"
Cohesion: 0.14
Nodes (12): FILE, PreviewFramebuffer(), namespace, string, data(), dataConst(), rgb565ToArgb(), saveBmp() (+4 more)

### Community 15 - "UI Component Rendering"
Cohesion: 0.29
Nodes (14): Adafruit_GFX, copyEllipsized(), TileCanvas, drawCenteredBounded(), drawStatusBadge(), drawSystemFrame(), gfxDrawCentered(), gfxFitSize() (+6 more)

### Community 16 - "Graphics Initialization"
Cohesion: 0.12
Nodes (8): Arduino_GFX, GfxFirmwareState, Settings, gfxBegin(), gfxDev(), gfxFirmwareUpdate(), gfxFitSize(), gfxTextW()

### Community 17 - "Tiled Rendering Engine"
Cohesion: 0.18
Nodes (12): GfxFirmwareState, gfxFirmwareUpdate(), beginTile(), TileMask, drawPixel(), fillScreen(), gfxMarkLineTiles(), gfxMarkPointTiles() (+4 more)

### Community 18 - "Platform Hardware Abstraction"
Cohesion: 0.12
Nodes (6): PlatformReset, String, platformResetInfo(), platformTcpConnect(), platformUpdateError(), WiFiClient

### Community 19 - "Arduino String Utilities"
Cohesion: 0.14
Nodes (6): String(), strlcat(), strlcpy(), Print(), class, class

### Community 20 - "Clock Preview Logic"
Cohesion: 0.19
Nodes (9): Settings, String, clockBegin(), clockForceResync(), clockFormatTime(), clockReapply(), clockService(), clockTimeStr() (+1 more)

### Community 21 - "Weather Icon Rendering"
Cohesion: 0.24
Nodes (15): TileCanvas, blendRoundedPanel(), ConditionLabel, first, second, copyShort(), drawCloud(), drawMainIcon() (+7 more)

### Community 22 - "Atmospheric Color Calculations"
Cohesion: 0.36
Nodes (11): blendPalette(), isAtmosphere(), isCloud(), lerpByte(), localMinuteForUtc(), localTm(), rgb565(), smoothAmount() (+3 more)

### Community 23 - "Polling Scheduler"
Cohesion: 0.30
Nodes (9): begin(), bind(), DisplayMode, Settings, indexOf(), jitteredInterval(), predictedDurationMs(), refillCredits() (+1 more)

### Community 24 - "Network Preview Logic"
Cohesion: 0.20
Nodes (6): Settings, String, netBegin(), netIP(), netSSID(), previewSetNetworkRssi()

### Community 25 - "Adafruit GFX Core"
Cohesion: 0.22
Nodes (3): Adafruit_GFX(), Adafruit_GFX_Button(), class

### Community 26 - "Project Component Overview"
Cohesion: 0.22
Nodes (10): Adafruit GFX Library, Adafruit GFX Vendor Info, Preview CMake Configuration, Scenic Weather Renderer, GitHub Mode Renderer, Network Mode Renderer, Radar Mode Renderer, Tile Renderer (+2 more)

### Community 27 - "Radar Preview Logic"
Cohesion: 0.24
Nodes (6): PreviewRadarState, Settings, previewSetRadarState(), radarInit(), radarPoll(), radarTest()

### Community 28 - "Sky and Scene Rendering"
Cohesion: 0.36
Nodes (10): blend565(), drawBackdrop(), drawCelestial(), drawMovingClouds(), drawPrecipitation(), drawSceneCloud(), drawSkyGradient(), drawSkylineAndBridge() (+2 more)

### Community 29 - "Time-based Color Palettes"
Cohesion: 0.20
Nodes (10): TimePalette, far, near, nightAmount, panelAlpha, secondary, skyHorizon, skyTop (+2 more)

### Community 30 - "Firmware Boot Context"
Cohesion: 0.25
Nodes (9): FirmwareContext, BootContext, accent, line1, line2, bootDirtyMask(), TileMask, firmwareDirtyMask() (+1 more)

### Community 31 - "Display Driver Setup"
Cohesion: 0.25
Nodes (7): Arduino_ST7789, Arduino_ST7789_DeskMate, Settings, gfxBegin(), gfxSetBrightness(), gfxSetRotation(), setRotation()

### Community 32 - "System Status Rendering"
Cohesion: 0.38
Nodes (7): bootAccent(), gfxApInfo(), gfxBoot(), gfxCrash(), gfxMessage(), revealBacklight(), gfxRenderTiled()

### Community 33 - "Tile Masking Utilities"
Cohesion: 0.40
Nodes (4): Adafruit_GFX(), gfxAllTilesMask(), class, TileMask

### Community 34 - "Project Documentation"
Cohesion: 0.50
Nodes (4): GitHub Build Workflow, DeskMate Changes Log, DeskMate README, Scalable Polling Architecture

### Community 35 - "Access Point Context"
Cohesion: 0.50
Nodes (4): ApContext, passwordState, ssid, url

### Community 36 - "Crash Debugging Context"
Cohesion: 0.50
Nodes (4): CrashContext, addr, epc, ip

### Community 37 - "System Message Context"
Cohesion: 0.50
Nodes (4): MessageContext, accent, message, title

## Knowledge Gaps
- **216 isolated node(s):** `build.sh script`, `class`, `class`, `class`, `namespace` (+211 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **15 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `millis()` connect `Radar Rendering Logic` to `GitHub Contribution Data`, `OTA Update Logic`, `Network Status Rendering`, `Desktop Simulator Main`, `Hardware Clock Management`, `Application Lifecycle Management`, `Weather API Client`, `Radar Data Fetching`, `Polling Scheduler`?**
  _High betweenness centrality (0.294) - this node is a cross-community bridge._
- **Why does `yield()` connect `GFX Drawing Primitives` to `GitHub Contribution Data`, `OTA Update Logic`, `Radar Rendering Logic`, `Desktop Simulator Main`, `Radar Data Fetching`, `Tiled Rendering Engine`?**
  _High betweenness centrality (0.165) - this node is a cross-community bridge._
- **Why does `gfxRenderTiled()` connect `System Status Rendering` to `GitHub Contribution Data`, `Firmware Preview Scenarios`, `Radar Rendering Logic`, `Network Status Rendering`, `Weather API Client`, `Tiled Rendering Engine`?**
  _High betweenness centrality (0.148) - this node is a cross-community bridge._
- **Are the 38 inferred relationships involving `millis()` (e.g. with `fetchGraphql()` and `.read()`) actually correct?**
  _`millis()` has 38 INFERRED edges - model-reasoned connections that need verification._
- **What connects `build.sh script`, `class`, `class` to the rest of the system?**
  _216 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `GitHub Contribution Data` be split into smaller, more focused modules?**
  _Cohesion score 0.05687645687645688 - nodes in this community are weakly interconnected._
- **Should `OTA Update Logic` be split into smaller, more focused modules?**
  _Cohesion score 0.06386946386946386 - nodes in this community are weakly interconnected._