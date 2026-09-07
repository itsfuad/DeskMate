#!/usr/bin/env python3
"""Archive a bin/ELF pair before rebuilding. Never overwrites an archive."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    files = [args.source / name for name in ("firmware.bin", "firmware.elf")]
    for path in files:
        if not path.is_file():
            parser.error(f"missing artifact: {path}")
    args.output.mkdir(parents=True, exist_ok=False)
    hashes = {}
    for path in files:
        destination = args.output / path.name
        shutil.copy2(path, destination)
        digest = hashlib.sha256(destination.read_bytes()).hexdigest()
        if digest != hashlib.sha256(path.read_bytes()).hexdigest():
            raise RuntimeError(f"artifact changed during copy: {path}")
        hashes[path.name] = digest
    def git(*arguments):
        return subprocess.check_output(["git", "--no-pager", *arguments], text=True)
    metadata = {"head": git("rev-parse", "HEAD").strip(),
                "tracked_dirty": bool(git("diff", "HEAD", "--stat")),
                "sha256": hashes,
                "provenance": "Files copied together; source/ELF correspondence must be established at build time."}
    (args.output / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (args.output / "SHA256SUMS").write_text("".join(
        f"{digest}  {name}\n" for name, digest in hashes.items()))
    shutil.copy2("platformio.ini", args.output / "platformio.ini")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
