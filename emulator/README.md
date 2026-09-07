# DeskMate ESP emulator

This is a desktop hardware-resource implementation for the DeskMate firmware.
It runs the real application lifecycle, scheduler, settings, provider clients,
web portal, OTA state machine, display modes, and renderers. There are no
feature-state fixtures or parallel preview implementations.

The emulator substitutes only resources that are physically attached to an
ESP: display transport, clock, Wi-Fi/socket access, flash storage, ADC/PWM,
board limits, reset, and firmware-update storage.

## Display memory

The shared renderer keeps its 32×32 logical damage grid, but composes and pushes
32×8 strips using a 512-byte RGB565 buffer instead of 2048 bytes. Strip dimensions
use `uint8_t`; screen coordinates remain signed 16-bit for clipping, and colors
remain 16-bit. Weather cloud position tables also use 8-bit values; their constant
scale table is removed. More callbacks and display writes are the tradeoff.

A clean ESP8266 build measured static RAM at 56852 bytes, down from 58432
(1580 bytes saved). This is a build measurement, not a hardware heap or TLS success
measurement. TLS admission thresholds are unchanged.

`deskmate-tile-test` checks oversized/negative dimensions, exact full-screen
coverage, the bottom-right damage tile, clipped regions, and strip stride. The
full emulator suite and six short ASan/UBSan stress scenarios passed with these
changes; these do not establish long-run hardware stability.

### Early clipping and transition order

`TileCanvas` rejects text, lines, circles, rounded rectangles, and filled triangles
that cannot touch the current strip before GFX rasterizes them. Icon rendering
also rejects invisible glyphs before bitmap reads or rotation-frame selection.
Partial intersections retain GFX's original rasterization, and wrapped/custom-font
text uses the original text path. No extra framebuffer or heap allocation is used.
Full and dirty-tile renders traverse horizontal strip rows top-to-bottom instead
of completing each 32×32 tile first; Wi-Fi/watchdog yields occur between strip rows.

`deskmate-tile-test` compares pixels and cursor positions against the previous GFX
paths, checks monotonic strip-row ordering, and prints a synthetic host frame
benchmark. One non-sanitized run measured 53543 µs before versus 5656 µs after
(9.47×); this is not a measurement of SPI transfer time or real device page latency.
The full emulator suite, ASan/UBSan comparison and short provider-recovery stress
passed. The ESP8266 build uses 56784 bytes of static RAM and the same 512-byte buffer.

## Requirements

Fedora packages:

```bash
sudo dnf install gcc-c++ cmake libX11-devel openssl-devel
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

## Constrained local stress

Requires Python 3 and Linux `/proc`. Build first, then run:

```bash
python3 emulator/stress.py
python3 emulator/stress.py --seconds 30 --time-scale 3600 --case normal
```

The runner launches and terminates its own headless ESP8266 processes, with
separate temporary flash and copied response fixtures per case. All HTTP clients
connect directly to loopback (no proxy use); fixture mode replaces outbound
TCP/TLS and DNS, including missing fixtures, without falling back to the Internet.
It does not use a device, existing emulator state, credentials, or OTA endpoints.
An ephemeral port is selected before launch; a bind collision fails the run.

Six cases cover the reported running heap (13352-byte heap, 12896-byte maximum
block, 1888-byte stack), just-below-admission heap (13119 bytes, historical
case name `crash-heap`), 8000-byte heap, 1024-byte maximum block,
512-byte continuation stack, and a completely full filesystem. Three concurrent
status clients overlap refresh requests; each batch also attempts configuration
changes and malformed/invalid JSON. Checks include status structure, expected
HTTP/application errors, persisted values, byte-for-byte rollback on failed saves,
absence of transaction leftovers, continued polling, and successful saving after
removing the full-disk filler. Low heap/stack intentionally reject mutations with
503; those malformed requests exercise admission rejection, not JSON parsing.
The storage unit test separately verifies reopening persisted settings and
partial-write rollback. The normal case selects each provider individually and
asserts a nonempty snapshot, an explicit error with retained data after replacing
its fixture, and recovery after restoring it. It checks GitHub calendar validity,
feature invalidation and carousel switching through emulator-only snapshot routes.
Other cases inject every fifth connection failure and every seventh truncated
response; their aggregate polling counters are not provider-recovery assertions.

For genuine wall-time soak (up to 24 hours per case):

```bash
python3 emulator/stress.py --case normal --soak-seconds 14400 \
  --warmup-seconds 60 --rss-growth-mib 8
