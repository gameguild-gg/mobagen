# Concepts — from zero

This explains everything the project rests on, assuming **no prior knowledge** of
C, C++, GPUs, OpenGL/WebGL, WebGPU, or shaders. It is long by design — read it in
sections, come back to it. Where useful it points at the real file in this repo.

> The other docs: [ARCHITECTURE.md](ARCHITECTURE.md) = how the code is laid out
> today · [LEARNING.md](LEARNING.md) = the ordered build path · [ROADMAP.md](ROADMAP.md)
> = the long-term goal. **This file = the *why* behind all of them.**

---

## 0. The one-sentence summary

We shoot one ray from the camera through every pixel on the screen, march each ray
through a 3D block of medical-scan data, and add up what it passes through to make
an image — and we do this on the **GPU**, written in two graphics languages
(WebGL and WebGPU), compiled to run **inside a web browser**.

Everything below unpacks that sentence.

---

## 1. CPU vs GPU (why this is hard and interesting)

A **CPU** (the normal processor) has a few very fast cores. It does things *one
after another*, extremely quickly. Great for logic, bad for "do the same thing to
a million things at once."

A **GPU** (graphics processor) has *thousands* of small, slower cores. It is built
to do **the same calculation on huge amounts of data simultaneously** — like
"compute a colour for all 2 million pixels on screen, all at once."

A screen image is millions of pixels. A medical volume is millions of little 3D
data points (voxels). So this whole project is a GPU problem: the GPU runs our
little "what colour is this pixel?" program **once per pixel, in parallel**.

The catch: the GPU is a separate device with its own memory. You can't just call
it like a function. You have to (1) upload data to it, (2) give it a tiny program
to run, (3) tell it to run, (4) read the result. Most of the "plumbing" in this
project is exactly that hand-off.

---

## 2. The languages and tools (what each is, why it exists)

### C and C++
- **C** is a low-level programming language: close to the machine, very fast, but
  *you* manage memory by hand. Most operating systems and drivers are written in C.
- **C++** is C plus higher-level features (classes/objects, templates, automatic
  cleanup). Still compiled to fast machine code, still manual memory, but more
  ergonomic. **Our engine is C++** ([src/main.cpp](../src/main.cpp), [src/engine/](../src/engine/)).
- "**Compiled**" means a tool (a *compiler*) translates the human-readable `.cpp`
  text into machine code the computer runs directly. (Contrast with JavaScript,
  which the browser reads and runs on the fly.)

