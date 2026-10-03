#!/usr/bin/env python3
"""Capture README media from the real firmware with isolated, fixture-backed flash."""
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


def capture(root, name, settings, seconds, animate=False):
    work = root / name
    flash = work / "state/littlefs"
    flash.mkdir(parents=True)
    (flash / "config.json").write_text(json.dumps(settings))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    command = [str(ROOT / "emulator/build/deskmate-emulator"),
               "--headless", "--board", "esp8266", "--scale", "1",
               "--state-dir", str(work / "state"), "--responses", str(FIXTURES),
               "--web-port", str(port), "--time-scale", str(SCALE),
               "--duration-ms", str(seconds * 1000),
               "--output", str(work / "final.bmp")]
    if animate:
        command += ["--capture-dir", str(work / "frames"), "--capture-ms", "250"]
    with (work / "run.log").open("w+") as log:
        try:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=log,
                           check=True, timeout=seconds / SCALE + 30)
        except (subprocess.SubprocessError, OSError):
            log.seek(0)
            print(log.read())
            raise
    with Image.open(work / "final.bmp") as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(OUTPUT / f"{name}.png")
    if animate:
        # Discard the first cycle so all provider snapshots are already populated.
        cycle_ms = settings["carouselSec"] * 1000 * (4 if name == "carousel" else 1)
        paths = sorted((work / "frames").glob("*.bmp"))
        timestamps = [int(path.stem.split("-")[1]) for path in paths]
        end = timestamps[-1]
        selected = [(path, stamp) for path, stamp in zip(paths, timestamps)
                    if end - cycle_ms <= stamp <= end]
        if len(selected) < 2:
            raise RuntimeError(f"Not enough animation frames for {name}")
        frames = []
        durations = []
        for index, (path, stamp) in enumerate(selected):
            with Image.open(path) as image:
                frames.append(image.resize((480, 480), Image.Resampling.NEAREST)
                              .quantize(colors=256, dither=Image.Dither.NONE))
            next_stamp = selected[index + 1][1] if index + 1 < len(selected) else stamp + 250
            durations.append(max(10, round((next_stamp - stamp) / 10) * 10))
        frames[0].save(OUTPUT / f"{name}.gif", save_all=True,
                       append_images=frames[1:], duration=durations, loop=0, optimize=True)
    print(f"Captured {name}", flush=True)


def main():
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
    # The animations already cover these final frames; keep the gallery focused.
    for name in ("github-pages", "carousel"):
        (OUTPUT / f"{name}.png").unlink()


if __name__ == "__main__":
    main()
