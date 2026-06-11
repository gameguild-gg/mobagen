# Learning Path

> Current WebGPU status: the Dawn host now consumes DOD `RenderBridge` commands
> directly. It embeds `apps/dicom_viewer/shaders/raygen.wgsl`, uploads a 3D texture, uploads the
> transfer LUT, and passes camera/window/mode/spacing/debug data as uniforms.
> The default browser input is still a 96^3 phantom. Native `USE_GDCM=ON` can now
> read a DICOM series into `VolumeBuffer` while preserving stored UInt16 voxels.
> For WebGPU, those UInt16 values are packed into an `RG8Unorm` 3D texture and
> reconstructed in WGSL before window/level is applied. The WebGPU path also has
> a first compute shader histogram pass: one invocation per voxel, `atomicAdd`
> into bins, read the bins back, then choose an automatic 1%-99% window. The
> learning point is the handoff: loader/ECS owns scene metadata, `RenderBridge`
> flattens it, and the renderer records GPU commands from that flat packet.

The road from "triangle on screen" to "DICOM volume ray caster" (and later, a
mesh ray tracer). Each rung **produces something visible** and feeds the DICOM
goal. A rung is done when it renders *and* you can explain why it works.

This is the *path*. The destination — full target architecture and todo — is the
north star in [ROADMAP.md](ROADMAP.md); the current code is in
[ARCHITECTURE.md](ARCHITECTURE.md). **New to C++/GPUs/graphics? Read
[CONCEPTS.md](CONCEPTS.md) first — it explains every idea here from zero.**

You are currently in **Tier 3** (real-DICOM prep + CPU memory ownership).

> Scope discipline: build the **volume ray caster first** (your dissertation
> deliverable). The ray tracer and any job-system / ECS / fiber work come *after*
> — they reuse the same ray-generation and compute infrastructure. Don't build a
> scheduler before there's something to schedule.

---

## Tier 1 — Earn the fundamentals  *(complete)*

1. **One honest WebGL2 build.** Triangle, working MVP camera, real delta-time,
   gated mouse-look. ✅
2. **Textured quad.** `Texture2D` wrapper → sample in the fragment shader by UV.
   ✅ *(uses a procedural checkerboard+gradient texture; swap to a real PNG via
   stb_image is optional. This is the seed of "sample data from a texture" — i.e.
   all of volume rendering.)*
3. **Render-to-texture (FBO).** Scene is drawn into an offscreen `Framebuffer`,
   then a fullscreen quad samples it through a post shader (vignette). ✅
   *This is the surface the ray caster will write into, and the "ping-pong"
   primitive behind GPGPU in WebGL2.* (WebGL build only so far.)

## Tier 2 — Ray marching (the heart of the thesis)

4. **Fullscreen quad + ray generation.** One ray per pixel from the inverse
   view-projection; ray direction shown as RGB. ✅ *(WebGL path. The old JS WebGPU
   bridge proved the idea; the current Dawn WebGPU host still needs the C++ WGSL
   port.)*
5. **March an implicit sphere (SDF).** `length(p) - r`; sphere-trace the ray,
   gradient-normal + diffuse shading on hit. ✅ *(WebGL path; the old JS WebGPU
   prototype also proved it. Identical loop to volume rendering, math instead of
   a texture.)*
6. **3D texture, synthetic volume.** A 64^3 soft ball in a `TEXTURE_3D`; the ray
   marches the unit cube and accumulates density front-to-back (DVR). ✅ *(WebGL
   path — a semi-transparent glowing ball. You are now volume rendering.)*
7. **Transfer function + compositing.** A 1D LUT mapping density → RGBA, with 4
   presets (Gray/Tissue/Shell/Cool). ✅ *(WebGL path)*
8. **Gradient shading + render modes.** Density-gradient normals for lit volume;
   DVR / MIP / Isosurface mode switch. ✅ *(WebGL path)*

**Tier 2 complete for WebGL.** A full synthetic-volume ray caster: rays -> 3D
texture -> transfer function -> shading -> DVR/MIP/Iso. The current WebGPU task is
to port this algorithm onto the Dawn host.

## Tier 3 — Real DICOM  *(in progress; path B = de-risk the data path first)*

We chose **path B**: prove the load → window → spacing pipeline against a raw
file *before* taking on the big DICOM-parser dependency. Everything downstream of
the loader gets proven on a stand-in phantom first.

- **B1 ✅** Load the volume from a **file** (`volume.raw` / `volume.mvol`), not
  generated inside the renderer — the same path real data will use. WebGL reads
   `volume.raw` from the WASM FS (Emscripten `--preload-file`); WebGPU reads
   `volume.mvol`. These binary files are local asset-manager outputs, not Git
   content: `make assets` reads `apps/dicom_viewer/assets/assets.json`,
   downloads/verifies the source dataset, and generates the renderer-ready files.
