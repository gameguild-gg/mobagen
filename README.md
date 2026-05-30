# DICOM Renderer

A WebAssembly-first GPU renderer. The primary goal is **DICOM volume ray casting**
(rendering medical CT/MRI scans by marching rays through a 3D volume); a later,
secondary goal is a **mesh ray tracer** for a personal game engine. Both share the
same ray-per-pixel foundation.

Built as a learning project: the code grows one understandable rung at a time, and
every step renders something you can see.

**Current stage:** triangle + interactive camera (the foundation rung).
**Goal:** see the north star in [docs/ROADMAP.md](docs/ROADMAP.md).

---

## Two renderers, two builds

A browser `<canvas>` can only ever hold one context type, so WebGL and WebGPU
cannot share a canvas at runtime. Each is therefore its **own build**:

- **WebGL2 (G2)** — `build/wasm-webgl` — the learning rung (immediate-mode, GLSL ES 3.0)
- **WebGPU (G3)** — `build/wasm-webgpu` — the destination (deferred-mode, WGSL, compute shaders for GPU-side DICOM work)
- **Native** — `build/native` — fast desktop iteration (WebGL/OpenGL only)

---

## Quick start

```powershell
# Emscripten env (once per shell)
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH  = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:EMSDK\node\22.16.0_64bit\bin;$env:PATH"

# WebGL2 build
emcmake cmake -B build/wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgl
cd build/wasm-webgl/bin ; python -m http.server 8083
# open http://localhost:8083/dicom_renderer.html
```

WebGPU build: add `-DUSE_WEBGPU=ON` and use a separate output dir. Full commands,
controls, and the native build are in [docs/LEARNING.md](docs/LEARNING.md).

---

## Documentation

Three lenses on the project — **now**, **the goal**, and **the path between**:

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — where the code *is now*: structure and *why* (one-renderer-per-build, the engine wrappers, the C ABI study module, GLSL-vs-WGSL).
- **[docs/ROADMAP.md](docs/ROADMAP.md)** — the **north star**: target architecture and full todo list for the DICOM volume caster (and the later ray tracer / engine). Where you're going, not where you are.
- **[docs/LEARNING.md](docs/LEARNING.md)** — the ordered path from *now* to *the goal*, with build/run commands and reference reading.

Superseded planning notes are kept in [docs/archive/](docs/archive/) for history.

---

## Controls

Hold mouse + drag to rotate · wheel to zoom · `C` toggles ORBIT/WASD ·
`WASD`+`Space` to move · `1`–`4` change color.

---

## Project layout

```
src/engine/   RAII GL wrappers (shader/buffer/VAO), camera, renderer, C ABI
src/main.cpp  entry point + the G2/G3 app variants (#ifdef USE_WEBGPU)
html/         Emscripten shells (one per renderer) + standalone camera demo
external/     CMake deps actually used: glm, sdl, glew (+ cpm, compilerchecks)
docs/         ARCHITECTURE, LEARNING, archive/
```
