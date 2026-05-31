# Learning Path

The road from "triangle on screen" to "DICOM volume ray caster" (and later, a
mesh ray tracer). Each rung **produces something visible** and feeds the DICOM
goal. A rung is done when it renders *and* you can explain why it works.

This is the *path*. The destination — full target architecture and todo — is the
north star in [ROADMAP.md](ROADMAP.md); the current code is in
[ARCHITECTURE.md](ARCHITECTURE.md).

You are currently finishing **Tier 1**.

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
   view-projection; ray direction shown as RGB. ✅ *(both renderers. This is where
   the camera reaches the shader — in WebGPU via a C++→JS matrix bridge (EM_ASM).)*
5. **March an implicit sphere (SDF).** `length(p) - r`; sphere-trace the ray,
   gradient-normal + diffuse shading on hit. ✅ *(both renderers — a lit sphere
   you can orbit. Identical loop to volume rendering, math instead of a texture.)*
6. **3D texture, synthetic volume.** A 64³ soft ball in a `TEXTURE_3D`; the ray
   marches the unit cube and accumulates density front-to-back (DVR). ✅ *(both
   renderers — a semi-transparent glowing ball. You are now volume rendering.)*
7. **Transfer function + compositing.** A 1D LUT mapping density → RGBA, with 4
   presets (Gray/Tissue/Shell/Cool). ✅ *(both renderers)*
8. **Gradient shading + render modes.** Density-gradient normals for lit volume;
   DVR / MIP / Isosurface mode switch. ✅ *(both renderers)*

**Tier 2 complete.** A full synthetic-volume ray caster: rays → 3D texture →
transfer function → shading → DVR/MIP/Iso, in WebGL2 and WebGPU.

## Tier 3 — Real DICOM  *(in progress, path B: de-risk first)*
- **B1 ✅** load the volume from a raw file (`volume.raw`), not generated in code —
  the same load path real data will use (both renderers).
- **B2 ✅** window/level in-shader (center+width sliders) — isolate density bands.
- **B3 ◀ next** voxel spacing (non-cubic volume box).
- **A** real DICOM parser (GDCM/DCMTK → WASM): parse a CT series, Hounsfield
  rescale, stack into the 3D texture. The heavy lift; B has de-risked everything
  downstream of it.

## Tier 3 — Real DICOM

8. **Proxy-cube entry/exit** for correct camera interaction with the volume box.
9. **Load a real CT series.** GDCM/DCMTK compiled to WASM; free data from
   The Cancer Imaging Archive. Stack slices → 3D texture, respect voxel spacing.
10. **Windowing + Hounsfield in the shader** (your first true "GPU-side
    processing"), then **gradient shading** (6-tap normal + Phong).
11. **Rendering modes:** DVR, MIP, isosurface. *Now it's a usable viewer.*

## Tier 4 — WebGPU + compute (the research contribution)

12. Port the ray caster into a **WGSL compute shader** writing a storage texture.
13. **GPU histogram + auto-windowing** with atomics — *impossible in WebGL2; this
    is the concrete justification for WebGPU.*
14. Optimizations: early-ray-termination, empty-space skipping, adaptive step.

## Tier 5 — *Only now* the systems material

15. A **job system** to load/preprocess slices off the main thread — the first
    place coroutines/fibers earn their keep (see archive notes on HPC orchestration).
16. The mesh **ray tracer**: Möller-Trumbore → BVH → shadow/reflection rays,
    reusing the Tier-2 ray generation.

Tiers 4–5 are roughly a year out. Read the archived HPC/architecture notes for
context now; don't build that layer yet.

---

## Build & run

Set the Emscripten environment once per shell:

```powershell
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH  = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:EMSDK\node\22.16.0_64bit\bin;$env:PATH"
```

### WebGL2 (G2) — the rung you're on
```powershell
emcmake cmake -B build/wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgl
cd build/wasm-webgl/bin ; python -m http.server 8083
# open http://localhost:8083/dicom_renderer.html
```

### WebGPU (G3) — the destination
```powershell
emcmake cmake -B build/wasm-webgpu -DUSE_WEBGPU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgpu
cd build/wasm-webgpu/bin ; python -m http.server 8084
# open http://localhost:8084/dicom_renderer.html  (Chrome/Edge 113+)
```

### Native (fast desktop iteration, WebGL/OpenGL only)
```powershell
cmake -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
./build/native/bin/dicom_renderer
```

### Standalone camera demo (no build)
```powershell
python -m http.server 8085
# open http://localhost:8085/html/camera-test.html
```

---

## Controls

| Input | Action |
|-------|--------|
| Hold mouse + drag | Rotate (ORBIT) / look (WASD) |
| Mouse wheel | Zoom (ORBIT) / speed (WASD) |
| `C` | Toggle ORBIT ↔ WASD |
| `WASD` + `Space` | Move (WASD mode) |
| `1` / `2` / `3` / `4` | Color variant |

---

## Reference reading

- **Will Usher — "Volume Rendering with WebGL"** (willusher.io). The single best
  WebGL2 volume-rendering tutorial — proxy cube + transfer functions. Start here for Tier 2–3.
- **Scratchapixel** — volume rendering + ray-surface intersection math (API-agnostic).
- **"Ray Tracing in One Weekend"** (Shirley) — for the Tier-5 mesh ray tracer.
- **MDPI 2025** — WebGPU volume rendering with early-ray-termination / adaptive sampling (Tier 4).
- **DCMTK / GDCM** — DICOM parsing (C/C++). **The Cancer Imaging Archive** — free datasets.