9. **B2 ✅** **Window/level** in the shader (center+width sliders) — remap a band
   of density to [0,1], clipping outside. The "bone window / brain window" knob.
10. **B3 ✅** **Voxel spacing** — the volume box scales per-axis by voxel spacing
    (uBoxHalf), so non-cubic scans aren't squished. (Phantom uses z=1.5 to show it.)
11. **A ✅ native-first** **Real DICOM parser handoff**: `volume_io` can read a
    DICOM series with GDCM in native builds and pass stored UInt16 voxels into
    `VolumeBuffer`. WebGPU uploads them as packed `RG8Unorm` bytes, reconstructs
    the UInt16 stored value in WGSL, then applies window/level on the GPU. WebGL
    and the browser phantom still use the simpler normalized R8 path. Browser/WASM
    DICOM parsing is still a separate decision because shipping GDCM/DCMTK into
    WASM is a heavy dependency choice.
12. **Memory ownership ✅ first pass**: `VolumeBuffer` uses `std::pmr::vector`
    and can allocate from a `VolumeArena`. This is the study bridge from "STD
    until it hurts" toward arena/pool ownership for large scan data.

> Note: instead of the classic "proxy-cube" two-pass for ray entry/exit, we do an
> analytic **ray–AABB (slab) intersection** in the fragment shader — simpler and
> equivalent for an axis-aligned volume box.

## Tier 4 — WebGPU + compute (the research contribution)

13. Port the ray caster into a **WGSL compute shader** writing a storage texture.
14. **GPU histogram + auto-windowing ✅ first pass** with atomics — *impossible in
    WebGL2; this is the concrete justification for WebGPU.* Current version:
   `apps/dicom_viewer/shaders/histogram.wgsl` builds a 256-bin R8 or 65,536-bin UInt16 histogram
    in a storage buffer. C++ reads the bins back and applies a 1%-99% auto window.
    Later version: do the percentile reduction fully on GPU.
15. Optimizations: early-ray-termination (have it), empty-space skipping, adaptive step.

## Tier 5 — *Only now* the systems material

16. A **job system** to load/preprocess slices off the main thread — the first
    place coroutines/fibers earn their keep (see archive notes on HPC orchestration).
17. The mesh **ray tracer**: Möller-Trumbore → BVH → shadow/reflection rays,
    reusing the Tier-2 ray generation.

Tiers 4–5 are roughly a year out. Read the archived HPC/architecture notes for
context now; don't build that layer yet.

---

## Build & run

### Bash-first workflow

The project now has a Makefile because the working habit is **Bash + make**.
The WASM targets export the Emscripten SDK variables and call CMake directly with
`C:/Users/MatheusMartins/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`.
This avoids the Windows shell wrapper problems we hit with `emcmake` /
`emsdk_env.sh`, while keeping the workflow pure Bash.

```bash
make help
```

If `make` is not on the machine yet, install GNU Make or run the commands inside
the Makefile manually from a Bash shell. The project should not depend on
PowerShell-specific setup.

On this Windows machine, GNU Make was installed with Winget as GnuWin32 Make.
Fresh Git Bash sessions may still need this PATH line until the environment is
refreshed:

```bash
export PATH="/c/Progra~2/GnuWin32/bin:$PATH"
make help
```

### WebGL2 (G2) — the learning rung
```bash
make wasm-webgl
cd build/wasm-webgl/bin && python -m http.server 8083 --bind 127.0.0.1
# open http://127.0.0.1:8083/dicom_renderer.html
```

### WebGPU (G3) — the destination
```bash
make wasm-webgpu
cd build/wasm-webgpu/bin && python -m http.server 8084 --bind 127.0.0.1
# open http://127.0.0.1:8084/dicom_renderer.html
```

### Native WebGL/OpenGL
```bash
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --target dicom_renderer --config Release
./build/native/bin/Release/dicom_renderer.exe
```

### Native WebGPU (Dawn + ImGui)
```bash
make native-webgpu
./build/native-webgpu/bin/Release/dicom_renderer.exe
```

When building the full Dawn-generated Visual Studio solution, external Dawn
targets can hit Windows file-lock or MSBuild memory pressure. The Makefile builds
the `dicom_renderer` target directly and passes `/m:1` through MSBuild with
`MSYS_NO_PATHCONV=1`, which is slower but reliable for Dawn's large generated
projects under Git Bash.

