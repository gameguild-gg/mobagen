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

Today the code is at the **"triangle + camera"** stage — the foundation rung.
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
| WebGPU (G3) | `-DUSE_WEBGPU=ON` | `build/wasm-webgpu/bin` | The destination — deferred-mode, **compute shaders** for GPU-side DICOM processing |
| Native | (no Emscripten) | `build/native/bin` | Fast desktop iteration; WebGL/OpenGL only |

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
    └── engine_c.*           C ABI boundary (STUDY MODULE — see below).

html/
├── shell_webgl.html         Emscripten shell for the WebGL2 build (canvas, FPS,
│                            color buttons that call into C++).
├── shell_webgpu.html        Emscripten shell for the WebGPU build (canvas, FPS,
│                            fetches the .wgsl shaders, camera bridge).
└── camera-test.html         Standalone JS camera demo (no build needed).

shaders/                     Shader source of truth (one file per program).
├── raygen.glsl / blit.glsl  GLSL: BOTH stages in one file, gated by
│                            VERTEX_SHADER / FRAGMENT_SHADER. EMBEDDED into a
│                            generated header at build time (no runtime FS).
└── raygen.wgsl / blit.wgsl  WGSL: one module each. COPIED beside the WebGPU
                             output and fetch()-ed at runtime by the shell.
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
The device, pipeline and draw live in **JavaScript** (`shell_webgpu.html`),
because WebGPU is a JS-first API. The C++ `tick()` calls `window.webgpu_render()`
once per frame — the C++ loop is the **single owner** of the frame (the shell does
*not* run its own `requestAnimationFrame`). Shaders are WGSL.

---

## GLSL ES 3.0 vs WGSL — why both shaders by hand

The same triangle is described twice: GLSL ES 3.00 (WebGL) and WGSL (WebGPU).
This is intentional for learning — the two languages express the same ideas
differently:

| Concept | GLSL ES 3.00 | WGSL |
|---------|--------------|------|
| Vertex input | `layout(location=0) in vec2 aPos;` | `@location(0) position: vec2f` |
| Entry point | `void main()` | `@vertex fn vs_main(...) -> @builtin(position) vec4f` |
| Uniform | `uniform vec4 color;` + `glUniform4fv` | `@group(0) @binding(0) var<uniform> u: Uniforms;` + `queue.writeBuffer` |
| Color out | `out vec4 fragColor;` | `-> @location(0) vec4f` |

Notice the color path especially: WebGL recompiles the shader with the color
baked in (a toy), while WebGPU writes the color into a **uniform buffer** —
the deferred-mode way. That contrast is the point.

---

## Known limitations (today)

- The WebGPU build draws a **static** triangle: the camera matrix is not yet
  wired into WGSL. That plumbing arrives with fullscreen-quad ray marching
  (LEARNING Tier 2), where the camera actually matters.
- No DICOM, no 3D textures, no ray marching yet — that's the road ahead.
- Native build is WebGL/OpenGL only; there is no native WebGPU path.
