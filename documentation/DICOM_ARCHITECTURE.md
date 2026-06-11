# Architecture

This document describes the **current** design — where the code actually is.
For where the project is *going* (target architecture + full todo) see the north
star, [ROADMAP.md](ROADMAP.md); for the ordered path between the two, see
[LEARNING.md](LEARNING.md).

---

## What this project is

A WebAssembly-first renderer whose primary goal is **DICOM volume ray casting**
(marching a ray through a 3D medical scan), with a secondary, later goal of a
**mesh ray tracer** for a personal game engine. Both share the same ray-per-pixel
foundation; they differ in what happens after the ray is cast.

Today the code has two renderer states:

- **WebGL2/OpenGL:** a working synthetic-volume ray caster. It does per-pixel ray
  generation, samples a 3D volume texture loaded from `volume.raw`, applies a
  transfer function, gradient shading, DVR/MIP/Isosurface modes, and window/level.
- **WebGPU:** a working **Dawn/emdawnwebgpu + SDL3 + Dear ImGui host**. It owns the
  WebGPU device/surface/command encoder in C++ on both native and wasm, consumes
  `RenderBridge` volume commands, uploads a 3D volume texture, and ray marches it
  with `apps/dicom_viewer/shaders/raygen.wgsl`. It also has a first compute pass,
  `apps/dicom_viewer/shaders/histogram.wgsl`, for GPU histogram + auto-windowing.

Binary volume files are not stored in Git. The checked-in source of truth is
`apps/dicom_viewer/assets/assets.json`: it records where to download public study
data, expected hashes, dimensions, and the recipe for generated local files. Run
`make assets` to materialize `apps/dicom_viewer/assets/volume.raw` and
`apps/dicom_viewer/assets/volume.mvol`.

The default data is still a 96^3 phantom standing in for a real DICOM scan, but
the native `USE_GDCM=ON` path now has the first DICOM handoff: load a DICOM
series, preserve stored UInt16 voxels in `VolumeBuffer`, upload them to WebGPU as
packed `RG8Unorm`, and do the window/level step in WGSL. WebGL and the browser
phantom still use the simpler normalized R8 path.
Everything below describes what exists now.

---

## One renderer per build (not runtime switching)

A browser `<canvas>` can hold exactly **one** context for its entire lifetime.
Once `getContext('webgl2')` is called, `getContext('webgpu')` returns `null`
(and vice-versa). So you **cannot** switch between WebGL and WebGPU on one canvas
at runtime — an earlier design attempted this and the WebGPU path silently never
initialized.

The fix: each renderer is a **separate compile-time build**, selected by a CMake
option, emitted into a separate output directory.

| Build | CMake | Output | Role |
|-------|-------|--------|------|
| WebGL2 (G2) | `-DUSE_WEBGPU=OFF` (default) | `build/wasm-webgl/bin` | The learning rung — immediate-mode, widely supported |
| WebGPU (G3) | `-DUSE_WEBGPU=ON` | `build/wasm-webgpu/bin` | The destination — Dawn/emdawnwebgpu host, **compute shaders** next |
| Native WebGL/OpenGL | `-DUSE_WEBGPU=OFF` | `build/native/bin` | Fast desktop iteration for the WebGL/OpenGL path |
| Native WebGPU | `-DUSE_WEBGPU=ON` | `build/native-webgpu/bin` | Fast desktop iteration for Dawn + ImGui |

WebGL2 is where you learn the pipeline. WebGPU is the real target, because
GPU-side volume processing (histograms, auto-windowing, wavefront ray casting)
needs compute shaders, which WebGL2 does not have.

---

## Source layout

```
apps/
├── dicom_viewer/            DICOM viewer executable, app-specific assets,
│   ├── sources/main.cpp     G2/G3 app variants selected by #ifdef USE_WEBGPU.
│   ├── assets/              Asset manifest; generated/downloaded volumes ignored.
│   ├── shaders/             DICOM viewer GLSL/WGSL shader source of truth.
│   └── htmls/               Emscripten shells and standalone camera test page.
├── core_demos/              Core examples/tests/benches.
├── dawn_probe/              Standalone Dawn risk-gate prototype.
├── fibers_prototype/        Coroutine/fiber learning prototype.
└── volume_demos/            Volume and DICOM-loader smoke tests.

core/
├── sources/                 Shared engine code: camera, ecs, jobs, input,
│                            messaging, net, reactive, render, resource, scene.
├── assets/                  Shared assets every app should bundle.
└── shaders/                 Shared shaders every app should bundle.

modules/
├── opengl_renderer/         RAII GL wrappers and C ABI study module.
├── volume/                  CPU-side owned volume bytes + .mvol reader.
└── volume_io/               Native DICOM loader module (GDCM when USE_GDCM=ON).
```

