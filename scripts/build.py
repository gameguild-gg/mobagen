#!/usr/bin/env python3
"""
MoBaGEn cross-platform build script.

Usage:
    python build.py <platform> [options]

Platforms:
    web       Emscripten/WASM (emsdk auto-installed to external/emsdk/)
    linux     GCC/Clang native
    osx       Apple Clang / Xcode CLT
    windows   MSVC + ClangCL (VS 2019 or 2022)
    ios       Xcode — device or simulator (macOS host only)
    android   NDK + Gradle -> APK, optional ADB push/launch

Run `python build.py <platform> --help` for per-platform options.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import textwrap
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Repo root (parent of the scripts/ directory containing this script)
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# Auto-discover app targets from apps/
# ---------------------------------------------------------------------------
def _discover_examples() -> list[str]:
    examples_dir = REPO_ROOT / "apps"
    if not examples_dir.is_dir():
        return []
    return sorted(
        p.name for p in examples_dir.iterdir() if p.is_dir() and not p.name.startswith(".")
    )

EXAMPLES: list[str] = _discover_examples()

# ---------------------------------------------------------------------------
# Colour helpers (no external deps)
# ---------------------------------------------------------------------------
_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None

def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOR else text

def info(msg: str)  -> None: print(_c("36", f"[build] {msg}"))
def ok(msg: str)    -> None: print(_c("32", f"[build] {msg}"))
def warn(msg: str)  -> None: print(_c("33", f"[build] WARN: {msg}"), file=sys.stderr)
def error(msg: str) -> None: print(_c("31", f"[build] ERROR: {msg}"), file=sys.stderr)

def die(msg: str, code: int = 1) -> None:
    error(msg)
    sys.exit(code)

# ---------------------------------------------------------------------------
# Subprocess helper
# ---------------------------------------------------------------------------
def run(
    cmd: list[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[dict] = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
    """Run a command, streaming output live."""
    display = " ".join(str(c) for c in cmd)
    info(f"$ {display}")
    merged_env = {**os.environ, **(env or {})}
    result = subprocess.run(cmd, cwd=cwd or REPO_ROOT, env=merged_env)
    if check and result.returncode != 0:
        die(f"Command failed (exit {result.returncode}): {display}")
    return result

def capture(cmd: list[str], *, cwd: Optional[Path] = None) -> str:
    """Run a command and return stdout, or '' on failure."""
    try:
        return subprocess.check_output(
            cmd, stderr=subprocess.DEVNULL, cwd=cwd or REPO_ROOT, text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""

# ---------------------------------------------------------------------------
# Build configuration dataclass
# ---------------------------------------------------------------------------
@dataclass
class BuildConfig:
    platform_name: str
    build_type: str = "MinSizeRel"
    target: str = "all"
    build_dir: Optional[Path] = None          # resolved in __post_init__
    clean: bool = False
    install_deps: bool = False
    parallel: int = field(default_factory=lambda: os.cpu_count() or 4)
    run_after: bool = False
    simulator: bool = False
    ios_team_id: Optional[str] = None          # iOS device signing
    ios_bundle_id: str = "gg.gameguild.mobagen"  # iOS launch identifier
    ios_device_udid: Optional[str] = None      # iOS physical device override
    ios_deployment_target: str = "15.0"       # iOS minimum OS version
    abi: str = "both"                          # android only
    apk_target: Optional[str] = None           # android only
    extra_cmake: list[str] = field(default_factory=list)

    def __post_init__(self) -> None:
        if self.build_dir is None:
            self.build_dir = REPO_ROOT / f"build-{self.platform_name}"
        else:
            self.build_dir = Path(self.build_dir).resolve()

# ---------------------------------------------------------------------------
# Abstract base Platform
# ---------------------------------------------------------------------------
class Platform(ABC):
    def __init__(self, cfg: BuildConfig) -> None:
        self.cfg = cfg

    @abstractmethod
    def detect_toolchain(self) -> None:
        """Check deps; die with a helpful message if missing."""

    @abstractmethod
    def configure_args(self) -> list[str]:
        """Return the full cmake configure command (list of strings)."""

    def build(self) -> None:
        cmd = [
            "cmake", "--build", str(self.cfg.build_dir),
            "--parallel", str(self.cfg.parallel),
        ]
        if self.cfg.target != "all":
            cmd += ["--target", self.cfg.target]
        run(cmd)

    def run_target(self) -> None:
        warn(f"--run not implemented for platform '{self.cfg.platform_name}'")

    # ---- shared helpers -------------------------------------------------- #

    def _cmake_configure(self, extra: Optional[list[str]] = None) -> None:
        build_dir = self.cfg.build_dir
        if self.cfg.clean and build_dir.exists():
            info(f"Cleaning {build_dir}")
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)

        cmd = self.configure_args()
        if extra:
            cmd += extra
        for kv in self.cfg.extra_cmake:
            cmd.append(f"-D{kv}" if not kv.startswith("-D") else kv)
        run(cmd)

    def execute(self) -> None:
        self.detect_toolchain()
        self._cmake_configure()
        self.build()
        if self.cfg.run_after:
            self.run_target()

# ---------------------------------------------------------------------------
# Web / Emscripten
# ---------------------------------------------------------------------------
class WebPlatform(Platform):
    EMSDK_DIR = REPO_ROOT / "external" / "emsdk"

    def _emsdk_bin(self) -> Path:
        suffix = ".bat" if platform.system() == "Windows" else ""
        return self.EMSDK_DIR / f"emsdk{suffix}"

    def _source_env(self) -> dict[str, str]:
        """Return env vars from emsdk_env by parsing its output."""
        env_script = self.EMSDK_DIR / "emsdk_env.sh"
        if not env_script.exists():
            return {}
        # Use bash to source and print the env diff
        try:
            out = subprocess.check_output(
                ["bash", "-c", f"source {env_script} > /dev/null 2>&1 && env"],
                text=True,
            )
            result = {}
            for line in out.splitlines():
                if "=" in line:
                    k, _, v = line.partition("=")
                    result[k] = v
            return result
        except Exception:
            return {}

    def _install_emsdk(self) -> None:
        if not self.EMSDK_DIR.exists():
            info("Cloning emsdk into external/emsdk/ ...")
            run(["git", "clone",
                 "https://github.com/emscripten-core/emsdk.git",
                 str(self.EMSDK_DIR)])
        else:
            info("emsdk directory already exists, skipping clone.")

        emsdk = self._emsdk_bin()
        info("Installing latest emsdk toolchain ...")
        run([str(emsdk), "install", "latest"], cwd=self.EMSDK_DIR)
        run([str(emsdk), "activate", "latest"], cwd=self.EMSDK_DIR)
        ok("emsdk installed and activated.")

    def detect_toolchain(self) -> None:
        if shutil.which("emcmake"):
            ok("emcmake found in PATH.")
            return

        emcmake_local = self.EMSDK_DIR / "upstream" / "emscripten" / "emcmake"
        if emcmake_local.exists():
            ok(f"emcmake found at {emcmake_local}")
            return

        if self.cfg.install_deps:
            self._install_emsdk()
        else:
            die(
                "emcmake not found.\n"
                "  Run with --install-deps to auto-install emsdk, or:\n"
                "    source external/emsdk/emsdk_env.sh\n"
                "  after running: python build.py web --install-deps"
            )

    def _emcmake(self) -> str:
        if shutil.which("emcmake"):
            return "emcmake"
        local = self.EMSDK_DIR / "upstream" / "emscripten" / "emcmake"
        if local.exists():
            return str(local)
        die("emcmake not found. Run with --install-deps first.")

    def configure_args(self) -> list[str]:
        return [
            self._emcmake(),
            "cmake",
            "-DCMAKE_C_ABI_COMPILED=ON",
            "-DCMAKE_CXX_ABI_COMPILED=ON",
            "-DEMSCRIPTEN=1",
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-DENABLE_TEST_COVERAGE=OFF",
            "-H.", f"-B{self.cfg.build_dir}",
        ]

    def _cmake_configure(self, extra: Optional[list[str]] = None) -> None:
        build_dir = self.cfg.build_dir
        if self.cfg.clean and build_dir.exists():
            info(f"Cleaning {build_dir}")
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)

        env = None
        cmd = self.configure_args()
        if extra:
            cmd += extra
        for kv in self.cfg.extra_cmake:
            cmd.append(f"-D{kv}" if not kv.startswith("-D") else kv)
        run(cmd, env=env)

    def build(self) -> None:
        env = None
        cmd = ["cmake", "--build", str(self.cfg.build_dir),
               "--parallel", str(self.cfg.parallel)]
        if self.cfg.target != "all":
            cmd += ["--target", self.cfg.target]
        run(cmd, env=env)

    def run_target(self) -> None:
        bin_dir = self.cfg.build_dir / "bin"
        if not bin_dir.exists():
            die(f"Build output not found at {bin_dir}. Build first.")
        info(f"Serving {bin_dir} at http://localhost:8000 (Ctrl+C to stop)")
        run([sys.executable, "-m", "http.server", "8000"], cwd=bin_dir, check=False)

# ---------------------------------------------------------------------------
# Linux
# ---------------------------------------------------------------------------
class LinuxPlatform(Platform):
    _APT_PACKAGES = [
        "build-essential", "cmake",
        "mesa-common-dev", "libgl1-mesa-dev",
        "libx11-dev", "libxrandr-dev", "libxinerama-dev",
        "libxcursor-dev", "libxi-dev", "libxss-dev",
        "libxtst-dev", "libxext-dev", "libxfixes-dev", "libxkbcommon-dev",
        "libxcb1-dev", "libx11-xcb-dev",
    ]

    def detect_toolchain(self) -> None:
        missing = []
        for tool in ("cmake", "cc"):
            if not shutil.which(tool):
                missing.append(tool)
        if missing:
            pkgs = " ".join(self._APT_PACKAGES)
            die(
                f"Missing tools: {', '.join(missing)}\n"
                f"  Install with:\n"
                f"    sudo apt install {pkgs}\n"
                f"    pip3 install jsonschema jinja2"
            )
        ok("Linux toolchain OK.")

    def configure_args(self) -> list[str]:
        return [
            "cmake",
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-H.", f"-B{self.cfg.build_dir}",
        ]

# ---------------------------------------------------------------------------
# macOS
# ---------------------------------------------------------------------------
class OsxPlatform(Platform):
    def detect_toolchain(self) -> None:
        if not shutil.which("cmake"):
            die("cmake not found. Install via: brew install cmake")
        xcode_path = capture(["xcode-select", "-p"])
        if not xcode_path:
            die(
                "Xcode Command Line Tools not found.\n"
                "  Install with: xcode-select --install"
            )
        ok(f"macOS toolchain OK (Xcode CLT at {xcode_path}).")

    def configure_args(self) -> list[str]:
        return [
            "cmake",
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-H.", f"-B{self.cfg.build_dir}",
        ]

# ---------------------------------------------------------------------------
# Windows
# ---------------------------------------------------------------------------
class WindowsPlatform(Platform):
    # vswhere locations
    _VSWHERE_PATHS = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
    ]

    def _find_vs(self) -> tuple[str, str]:
        """Return (generator_string, vs_install_path) or die."""
        vswhere = next((p for p in self._VSWHERE_PATHS if p.exists()), None)
        if vswhere is None:
            die(
                "vswhere.exe not found. Install Visual Studio 2019 or 2022 "
                "with the 'Desktop development with C++' workload."
            )

        for year, version in [("2022", "17"), ("2019", "16")]:
            path = capture([
                str(vswhere), "-version", f"[{version},",
                "-property", "installationPath",
                "-requires", "Microsoft.VisualStudio.Component.VC.Llvm.Clang",
            ])
            if path:
                generator = f"Visual Studio {version} {year}"
                return generator, path

        # Found VS but without ClangCL — warn and fall back to default toolset
        for year, version in [("2022", "17"), ("2019", "16")]:
            path = capture([str(vswhere), "-version", f"[{version},",
                            "-property", "installationPath"])
            if path:
                warn(
                    "ClangCL toolset not found in Visual Studio.\n"
                    "  Install it: VS Installer → Individual components → "
                    "'C++ Clang Compiler for Windows'.\n"
                    "  Falling back to default MSVC toolset."
                )
                generator = f"Visual Studio {version} {year}"
                return generator, path

        die("Visual Studio 2019 or 2022 not found.")

    def detect_toolchain(self) -> None:
        if platform.system() != "Windows":
            die("Windows platform selected but host is not Windows.")
        self._generator, self._vs_path = self._find_vs()
        ok(f"Found: {self._generator} at {self._vs_path}")

    def configure_args(self) -> list[str]:
        args = [
            "cmake",
            "-H.", f"-B{self.cfg.build_dir}",
            "-G", self._generator,
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-DENABLE_TEST_COVERAGE=OFF",
        ]
        # Only add -T ClangCL if ClangCL was found
        if "Llvm" in capture(["where", "clang-cl"], cwd=REPO_ROOT) or shutil.which("clang-cl"):
            args += ["-T", "ClangCL"]
        return args

    def build(self) -> None:
        run([
            "cmake", "--build", str(self.cfg.build_dir),
            "--config", self.cfg.build_type,
            "--parallel", str(self.cfg.parallel),
        ] + (["--target", self.cfg.target] if self.cfg.target != "all" else []))

# ---------------------------------------------------------------------------
# iOS
# ---------------------------------------------------------------------------
class IosPlatform(Platform):
    DEFAULT_BUNDLE_ID = "gg.gameguild.mobagen"
    DEFAULT_DEPLOYMENT_TARGET = "15.0"

    def _seed_dawn_generated_headers(self) -> None:
        dst_dir = (self.cfg.build_dir / "_deps" / "dawn-build" / "gen" / "include").resolve()

        source_include_dirs = [
            p for p in sorted(REPO_ROOT.glob("build-*/_deps/dawn-build/gen/include")) if p.is_dir()
        ]
        for src in source_include_dirs:
            src_resolved = src.resolve()
            # Ignore this build's own destination and copy from another successful build.
            if src_resolved == dst_dir:
                continue
            shutil.copytree(src_resolved, dst_dir, dirs_exist_ok=True)
            return

        # Fallback for first-ever iOS configure: seed only the compatibility
        # header Dawn expects at configure time.
        dawn_roots = sorted(REPO_ROOT.glob("external/dawn/*/include/webgpu/webgpu.h"))
        if dawn_roots:
            compat_dst = dst_dir / "dawn"
            compat_dst.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dawn_roots[-1], compat_dst / "webgpu.h")
            return

        # Dawn's Xcode/iOS configure path can validate generated headers before
        # the custom command runs. Seed the generated include tree from another
        # successful build so configure can complete, then let Dawn regenerate
        # it during the iOS build.

    def detect_toolchain(self) -> None:
        if platform.system() != "Darwin":
            die("iOS builds require a macOS host.")
        xcode_path = capture(["xcode-select", "-p"])
        if not xcode_path:
            die("Xcode not found. Install from the App Store, then run:\n"
                "  xcode-select --install")
        if not self.cfg.simulator:
            warn(
                "Device builds require code signing.\n"
                "  Pass --ios-team-id to auto-configure signing, or open the\n"
                "  generated Xcode project and configure your team manually.\n"
                "  Or use --simulator for unsigned simulator builds."
            )
            if not self.cfg.ios_team_id:
                warn(
                    "No --ios-team-id provided. Device build can still work if your\n"
                    "Xcode defaults already include signing for this project."
                )
        ok(f"Xcode found at {xcode_path}.")

    def _cmake_configure(self, extra: Optional[list[str]] = None) -> None:
        build_dir = self.cfg.build_dir
        if self.cfg.clean and build_dir.exists():
            info(f"Cleaning {build_dir}")
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)

        self._seed_dawn_generated_headers()

        env = None
        cmd = self.configure_args()
        if extra:
            cmd += extra
        for kv in self.cfg.extra_cmake:
            cmd.append(f"-D{kv}" if not kv.startswith("-D") else kv)
        run(cmd, env=env)

    def configure_args(self) -> list[str]:
        args = [
            "cmake",
            "-G", "Xcode",
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-DCMAKE_SYSTEM_NAME=iOS",
            f"-DCMAKE_OSX_DEPLOYMENT_TARGET={self.cfg.ios_deployment_target}",
            f"-DIOS_DEPLOYMENT_TARGET={self.cfg.ios_deployment_target}",
            "-DENABLE_TEST_COVERAGE=OFF",
            "-H.", f"-B{self.cfg.build_dir}",
        ]
        if self.cfg.simulator:
            args += [
                "-DCMAKE_OSX_SYSROOT=iphonesimulator",
                "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
                "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO",
                "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=",
                f"-DIOS_BUNDLE_ID={self.cfg.ios_bundle_id}",
            ]
        else:
            args += [
                "-DCMAKE_OSX_ARCHITECTURES=arm64",
                "-DIOS_CODE_SIGN_STYLE=Automatic",
                f"-DIOS_BUNDLE_ID={self.cfg.ios_bundle_id}",
            ]
            if self.cfg.ios_team_id:
                args += [f"-DIOS_DEVELOPMENT_TEAM={self.cfg.ios_team_id}"]
        return args

    def build(self) -> None:
        sdk = "iphonesimulator" if self.cfg.simulator else "iphoneos"
        cmd = [
            "cmake", "--build", str(self.cfg.build_dir),
            "--config", self.cfg.build_type,
        ]
        if self.cfg.target != "all":
            cmd += ["--target", self.cfg.target]
        cmd += ["--", "-sdk", sdk, "-allowProvisioningUpdates"]
        run(cmd)

    def _find_app_bundle(self) -> Path:
        app_bundles = sorted(self.cfg.build_dir.rglob("*.app"))
        if not app_bundles:
            die(f"No .app bundle found in {self.cfg.build_dir}")
        preferred_name = f"{self.cfg.target}.app" if self.cfg.target != "all" else None
        build_type = self.cfg.build_type  # e.g. "MinSizeRel"

        # Helper: check if a bundle has the actual executable binary
        def is_valid(app: Path) -> bool:
            # The executable must exist inside the bundle (binary plist check is unreliable)
            exec_name = app.stem  # e.g. "rmluidemo" from "rmluidemo.app"
            return (app / exec_name).exists()

        # 1. Prefer bundles matching target name AND build config directory AND valid plist
        if preferred_name:
            for app in app_bundles:
                if app.name == preferred_name and build_type in app.parts and is_valid(app):
                    return app
            # 2. Any bundle matching target name that is valid
            for app in app_bundles:
                if app.name == preferred_name and is_valid(app):
                    return app
            # 3. Any bundle matching target name (even without plist, last resort)
            for app in app_bundles:
                if app.name == preferred_name:
                    return app
        # 4. First valid bundle
        for app in app_bundles:
            if is_valid(app):
                return app
        return app_bundles[0]

    def _find_connected_device_udid(self) -> Optional[str]:
        # xctrace output is simple plain text and available with Xcode CLT.
        devices_out = capture(["xcrun", "xctrace", "list", "devices"])
        if not devices_out:
            return None

        import re
        in_devices_section = False
        for line in devices_out.splitlines():
            line = line.strip()
            if line.startswith("=="):
                in_devices_section = line == "== Devices =="
                continue
            if not in_devices_section:
                continue
            if not line or "Simulator" in line or "Mac" in line:
                continue
            if "iPhone" not in line and "iPad" not in line:
                continue
            match = re.search(r"\(([0-9A-Fa-f-]{8,})\)\s*$", line)
            if match:
                return match.group(1)
        return None

    def _run_on_device(self) -> None:
        app_bundle = self._find_app_bundle()
        info(f"Using app bundle: {app_bundle}")

        udid = self.cfg.ios_device_udid or self._find_connected_device_udid()
        if not udid:
            die(
                "No connected iOS device detected.\n"
                "  Connect your iPad via USB, unlock it, and tap 'Trust'.\n"
                "  You can also pass --ios-device-udid explicitly."
            )

        if not capture(["xcrun", "devicectl", "help"]):
            die(
                "xcrun devicectl is unavailable.\n"
                "  Install/update Xcode (15+) and Command Line Tools."
            )

        bundle_id = self.cfg.ios_bundle_id or self.DEFAULT_BUNDLE_ID
        run(["xcrun", "devicectl", "device", "install", "app", "--device", udid, str(app_bundle)])
        run(["xcrun", "devicectl", "device", "process", "launch", "--device", udid, bundle_id])
        ok(f"Launched {bundle_id} on iOS device {udid}")

    def run_target(self) -> None:
        if not self.cfg.simulator:
            self._run_on_device()
            return

        # Find the .app bundle
        app_bundle = self._find_app_bundle()
        info(f"Using app bundle: {app_bundle}")

        # Boot a simulator if none is running
        booted = capture(["xcrun", "simctl", "list", "devices", "booted", "-j"])
        if '"udid"' not in booted:
            info("No booted simulator found. Booting default iPhone simulator ...")
            # Get a suitable device UDID
            devices_json = capture(["xcrun", "simctl", "list", "devices", "available", "-j"])
            # Simple heuristic: find last iPhone entry
            udid = ""
            for line in devices_json.splitlines():
                if '"udid"' in line:
                    udid = line.split('"')[3]
            if not udid:
                die("Could not find an available simulator. Open Xcode → Simulator first.")
            run(["xcrun", "simctl", "boot", udid])
        else:
            # Extract first booted UDID
            udid = ""
            for line in booted.splitlines():
                if '"udid"' in line:
                    udid = line.split('"')[3]
                    break

        run(["xcrun", "simctl", "install", udid, str(app_bundle)])
        bundle_id = self.cfg.ios_bundle_id or self.DEFAULT_BUNDLE_ID
        run(["xcrun", "simctl", "launch", udid, bundle_id])
        info(f"Launched {bundle_id} on simulator {udid}")

# ---------------------------------------------------------------------------
# Android
# ---------------------------------------------------------------------------
_ANDROID_SDK_CANDIDATES = {
    "Darwin": [
        Path.home() / "Library" / "Android" / "sdk",
    ],
    "Linux": [
        Path.home() / "Android" / "Sdk",
        Path("/opt/android-sdk"),
    ],
    "Windows": [
        Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk",
    ],
}

_CMDLINE_TOOLS_URLS = {
    "Darwin":  "https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip",
    "Linux":   "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip",
    "Windows": "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip",
}

class AndroidPlatform(Platform):
    ABIS = ["arm64-v8a", "x86_64"]
    APP_ID = "gg.gameguild.mobagen"
    MIN_SDK = 24

    def _find_sdk(self) -> Optional[Path]:
        # Env vars first
        for var in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
            val = os.environ.get(var)
            if val and Path(val).exists():
                return Path(val)
        # Common locations
        for p in _ANDROID_SDK_CANDIDATES.get(platform.system(), []):
            if p.exists():
                return p
        return None

    def _find_ndk(self, sdk: Path) -> Optional[Path]:
        # Explicit env var
        val = os.environ.get("ANDROID_NDK_HOME")
        if val and Path(val).exists():
            return Path(val)
        # SDK/ndk/<version>
        ndk_dir = sdk / "ndk"
        if ndk_dir.is_dir():
            versions = sorted(ndk_dir.iterdir(), reverse=True)
            if versions:
                return versions[0]
        # SDK/ndk-bundle (older layout)
        bundle = sdk / "ndk-bundle"
        if bundle.exists():
            return bundle
        return None

    def _check_java(self) -> None:
        java_out = capture(["java", "-version"])
        if not java_out:
            # java -version goes to stderr
            try:
                java_out = subprocess.check_output(
                    ["java", "-version"], stderr=subprocess.STDOUT, text=True
                )
            except (subprocess.CalledProcessError, FileNotFoundError):
                java_out = ""
        if not java_out:
            warn(
                "Java not found. Android builds require Java 17.\n"
                "  Install: https://adoptium.net/  or via brew: brew install openjdk@17"
            )
            return
        # Extract major version
        import re
        m = re.search(r'version "(\d+)', java_out)
        if not m:
            m = re.search(r'version "1\.(\d+)', java_out)
            major = int(m.group(1)) if m else 0
        else:
            major = int(m.group(1))
        if major < 17:
            warn(
                f"Java {major} found but Android Gradle Plugin 8.x requires Java 17+.\n"
                "  Install: https://adoptium.net/  or: brew install openjdk@17\n"
                "  Then set JAVA_HOME accordingly."
            )
        else:
            ok(f"Java {major} OK.")

    def _install_sdk(self) -> Path:
        import urllib.request
        import zipfile

        sdk_root = Path.home() / "Android" / "Sdk"
        sdk_root.mkdir(parents=True, exist_ok=True)
        system = platform.system()
        url = _CMDLINE_TOOLS_URLS.get(system)
        if not url:
            die(f"No cmdline-tools URL for system: {system}")

        zip_path = sdk_root / "cmdline-tools.zip"
        info(f"Downloading Android cmdline-tools from {url} ...")
        urllib.request.urlretrieve(url, zip_path)

        info("Extracting cmdline-tools ...")
        with zipfile.ZipFile(zip_path, "r") as z:
            z.extractall(sdk_root / "cmdline-tools")
        zip_path.unlink()

        # Rename extracted 'cmdline-tools' to 'latest' per SDK structure
        extracted = sdk_root / "cmdline-tools" / "cmdline-tools"
        target = sdk_root / "cmdline-tools" / "latest"
        if extracted.exists() and not target.exists():
            extracted.rename(target)

        # Accept licenses and install NDK + platform-tools
        sdkmanager = target / "bin" / ("sdkmanager.bat" if system == "Windows" else "sdkmanager")
        env = {**os.environ, "ANDROID_SDK_ROOT": str(sdk_root)}
        # Accept all licenses
        proc = subprocess.Popen(
            [str(sdkmanager), "--licenses"],
            stdin=subprocess.PIPE, env=env,
        )
        proc.communicate(input=b"y\n" * 20)

        run([str(sdkmanager), "ndk;latest", "platform-tools",
             f"platforms;android-{self.MIN_SDK}"], env=env)
        ok(f"Android SDK installed at {sdk_root}")
        return sdk_root

    def detect_toolchain(self) -> None:
        # Validate --apk-target
        if not self.cfg.apk_target:
            die(
                "--apk-target is required for Android builds.\n"
                f"  Valid targets: {', '.join(EXAMPLES)}"
            )
        if self.cfg.apk_target not in EXAMPLES:
            die(
                f"Unknown --apk-target '{self.cfg.apk_target}'.\n"
                f"  Valid targets: {', '.join(EXAMPLES)}"
            )

        self._check_java()

        # Find or install SDK
        sdk = self._find_sdk()
        if sdk is None:
            if self.cfg.install_deps:
                sdk = self._install_sdk()
            else:
                die(
                    "Android SDK not found.\n"
                    "  Set ANDROID_SDK_ROOT or ANDROID_HOME, or use --install-deps.\n"
                    "  Common locations:\n"
                    "    macOS:   ~/Library/Android/sdk\n"
                    "    Linux:   ~/Android/Sdk\n"
                    "    Windows: %LOCALAPPDATA%\\Android\\Sdk"
                )
        self._sdk = sdk
        ok(f"Android SDK: {sdk}")

        # Find or install NDK
        ndk = self._find_ndk(sdk)
        if ndk is None:
            if self.cfg.install_deps:
                # sdkmanager already installed ndk;latest above — try again
                ndk = self._find_ndk(sdk)
            if ndk is None:
                die(
                    "Android NDK not found.\n"
                    "  Set ANDROID_NDK_HOME, or use --install-deps.\n"
                    f"  Or install manually: sdkmanager 'ndk;latest'"
                )
        self._ndk = ndk
        ok(f"Android NDK: {ndk}")

        # Resolve selected ABIs
        if self.cfg.abi == "both":
            self._abis = self.ABIS
        else:
            self._abis = [self.cfg.abi]

    def configure_args(self) -> list[str]:
        # Not used directly — we call per-ABI configure in execute()
        return []

    def _configure_abi(self, abi: str) -> Path:
        build_dir = REPO_ROOT / f"build-android-{abi}"
        if self.cfg.clean and build_dir.exists():
            info(f"Cleaning {build_dir}")
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)

        toolchain = self._ndk / "build" / "cmake" / "android.toolchain.cmake"
        cmd = [
            "cmake",
            "-H.", f"-B{build_dir}",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DANDROID_ABI={abi}",
            f"-DANDROID_PLATFORM=android-{self.MIN_SDK}",
            f"-DANDROID_MAIN_TARGET={self.cfg.apk_target}",
            f"-DCMAKE_BUILD_TYPE={self.cfg.build_type}",
            "-DENABLE_TEST_COVERAGE=OFF",
        ]
        for kv in self.cfg.extra_cmake:
            cmd.append(f"-D{kv}" if not kv.startswith("-D") else kv)
        run(cmd)
        return build_dir

    def _build_abi(self, build_dir: Path) -> None:
        run(["cmake", "--build", str(build_dir),
             "--parallel", str(self.cfg.parallel)])

    def _find_cpm_sdl_source(self) -> Optional[Path]:
        """Locate SDL3 source downloaded by CPM."""
        cpm_cache = os.environ.get("CPM_SOURCE_CACHE", "")
        search_roots = []
        if cpm_cache:
            search_roots.append(Path(cpm_cache))
        # Common CPM cache locations
        search_roots += [
            REPO_ROOT / "build-android-arm64-v8a" / "_deps" / "sdl3-src",
            REPO_ROOT / "build-android-x86_64" / "_deps" / "sdl3-src",
        ]
        # Also look in any build dir _deps
        for d in REPO_ROOT.glob("build-*/_deps/sdl3-src"):
            search_roots.append(d)

        for root in search_roots:
            if root.is_dir():
                # Check for android-project inside
                candidate = root / "android-project"
                if candidate.is_dir():
                    return root
        return None

    def _copy_sdl_java(self) -> None:
        """Copy SDL3 Android Java sources into android/ (git-ignored)."""
        sdl_src = self._find_cpm_sdl_source()
        if sdl_src is None:
            warn(
                "Could not find CPM-downloaded SDL3 source to copy Java files.\n"
                "  Run 'cmake -H. -Bbuild-android-arm64-v8a ...' first, "
                "or set CPM_SOURCE_CACHE."
            )
            return

        src_java = sdl_src / "android-project" / "app" / "src" / "main" / "java" / "org"
        dst_java = REPO_ROOT / "platforms" / "android" / "app" / "src" / "main" / "java" / "org"

        if not src_java.is_dir():
            warn(f"SDL3 Java sources not found at expected path: {src_java}")
            return

        if dst_java.exists():
            shutil.rmtree(dst_java)
        shutil.copytree(src_java, dst_java)
        ok(f"Copied SDL3 Java sources → {dst_java}")

    def _copy_so_files(self, abi_dirs: dict[str, Path]) -> None:
        """Copy built .so files into android/app/src/main/jniLibs/<abi>/.

        Both `libmain.so` (the example target) and `libSDL3.so` (the SDL3
        shared library that libmain.so links against) need to land in
        jniLibs so Android's `System.loadLibrary` can find them at
        runtime. AGP is configured NOT to run CMake itself
        (no `externalNativeBuild` block) so it does not auto-merge
        anything — we have to do it explicitly here.
        """
        # Files we always need: example binary + its SDL3 dependency.
        # The example comes out as `libmain.so` (since we now build it as
        # a SHARED library in CMakeLists.txt). SDL3 is built as part of
        # CPM and lands next to the other libs in build/libs/.
        required = ("libmain.so", "libSDL3.so")

        for abi, build_dir in abi_dirs.items():
            dest_dir = REPO_ROOT / "platforms" / "android" / "app" / "src" / "main" / "jniLibs" / abi
            dest_dir.mkdir(parents=True, exist_ok=True)

            for name in required:
                hits = list(build_dir.rglob(name))
                if not hits:
                    warn(f"{name} not found in {build_dir}. Searched recursively.")
                    continue
                src = hits[0]
                dst = dest_dir / name
                shutil.copy2(src, dst)
                ok(f"Copied {src} → {dst}")

    def _write_local_properties(self) -> None:
        lp = REPO_ROOT / "platforms" / "android" / "local.properties"
        sdk_path = str(self._sdk).replace("\\", "\\\\")
        ndk_path = str(self._ndk).replace("\\", "\\\\")
        lp.write_text(
            f"sdk.dir={sdk_path}\n"
            f"ndk.dir={ndk_path}\n"
        )
        ok(f"Wrote {lp}")

    def _run_gradle(self) -> Path:
        android_dir = REPO_ROOT / "platforms" / "android"
        gradlew = android_dir / ("gradlew.bat" if platform.system() == "Windows" else "gradlew")
        if not gradlew.exists():
            die(f"Gradle wrapper not found at {gradlew}. "
                "Make sure the platforms/android/ scaffold exists.")
        gradlew.chmod(gradlew.stat().st_mode | 0o111)  # ensure executable

        run(
            [str(gradlew), "assembleDebug",
             f"-PANDROID_MAIN_TARGET={self.cfg.apk_target}"],
            cwd=android_dir,
            env={**os.environ, "ANDROID_SDK_ROOT": str(self._sdk)},
        )
        apk = android_dir / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
        if apk.exists():
            ok(f"APK built: {apk}")
        else:
            warn(f"APK not found at expected location: {apk}")
        return apk

    def execute(self) -> None:
        self.detect_toolchain()

        # Configure + build per ABI
        abi_dirs: dict[str, Path] = {}
        for abi in self._abis:
            info(f"--- Building ABI: {abi} ---")
            build_dir = self._configure_abi(abi)
            self._build_abi(build_dir)
            abi_dirs[abi] = build_dir

        # Prepare android/ Gradle project
        self._write_local_properties()
        self._copy_sdl_java()
        self._copy_so_files(abi_dirs)

        # Build APK
        apk = self._run_gradle()

        if self.cfg.run_after:
            self._adb_install_and_launch(apk)

    def _adb_install_and_launch(self, apk: Path) -> None:
        adb = shutil.which("adb")
        if not adb:
            # Try SDK platform-tools
            adb_candidate = self._sdk / "platform-tools" / (
                "adb.exe" if platform.system() == "Windows" else "adb"
            )
            if adb_candidate.exists():
                adb = str(adb_candidate)
        if not adb:
            warn("adb not found. Add Android SDK platform-tools to PATH.")
            return

        # Check for connected devices
        devices_out = capture([adb, "devices"])
        lines = [l for l in devices_out.splitlines()[1:] if l.strip() and "offline" not in l]
        if not lines:
            warn("No ADB device or emulator connected. Skipping install/launch.")
            return

        device_id = lines[0].split()[0]
        info(f"ADB device found: {device_id}")

        run([adb, "-s", device_id, "install", "-r", str(apk)])
        run([adb, "-s", device_id, "shell", "am", "start",
             "-n", f"{self.APP_ID}/.MoBaGenActivity"])
        ok(f"Launched {self.APP_ID} on {device_id}")

# ---------------------------------------------------------------------------
# Platform registry
# ---------------------------------------------------------------------------
PLATFORMS: dict[str, type[Platform]] = {
    "web":     WebPlatform,
    "linux":   LinuxPlatform,
    "osx":     OsxPlatform,
    "windows": WindowsPlatform,
    "ios":     IosPlatform,
    "android": AndroidPlatform,
}

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="build.py",
        description=textwrap.dedent("""\
            MoBaGEn cross-platform build script.
            Replaces the scripts/ folder for all platforms.
        """),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "platform",
        choices=list(PLATFORMS.keys()),
        help="Target platform to build for.",
    )
    parser.add_argument(
        "--build-type", default="MinSizeRel",
        choices=["Debug", "Release", "MinSizeRel"],
        help="CMake build type. [default: MinSizeRel]",
    )
    parser.add_argument(
        "--target", default="all",
        help="CMake target to build. [default: all]",
    )
    parser.add_argument(
        "--build-dir", default=None,
        help="Build output directory. [default: build-<platform>]",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="Wipe build directory before configuring.",
    )
    parser.add_argument(
        "--install-deps", action="store_true",
        help="Auto-install missing toolchain (emsdk, Android SDK/NDK, etc.).",
    )
    parser.add_argument(
        "--parallel", type=int, default=os.cpu_count() or 4,
        metavar="N",
        help=f"Parallel build jobs. [default: {os.cpu_count() or 4}]",
    )
    parser.add_argument(
        "--run", dest="run_after", action="store_true",
        help=(
            "After building: web → serve on :8000 | "
            "android → adb install+launch | ios → launch on simulator/device."
        ),
    )
    parser.add_argument(
        "--simulator", action="store_true",
        help="[ios/android] Target simulator/emulator instead of physical device.",
    )
    parser.add_argument(
        "--ios-team-id", default=None,
        help="[ios] Apple Developer Team ID for automatic code signing on device builds.",
    )
    parser.add_argument(
        "--ios-bundle-id", default=IosPlatform.DEFAULT_BUNDLE_ID,
        help=f"[ios] Product bundle identifier. [default: {IosPlatform.DEFAULT_BUNDLE_ID}]",
    )
    parser.add_argument(
        "--ios-device-udid", default=None,
        help="[ios] Physical device UDID override (uses first connected device by default).",
    )
    parser.add_argument(
        "--ios-deployment-target", default=IosPlatform.DEFAULT_DEPLOYMENT_TARGET,
        help=f"[ios] Minimum iOS version to target. [default: {IosPlatform.DEFAULT_DEPLOYMENT_TARGET}]",
    )
    parser.add_argument(
        "--abi", default="both",
        choices=["arm64-v8a", "x86_64", "both"],
        help="[android] ABI(s) to build. [default: both]",
    )
    parser.add_argument(
        "--apk-target", default=None,
        metavar="EXAMPLE",
        help=(
            f"[android] Example target to package into the APK. "
            f"Valid: {', '.join(EXAMPLES)}. [required for android]"
        ),
    )
    parser.add_argument(
        "--extra-cmake", action="append", default=[],
        metavar="KEY=VAL",
        help="Pass extra -DKEY=VAL to cmake (repeatable).",
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    cfg = BuildConfig(
        platform_name=args.platform,
        build_type=args.build_type,
        target=args.target,
        build_dir=args.build_dir,
        clean=args.clean,
        install_deps=args.install_deps,
        parallel=args.parallel,
        run_after=args.run_after,
        simulator=args.simulator,
        ios_team_id=args.ios_team_id,
        ios_bundle_id=args.ios_bundle_id,
        ios_device_udid=args.ios_device_udid,
        ios_deployment_target=args.ios_deployment_target,
        abi=args.abi,
        apk_target=args.apk_target,
        extra_cmake=args.extra_cmake,
    )

    platform_cls = PLATFORMS[args.platform]
    p = platform_cls(cfg)

    info(f"Platform : {args.platform}")
    info(f"Build dir: {cfg.build_dir}")
    info(f"Build type: {cfg.build_type}")
    info(f"Target   : {cfg.target}")

    p.execute()
    ok("Build complete.")


if __name__ == "__main__":
    main()
