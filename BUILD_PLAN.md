# MoBaGEn Build System Plan

## Goal

Replace the `scripts/` folder with a single `build.py` Python script that handles all platforms:
`web`, `linux`, `osx`, `windows`, `ios`, `android`.

Priority: **web (Emscripten)** first. All platforms must be able to run and build.

---

## File Changes

| File | Action |
|---|---|
| `build.py` | Create |
| `android/` (full Gradle scaffold) | Create |
| `CMakeLists.txt` | Modify — add `ANDROID_MAIN_TARGET` renaming block |
| `external/sdl.cmake` | Modify — conditional `SDL_SHARED` for Android |
| `.gitignore` | Modify — add android generated dirs |
| `.github/workflows/web.yml` | Modify — drop Docker, use `python build.py web --install-deps` |
| `.github/workflows/linux.yml` | Modify — replace raw cmake with `python build.py linux` |
| `.github/workflows/osx.yml` | Modify — replace raw cmake with `python build.py osx` |
| `.github/workflows/windows.yml` | Modify — replace raw cmake with `python build.py windows` |
| `.github/workflows/release.yml` | Modify — update all nightly jobs to use build.py |
| `scripts/` (6 files) | Delete |

---

## CLI Design

```
python build.py <platform> [options]

Platforms:
  web       Emscripten/WASM via emsdk (cloned into external/emsdk/, git-ignored)
  linux     GCC/Clang native
  osx       Apple Clang / Xcode CLT
  windows   MSVC + ClangCL (VS 2019 or 2022, detected via vswhere.exe)
  ios       Xcode — device or simulator (macOS host only)
  android   NDK + Gradle → APK, optional ADB push/launch

Common options:
  --build-type  Debug|Release|MinSizeRel       [MinSizeRel]
  --target      CMake target to build          [all]
  --build-dir   Output directory               [build-<platform>]
  --clean       Wipe build dir before configure
  --install-deps  Auto-install missing toolchain (emsdk, NDK, cmdline-tools)
  --parallel N  Parallel jobs                  [os.cpu_count()]
  --run         web → serve :8000 | android → adb install+launch | ios → simctl
  --extra-cmake KEY=VAL  Pass -DKEY=VAL to cmake (repeatable)

ios / android only:
  --simulator   Target emulator/simulator instead of physical device

android only:
  --abi         arm64-v8a | x86_64 | both      [both]
  --apk-target  Example name from examples/    [required]
                Valid: catchthecat, chess, flocking, headless,
                       hideandseeksquared, life, maze, rmluidemo, scenario
```

---

## `build.py` Internal Structure

```
build.py
├── EXAMPLES = [...]           # auto-discovered from examples/ at startup
├── @dataclass BuildConfig     # all parsed CLI args + derived paths
├── run(cmd, env, cwd)         # subprocess wrapper, streams output live
│
├── class Platform (ABC)
│   ├── detect_toolchain()     # verify deps present; error with install hint
│   ├── configure_args()       # returns list of cmake -D flags + generator
│   ├── build()                # cmake --build ...
│   └── run_target()           # serve / adb / simctl
│
├── WebPlatform
│   ├── detect: find emcmake in PATH; else check external/emsdk/
│   ├── install (--install-deps):
│   │     git clone https://github.com/emscripten-core/emsdk.git external/emsdk/
│   │     ./emsdk install latest && ./emsdk activate latest
│   │     source emsdk_env.sh into subprocess env
│   ├── configure: emcmake cmake -DEMSCRIPTEN=1 -DENABLE_TEST_COVERAGE=OFF
│   │   build-dir: build-web/  output: build-web/bin/
│   └── run (--run): python -m http.server 8000 from build-web/bin/
│
├── LinuxPlatform
│   ├── detect: cmake + cc in PATH
│   │   required apt packages:
│   │     build-essential cmake mesa-common-dev libgl1-mesa-dev
│   │     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
│   └── configure: plain cmake
│
├── OsxPlatform
│   ├── detect: xcode-select -p (hint: xcode-select --install)
│   └── configure: plain cmake
│
├── WindowsPlatform
│   ├── detect: vswhere.exe → find VS 2019 or 2022 + ClangCL component
│   │   warn if ClangCL toolset not installed
│   ├── configure: -G "Visual Studio N YYYY" -T ClangCL -DENABLE_TEST_COVERAGE=OFF
│   └── build: cmake --build --config <build-type>
│
├── IosPlatform
│   ├── detect: must be macOS; xcode-select -p
│   │   device builds: warn that manual signing is required in Xcode
│   ├── configure device:
│   │     -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
│   │     -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO
│   ├── configure simulator:
│   │     -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator
│   │     -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
│   └── run --simulator:
│         xcrun simctl list → pick first booted device or boot default
│         xcrun simctl install <udid> <app.bundle>
│         xcrun simctl launch  <udid> gg.gameguild.mobagen
│
└── AndroidPlatform
    ├── detect SDK:
    │   $ANDROID_SDK_ROOT → $ANDROID_HOME
    │   → ~/Library/Android/sdk (mac) → ~/Android/Sdk (linux)
    │   → %LOCALAPPDATA%\Android\Sdk (win)
    ├── detect NDK: $ANDROID_NDK_HOME → SDK/ndk/<latest version>
    ├── detect Java: `java -version` → warn + suggest install if < 17
    ├── install (--install-deps):
    │     download cmdline-tools zip for host OS
    │     extract → sdkmanager "ndk;latest" "platform-tools"
    ├── validate --apk-target in EXAMPLES list
    ├── per ABI (arm64-v8a and/or x86_64):
    │   cmake configure with toolchain file, ANDROID_ABI, ANDROID_PLATFORM=android-24
    │   cmake build → .so in build-android-<abi>/libs/
    ├── copy SDL Java sources:
    │   find CPM cache → SDL3 source → android-project/app/src/main/java/org/libsdl/
    │   copy to android/app/src/main/java/org/libsdl/  (git-ignored)
    ├── write android/local.properties (sdk.dir=<SDK_ROOT>)
    ├── copy .so into android/app/src/main/jniLibs/<abi>/libmain.so
    ├── ./gradlew assembleDebug → android/app/build/outputs/apk/debug/app-debug.apk
    └── run (--run, adb device detected):
          adb install -r app-debug.apk
          adb shell am start -n gg.gameguild.mobagen/.MoBaGenActivity
```