GLSL is embedded at build time into a generated C++ header. WGSL is kept beside it
so the WebGPU port can preserve the same algorithm in the WebGPU shader language.
```

### Asset manager

The project has the first small asset manager in `scripts/assets.py`.

It does four jobs:

1. Reads `apps/dicom_viewer/assets/assets.json`.
2. Downloads large source datasets into `apps/dicom_viewer/assets/cache/`.
3. Verifies SHA-256 hashes before using them.
4. Generates renderer-ready local files (`volume.raw`, `volume.mvol`).

This keeps the repo lightweight and reproducible: code, manifests, hashes, and
conversion recipes are versioned; binary payloads are local build artifacts.

### One class = one GPU object

`ShaderProgram`, `VertexBuffer`, `VertexArray` each wrap a single GL object with
RAII (acquire in constructor, release in destructor). This is the Magnum-style
"thin wrapper" philosophy: no bloated engine, just typed handles over raw GL.

### The `Renderer` class

A small abstraction that owns the clear color and issues `clear()` / `draw()`.
The WebGL build uses it instead of scattering raw `glClear`/`glDrawArrays` through
the frame loop. It is deliberately minimal; it will grow when render passes /
render-to-texture arrive (see LEARNING Tier 1.3).

### The C ABI (`engine_c.*`) — a study module

`engine_c.h/.cpp` exposes the C++ engine through a flat C interface (opaque
handles, `extern "C"`). C has a stable ABI; C++ does not (name mangling, vtable
layout). A C boundary is how VTK/ITK/wgpu-native let other languages
(Python, JS, Rust) drive a C++ core.

It is **not yet consumed** by a second front-end — it's kept compiled so it stays
correct, and so it can be exercised as a learning exercise (e.g. driving the
engine from a separate C "core" or Python `ctypes` script). When mobagen calls
this engine as a module, this is the seam it will call through.

---

## The frame loop

Both builds share one structure (`main.cpp`):

```
main()
  app.init()
  emscripten_set_main_loop(em_tick)   // browser owns the loop
      every frame:
        process_input(running)        // SDL events -> camera (shared)
        camera.update(dt)             // dt from SDL high-res counter
        <render>                      // renderer-specific
```

- **Input is shared**: `process_input()` polls SDL events and drives the one
  global `Camera`. SDL events work identically native and in the browser.
- **Mouse-look is gated**: the camera only rotates while a mouse button is held,
  so moving the cursor to a button doesn't spin the view.
- **Delta time is real**: measured from `SDL_GetPerformanceCounter`, clamped to
  avoid jumps after a stall.

### WebGL2 render path
Immediate-mode teaching path: render a fullscreen quad into an offscreen
`Framebuffer`, run the GLSL ray marcher per pixel, then blit the result to the
screen. Shaders are GLSL ES 3.00 in the browser and GLSL 3.30 core natively
(same source, different `#version` header).

### WebGPU render path
The device, surface, command encoder, render pass, and ImGui draw now live in
**C++** through Dawn's WebGPU C API. Native builds link Dawn directly; wasm builds
use Dawn's `emdawnwebgpu` package. This replaces the earlier JavaScript WebGPU
bridge and gives us one host model for native and browser.

Current frame shape:

```
process SDL3 input -> update camera -> acquire WGPU surface texture
  -> begin render pass -> draw WGSL volume ray-cast -> draw ImGui overlay
  -> submit command buffer
```

The volume pass consumes a flat `RenderBridge` command list, writes camera/window
settings to uniform buffers, samples a 3D texture, and then draws ImGui on top so
the controls stay inspectable. When GDCM is enabled natively, WebGPU texture
bytes can come from a DICOM series preserved as packed UInt16-in-RG8 data;
otherwise they come from the synthetic phantom.

The histogram pass is separate from the render pass and runs on demand from the
ImGui button **Auto window from GPU histogram**:

```
clear histogram storage buffer
  -> compute shader: one invocation per voxel
  -> textureLoad(volume) -> scalar bin
  -> atomicAdd(histogram[bin], 1)
  -> copy histogram buffer to readback buffer
  -> C++ maps readback and chooses p01/p99 window
```

This is deliberately the first WebGPU-only study rung. WebGL2 can sample the
same volume, but it cannot run compute workgroups or use storage-buffer atomics.
The percentile reduction still happens on CPU after readback; moving that
reduction onto GPU is the next refinement.

### CPU volume memory path

