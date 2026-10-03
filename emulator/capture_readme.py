#!/usr/bin/env python3
"""Capture README media from the real firmware with isolated, fixture-backed flash."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import socket
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets/readme"
FIXTURES = ROOT / "emulator/tests/fixtures"
SCALE = 4  # Accelerate acquisition only; GIF delays retain firmware timing.


def capture(root, name, settings, seconds, animate=False, day_cycle=False):
    work = root / name
    flash = work / "state/littlefs"
    flash.mkdir(parents=True)
    (flash / "config.json").write_text(json.dumps(settings))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    time_scale = 3600 if day_cycle else SCALE
    capture_ms = 300000 if day_cycle else 250
    command = [str(ROOT / "emulator/build/deskmate-emulator"),
               "--headless", "--board", "esp8266", "--scale", "1",
               "--state-dir", str(work / "state"), "--responses", str(FIXTURES),
               "--web-port", str(port), "--time-scale", str(time_scale),
               "--duration-ms", str(seconds * 1000),
               "--output", str(work / "final.bmp")]
    if animate:
        command += ["--capture-dir", str(work / "frames"), "--capture-ms", str(capture_ms)]
    if day_cycle:
        # Midnight UTC is 06:00 in the fixture's Dhaka timezone.
        epoch = int(datetime(2026, 10, 3, tzinfo=timezone.utc).timestamp())
        command += ["--epoch", str(epoch)]
    with (work / "run.log").open("w+") as log:
        try:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=log,
                           check=True, timeout=seconds / time_scale + 30)
        except (subprocess.SubprocessError, OSError):
            log.seek(0)
            print(log.read())
            raise
    if not animate:
        with Image.open(work / "final.bmp") as image:
            image.resize((480, 480), Image.Resampling.NEAREST).save(OUTPUT / f"{name}.png")
    if animate:
        # Keep the final complete cycle after provider warmup.
        cycle_ms = (86400000 if day_cycle else
                    settings["carouselSec"] * 1000 * (4 if name == "carousel" else 1))
        paths = sorted((work / "frames").glob("*.bmp"))
        timestamps = [int(path.stem.split("-")[1]) for path in paths]
        end = timestamps[-1]
        selected = [(path, stamp) for path, stamp in zip(paths, timestamps)
                    if end - cycle_ms <= stamp <= end]
        if len(selected) < 2:
            raise RuntimeError(f"Not enough animation frames for {name}")
        palette = None
        if day_cycle:
            # One shared palette prevents quantization flicker during slow blends.
            sheet = Image.new("RGB", (60, 60 * len(selected)))
            for index, (path, _) in enumerate(selected):
                with Image.open(path) as image:
                    sheet.paste(image.resize((60, 60), Image.Resampling.NEAREST), (0, index * 60))
            palette = sheet.quantize(colors=256)
        frames = []
        durations = []
        previous_boundary = 0
        for index, (path, stamp) in enumerate(selected):
            with Image.open(path) as image:
                image = image.resize((480, 480), Image.Resampling.NEAREST)
                frames.append(image.quantize(palette=palette, colors=256, dither=Image.Dither.NONE))
            next_stamp = selected[index + 1][1] if index + 1 < len(selected) else stamp + capture_ms
            playback_scale = time_scale if day_cycle else 1
            # Round cumulative time, avoiding drift from GIF's 10 ms resolution.
            boundary = round((next_stamp - selected[0][1]) / playback_scale / 10) * 10
            durations.append(max(10, boundary - previous_boundary))
            previous_boundary = boundary
        frames[0].save(OUTPUT / f"{name}.gif", save_all=True,
                       append_images=frames[1:], duration=durations, loop=0, optimize=True)
    print(f"Captured {name}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weather-cycle-only", action="store_true",
                        help="Regenerate only the accelerated morning–night–morning GIF")
    args = parser.parse_args()
    subprocess.run([str(ROOT / "emulator/build.sh")], cwd=ROOT, check=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    settings = json.loads((FIXTURES / "legacy-config.json").read_text())
    settings["github"] = json.loads((FIXTURES / "github-config.json").read_text())["github"]
    settings.update(hostname="deskmate-demo", carouselSec=12, carouselWeather=True,
                    carouselNetwork=True, carouselRadar=True, carouselGithub=True)
    settings["network"] = {"probeHost": "1.1.1.1", "probePort": 443,
                           "dnsHost": "github.com", "pollSec": 3}
    with tempfile.TemporaryDirectory(prefix="deskmate-media-") as temporary:
        root = Path(temporary)
        if not args.weather_cycle_only:
            for mode, seconds in (("weather", 6), ("network", 18), ("radar", 25)):
                settings["mode"] = mode
                capture(root, mode, settings, seconds)
            settings["mode"] = "github"
            for page in ("Inbox", "Pulls", "Pulse"):
                for candidate in ("Inbox", "Pulls", "Pulse"):
                    settings["github"][f"page{candidate}"] = candidate == page
                capture(root, f"github-{page.lower()}", settings, 6)
            settings["github"].update(pageInbox=True, pagePulls=True, pagePulse=True)
            capture(root, "github-pages", settings, 28, animate=True)
            settings["mode"] = "carousel"
            capture(root, "carousel", settings, 100, animate=True)
        settings["mode"] = "weather"
        capture(root, "weather-cycle", settings, 90000, animate=True, day_cycle=True)


if __name__ == "__main__":
    main()
