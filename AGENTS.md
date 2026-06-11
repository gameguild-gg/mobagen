# MoBaGEn — Agent Quick Reference

## Project

C++ educational game engine (WebGPU + SDL3). Targets native (macOS/Linux/Windows) and WebAssembly via Emscripten.

## Build System

- **CMake ≥ 3.16.3** required. **In-source builds are forbidden** (CMake fatal-errors).
- **No vcpkg/conan.** All deps fetched at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) (`external/cpm.cmake`).
- Cold configure is slow (~5–10 min) because Dawn (WebGPU) is built from source. Set `CPM_SOURCE_CACHE` to a persistent path to avoid re-downloading:
  ```bash
  export CPM_SOURCE_CACHE=~/.cpm_cache
  ```

## Key Commands

### Native build (macOS / Linux)
```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build/ --parallel
```

### Windows (ClangCL preferred)
```bat
cmake -H. -Bbuild -G "Visual Studio 16 2019" -T ClangCL -DCMAKE_BUILD_TYPE=MinSizeRel -DENABLE_TEST_COVERAGE=OFF
cmake --build build --config MinSizeRel --parallel
```

### Tests only
```bash
cmake -S. -Bbuild -DENABLE_TEST_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel --target CoreTests
cd build/test && ctest --build-config Debug
```
`ctest` must be invoked from inside `build/test/`, not `build/`. `ENABLE_TEST_COVERAGE` must be **OFF on Windows**.

### Format check / fix
```bash
# Requires: pip3 install clang-format==14.0.6 cmake_format==0.6.11 pyyaml
cmake -S. -BbuildStyle -DENABLE_TEST_COVERAGE=ON
cmake --build buildStyle --target check-format   # CI check
cmake --build buildStyle --target fix-format      # local auto-fix
```

### Emscripten / WebAssembly
```bash
python scripts/build.py web --install-deps   # installs emsdk + builds; output in build-web/bin/
```

## CMake Options Worth Knowing

| Option | Default | Notes |
|---|---|---|
| `ENABLE_TEST_COVERAGE` | ON (OFF on Win/Emscripten) | Adds coverage flags to `core` library, not the test binary |
| `BUILD_EXAMPLES` | ON | Adds all subdirs under `apps/` (examples + editor) |
| `CXX_STANDARD_TARGET` | C++23 (forced) | Override: `20`, `23`, or `26` |
| `USE_GDCM` | OFF | Build GDCM (DICOM parsing) for volume_io module |
| `USE_ITK` | OFF | Build ITK (medical image processing) |
| `USE_VTK` | OFF | Build VTK (visualization toolkit) |
| `BUILD_CORE_DEMOS` | ON | Build engine learning demos under `apps/core_demos/` |
| `BUILD_VOLUME_DEMOS` | ON | Build volume smoke tests under `apps/volume_demos/` |
| `BUILD_DICOM_VIEWER` | ON | Build the DICOM viewer app |
| `BUILD_PROTOTYPES` | OFF | Build learning prototypes (`dawn_probe`, `fibers_prototype`)
| `USE_SANITIZER` | — | Address/Thread/Undefined etc. |
| `USE_CCACHE` | — | Enable ccache |

## Architecture

The engine uses a **Data-Oriented Design (DOD)** core with an **Entity-Component-System (ECS)** architecture.

### Core Subsystems (`core/sources/`)

| Subsystem | Target | Type | Description |
|-----------|--------|------|-------------|
| ecs | `mobagen::core_ecs` | INTERFACE | Entity-component registry (World, Storage, SparseSet) |
| jobs | `mobagen::core_jobs` | STATIC | Work-stealing scheduler + coroutine tasks + Chase-Lev deque |
| reactive | `mobagen::core_reactive` | INTERFACE | Signal/Computed/Effect reactive state (Angular-style) |
| messaging | `mobagen::core_messaging` | INTERFACE | Notifier (multicast) + EventBus (async by-type) |
| scene | `mobagen::core_scene` | INTERFACE | Transform hierarchy + TransformSystem (linearized resolve) |
| camera | `mobagen::core_camera` | INTERFACE | Orbit + WASD camera with perspective/orthographic |
| input | `mobagen::core_input` | INTERFACE | SDL-agnostic input state (key/mouse edge detection) |
| render | `mobagen::core_render` | INTERFACE | RenderBridge (ECS → flat draw commands) + volume types |
| resource | `mobagen::core_resource` | INTERFACE | Stable-handle asset store (ResourceRegistry<T>) |
| net | `mobagen::core_net` | INTERFACE | Networking (gated behind BUILD_WEBREQUEST) |

- **`core/`** — umbrella INTERFACE target (`mobagen::core`) linking all subsystems + SDL3, SDL3_image, ImGui, Dawn WebGPU, RmlUi. All consumers get these transitively; do **not** re-link them in app/editor CMakeLists.
- **`apps/`** / **`modules/`** — each subdirectory is auto-discovered via `subdirlist` macro. Adding a new directory is sufficient; no parent CMakeLists edit needed.
- **`test/`** — doctest 2.4.12. Single binary: `CoreTests`. Format targets are also configured here.
- **`external/`** — one `.cmake` file per third-party lib. `external.cmake` is the aggregator.

### Renderer

**WebGPU / Dawn only.** No WebGL, no OpenGL, no GLEW. 2D rendering uses ImGui background draw list (`ImGui::GetBackgroundDrawList()`).

## Platform Quirks

- **macOS:** `imgui_impl_wgpu.cpp` is compiled as Objective-C++ (`-x objective-c++ -fno-objc-arc`) and links Cocoa/Metal/QuartzCore. Handled in `external/imgui.cmake`.
- **Emscripten:**
  - Use `-sUSE_SDL=0` — SDL3 built from source, not the Emscripten port.
  - Do **not** use `-sUSE_WEBGPU=1` (deprecated). WebGPU comes from the `emdawnwebgpu` port in the Dawn tree.
  - Output: `bin-emscripten/bin/` (not `build/bin/`).

## Code Style

- clang-format: Google style, column limit **150**, indent **2**, `SortIncludes: Never`.
- clang-tidy: broad checks minus `abseil-*`, `android-*`, `fuchsia-*`, `google-*`, `llvm*`.
- Exact pip versions for CI: `clang-format==14.0.6`, `cmake_format==0.6.11`.

## Linux System Dependencies

```bash
sudo apt install build-essential cmake mesa-common-dev libgl1-mesa-dev \
  libx11-dev mesa-utils libgl-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev
pip3 install jsonschema jinja2
```

## CI

All workflows trigger on every push/PR. Key jobs: `linux.yml`, `osx.yml`, `windows.yml`, `test.yml` (coverage), `web.yml` (Emscripten → GitHub Pages), `style.yml`. CPM modules are cached keyed on CMakeLists hash.

## Notes for Agents

- Many examples under `apps/` are **intentionally incomplete** — they are student exercises. Do not "fix" stub implementations unless asked.
- `opencode.json` is configured at the repo root. It points `instructions` at this file and registers a local `agentmemory` MCP server (started via `npx -y @agentmemory/mcp`). `.opencode/` is empty.
- Release is managed by semantic-release; `CHANGELOG.md` is auto-generated. Do not edit it manually.