`volume::VolumeBuffer` is the current bridge between medical-image loading and
GPU upload. It owns bytes plus `VolumeMetadata`, and records what those bytes
mean:

```
phantom / WebGL path:
  density bytes -> R8 VolumeBuffer -> R8 3D texture -> shader window/level

native DICOM / WebGPU path:
  DICOM UInt16 slices -> U16PackedRG8 VolumeBuffer -> RG8 3D texture
                       -> WGSL reconstruct UInt16 -> shader window/level
```

The packed path stores one 16-bit voxel as two 8-bit texture channels:

```
R = low byte
G = high byte
stored = round(R * 255) + round(G * 255) * 256
```

Packed UInt16 textures use nearest sampling. Linear filtering would interpolate
the individual bytes before reconstruction, which corrupts the stored value.
The R8 phantom path can keep linear filtering because it is already normalized
density data.

For learning, `VolumeBuffer` supports two ownership modes:

- default heap storage, using `std::pmr::vector<std::uint8_t>`;
- arena-backed storage, using `VolumeArena` and `std::pmr::monotonic_buffer_resource`.

That is the first concrete memory-management practice step: start with standard
containers, then make the allocation policy explicit before introducing ECS
resource handles or GPU residency management.

### Browser startup and resize contract

The HTML shell owns the canvas element and knows the real drawing-buffer size
after CSS layout and device-pixel-ratio are applied. The C++ renderer owns the
GPU viewport and camera aspect ratio. The boundary is:

```
HTML resize -> canvas.width/height -> _on_canvas_resize(w, h)
             -> g_canvas_w/h -> WebGL viewport or WebGPU surface configure
```

The shell may resize the canvas before the WASM runtime is callable, so it gates
`_on_canvas_resize` behind `Module.onRuntimeInitialized`. Calling exported C++
too early aborts Emscripten before `main()` has created the GL/WebGPU context.

WebGPU adapter/device requests are async. The browser build uses Dawn's
`AllowProcessEvents` callbacks, so the init loop must call
`wgpuInstanceProcessEvents()` while yielding with `emscripten_sleep()`. Without
that event pump, JavaScript resolves the request but C++ never receives the
callback.

---

## GLSL ES 3.0 vs WGSL — why both shaders by hand

The ray caster is written twice: `apps/dicom_viewer/shaders/raygen.glsl` (GLSL ES 3.00, WebGL) and
`apps/dicom_viewer/shaders/raygen.wgsl` (WGSL, WebGPU). Same algorithm, two languages — intentional
for learning. How resources reach the shader is the most instructive contrast:

| Concept | GLSL ES 3.00 | WGSL |
|---------|--------------|------|
| Vertex input | `layout(location=0) in vec2 aPos;` | `@location(0) position: vec2f` |
| Entry point | `void main()` | `@vertex fn vs_main(...) -> @builtin(position) vec4f` |
| Scalar/vec uniform | `uniform mat4 m;` + `glUniformMatrix4fv` | `@group(0) @binding(0) var<uniform> ...` + `queue.writeBuffer` |
| 3D texture sample | `texture(uVolume, p)` | `textureSampleLevel(volume, samp, p, 0.0)` |
| Sampler | implicit in `sampler3D` | a separate `var ... : sampler` binding |
| Resource wiring | individual `glUniform*` / texture units | one **bind group** = `{ buffers, textures, samplers }` |

The camera reaches GLSL through `glUniformMatrix4fv`. In the Dawn WebGPU path,
the same camera reaches WGSL through a uniform buffer updated with
`wgpuQueueWriteBuffer`.

---

## Known limitations (today)

Current status: WebGPU ray-casting renders the volume path. The remaining
limitation is making DICOM loading portable to WASM, not the existence of the
WGSL pass.

- **DICOM is native-first** — `USE_GDCM=ON` is wired for native smoke tests and
  renderer upload. The browser path still uses the prebuilt raw phantom until we
  decide how much of GDCM/DCMTK should ship into WASM.
- **Browser DICOM is still undecided** — the WebGPU renderer can consume packed
  UInt16 volume bytes, but the browser path still needs an explicit decision on
  whether GDCM/DCMTK belongs in WASM or whether DICOM conversion happens before
  upload.
- **Native WebGPU + GDCM build is heavy** — `make native-dicom` is verified but
  intentionally throttled to single-threaded MSBuild in the Makefile to avoid
  Dawn-generated project memory pressure.
- **WebGPU needs a GPU adapter** — adapter creation can fail where the browser has
  no usable GPU (hardware acceleration off, blocklist, or unsupported backend).
  The shell now shows a visible startup error instead of leaving only a blue
  canvas.
