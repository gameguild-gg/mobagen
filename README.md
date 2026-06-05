# DICOM Renderer

A WebAssembly-first GPU renderer. The primary goal is **DICOM volume ray casting**
(rendering medical CT/MRI scans by marching rays through a 3D volume); a later,
secondary goal is a **mesh ray tracer** for a personal game engine. Both share the
same ray-per-pixel foundation.

Built as a learning project: the code grows one understandable rung at a time, and
every step renders something you can see.

**Current stage:** WebGL has the synthetic-volume ray caster; WebGPU has the
Dawn/emdawnwebgpu + SDL3 + ImGui host that the DICOM renderer will move onto.
**Goal:** see the north star in [docs/ROADMAP.md](docs/ROADMAP.md).

---

## Two renderers, two builds

A browser `<canvas>` can only ever hold one context type, so WebGL and WebGPU
cannot share a canvas at runtime. Each is therefore its **own build**:

- **WebGL2 (G2)** — `build/wasm-webgl` — the learning rung (immediate-mode, GLSL ES 3.0)
- **WebGPU (G3)** — `build/wasm-webgpu` — the destination (Dawn/emdawnwebgpu, deferred-mode, WGSL/compute next)
- **Native WebGL/OpenGL** — `build/native` — fast desktop iteration for the WebGL path
- **Native WebGPU** — `build/native-webgpu` — fast desktop iteration for Dawn + ImGui

---

## Quick start

```bash
# WebGL2 build
make wasm-webgl
cd build/wasm-webgl/bin && python -m http.server 8083 --bind 127.0.0.1
# open http://127.0.0.1:8083/dicom_renderer.html

# WebGPU/Dawn build
make wasm-webgpu
cd build/wasm-webgpu/bin && python -m http.server 8084 --bind 127.0.0.1
# open http://127.0.0.1:8084/dicom_renderer.html
```

The Makefile sets the Emscripten SDK variables and calls CMake with the
Emscripten toolchain file directly. Full commands, controls, native builds, and
caveats are in [docs/LEARNING.md](docs/LEARNING.md).

---

## Documentation

Three lenses on the project — **now**, **the goal**, and **the path between**:

- **[docs/CONCEPTS.md](docs/CONCEPTS.md)** — **start here if you're new to C++/GPUs/graphics.** Explains every idea from zero (CPU vs GPU, shaders, ray casting, volume rendering, the languages/tools) and ties each to the actual code.
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — where the code *is now*: structure and *why* (one-renderer-per-build, the engine wrappers, the C ABI study module, GLSL-vs-WGSL).
- **[docs/ROADMAP.md](docs/ROADMAP.md)** — the **north star**: target architecture and full todo list for the DICOM volume caster (and the later ray tracer / engine). Where you're going, not where you are.
- **[docs/LEARNING.md](docs/LEARNING.md)** — the ordered path from *now* to *the goal*, with build/run commands and reference reading.

Superseded planning notes are kept in [docs/archive/](docs/archive/) for history.

---

## Controls

Hold mouse + drag to rotate · right/middle drag to pan · wheel to zoom · `C`
toggles ORBIT/WASD · `WASD`+`Space`+`Shift/Ctrl` move in WASD mode · `1`–`4`
change the transfer function. WebGL exposes ray-debug and sampling controls in
HTML; WebGPU exposes the same study controls in the ImGui panel.

---

## Project layout

```
src/engine/   RAII GL wrappers, camera, renderer helpers, C ABI study module
src/main.cpp  entry point + the G2/G3 app variants (#ifdef USE_WEBGPU)
core/         DOD study core: jobs, ecs, reactive, messaging, scene, net
html/         Emscripten shells (WebGL and Dawn WebGPU)
external/     CMake deps: glm, SDL3, GLEW, Dawn/emdawnwebgpu, ImGui
docs/         current docs plus archived historical notes
```
