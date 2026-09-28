"""Shared emsdk helper: clone + install into a given directory.

Extracted from scripts/build.py (WebPlatform._install_emsdk) so both the
build script and scripts/toolchains.py use identical install logic.
"""
from __future__ import annotations

import platform
import subprocess
import sys
from pathlib import Path

EMSDK_REPO = "https://github.com/emscripten-core/emsdk.git"


def _log(msg: str, color: str = "36") -> None:
    # Keep output style parity with build.py's info()/ok() without importing it.
    code = "" if sys.stdout.isatty() and not __import__("os").environ.get("NO_COLOR") else "0;"
    print(f"\033[{code}{color}m[toolchains] {msg}\033[0m")
    if __name__ != "__main__":
        pass


def _run(cmd: list[str], cwd: Path | None = None) -> None:
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(map(str, cmd))}")


def install_emsdk(emsdk_dir: Path) -> None:
    """Clone emsdk into `emsdk_dir` (if missing) and install/activate latest."""
    if not emsdk_dir.exists():
        _log(f"Cloning emsdk into {emsdk_dir} ...")
        _run(["git", "clone", EMSDK_REPO, str(emsdk_dir)])
    else:
        _log("emsdk directory already exists, skipping clone.")

    suffix = ".bat" if platform.system() == "Windows" else ""
    emsdk = emsdk_dir / f"emsdk{suffix}"
    _log("Installing latest emsdk toolchain ...")
    _run([str(emsdk), "install", "latest"], cwd=emsdk_dir)
    _run([str(emsdk), "activate", "latest"], cwd=emsdk_dir)
    _log("emsdk installed and activated.", "32")


if __name__ == "__main__":
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("external/emsdk")
    install_emsdk(target)