### Optional build flags (brought over from master)
- **DICOM (Tier 3-A):** `-DUSE_GDCM=ON` builds GDCM from source and links the
  DICOM reader into `volume_io`. Heavy first build; OFF by default.
- **Sanitizers (native):** `-DUSE_SANITIZER=Address` (or `Undefined`, `Thread`,
  `Leak`, `"Address;Undefined"`) — catches OOB/UAF at runtime. Would have caught
  the `keys_pressed_` overflow immediately.
- **Static analysis:** `-DUSE_STATIC_ANALYZER=clang-tidy` (or `iwyu`, `cppcheck`).
- **ccache:** `-DUSE_CCACHE=YES` to cache compiles.
- `-DUSE_ITK=ON` / `-DUSE_VTK=ON` exist for later; not used yet.

A `build/<type>/third_party.txt` listing every dependency's license is written
each configure (CPMLicenses).

The lightweight DICOM verification target is:

```bash
make dicom-smoke
```

It validates the native GDCM loader without requiring the full native WebGPU +
GDCM renderer link. The current smoke sample reports a `96 x 96 x 96` volume,
`1.000 x 1.000 x 1.500 mm` spacing, slope `1.000`, intercept `-1024.0`, and a
center voxel of stored `1034 -> 10.0 HU`.

The full native renderer + GDCM verification target is:

```bash
make native-dicom
./build/native-dicom/bin/Release/dicom_renderer.exe
```

For command-line smoke testing without manually closing the window, run it in the
background and kill it after startup:

```bash
./build/native-dicom/bin/Release/dicom_renderer.exe > build/native-dicom/runtime-smoke.log 2>&1 &
pid=$!
sleep 25
kill "$pid" 2>/dev/null
sed -n '1,120p' build/native-dicom/runtime-smoke.log
```

A successful initialization prints that WebGPU is ready, loads the DICOM sample,
initializes the packed UInt16 RG8 volume renderer, and opens the ImGui panel.

The HTML shell files are part of the Emscripten link step, not runtime assets.
`apps/dicom_viewer/CMakeLists.txt` marks `apps/dicom_viewer/htmls/shell_dawn.html` and `apps/dicom_viewer/htmls/shell_webgl.html` as
link dependencies so a UI/template edit relinks `dicom_renderer.html` instead
of leaving you with stale generated HTML.

> ⚠️ **After every rebuild, hard-refresh the browser** (Ctrl+Shift+R, or DevTools →
> "Empty Cache and Hard Reload"). A soft refresh serves a stale cached `.wasm`/`.data`
> against the new files and blanks the canvas — this is *not* a bug, just cache.

---

## Controls

| Input | Action |
|-------|--------|
| Click canvas | Focus browser input so SDL receives keys |
| Hold mouse + drag | Rotate (ORBIT) / look (WASD) |
| Mouse wheel | Zoom (ORBIT) / speed (WASD) |
| `C` | Toggle ORBIT ↔ WASD |
| `WASD` + `Space` | Move (WASD mode) |
| `Shift` / `Ctrl` | Descend in WASD mode |
| Right/middle drag | Pan in ORBIT mode |
| `R` | Reset the camera |
| `P` | Request browser pointer lock |
| `1` / `2` / `3` / `4` | Transfer-function preset (Gray / Tissue / Shell / Cool) |
| Mode buttons | DVR · MIP · Isosurface |
| Window sliders | Center / Width (window-level) |
| Debug view | Final image · ray direction · ray depth · sample count |
| Ray samples | Quality/cost knob for the ray-marching loop |
| Opacity scale | Per-sample opacity multiplier for DVR compositing |

WebGL exposes renderer controls in the HTML panel. WebGPU exposes them through
ImGui inside the canvas, because the Dawn/WebGPU renderer owns its UI as part of
the render pass. The WebGPU ImGui panel prints the active camera mode, position,
yaw/pitch, and either orbit radius or WASD speed. That is intentional study
feedback: when you drag, scroll, or press `C`, you can see which state changed
instead of guessing whether input reached the engine.

---

## Reference reading

- **Will Usher — "Volume Rendering with WebGL"** (willusher.io). The single best
  WebGL2 volume-rendering tutorial — proxy cube + transfer functions. Start here for Tier 2–3.
- **Scratchapixel** — volume rendering + ray-surface intersection math (API-agnostic).
- **"Ray Tracing in One Weekend"** (Shirley) — for the Tier-5 mesh ray tracer.
- **MDPI 2025** — WebGPU volume rendering with early-ray-termination / adaptive sampling (Tier 4).
- **DCMTK / GDCM** — DICOM parsing (C/C++). **The Cancer Imaging Archive** — free datasets.
