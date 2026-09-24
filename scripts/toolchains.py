#!/usr/bin/env python3
"""On-demand toolchain fetcher for MoBaGEn (dynamic-loading todo 19).

Fetches pinned, hash-verified toolchains into external/ (gitignored).
EXPLICIT INVOCATION ONLY — nothing here runs at configure/build time.

Usage:
    python3 scripts/toolchains.py list              # show the registry
    python3 scripts/toolchains.py fetch <name>      # name in the registry

Registry entries:
    emsdk     Emscripten SDK (web builds)   — git clone + `emsdk install latest`
    wasi-sdk  WASI clang toolchain (native-runtime guests, todo 18)
    emception Emscripten-in-browser (pinned source commit; upstream ships
              no release assets — Docker-build project, jprendes/emception)

Idempotent: a second fetch of the same entry prints "present" and exits 0.
Checksum mismatch aborts with clean state (no partial install dirs).
"""
from __future__ import annotations

import argparse
import hashlib
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXTERNAL = REPO_ROOT / "external"
DOWNLOADS = EXTERNAL / "downloads"

WASI_SDK_VER = "34.0"
EMCEPTION_PIN = "5ff1975a5d286b200edc380ba8cc0de94d5c0cdc"

# wasi-sdk release asset for this host: (tar.gz name, sha256).
# Hashes below are of the upstream release tarballs (verified on this machine).
WASI_ASSETS: dict[str, dict[str, str]] = {
    "x86_64-macos": {
        "file": f"wasi-sdk-{WASI_SDK_VER}-x86_64-macos.tar.gz",
        "sha256": "87d27fa8adc68dee59bfbf2e22a6d34ef717c34d6bf1d8af2a56fc929d9ce0eb",
    },
    "arm64-macos": {
        "file": f"wasi-sdk-{WASI_SDK_VER}-arm64-macos.tar.gz",
        "sha256": "9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9",
    },
    "x86_64-linux": {
        "file": f"wasi-sdk-{WASI_SDK_VER}-x86_64-linux.tar.gz",
        # Filled in for CI (todo 22): ubuntu runners fetch wasi-sdk to build the
        # native-runtime (wasi) guests so the WAMR module suites run in CI.
        "sha256": "b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4",
    },
}


def _host_key() -> str:
    machine = "arm64" if platform.machine() == "arm64" else "x86_64"
    system = platform.system().lower()
    if system not in ("darwin", "linux"):
        raise SystemExit("error: wasi-sdk pin covers macOS/Linux only")
    return f"{machine}-{'macos' if system == 'darwin' else 'linux'}"


@dataclass
class Entry:
    name: str
    version: str
    pin: str          # sha256 (archives) / commit sha / "latest" (emsdk)
    target: Path      # install dir under external/
    kind: str         # "archive" | "git" | "emsdk"
    live_pin: str = ""  # filled per-host for archive entries
    live_file: str = ""
    live_url: str = ""


def registry() -> dict[str, Entry]:
    entries: list[Entry] = []

    emsdk_dir = EXTERNAL / "emsdk"
    entries.append(Entry("emsdk", "latest", "latest", emsdk_dir, "emsdk"))

    wasi_dir = EXTERNAL / f"wasi-sdk-{WASI_SDK_VER.split('.')[0]}"
    key = _host_key()
    asset = WASI_ASSETS.get(key)
    if asset is None:
        raise SystemExit(f"error: no wasi-sdk pin for host {key}")
    entries.append(Entry(
        "wasi-sdk",
        WASI_SDK_VER,
        asset["sha256"] or "(unpinned for this host — see scripts/toolchains.py)",
        wasi_dir,
        "archive",
        live_pin=asset["sha256"],
        live_file=asset["file"],
        live_url=(f"https://github.com/WebAssembly/wasi-sdk/releases/download/"
                  f"wasi-sdk-{WASI_SDK_VER.split('.')[0]}/{asset['file']}"),
    ))

    entries.append(Entry(
        "emception",
        "master@" + EMCEPTION_PIN[:8],
        EMCEPTION_PIN,
        EXTERNAL / "emception",
        "git",
    ))
    return {e.name: e for e in entries}


def _die(msg: str, cleanup: Path | None = None) -> None:
    if cleanup is not None:
        shutil.rmtree(cleanup, ignore_errors=True)
    raise SystemExit(f"error: {msg}")


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _present(entry: Entry) -> bool:
    if entry.name == "emsdk":
        # T5's install leaves upstream/emscripten in place; build.py looks there.
        return (entry.target / "upstream" / "emscripten").is_dir()
    if entry.name == "emception":
        if not entry.target.is_dir():
            return False
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=entry.target,
            capture_output=True, text=True, check=False).stdout.strip()
        return head == entry.pin
    # archive: install dir receipt
    return (entry.target / "bin" / "clang").exists()