---

## `android/` Gradle Scaffold

```
android/
├── .gitignore
│     # local.properties, build/, .gradle/
│     # app/src/main/java/org/libsdl/   ← copied from CPM SDL3 source at build time
│     # app/src/main/jniLibs/           ← .so files copied from cmake output
├── build.gradle             # AGP classpath
├── settings.gradle          # rootProject.name = "mobagen"; include ':app'
├── gradle.properties        # org.gradle.jvmargs, AndroidX
├── gradlew                  # unix launcher (executable)
├── gradlew.bat              # windows launcher
├── gradle/wrapper/
│   ├── gradle-wrapper.jar
│   └── gradle-wrapper.properties   # Gradle 8.6, bin distribution
└── app/
    ├── build.gradle
    │   ├── compileSdk 34, minSdk 24, targetSdk 34
    │   ├── applicationId "gg.gameguild.mobagen"
    │   ├── abiFilters "arm64-v8a", "x86_64"
    │   └── externalNativeBuild.cmake {
    │         path "../../CMakeLists.txt"
    │         arguments "-DANDROID_MAIN_TARGET=<from build.py>"
    │                   "-DENABLE_TEST_COVERAGE=OFF"
    │       }
    └── src/main/
        ├── AndroidManifest.xml       # SDLActivity-based, gg.gameguild.mobagen
        ├── java/gg/gameguild/mobagen/
        │   └── MoBaGenActivity.java  # extends SDLActivity
        ├── java/org/libsdl/app/      # .gitignored — copied by build.py
        └── res/
            ├── values/strings.xml
            └── mipmap-hdpi/ic_launcher.png
```

---

## CMakeLists.txt Addition (bottom of file)

```cmake
# Android: rename selected example target output to "main" so SDLActivity loads it
if(ANDROID AND DEFINED ANDROID_MAIN_TARGET)
  if(TARGET ${ANDROID_MAIN_TARGET})
    set_target_properties(${ANDROID_MAIN_TARGET} PROPERTIES OUTPUT_NAME "main")
  else()
    message(WARNING "ANDROID_MAIN_TARGET '${ANDROID_MAIN_TARGET}' not found")
  endif()
endif()
```

---

## external/sdl.cmake Change

Replace hardcoded `SDL_SHARED OFF` / `SDL_STATIC ON` with:

```cmake
if(ANDROID)
  set(_sdl_shared ON)
  set(_sdl_static OFF)
else()
  set(_sdl_shared OFF)
  set(_sdl_static ON)
endif()
# then use ${_sdl_shared} / ${_sdl_static} in CPMAddPackage OPTIONS
```

---

## CI Changes Summary

| Workflow | Before | After |
|---|---|---|
| `web.yml` | Docker `emscripten/emsdk` + `./scripts/emscripten-build.sh` | bare `ubuntu-latest` + `python build.py web --install-deps` |
| `linux.yml` | raw `cmake -Bbuild -H. ...` | `python build.py linux` |
| `osx.yml` | raw `cmake -Bbuild -H. ...` | `python build.py osx` |
| `windows.yml` | raw `cmake -H. -Bbuild ...` | `python build.py windows` |
| `release.yml` nightly | raw cmake per OS | `python build.py <platform>` |
| `release.yml` emscripten | Docker + `emscripten-build.sh` | bare Ubuntu + `python build.py web --install-deps` |

Build output: `build-web/bin/` replaces `bin-emscripten/bin/`. Release zip steps updated to match.

---

## .gitignore Additions

```
# Android generated/copied at build time
android/local.properties
android/app/build/
android/.gradle/
android/build/
android/app/src/main/java/org/libsdl/
android/app/src/main/jniLibs/
```

Note: `external/emsdk/` is already covered by the existing `external/*/` rule.

---

## Status

- [ ] `build.py` — web, linux, osx, windows
- [ ] `build.py` — ios, android
- [ ] `android/` Gradle scaffold
- [ ] `CMakeLists.txt` modification
- [ ] `external/sdl.cmake` modification
- [ ] `.gitignore` update
- [ ] CI workflows updated
- [ ] `scripts/` deleted
