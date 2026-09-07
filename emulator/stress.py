#!/usr/bin/env python3
"""Bounded Linux loopback stress; no third-party Python dependencies."""
import argparse
import concurrent.futures
import http.client
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
CASES = {"normal": ["--heap-bytes", "13352", "--max-block-bytes", "12896",
                    "--stack-bytes", "1888"],
         "crash-heap": ["--heap-bytes", "13119", "--max-block-bytes", "12896"],
         "low-heap": ["--heap-bytes", "8000"],
         "low-block": ["--max-block-bytes", "1024"],
         "low-stack": ["--stack-bytes", "512"], "full-disk": []}


def request(port, path, body=None, expected=200):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request("GET" if body is None else "POST", path, body,
                           {"Content-Type": "application/json"})
        response = connection.getresponse()
        data = response.read(262145)
        assert len(data) <= 262144, "oversized response"
        assert response.status == expected, (path, response.status, data[:200])
        if not 200 <= expected < 300:
            assert data, "empty error response"
            return {}
        result = json.loads(data)
        assert isinstance(result, dict), result
        return result
    finally:
        connection.close()


def resources(pid):
    status = Path(f"/proc/{pid}/status").read_text()
    rss = int(re.search(r"VmRSS:\s+(\d+)", status)[1])
    return rss, len(list(Path(f"/proc/{pid}/fd").iterdir()))


