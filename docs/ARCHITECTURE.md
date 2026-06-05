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
  WebGPU device/surface/command encoder in C++ on both native and wasm. The volume
  ray-cast is the next layer to port onto this host.

The data is currently a 96^3 phantom standing in for a real DICOM scan (Tier 3-A).
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
src/
├── main.cpp                 Entry point + the two app variants (G2 / G3),
│                            selected by #ifdef USE_WEBGPU. Owns the main loop,
│                            input, and the shared camera.
└── engine/
    ├── camera.h             Header-only camera: ORBIT (DICOM) + WASD (engine fly).
    ├── shader_program.*     RAII wrapper around a GL program (compile/link/uniforms).
    ├── vertex_buffer.*      RAII wrapper around a VBO.
    ├── vertex_array.*       RAII wrapper around a VAO + attribute layout.
    ├── renderer.*           Thin "clear + draw" abstraction (used by the WebGL build).
    └── engine_c.*           ABIC  boundary (STUDY MODULE — see below).

html/
├── shell_webgl.html         Emscripten shell for the WebGL2 build (canvas, FPS,
│                            color buttons that call into C++).
├── shell_dawn.html          Plain canvas shell for the Dawn/emdawnwebgpu build.
│                            The UI is Dear ImGui, drawn by C++ into the canvas.
└── camera-test.html         Standalone JS camera demo (no build needed).

shaders/                     Shader source of truth (one file per program).
├── raygen.glsl / blit.glsl  GLSL: BOTH stages in one file, gated by
│                            VERTEX_SHADER / FRAGMENT_SHADER. Embedded at build
│                            time into a generated header (compiled twice).
└── raygen.wgsl / blit.wgsl  WGSL: one module each. Kept as the source for the
                             upcoming Dawn/WebGPU ray-cast port.

GLSL is embedded at build time into a generated C++ header. WGSL is kept beside it
so the WebGPU port can preserve the same algorithm in the WebGPU shader language.
```

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
Direct, immediate-mode: `renderer.clear()` → set `view_projection` uniform →
`renderer.draw(vao, 3)` → `SDL_GL_SwapWindow`. Shaders are GLSL ES 3.00 in the
browser, GLSL 3.30 core natively (same source, different `#version` header).

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
settings to uniform buffers, samples the synthetic 3D phantom texture, and then
draws ImGui on top so the controls stay inspectable.

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

The ray caster is written twice: `shaders/raygen.glsl` (GLSL ES 3.00, WebGL) and
`shaders/raygen.wgsl` (WGSL, WebGPU). Same algorithm, two languages — intentional
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

Current status: WebGPU ray-casting now renders the synthetic phantom. The
remaining limitation is real DICOM input, not the existence of the WGSL pass.

- **No real DICOM yet** — the volume is a 96^3 synthetic phantom loaded from
  `volume.raw`. The DICOM parser (GDCM/DCMTK → WASM) is Tier 3-A.
- **WebGPU volume ray-cast is synthetic** — Dawn + ImGui + WGSL ray marching are
  green, but the input is still a generated phantom rather than a patient scan.
- **8-bit volume first** — the current phantom is 8-bit. Real DICOM will bring
  16-bit values plus Hounsfield rescale.
- **WebGPU needs a GPU adapter** — adapter creation can fail where the browser has
  no usable GPU (hardware acceleration off, blocklist, or unsupported backend).
  The shell now shows a visible startup error instead of leaving only a blue
  canvas.