```

Soak forces time scale 1, measures only after warmup, and reports wall duration,
RSS slope in KiB/minute, sample count and peak RSS/FD growth. The slope is diagnostic,
not an automatic leak verdict. A bounded, downsampled history avoids turning the
runner itself into an unbounded queue. Four hours requested means four real hours,
not a fast clock. A separate scheduler unit test crosses `millis()` rollover.

Default duration is eight wall seconds per case at 3600x (about eight firmware
clock hours per case). `test.sh` uses a two-second smoke run. Durations are bounded
to 2–120 seconds, scale to 1–10000, each socket to three seconds, startup to ten
seconds, and process shutdown to five seconds before forced termination. Batch
completion can extend the requested duration. Temporary state is cleaned even on
failure; the final log excerpt is printed when a case fails. Python `-O` is
rejected because the checks require assertions.

JSON reports distinguish batch request counts, firmware-clock hours, outbound
attempts, injected failures, scheduler completions/failures, and observed RSS/FD
growth. Counts are throughput-dependent, not deterministic. Extra startup,
read-back and recovery requests are not included in batch counts. The default
peak-growth limits, sampled after each batch against the ready-process baseline,
are 64 MiB RSS and eight file descriptors. They catch gross growth, not every
transient peak or slow leak. `--rss-growth-mib` permits an explicit 1–512 MiB budget.

### Resource controls and limitations

| Option | Meaning |
| --- | --- |
| `--heap-bytes N` | Logical initial free heap |
| `--max-block-bytes N` | Logical maximum allocatable block, capped by free heap |
| `--stack-bytes N` | Reported continuation stack headroom |
| `--flash-bytes N` | LittleFS file-content capacity |
| `--time-scale N` | Firmware monotonic time acceleration |
| `--network-fail-every N` | Reject every Nth outbound connect attempt |
| `--truncate-every N` | Truncate every Nth attempted connection's fixture response |

Resource values of zero select board defaults; zero fault intervals disable
injection. Scale must be positive. These unsigned options reject negatives,
trailing junk and values beyond uint32; ports additionally reject values above
65535. Duration is measured in accelerated firmware milliseconds.

Network reservations are **logical**, not real `malloc` interception. ESP8266
secure-client construction reserves a shared 6200-byte DRAM BearSSL stack until
the last secure client is destroyed; `stop()` does not release it. Connections
add the TLS RX chunk, plain connections 512 bytes. Construction failure refuses
connection in the emulator rather than reproducing the core's fatal abort.
The pre-construction budget is 13120 bytes: historical 12608-byte total plus a
512-byte client allowance. The 6200-byte stack is now accounted within that total,
not added on top. The factory rechecks 6408 bytes free and 5120 contiguous after
construction; connection sites check again. This restores admission at the reported
13352-byte heap without discarding constructor accounting. The historical `crash-heap`
case now tests 13119 bytes, immediately below admission, and intentionally skips HTTPS.
The previous 18808-byte policy was an overly restrictive diagnostic change, not a
measured requirement. This revised estimate is NOT proof that hardware handshake
peaks fit or that the original null-write bug is fixed; physical observation is needed.
TX buffers, TLS internals, `String`, JSON objects and other host allocations are
not charged to the emulated heap. Stack settings test firmware admission guards,
not host stack exhaustion. LittleFS counts file contents, not flash metadata,
wear, erase blocks, or power-loss durability. Accelerated clock hours are not
hours of ESP CPU execution, real networking, or physical uptime; wall-clock
throughput and host scheduling still determine how much work runs. The emulator
is not an ESP allocator/fragmentation, cycle-accurate CPU, RF, electrical, or
ST7789 gamma emulator. Continued scheduler progress under periodic faults does
not prove every provider request recovered successfully.

### Host sanitizers (opt-in)

The default build is unchanged. A separate Clang ASan/UBSan build:

```bash
cmake -S emulator -B emulator/build-sanitize \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  -DDESKMATE_SANITIZERS=ON
cmake --build emulator/build-sanitize -j 4
emulator/build-sanitize/deskmate-storage-constraint-test
ASAN_OPTIONS=quarantine_size_mb=8:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 emulator/stress.py --binary emulator/build-sanitize/deskmate-emulator --time-scale 60
```

ASan's default quarantine can itself exceed the runner's RSS-growth budget.
The command explicitly bounds quarantine to 8 MiB while retaining leak detection;
this reduces freed-allocation history, not instrumentation of live allocations.
Sanitizers validate host code, not hardware memory behavior. Use a lower clock
scale with instrumentation: at 3600x, sanitizer overhead also multiplies measured
job durations and can trigger provider timeouts or scheduler admission holds.
A 3600x sanitized run failed a Radar recovery assertion; the 60x rerun passed.

### Allocation faults and boundary tests

`emulatorFailNothrowAfter(N)` fails the next explicit `new (std::nothrow)` after
N successes, once. Ordinary `new`, arrays, `malloc`, SDK internals and desktop TLS
are not intercepted. Settings and Radar tests verify actual null-allocation
rollback and subsequent recovery. `emulatorFailStringGrowthAfter(N)` exercises
`String::reserve`, C-string assignment and `concat` growth failure; it does not
cover all constructors, copy assignment, numeric operators or ESP string layout.
The settings tests check reserve/concat retention and recovery. Heap, largest-block
and mutation-stack threshold tests run in the connectivity test, along with
breadcrumb checksum, ring and counter-rollover tests.

### Deferred Radar test and HTTP regressions

`POST /api/test/radar` now validates input and returns HTTP 202 with an `id` and
`state: queued`. Only one test can be pending; another POST receives 409. Poll
`GET /api/test/radar?id=N` until `state: done`, then inspect `ok`, `aircraft`,
`httpCode` and `error`. Results last until the next test; old IDs receive 409.
The bundled UI handles this protocol, bounds waiting and refuses to mark changed
settings as verified. Reload an old browser tab after installing this firmware.

The queued record stores only the URL, origin, altitude filter and timeout. No
full Settings candidate or JSON-body copy survives into TLS. On ESP8266, the
server adapter closes the completed response and releases retained argument
storage after `handleClient()` returns; it will not discard an incomplete request.
The test executes on a later loop pass and is cancelled for reboot/update work.
It uses the normal bounded parser but never publishes aircraft or trail changes
from unsaved test settings. This avoids request/TLS allocation overlap without
reserving a second permanent arena. SDK allocations remain outside our control.

`DeferredRadarTest.py` tests this protocol at 13352-byte logical heap, including
contention, invalid input, settings changes, no live snapshot mutation and stale
IDs. `HttpRequestTest.cpp` covers host-only URLs (previously undefined pointer
subtraction), ports/query/fragment handling, short writes and long headers. Header
reading now bounds indexes and work; the previous reader could read beyond its
128-byte line buffer when stripping CR from a long header. HTTP GET and GitHub
request writes now stop on short writes. These fixes do not establish the cause
of the original ROM memcpy exception. Radar also rejects nonfinite/out-of-range
numeric data before integer conversion, and Weather rejects nonfinite floats.

### ESP8266 crash breadcrumbs and exact symbols

Runtime tracing is now OFF by default. Define `DESKMATE_CRASH_TRACE` explicitly
for a diagnostic build; otherwise no RTC writes, ring allocation, or retained
breadcrumb text are added. This removes experimental diagnostic overhead from
normal operation. The original release binary remains archived unchanged.

Project include directories use source-only `-I` flags rather than `-iquote` so
PlatformIO/SCons tracks quoted-header dependencies. A clean rebuild exposed stale
objects under the previous configuration. After this correction, changing
`CrashBreadcrumbs.h` was verified to recompile all seven dependent source files.
Always clean once when moving from an older build configuration.

When enabled, `src/core/CrashBreadcrumbs.h` keeps eight fixed-size records in RTC user memory
starting at word 32, after eboot's reserved first 128 bytes. Each includes uptime,
operation, detail, free heap, largest block and free stack. Records use a version
and checksum; no credentials, pointers or flash writes. On next boot `/crashlog`
includes the previous ring if valid. Power loss or an interrupted RTC write may
invalidate it. This is context, not a backtrace, and physical reset retention
still requires manual hardware verification. Other platforms report unsupported.

Operation codes: 1 boot, 2 settings, 3 display initialization, 4 network startup,
5 poll begin (mode detail), 6 poll end (mode shifted 8 bits plus result),
7 HTTP connect, 8 HTTP headers, 9 config mutation, 10 invalidation, 11 parse,
12 web status, 13 web root, 14 render begin, 15 render end (1 tiles, 2 region),
16 connect returned (port on success, zero on failure), 17 request write begin,
18 request write end, 19 TLS admission rejected, 20 deferred Radar test begin
(detail is test ID), 21 deferred Radar test end (detail is success). Write-end marks completion of
write calls, not acknowledgement of all bytes. These stages cover HTTP GET and
GitHub GraphQL requests.
RTC snapshots have a small RAM/CPU cost; this is diagnostic firmware, not proof
that the original `memcpy` crash is repaired.

Preserve a local binary/ELF pair before rebuilding (output must not exist):

```bash
python3 scripts/preserve_firmware.py --source .pio/build/deskmate \
  --output firmware-artifacts/my-build
```

The archive contains checksums, Git state and PlatformIO configuration. Copying
an old pair does not independently establish correspondence to its sources.
CI archives the pair immediately after building, adds package versions, and
uploads the debug archive with release firmware. The original released 4.8.7
pair is preserved locally under `firmware-artifacts/v4.8.7-original/`.