def wait_snapshot(port, provider, predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        request(port, "/api/refresh", "{}")
        snapshot = request(port, "/api/emulator/snapshots")[provider]
        if predicate(snapshot):
            return snapshot
        time.sleep(.02)
    raise AssertionError((provider, "snapshot timeout", snapshot))


def verify_providers(port, fixtures):
    # Pin Radar to one recorded snapshot so rollback comparisons are meaningful.
    radar_data = (fixtures / "radar-1.json").read_bytes()
    for path in fixtures.glob("radar-*.json"):
        path.write_bytes(radar_data)
    reports = {}
    for provider, names, payload in (
        ("weather", ["weather-current.http", "weather-forecast.http"], ["city", "temp"]),
        ("radar", [p.name for p in fixtures.glob("radar-*.json")], ["count"]),
        ("github", ["github-lists.http"], ["login", "inbox", "pulls"]),
    ):
        assert request(port, "/api/config", json.dumps({"mode": provider}))["ok"]
        good = wait_snapshot(port, provider, lambda s: s["valid"] and not s["error"])
        assert any(good[key] for key in payload), (provider, "empty fixture snapshot", good)
        saved = {name: (fixtures / name).read_bytes() for name in names}
        for name in names:
            path = fixtures / name
            temporary = path.with_suffix(".tmp")
            temporary.write_bytes(b"{\"broken\":" if name.endswith(".json") else
                                  b"HTTP/1.0 503 Unavailable\r\nContent-Length: 0\r\n\r\n")
            temporary.replace(path)
        failed = wait_snapshot(port, provider, lambda s: s["error"])
        assert failed["valid"], (provider, "lost cached snapshot", failed)
        assert all(failed[key] == good[key] for key in payload), (provider, good, failed)
        for name, data in saved.items():
            path = fixtures / name
            temporary = path.with_suffix(".tmp")
            temporary.write_bytes(data)
            temporary.replace(path)
        recovered = wait_snapshot(port, provider, lambda s: s["valid"] and not s["error"])
        assert all(recovered[key] == good[key] for key in payload), (provider, recovered)
        if provider == "github":
            wait_snapshot(port, provider, lambda s: s["calendarValid"] and not s["calendarFailed"])
        reports[provider] = {"before": good, "failed": failed, "recovered": recovered}
    before = request(port, "/api/emulator/snapshots")
    assert request(port, "/api/config", '{"github":{"pollSec":901}}')["ok"]
    after = request(port, "/api/emulator/snapshots")
    assert after["invalidations"] > before["invalidations"], "feature config did not invalidate"
    assert request(port, "/api/config", '{"mode":"carousel","carouselSec":5}')["ok"]
    return reports


def trend(samples):
    if len(samples) < 2:
        return 0.0
    mean_t = sum(s[0] for s in samples) / len(samples)
    mean_rss = sum(s[1] for s in samples) / len(samples)
    denominator = sum((s[0] - mean_t) ** 2 for s in samples)
    return (sum((s[0] - mean_t) * (s[1] - mean_rss) for s in samples) /
            denominator * 60 if denominator else 0.0)


def run_case(binary, name, seconds, scale, rss_limit, warmup=0, soak=False):
    with tempfile.TemporaryDirectory(prefix="deskmate-stress-") as temporary:
        root = Path(temporary)
        state = root / "state"
        fs = state / "littlefs"
        fs.mkdir(parents=True)
        fixtures = root / "fixtures"
        shutil.copytree(ROOT / "tests/fixtures", fixtures)
        config = fs / "config.json"
        settings = json.loads((fixtures / "legacy-config.json").read_text())
        settings["github"] = json.loads((fixtures / "github-config.json").read_text())["github"]
        settings.update(mode="carousel", carouselSec=5, carouselWeather=True,
                        carouselNetwork=True, carouselRadar=True, carouselGithub=True)
        config.write_text(json.dumps(settings))
        original = config.read_bytes()
        flags = CASES[name].copy()
        if name == "full-disk":
            (fs / "filler").write_bytes(b"x" * 4096)
            flags += ["--flash-bytes", str(len(original) + 4096)]
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        command = [str(binary), "--headless", "--board", "esp8266",
                   "--state-dir", str(state), "--responses", str(fixtures),
                   "--web-port", str(port), "--time-scale", str(scale),
                   "--duration-ms", str(min(4294967295, int((seconds + warmup + 180) * scale * 1000)))] + flags
        if name != "normal":
            command += ["--network-fail-every", "5", "--truncate-every", "7"]
        with (root / "run.log").open("w+") as log:
            process = subprocess.Popen(command, stdout=log, stderr=log)
            try:
                ready = time.monotonic() + 10
                while True:
                    assert process.poll() is None, "emulator exited during startup"
                    try:
                        first = request(port, "/api/status")
                        break
                    except (OSError, http.client.HTTPException):
                        if time.monotonic() >= ready:
                            raise TimeoutError("portal startup")
                        time.sleep(.05)
                providers = verify_providers(port, fixtures) if name == "normal" else {}
                warmup_end = time.monotonic() + warmup
                while time.monotonic() < warmup_end:
                    request(port, "/api/status")
                    time.sleep(.1)
                original = config.read_bytes()
                first = request(port, "/api/status")
                first_snapshots = request(port, "/api/emulator/snapshots")
                baseline = resources(process.pid)
                samples = []
                peaks = list(baseline)
                counts = {"status": 0, "refresh": 0, "mutation": 0, "malformed": 0}
                blocked = name in ("low-heap", "low-stack")
                started = time.monotonic()
                deadline = started + seconds
                with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                    while time.monotonic() < deadline:
                        reads = [pool.submit(request, port, "/api/status") for _ in range(3)]
                        assert request(port, "/api/refresh", "{}")["ok"]
                        counts["refresh"] += 1
                        for future in reads:
                            assert "uptime" in future.result()
                            counts["status"] += 1
                        interval = 5 + counts["mutation"] % 2
                        body = json.dumps({"carouselSec": interval})
                        result = request(port, "/api/config", body,
                                         503 if blocked else 200)
                        if name == "full-disk" or blocked:
                            if name == "full-disk":
                                assert result["ok"] is False and "save" in result["error"]
                            assert config.read_bytes() == original
                        else:
                            assert result["ok"]
                            assert request(port, "/api/config")["carouselSec"] == interval
                            assert json.loads(config.read_bytes())["carouselSec"] == interval
                        counts["mutation"] += 1
                        for bad, code in (('{"hostname":', 400), ('{"hostname":"bad host"}', 422)):
                            request(port, "/api/config", bad, 503 if blocked else code)
                            counts["malformed"] += 1
                        assert not (fs / "config.json.tmp").exists()
                        sample = resources(process.pid)
                        samples.append((time.monotonic() - started, *sample))
                        if len(samples) > 4096:
                            samples = samples[::2]
                        peaks = [max(a, b) for a, b in zip(peaks, sample)]
                        assert peaks[0] - baseline[0] <= rss_limit, ("RSS growth KiB", baseline, peaks)
                        assert peaks[1] - baseline[1] <= 8, ("FD growth", baseline, peaks)
                        time.sleep(.1 if soak else .01)
                wall_seconds = time.monotonic() - started
                assert wall_seconds >= seconds, ("wall-time run too short", wall_seconds)
                last = request(port, "/api/status")
                snapshots = request(port, "/api/emulator/snapshots")
                if name == "normal" and seconds * scale >= 10:
                    assert snapshots["switches"] > first_snapshots["switches"], "carousel never switched"
                if name in ("normal", "full-disk", "crash-heap"):
                    assert last["pollCompleted"] > first["pollCompleted"], "polling did not progress"
                if name == "crash-heap":
                    assert all(not snapshots[p]["valid"] for p in ("weather", "radar", "github")), snapshots
                elapsed = last["uptime"] - first["uptime"]
                assert elapsed >= seconds * scale * .8, ("accelerated time", elapsed)
                if name == "full-disk":
                    # Free enough capacity for the complete transactional replacement.
                    (fs / "filler").unlink()
                    assert request(port, "/api/config", '{"carouselSec":15}')["ok"]
                    assert json.loads(config.read_bytes())["carouselSec"] == 15
                assert request(port, "/api/status")["uptime"] >= last["uptime"]
                process.terminate()
                assert process.wait(timeout=5) == 0
                log.seek(0)
                output = log.read()
                assert "ERROR: AddressSanitizer" not in output and "runtime error:" not in output, output[-4000:]
                counters = re.search(r"outbound requests (\d+), injected failures (\d+)", output)
                assert counters, "missing resource summary"
                if name in ("normal", "full-disk"):
                    assert int(counters[1]) > 5, counters.groups()
                    if name == "full-disk":
                        assert int(counters[2]) > 0, counters.groups()
                report = {"case": name, "kind": "wall-time-soak" if soak else "accelerated-scheduler",
                          "wall_seconds": round(wall_seconds, 3), "warmup_seconds": warmup,
                          "rss_slope_kib_per_minute": round(trend(samples), 3),
                          "samples": len(samples), "providers": providers,
                          "carousel_switches": snapshots["switches"] - first_snapshots["switches"],
                          "counts": counts, "emulated_hours": round(elapsed / 3600, 3),
                          "rss_growth_kib": peaks[0] - baseline[0], "fd_growth": peaks[1] - baseline[1],
                          "outbound_requests": int(counters[1]), "injected_failures": int(counters[2]),
                                                    "poll_completed": last["pollCompleted"], "poll_failed": last["pollFailed"]}
                print(json.dumps(report), flush=True)
                return report
            except BaseException:
                log.flush()
                log.seek(0)
                print(log.read()[-6000:], flush=True)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


def main():
    if not __debug__:
        raise RuntimeError("stress assertions require Python without -O")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/deskmate-emulator")
    parser.add_argument("--seconds", type=int, default=8, choices=range(2, 121), metavar="2..120")
    parser.add_argument("--time-scale", type=int, default=3600, choices=range(1, 10001), metavar="1..10000")
    parser.add_argument("--rss-growth-mib", type=int, default=64, choices=range(1, 513), metavar="1..512")
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--soak-seconds", type=int, choices=range(10, 86401), metavar="10..86400",
                        help="genuine wall-clock soak, time scale fixed to 1")
    parser.add_argument("--warmup-seconds", type=int, default=5, choices=range(0, 601), metavar="0..600")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    assert Path("/proc/self/status").exists(), "Linux /proc required for resource checks"
    for name in args.case or CASES:
        run_case(binary, name, args.soak_seconds or args.seconds,
                 1 if args.soak_seconds else args.time_scale, args.rss_growth_mib * 1024,
                 args.warmup_seconds if args.soak_seconds else 0, bool(args.soak_seconds))


if __name__ == "__main__":
    main()
