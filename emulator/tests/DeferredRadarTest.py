#!/usr/bin/env python3
"""Local-only regression for the single-slot deferred Radar API."""
import argparse
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from stress import ROOT, request, wait_snapshot


def main():
    if not __debug__:
        raise RuntimeError("assertions required")
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=ROOT / "build/deskmate-emulator")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="deskmate-deferred-radar-") as temporary:
        root = Path(temporary)
        fs = root / "state/littlefs"
        fs.mkdir(parents=True)
        fixtures = root / "fixtures"
        shutil.copytree(ROOT / "tests/fixtures", fixtures)
        radar_data = (fixtures / "radar-1.json").read_bytes()
        for path in fixtures.glob("radar-*.json"):
            path.write_bytes(radar_data)
        settings = json.loads((fixtures / "legacy-config.json").read_text())
        settings.update(mode="radar")
        (fs / "config.json").write_text(json.dumps(settings))
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        with (root / "log").open("w+") as log:
            process = subprocess.Popen([str(args.binary.resolve()), "--headless",
                "--duration-ms", "60000", "--web-port", str(port),
                "--state-dir", str(root / "state"), "--responses", str(fixtures),
                "--heap-bytes", "13352", "--max-block-bytes", "12896"], stdout=log, stderr=log)
            try:
                ready = time.monotonic() + 10
                while True:
                    try:
                        request(port, "/api/status")
                        break
                    except OSError:
                        assert process.poll() is None and time.monotonic() < ready
                        time.sleep(.05)
                wait_snapshot(port, "radar", lambda s: s["valid"] and not s["error"])
                assert request(port, "/api/config", '{"mode":"network"}')["ok"]
                before = request(port, "/api/emulator/snapshots")["radar"]
                persisted = (fs / "config.json").read_bytes()
                assert request(port, "/api/test/radar")["state"] == "idle"
                request(port, "/api/test/radar", '{"radar":', 400)
                request(port, "/api/test/radar", '{}', 422)
                radar = settings["radar"].copy()
                radar["minAltFt"] = 60000
                body = json.dumps({"radar": radar})
                queued = request(port, "/api/test/radar", body, 202)
                assert queued["state"] == "queued"
                request(port, "/api/test/radar", body, 409)
                # A settings change must not replace the pending test's copied origin/filter.
                assert request(port, "/api/config", '{"brightness":45}')["ok"]
                deadline = time.monotonic() + 15
                result = {}
                while time.monotonic() < deadline:
                    result = request(port, "/api/test/radar?id=" + str(queued["id"]))
                    if result["state"] == "done":
                        break
                    time.sleep(.1)
                assert result.get("ok") and result["aircraft"] == 0, result
                after = request(port, "/api/emulator/snapshots")["radar"]
                assert before == after, (before, after)
                assert json.loads(persisted)["radar"] == json.loads((fs / "config.json").read_bytes())["radar"]
                assert set(result) == {"id", "state", "ok", "aircraft", "httpCode", "error"}
                second = request(port, "/api/test/radar", body, 202)
                assert second["id"] != queued["id"]
                request(port, "/api/test/radar?id=" + str(queued["id"]), expected=409)
                process.terminate()
                assert process.wait(timeout=5) == 0
                log.seek(0)
                output = log.read()
                assert "ERROR: AddressSanitizer" not in output and "runtime error:" not in output
                print("Deferred Radar: 13352-byte heap, contention, validation, isolated snapshot, stale ID passed")
            except BaseException:
                log.seek(0)
                print(log.read()[-6000:])
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == "__main__":
    main()
