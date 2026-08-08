from pathlib import Path
import re
import shutil

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
config_path = project_dir / "src" / "boards" / "config.h"
config = config_path.read_text(encoding="utf-8")
match = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', config)
if not match:
    raise RuntimeError("FW_VERSION was not found in src/boards/config.h")

version = match.group(1)


def copy_versioned_artifact(source, target, env):
    artifact = target[0] if hasattr(target, "__getitem__") else target
    artifact = Path(str(artifact))
    versioned = artifact.with_name(f"{artifact.stem}-{version}{artifact.suffix}")
    shutil.copy2(artifact, versioned)
    print(f"Versioned firmware artifact: {versioned}")


env.AddPostAction("$BUILD_DIR/firmware.bin", copy_versioned_artifact)
env.AddPostAction("$BUILD_DIR/firmware.factory.bin", copy_versioned_artifact)