def _fetch_archive(entry: Entry) -> None:
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    cached = DOWNLOADS / entry.live_file
    tmp = cached.with_suffix(cached.suffix + ".part")

    if not cached.exists():
        try:
            print(f"[toolchains] downloading {entry.name} {entry.version} "
                  f"-> {cached} ...")
            urllib.request.urlretrieve(entry.live_url, tmp)
            tmp.replace(cached)
        except OSError as e:
            _die(f"{entry.name}: download failed (offline? mirror unreachable?): {e}",
                 cleanup=tmp if tmp.exists() else None)
    else:
        print(f"[toolchains] {entry.name}: using cached archive {cached}")

    actual = _sha256(cached)
    if actual != entry.live_pin:
        # Bad / tampered archive: clean state, then abort.
        cached.unlink(missing_ok=True)
        _die(f"{entry.name}: checksum mismatch (expected {entry.live_pin[:16]}..., "
             f"got {actual[:16]}...). Corrupt cache removed; refetch to redownload.")
    print(f"[toolchains] {entry.name}: sha256 OK ({actual[:16]}...)")

    staging = DOWNLOADS / f".{entry.name}-staging"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    try:
        with tarfile.open(cached, "r:gz") as t:
            t.extractall(staging, filter="data")
    except (tarfile.TarError, OSError) as e:
        _die(f"{entry.name}: extraction failed: {e}", cleanup=staging)

    inner = next(p for p in staging.iterdir() if p.is_dir())
    if entry.target.exists():
        shutil.rmtree(entry.target)
    try:
        inner.rename(entry.target)  # atomic within downloads/ parent
    except OSError:
        shutil.move(str(inner), str(entry.target))
    shutil.rmtree(staging, ignore_errors=True)
    print(f"[toolchains] {entry.name}: installed to {entry.target}")


def _fetch_git(entry: Entry) -> None:
    try:
        print(f"[toolchains] cloning {entry.name} at {entry.pin[:12]} ...")
        subprocess.run(
            ["git", "clone", "https://github.com/jprendes/emception.git",
             str(entry.target)],
            check=True, capture_output=True)
        subprocess.run(
            ["git", "checkout", entry.pin], cwd=entry.target, check=True,
            capture_output=True)
    except subprocess.CalledProcessError as e:
        tail = (e.stderr or b"").decode(errors="replace").strip().splitlines()[-1:]
        shutil.rmtree(entry.target, ignore_errors=True)  # clean partial clone
        _die(f"{entry.name}: git clone/checkout failed "
             f"(offline? unreachable mirror?): {tail}")
    print(f"[toolchains] {entry.name}: source pinned to {entry.pin[:12]} "
          f"at {entry.target}")


def _fetch_emsdk(entry: Entry) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from emsdk_toolchain import install_emsdk
    install_emsdk(entry.target)


def fetch(entry: Entry) -> None:
    if _present(entry):
        print(f"[toolchains] {entry.name}: present ({entry.target})")
        return
    entry.target.parent.mkdir(parents=True, exist_ok=True)
    if entry.kind == "archive":
        try:
            _fetch_archive(entry)
        finally:
            staging = DOWNLOADS / f".{entry.name}-staging"
            shutil.rmtree(staging, ignore_errors=True)
    elif entry.kind == "git":
        _fetch_git(entry)
    else:
        try:
            _fetch_emsdk(entry)
        except (RuntimeError, subprocess.CalledProcessError) as e:
            _die(f"emsdk install failed (offline?): {e}",
                 cleanup=None)


def do_list(entries: dict[str, Entry]) -> None:
    print(f"{'name':<10} {'version':<16} {'pin':<24} {'path':<28} installed?")
    for e in entries.values():
        pin = e.pin if len(e.pin) <= 22 else e.pin[:19] + "..."
        print(f"{e.name:<10} {e.version:<16} {pin:<24} "
              f"{e.target.relative_to(REPO_ROOT)!s:<28} "
              f"{'yes' if _present(e) else 'no'}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("cmd", choices=["list", "fetch"])
    parser.add_argument("name", nargs="?", help="toolchain name for fetch")
    args = parser.parse_args()

    entries = registry()
    if args.cmd == "list":
        do_list(entries)
        return
    if args.name not in entries:
        _die(f"unknown toolchain '{args.name}'. "
             f"MUST-COMMAND: python3 scripts/toolchains.py list")
    fetch(entries[args.name])


if __name__ == "__main__":
    main()
