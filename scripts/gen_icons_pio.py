"""PlatformIO pre-build hook for generated icon assets."""
from pathlib import Path
import subprocess
import sys

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
result = subprocess.run(
    [sys.executable, str(root / "scripts" / "gen_icons.py")],
    cwd=root,
)
if result.returncode:
    env.Exit(result.returncode)