### Why C++ here?
Speed and control. A renderer touches millions of data points per frame; you want
machine-code speed and direct control over GPU memory. C++ is the standard choice
for engines (Unreal, Unity's core, etc.).

### WebAssembly (WASM) and Emscripten
A browser can't run C++ directly. **WebAssembly** is a compact binary format that
browsers run at near-native speed. **Emscripten** is a special compiler that turns
our C++ into:
- `dicom_renderer.wasm` — the compiled machine-ish code,
- `dicom_renderer.js` — "glue" JavaScript that loads the wasm and connects it to
  the browser,
- `dicom_renderer.html` — the page that hosts it.

So: we write C++ once; Emscripten makes it run in the browser. (We also build a
**native** version for fast desktop testing — same C++, normal compiler.)

### JavaScript and the browser
**JavaScript (JS)** is the language browsers run natively. It controls the web
page, handles buttons, and — important here — is the only way to talk to **WebGPU**.
So our project is part C++ (the engine) and part JS (the page + WebGPU calls), and
they have to pass data back and forth (more on that in §9).

### What a "shader" is
A **shader** is a tiny program that runs **on the GPU**, once per vertex or once
per pixel. You write it in a special GPU language, the GPU compiles it, and then
it runs in massive parallel. Shaders are where the actual image gets computed.

### OpenGL / WebGL2 / GLSL
- **OpenGL** is an old, widely-supported API (set of commands) for telling a GPU
  what to draw. **OpenGL ES** is its "embedded/mobile" variant.
- **WebGL2** is OpenGL ES 3.0 exposed *inside the browser*. It's what we use for
  the "G2" build. Mature, runs almost everywhere.
- **GLSL** ("GL Shading Language") is the language you write OpenGL/WebGL shaders
  in. Our GLSL lives in [shaders/raygen.glsl](../shaders/raygen.glsl) and
  [shaders/blit.glsl](../shaders/blit.glsl). It looks like C.

### WebGPU / WGSL
- **WebGPU** is the *modern* browser GPU API (successor to WebGL). It's lower-level,
  faster for big workloads, and — crucially for us — supports **compute shaders**
  (general GPU programs, needed later for fast DICOM processing).
- **WGSL** ("WebGPU Shading Language") is WebGPU's shader language. Same ideas as
  GLSL, different spelling. Ours: [shaders/raygen.wgsl](../shaders/raygen.wgsl),
  [shaders/blit.wgsl](../shaders/blit.wgsl).

We build the **same renderer twice** (WebGL+GLSL and WebGPU+WGSL) on purpose — to
learn both, and because WebGL is the easy starting point and WebGPU is the goal.

### The helper libraries
- **SDL2** — opens a window and gives us keyboard/mouse events, the same way on
  desktop and in the browser. (We never call browser/OS window code directly.)
- **GLM** — a math library: vectors (`vec3`), matrices (`mat4`) and the operations
  on them. Graphics is *all* vector/matrix math; GLM provides it.
- **GLEW** — on desktop only, finds the GPU driver's functions.
- **CMake** — describes how to build the project (which files, which flags) so one
  description produces the native and both WASM builds.

---

## 3. The GPU drawing model (the five nouns)

Almost all GPU drawing uses the same five ideas:

1. **Vertices** — points in space (e.g. the 3 corners of a triangle). Stored in a
   **vertex buffer** (a chunk of GPU memory). In our code: [vertex_buffer](../src/engine/vertex_buffer.h).
2. **The vertex shader** — runs once per vertex; decides where each vertex lands on
   screen.
3. **Rasterization** — the GPU automatically fills in the pixels *between* the
   vertices (turns a triangle into the pixels it covers). You don't write this; the
   GPU does it.
4. **The fragment shader** — runs once per covered pixel ("fragment"); decides that
   pixel's **colour**. **This is where 95% of our work happens.**
5. **Output** — the colour goes into an image (the screen, or an offscreen
   **texture** — see §6).

Two more nouns you pass *into* shaders:
- **Uniforms** — values that are the *same* for every vertex/pixel of one draw
  (e.g. the camera matrix). Like global constants for that draw call.
- **Textures** — images/data arrays the shader can *sample* (read) at any
  coordinate. A 2D texture is a picture; a **3D texture** is a stack of pictures =
  our volume. In our code: [texture](../src/engine/texture.h), [texture3d](../src/engine/texture3d.h).

### Coordinates: NDC
The GPU's screen space is **NDC** ("normalized device coordinates"): x and y both
run from **-1 to +1**, regardless of resolution. (-1,-1) is one corner, (+1,+1) the
opposite. The vertex shader's job is ultimately to output NDC positions.

---

## 4. A tour of our code (what each file is)

```
src/
  main.cpp            The whole app: open window, the per-frame loop, input,
                      and the renderer-specific code (#ifdef USE_WEBGPU picks one).
  engine/
    camera.h          Turns mouse/keys into the camera's view+projection matrices.
    shader_program.*  RAII wrapper: compile GLSL, set uniforms (WebGL/native).
    vertex_buffer.*   RAII wrapper: a chunk of vertex data on the GPU.
    vertex_array.*    RAII wrapper: "how to read" that vertex data (the layout).
    texture.*         RAII wrapper: a 2D GPU texture.
    texture3d.*       RAII wrapper: a 3D GPU texture (the volume).
    framebuffer.*     RAII wrapper: an offscreen render target (§6).
    renderer.*        Small "clear + draw" helper used by the WebGL path.
    engine_c.*        A C-style interface (study module; see §10).
shaders/
  raygen.glsl/.wgsl   The ray caster (the important shader): one file per language.
  blit.glsl/.wgsl     The "copy the offscreen image to the screen" shader.
html/
  shell_webgl.html        The web page for the WebGL build (UI + canvas).
  shell_webgpu.html.in    Template for the WebGPU page; CMake bakes the WGSL in.
```

### Two words you'll see a lot
- **RAII** ("Resource Acquisition Is Initialization") — a C++ habit: an object
  *grabs* a GPU resource when created and *frees* it automatically when it goes out
  of scope. It means we don't leak GPU memory. Every `engine/*` wrapper is RAII:
  one C++ object = one GPU object.
- **`#ifdef USE_WEBGPU`** — a compile-time switch. Code inside it is included only
  in the WebGPU build; the `#else` part only in the WebGL build. That's how one
  `main.cpp` produces two different programs.

### The per-frame loop (the heartbeat)
The browser owns the clock. We hand it one function, `tick()`, that it calls ~60
times/second. Each `tick()`:
1. `process_input()` — read mouse/keyboard, update the camera.
2. update the camera using real elapsed time (`measure_delta_seconds()`),
3. **render** — run the shaders, producing this frame's image.

On desktop it's a plain `while (running) tick();` loop. Same `tick()`, two hosts —
that symmetry is deliberate (a browser can't use a blocking `while` loop).

---

## 5. The rendering concepts, in the order we built them

Each of these is a "Tier" in [LEARNING.md](LEARNING.md). Here's the *idea* behind each.

### 5.1 Drawing a triangle
The "hello world" of GPUs. Put 3 vertices in a buffer, write a vertex shader that
passes them through, write a fragment shader that returns a flat colour. Proves the
whole pipeline (upload → vertex → rasterize → fragment → screen) works.

### 5.2 The camera (matrices and why)
We want to look at a 3D scene from a movable viewpoint. A **matrix** is a little
grid of numbers that, when you multiply a point by it, *transforms* that point
(moves/rotates/scales it). Two matrices matter:
- **View matrix** — moves the world so the camera sits at the origin looking down
  one axis ("put the camera's eye at 0,0,0").
- **Projection matrix** — applies perspective (far things look smaller) and maps
  everything into NDC.

Multiply them → **view-projection**. Multiply a 3D point by it → where that point
lands on screen. [camera.h](../src/engine/camera.h) builds these from mouse/keys.
Two modes: **ORBIT** (rotate around the object — for inspecting a scan) and **WASD**
(fly around — game-style).

### 5.3 Textures and sampling
A **texture** is data the GPU can read at any coordinate. Coordinates are **UVs**:
(0,0) to (1,1) across the image, independent of pixel size. "**Sampling**" =
"read the texture at this UV," and the GPU can **interpolate** smoothly between
stored values (so a small image scaled up looks smooth, not blocky).

Why it matters: **volume rendering is just "sample a (3D) texture along a ray."**
The 2D textured-quad step is the simplest version of that idea.

### 5.4 Render-to-texture (framebuffers)
Normally pixels go to the screen. A **framebuffer** lets you redirect them **into a
texture** instead. Then a *second* pass reads that texture and draws it to the
screen. Why bother? Because once the whole image is in a texture you can *process*
it (effects), and — the real reason — **the ray caster computes its result into a
texture, then a "blit" pass copies it to the screen.** ([framebuffer.h](../src/engine/framebuffer.h),
and the "blit" shaders.) This is the Unity *RenderTexture* idea.

### 5.5 Ray generation (the turning point)
Now we stop drawing shapes and start **ray casting**. We draw one big rectangle
covering the whole screen ("fullscreen quad"), so the **fragment shader runs once
per screen pixel**. For each pixel we ask: *if I shot a straight line (a "ray")
from the camera through this pixel into the 3D world, where does it go?*

We compute that ray from the **inverse** of the view-projection matrix (it undoes
the camera transform, turning a screen pixel back into a world-space direction).
Output: ray origin + ray **direction**. We first just paint the direction as colour
to prove it's correct (orbit the camera, the colours swing).

> This is the single most important idea. Everything after just *uses* the ray.

### 5.6 Marching a sphere (ray marching with an SDF)
Given a ray, how do we know if it hits something? We **march**: take small steps
along the ray, and at each step ask "how far am I from the surface?" using a
**Signed Distance Function (SDF)** — for a sphere that's just `length(point) - radius`
(positive outside, zero on the surface, negative inside). When the distance is ~0,
we hit. Stepping by exactly that distance ("sphere tracing") is efficient.

This is the same loop as volume rendering — only the "what's here?" question
differs (math now, texture later).

### 5.7 3D textures and the synthetic volume
A **3D texture** is a cube of data — width × height × **depth** voxels — sampled by
a 3D coordinate. Our volume lives in the cube from (-1,-1,-1) to (1,1,1). We
generate a fake one on the CPU (a soft ball, later a head-shaped phantom) and
upload it. Now "what's here?" = *sample the 3D texture at this point*.
([texture3d.h](../src/engine/texture3d.h); the data comes from `volume.raw`.)

### 5.8 Direct Volume Rendering (compositing)
A scan has no hard surfaces — it's a cloud of densities. So instead of *stopping*
at a hit, the ray **passes through** and **accumulates**: at each step it reads the
density, turns it into a colour + a little opacity, and blends it on top of what
it's gathered so far ("front-to-back compositing"):

```
accumulatedColour += (1 - accumulatedOpacity) * stepOpacity * stepColour;
accumulatedOpacity += (1 - accumulatedOpacity) * stepOpacity;
```

The `(1 - accumulatedOpacity)` means once the ray is "full" (opaque), later
samples contribute nothing — so we can **stop early** (a speed trick). This is
exactly how smoke/fog and medical volumes are rendered.

### 5.9 Transfer function (the key clinical knob)
Raw density is just a number. A **transfer function** maps each density value to a
**colour + opacity** via a lookup table (a tiny 256-wide texture). Change the table
and the *same data* shows different things: make a density band opaque and coloured
to reveal a structure, make the rest transparent to see through it. This is how a
radiologist isolates bone vs soft tissue. Our 1–4 presets (Gray/Tissue/**Shell**/
Cool) are different tables.

### 5.10 Gradient shading (making it look 3D)
A flat density cloud looks like fog. To get 3D-looking lighting we need a **normal**
(the direction a surface faces). In a volume there's no surface, but the
**gradient** of the density (how fast it changes, and in which direction) points
across boundaries — that *is* the normal. We compute it by sampling the volume a
tiny step in each of x/y/z ("central differences") and apply standard diffuse
lighting (`brightness = how much the normal faces the light`). Now bone looks solid.

### 5.11 Render modes (DVR / MIP / Isosurface)
Same ray, different "what do I output?":
- **DVR** — accumulate through (§5.8). The translucent look.
- **MIP** (Maximum Intensity Projection) — just keep the **brightest** density the
  ray passed. Standard for showing contrast-filled blood vessels.
- **Isosurface** — stop at the **first** density above a threshold and shade it like
  a hard surface (skull, bone) — without building any geometry.

### 5.12 Window/level (windowing)
Real scans store a *huge* range of values (CT: air ≈ −1000 to bone ≈ +1000). You
can't show all of it usefully, so you pick a **window**: a center and a width. Only
values inside `[center − width/2, center + width/2]` are shown, mapped to the full
brightness range; everything outside is clipped. "Bone window," "lung window,"
"brain window" are just different center/width pairs over the same scan. Our Window
sliders do exactly this, before the transfer-function lookup.

---

## 6. Why two renderers (WebGL vs WebGPU)

A browser `<canvas>` (the drawing area) can use **only one** GPU API for its whole
life. So you can't switch WebGL↔WebGPU live — each is its own **build** of the app.

| | WebGL2 (G2) | WebGPU (G3) |
|---|---|---|
| Age/role | older, runs everywhere — our **learning rung** | modern — our **destination** |
| Style | "immediate": each command runs now | "deferred": record a batch, submit |
| Shader language | GLSL | WGSL |
| Compute shaders? | ❌ | ✅ (needed later for fast DICOM processing) |
| How resources reach the shader | individual calls + texture "units" | one **bind group** = {buffers, textures, samplers} |

We write the ray caster in both languages (same algorithm) so the differences are
visible side by side. WebGL is where you *learn*; WebGPU is where the research goes
(it can run general parallel programs, not just draw).

---

## 7. The awkward bits (and why they exist)

- **C++ ↔ JavaScript bridge.** In the WebGPU build the renderer lives in JS, but the
  camera lives in C++. So every frame C++ must *hand* JS the camera matrix (16
  numbers). It does this by writing them into shared memory and calling a JS
  function (`EM_ASM`). This is "the camera→WGSL bridge." It feels clunky because two
  languages are cooperating across a boundary.
- **No filesystem in the browser.** C++ usually reads files with `fopen`. In the
  browser there's no disk. So for WebGL we *bundle* `volume.raw` into the wasm
  package (Emscripten `--preload-file`) and read it from a fake in-memory
  filesystem; for WebGPU we `fetch()` it over HTTP. Same data, two delivery methods.
- **"Hard refresh or it's blank."** The browser caches the `.wasm`/`.data`. After a
  rebuild a normal refresh can load the *new* page with the *old* cached code →
  mismatch → blank canvas. Always Ctrl+Shift+R after building. (Not a bug; cache.)

---

## 8. A few bugs we hit (and what they teach)

- **`glViewport` before the GL context existed** → "Cannot read 'viewport' of
  undefined." Lesson: GPU calls are only valid *after* the GPU context is created;
  startup order matters.
- **`keys_pressed_[keycode]` out of bounds** → "memory access out of bounds." The
  array had 256 slots but special keys (arrows) have codes near 1.07 *billion*.
  Lesson: in C++ there's no automatic bounds checking — an out-of-range array write
  corrupts memory (here, a hard crash in WASM). Always bounds-check indices from
  external input.

These are the classic flavour of low-level bugs: they compile fine and only bite at
runtime. That's the trade for C++'s speed/control.

---

## 9. How a single frame actually flows (putting it together)

WebGL build, one `tick()`:
1. **C++**: read input, update camera → get a `view_projection` matrix.
2. **C++**: bind the offscreen framebuffer; for this frame, set the camera matrix,
   the volume texture, the transfer table, mode, and window as **uniforms**.
3. **GPU (vertex shader)**: emits a fullscreen rectangle.
4. **GPU (fragment shader, `raygen.glsl`)**: for *every pixel*, build the ray,
   march it through the 3D texture, apply window → transfer → shading → composite,
   output a colour into the **offscreen texture**.
5. **C++**: switch to the screen; run the **blit** shader to copy the offscreen
   texture onto the canvas.
6. Browser shows the canvas; ~16ms later, `tick()` again.

WebGPU is the same flow, but steps 2/5 are expressed as WebGPU "render passes" in
JavaScript, and the camera matrix arrives via the C++→JS bridge.

---

## 10. Glossary (quick reference)

- **CPU / GPU** — serial processor / massively-parallel processor.
- **C / C++** — compiled low-level languages; our engine is C++.
- **Compile** — translate source code to machine code.
- **WASM** — WebAssembly; compiled code the browser runs fast.
- **Emscripten** — compiler: C++ → WASM + JS for the browser.
- **Shader** — tiny program that runs on the GPU per vertex/pixel.
- **GLSL / WGSL** — shader languages for WebGL / WebGPU.
- **OpenGL / WebGL2 / WebGPU** — APIs for commanding the GPU.
- **Vertex / fragment shader** — runs per corner / per pixel.
- **Rasterize** — GPU fills pixels between vertices.
- **Uniform** — a constant passed into a shader for one draw.
- **Texture** — data the shader samples at a coordinate (2D image or 3D volume).
- **Sample / UV** — read a texture / its 0–1 coordinates.
- **NDC** — normalized device coords, screen space −1..+1.
- **Matrix / vector** — the math of moving points around (GLM gives us these).
- **View / projection** — camera position / perspective matrices.
- **Framebuffer / render-to-texture** — draw into a texture instead of the screen.
- **Ray** — a line from the camera through a pixel into the scene.
- **Ray casting / marching** — stepping along a ray to find/accumulate what it hits.
- **SDF** — signed distance function (distance to a surface).
- **Voxel** — a 3D pixel; the volume is a grid of voxels.
- **DVR** — direct volume rendering (accumulate density along the ray).
- **Compositing** — blending samples front-to-back with opacity.
- **Transfer function** — density → colour+opacity lookup table.
- **Gradient** — direction/rate of density change; used as a normal for lighting.
- **MIP / Isosurface** — brightest-value / first-threshold rendering modes.
- **Window/level** — center+width that selects a value band to display.
- **RAII** — C++ pattern: object owns and auto-frees a resource.
- **Bind group** (WebGPU) — a bundle of resources handed to a shader.
- **SDL2 / GLM / GLEW / CMake** — windowing+input / math / GL loader / build tool.
- **DICOM** — the medical-imaging file format (the real data, Tier 3-A).
- **Hounsfield units** — CT's density scale (air −1000, water 0, bone +1000).
