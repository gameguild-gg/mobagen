# Roadmap — Target Architecture & Goal

> **This document is the NORTH STAR: where the project is going, not where it is.**
> It is the target architecture and the full todo list for the goal — a
> WebAssembly DICOM volume ray caster (plus a later mesh ray tracer / engine).
>
> - For **where the code actually is now**, see [ARCHITECTURE.md](ARCHITECTURE.md).
> - For **the ordered path** from here to there, see [LEARNING.md](LEARNING.md).
>
> Sequencing discipline (the one real rule): don't *build* the orchestration
> layer — jobs, fibers, ECS — before there is something to orchestrate. It's fine
> to have it written down here as the goal; just build it in the order LEARNING
> lays out.

**Branch:** feat/wasm-dicom-raytracing-renderer
**Goal:** WebAssembly-based DICOM volume ray caster, WebGL2 (learning) → WebGPU (destination)

---

## 📋 Executive Summary

This document tracks what has been completed, what remains, and the structured learning path to build:
1. **DICOM Volume Ray Casting** — Your primary deliverable (medical imaging)
2. **Ray Tracing Engine** — Game engine with mesh-based ray tracing
3. **Unified Renderer Architecture** — Both paths sharing the same infrastructure

---

## ✅ Completed This Session (May 28)

### Infrastructure & Build System

- ✅ **Build directory reorganization**
  - Moved from scattered `build-*` directories to organized structure
  - `build/native/` → Desktop app
  - `build/wasm-webgl/` → G2 (WebGL) WASM
  - `build/wasm-webgpu/` → G3 (WebGPU) WASM

- ✅ **CMakeLists.txt updated**
  - Auto-detects build type (EMSCRIPTEN, USE_WEBGPU)
  - Organizes output by `build_type_dir`
  - Outputs to `build/{native|wasm-webgl|wasm-webgpu}/bin`

- ✅ **Repository cleanup**
  - Deleted 13 old build directories
  - Removed the old `main.cpp.backup` file
  - Removed `diagnostic.log`
  - Cleaned up stray log files

- ✅ **File organization**
  - Moved HTML files: `test.html`, `camera-test.html` → `html/`
  - Moved all `.md` docs → `docs/` except `README.md`
  - Created `docs/INDEX.md` for navigation

### Documentation

- ✅ **BUILD_STRUCTURE.md** — New build directory guide with quick commands
- ✅ **BUILD_WASM.md** — Updated with new directory structure
- ✅ **QUICKSTART.md** — Updated with new build commands
- ✅ **README.md** — Updated with new documentation links and structure
- ✅ **docs/INDEX.md** — Master documentation index

### Camera System

- ✅ **core/sources/camera/camera.hpp** — Complete camera implementation
  - Orbit mode: Rotate around focal point, adjustable distance
  - WASD mode: Free movement with mouse look
  - Mouse wheel: Zoom (Orbit) or speed adjustment (WASD)
  - Mode toggle via 'C' key

- ✅ **SDL Input Integration (apps/dicom_viewer/sources/main.cpp)**
  - Keyboard handlers: `on_key_pressed()`, `on_key_released()`
  - Mouse handlers: `on_mouse_motion()`, `on_mouse_wheel()`
  - Works identically in native and WASM builds

- ✅ **Shader Integration**
  - Camera matrices passed as uniform: `view_projection`
  - Updated vertex shader to use camera matrix
  - Both WebGL and WebGPU paths integrated

- ✅ **Testing**
  - `html/camera-test.html` — Standalone JavaScript camera demo
  - Tests both Orbit and WASD modes
  - Real-time stats (position, distance, yaw/pitch, FPS)
  - Server running on `http://localhost:8084/html/camera-test.html`

### Project Organization

- ✅ **New structure in place**
  ```
  mobagen/
  ├── README.md              ← Overview
  ├── docs/                  ← All documentation
  │   ├── INDEX.md           ← Doc navigation
  │   ├── QUICKSTART.md
  │   ├── BUILD_*.md
  │   ├── CAMERA_*.md
  │   └── WEEK*.md
  ├── html/                  ← Test files + templates
  ├── apps/                  ← app executables and app-specific source/assets
  ├── core/sources/          ← shared engine C++ source
  ├── modules/               ← reusable app-consumable modules
  ├── build/                 ← Organized outputs
  │   ├── native/bin
  │   ├── wasm-webgl/bin
  │   └── wasm-webgpu/bin
  └── external/, cmake/      ← Dependencies
  ```

---

## ❌ Not Yet Covered — Learning Phases

### **Phase 1: Foundation (Weeks 1–3)** 

Goal: Understand GPU rendering pipeline fundamentals.

| Step | Task | Status | Dependency |
|------|------|--------|-----------|
| 1.1 | Window + WebGL2 context (SDL2 + GLEW) | ✅ | CMake setup |
| 1.2 | Triangle with GLES3 shaders | ✅ | 1.1 |
| 1.3 | Uniforms + matrix transforms (camera MVP) | ✅ | 1.2 |
| 1.4 | Textured quad (sampler + UV) | ✅ | 1.3 |

> Note: 1.4 uses a **procedurally generated** texture (checkerboard + UV gradient),
> which teaches the full texture pipeline with no asset/decoder dependency and
> mirrors the synthetic-volume step in Tier 2. Loading a real PNG via stb_image
> is an optional swap on the same `Texture2D` wrapper.

**Deliverables:**
- Window management abstraction
- Shader compilation pipeline
- Buffer management (VAO/VBO)
- Uniform passing system
- Texture loading

---

### **Phase 2: Scene & Lighting (Weeks 4–6)**

Goal: Multiple objects, camera system, lighting.

| Step | Task | Status | Dependency |
|------|------|--------|-----------|
| 2.1 | Free-fly camera (keyboard + mouse) | ✅ | 1.4 |
| 2.2 | Multiple objects (multi-draw organization) | ❌ | 2.1 |
| 2.3 | Phong lighting (ambient + diffuse + specular) | ❌ | 2.2 |
| 2.4 | Framebuffer objects (FBO) / render-to-texture | ✅ | 2.1 |

**Deliverables:**
- Camera abstraction with orbit/WASD
- Renderer abstraction (shader → uniforms → draw)
- Lighting model
- Post-processing pipeline foundation

---

### **Phase 3: Volume Ray Casting Bridge (Weeks 7–10)**

**🔴 PRIORITY: This is your primary work**

Goal: Transition from geometry rendering to volume data rendering.

#### **Part A: Ray Marching Foundation**

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 3.1 | Fullscreen quad + ray generation | ✅ | both renderers; ray dir as RGB |
| 3.2 | Ray march implicit sphere (SDF) | ✅ | both renderers; lit, orbitable |
| 3.3 | 3D texture with synthetic volume | ✅ | both renderers; DVR front-to-back |
| 3.4 | Transfer function (1D LUT) | ◀ NEXT | Maps intensity → RGBA |

**Code structure for 3.1:**
```glsl
#version 300 es
precision highp float;

uniform vec2 resolution;
uniform mat4 invView;
uniform mat4 invProjection;

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    vec3 rayDir = computeRayDirection(uv, invView, invProjection);
    vec3 rayOrigin = /* camera position */;
    
    // March along ray
    vec3 color = vec3(0.0);
    float t = 0.0;
    for (int i = 0; i < 128; i++) {
        vec3 p = rayOrigin + rayDir * t;
        float density = sampleScene(p); // later: texture lookup
        // accumulate color...
        t += stepSize;
    }
    
    fragColor = vec4(color, 1.0);
}
```

#### **Part B: Real DICOM Data**

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 3.5 | Proxy cube method (camera interaction) | ❌ | Vertex + fragment shader pair |
| 3.6 | Load DICOM dataset (DCMTK/GDCM) | ❌ | Parse + stack slices → 3D texture |
| 3.7 | Windowing in shader (GPU Hounsfield) | ❌ | Window/level uniforms |
| 3.8 | Gradient-based shading (6-tap normal) | ❌ | Per-sample Phong lighting |

**DICOM Key Fields:**
- `Rows`, `Columns` — Slice dimensions
- `BitsAllocated`, `BitsStored` — Usually 16-bit
- `RescaleSlope`, `RescaleIntercept` — Convert to Hounsfield Units
- `PixelSpacing` — In-plane resolution (mm)
- `SliceThickness` — Z resolution
- `WindowCenter`, `WindowWidth` — Display windowing

#### **Part C: Rendering Modes**

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 3.8 | Gradient shading | ✅ | both renderers; density-gradient normals |
| 3.9 | MIP mode (Max Intensity Projection) | ✅ | both renderers; mode switch |
| 3.10 | Isosurface rendering (threshold crossing) | ✅ | both renderers; mode switch |
| 3.11 | Compositing modes (average IP, etc.) | ❌ | Different accumulation strategies |

---

### **Phase 4: WebGPU + Compute Shaders (Weeks 11–14)**

**Goal:** Migrate from fragment shader to compute shader for better performance.

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 4.1 | WebGPU boilerplate (compute pipeline) | ❌ | Storage texture output |
| 4.2 | Port ray caster to WGSL compute | ❌ | Same algorithm, different API |
| 4.3 | GPU histogram + auto-windowing | ❌ | Atomic operations in compute |
| 4.4 | Empty space skipping (occupancy grid) | ❌ | Precompute low-res occupancy |
| 4.5 | Adaptive step size optimization | ❌ | Based on gradient magnitude |

**Why Compute > Fragment:**
- Can use shared workgroup memory
- Storage buffers for arbitrary data
- Atomic operations for reductions
- Better for general GPU algorithms

---

### **Phase 5: Ray Tracing Engine (Weeks 15–18)**

**Goal:** Implement mesh-based ray tracing for game engine.

#### **Part A: Basic Ray-Surface Intersection**

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 5.1 | Single triangle intersection (Möller–Trumbore) | ❌ | Geometric ray-triangle test |
| 5.2 | Multiple triangles (brute force) | ❌ | Loop all, find closest hit |
| 5.3 | BVH construction (CPU) | ❌ | Binary tree for acceleration |
| 5.4 | BVH traversal (GPU, iterative) | ❌ | O(log n) ray acceleration |

**Möller–Trumbore algorithm:**
```
Intersect ray R = O + tD with triangle (v0, v1, v2)
Solve: O + tD = v0 + u(v1-v0) + v(v2-v0)
Returns: t (ray parameter), u,v (barycentric coords)
```

#### **Part B: Shading & Effects**

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 5.5 | Basic materials + surface normals | ❌ | From vertex positions |
| 5.6 | Shadow rays (occlusion testing) | ❌ | Cast ray toward light |
| 5.7 | Mirror reflections (specular bounces) | ❌ | Recurse with reflected ray |
| 5.8 | Path tracing (stochastic sampling) | ❌ | Many samples/frame accumulation |

**Path tracing convergence:**
- Hundreds of frames to converge
- Progressive accumulation buffer
- Tone mapping for HDR → display

---

### **Phase 6: Convergence & Hybrid (Weeks 19–22)**

**Goal:** Unified architecture supporting both volume casting and ray tracing.

| Step | Task | Status | Notes |
|------|------|--------|-------|
| 6.1 | Unified renderer abstraction | ❌ | Choose ray caster vs. tracer |
| 6.2 | Scene representation (volume + mesh) | ❌ | Both in same render pass |
| 6.3 | Hybrid rendering (DICOM + surgical tools) | ❌ | Volume occludes by mesh hits |
| 6.4 | Performance profiling & optimization | ❌ | Identify bottlenecks |

**Hybrid example:**
```
Ray hits volume → accumulate color
Ray hits mesh → compare depths, mesh occludes
Ray bounces off mesh → recurse (ray tracing)
Result: Medical scene with surgical guidance
```

---

### **Phase 7: Advanced Architecture (Weeks 23+)**

**Goal:** Doctoral-level systems and optimizations.

#### **Job Scheduling (Fiber/C++26)**

| Task | Status | Notes |
|------|--------|-------|
| Fiber system (CPU work distribution) | ❌ | C++20 coroutines or C++26 execution |
| GPU job coordination | ❌ | CPU↔GPU job graphs |
| Dependency resolution | ❌ | when_all, let_value patterns |

**Why Relevant:**
- DICOM parsing/loading in background (I/O bound)
- Volume preprocessing (CPU windowing, segmentation)
- GPU dispatch management

#### **ECS + Memory Management**

| Task | Status | Notes |
|------|--------|-------|
| Entity-Component-System | ❌ | Organize DICOM series + annotations |
| Arena/pool allocators | ❌ | Large voxel data (150MB+ per scan) |
| GPU resource lifetime | ❌ | Deferred release, ref-counting |

**Memory Challenge:**
- Single CT scan: 512×512×300 × 16-bit = ~150MB
- std::vector reallocs are too expensive
- Need custom allocators for GPU textures

#### **C++ Language Features (C++23/26)**

| Feature | Status | Benefit |
|---------|--------|---------|
| C++20 Coroutines | ❌ | stackless async (fiber alternative) |
| C++26 std::execution | ❌ | Senders/receivers for job graphs |
| Structured bindings | ✅ | Already using in modern C++ |

---

## 📊 Current Status Summary

| Phase | Title | Progress | Timeline |
|-------|-------|----------|----------|
| **1** | Triangle to Screen | 0% | Weeks 1–3 |
| **2** | Camera + Scene | 30% | Weeks 4–6 |
| **3** | **Volume Ray Casting** | **0%** | **Weeks 7–10** |
| **4** | WebGPU Migration | 0% | Weeks 11–14 |
| **5** | Ray Tracing | 0% | Weeks 15–18 |
| **6** | Hybrid Rendering | 0% | Weeks 19–22 |
| **7** | Advanced (ECS, Fibers) | 0% | Weeks 23+ |

**Overall:** ~5% complete (camera system + build infrastructure)

---

## 🎯 Immediate Next Steps

### This Week (May 28 - June 3)

**Objective:** Get Phase 3, Step 3.1 working (fullscreen ray marching)

1. **Scaffold project structure**
  - Create `apps/dicom_viewer/shaders/raycast.glsl` for fragment shader
  - Create `modules/opengl_renderer/sources/raycast_renderer.h/cpp` for pipeline
  - Update `apps/dicom_viewer/sources/main.cpp` to use new renderer

2. **Implement fullscreen quad**
   - Vertex shader: Pass screen UV to fragment shader
   - Fragment shader: Compute ray per pixel
   - Visualize rays as colors (red-green gradient)

3. **Add ray generation**
   - Camera position/direction from camera system
   - Inverse view-projection matrix
   - Compute ray origin + direction from screen UV

4. **Test in browser**
   - Build with `emcmake cmake -B build/wasm-webgl`
   - Run `cd build/wasm-webgl/bin && python -m http.server 8083`
   - Open `http://localhost:8083/dicom_renderer.html`
   - Should see colorful ray directions as gradient

### Following Weeks

**Week 2:** Ray marching implicit sphere (Step 3.2)
**Week 3:** 3D texture + synthetic volume (Step 3.3)
**Week 4:** Transfer function (Step 3.4)
**Week 5:** Real DICOM loading (Step 3.6)

---

## 📚 Learning Resources

### Ray Casting (Your Priority)

- **Will Usher's WebGL2 Volume Rendering** (willusher.io)
  - Best WebGL2 volume rendering tutorial
  - Proxy cube method, transfer functions
  - → START HERE FOR PHASE 3

- **Scratchapixel — Volume Rendering Chapter**
  - Math-focused, API-agnostic
  - Ray marching, sampling, compositing

- **2025 MDPI WebGPU Volume Rendering Paper**
  - Early ray termination, adaptive sampling
  - Directly relevant to Phase 4

- **Shadertoy — "volume rendering" search**
  - Live shadertoy implementations
  - Study + modify in real-time

### Ray Tracing (Game Engine)

- **"Ray Tracing in One Weekend"** (Peter Shirley)
  - Classic introduction
  - Build in C++ first, port to WGSL

- **"Ray Tracing: The Next Week"** (Shirley)
  - BVH, textures, materials

- **Sebastian Lague — "Coding Adventure: Ray Tracing" (video)**
  - Visual explanations of BVH

- **gnikoloff/webgpu-raytracer** (GitHub)
  - Production WGSL compute ray tracer
  - Reference implementation

### General

- **LearnOpenGL** (learnopengl.com)
  - Desktop GL but concepts transfer to GLES3/WebGL2
  - Skip anything "core profile only"

- **Magnum Graphics** (magnum.graphics)
  - Reference architecture for thin GL wrapper
  - Multi-platform (desktop + WASM)

---

## 🏗️ High-Performance Architecture for Raytracing/Path Tracing

### The Performance Hierarchy (Fastest to Slowest)

**CPU-side hot paths:**
1. Inlined direct function call / plain loop
2. Direct function call
3. Function pointer / virtual call
4. User-space callback/event dispatch
5. Lock-free queue / ring buffer event
6. Fiber/coroutine scheduling
7. OS condition variable / mutex / semaphore
8. OS thread context switch
9. OS signals / interrupts
10. CPU-GPU synchronization / GPU readback / blocking fences

**Key insight:** Direct execution beats all forms of coordination/scheduling because coordination adds overhead.

### What NOT to Do (Common Mistakes)

❌ **Signal per ray**
```cpp
// TERRIBLE - architectural suicide
Ray generated → emit RayGeneratedSignal
Ray hit → emit RayHitSignal
Material sampled → emit MaterialSampledSignal
// Each signal triggers dispatch, routing, listeners...
// Overhead destroys throughput
```

❌ **Fiber per tiny operation**
```cpp
// BAD - scheduling overhead larger than work
for every particle:
    yield fiber  // Context switch costs more than particle update
```

❌ **Event per object movement**
```cpp
// BAD - millions of events per frame
struct EntityMoved { EntityId id; Vec3 position; };
// If emitted thousands/millions of times per frame, kills cache
```

❌ **Fine-grained GPU synchronization**
```cpp
// BAD - CPU stalls waiting for GPU
CPU tells GPU to do tiny thing
CPU waits for result
GPU finishes
CPU reads result
repeat millions of times
// Destroys GPU utilization
```

### What TO Do (Best Practices)

✅ **Batch operations into jobs**
```cpp
// GOOD - work per job amortizes scheduling overhead
job_system.parallel_for(particle_count, batch_size, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        particles[i].position += particles[i].velocity * dt;
    }
});
// One job per 1000 particles, not per particle
```

✅ **Direct computation in hot paths**
```cpp
// GOOD - tight, cache-friendly, vectorizable
for (size_t i = 0; i < count; ++i) {
    positions[i] += velocities[i] * dt;
}
// No signals, no events, no callbacks
```

✅ **Coarse-grained events at system boundaries**
```cpp
// GOOD - events when something significant happened
struct PhysicsStepCompleted { uint64_t frame_index; };
struct TextureLoaded { TextureId id; };
struct RenderCompleted { FrameId frame; };
// Not per-object per-frame events
```

✅ **Fibers for coarse async flows**
```cpp
// GOOD - suspend without blocking OS thread
fiber RenderFrame() {
    auto sceneReady = schedule_scene_updates();
    yield_until(sceneReady);
    
    auto accelReady = schedule_bvh_builds();
    yield_until(accelReady);
    
    submit_gpu_frame();
}
// Worker thread continues with other work while fiber waits
```

✅ **GPU async execution with batching**
```cpp
// GOOD - large batches with minimal CPU-GPU sync
for (int pass = 0; pass < num_passes; ++pass) {
    DispatchRaygenKernel(large_batch);     // GPU queue
    DispatchShadeKernel(large_batch);      // GPU queue
    DispatchAccumulateKernel(large_batch); // GPU queue
}
gpu_queue.submit_all();  // One big batch
gpu_fence.wait();        // Sync only at frame boundary
// Not "sync after each ray" or "sync after each pixel"
```

### The Right Granularity Rule

The key question: **Is scheduling cost smaller than work cost?**

| Scenario | Granularity | Overhead | Result |
|----------|------------|----------|--------|
| One operation = 10 ns, scheduling = 200 ns | Per-operation | 200/10 = 20× | **LOSE** |
| Batch = 50 µs, scheduling = 200 ns | Per-batch | 200/50000 ≈ 0.4% | **WIN** |
| Tile rendering = 5 ms, scheduling = 200 ns | Per-tile | 200/5000000 ≈ 0% | **WIN** |
| GPU kernel dispatch = 1 µs, launch = 100 ns | Per-kernel | 100/1000 ≈ 10% | **OK** |

---

## 🎨 Correct Architecture for Raytracing Renderer

### Hierarchy (Top to Bottom)

```
Renderer Coordinator
    ├─ RenderGraph
    │   (Describes passes and dependencies)
    │
    ├─ EventBus
    │   (Coarse system notifications)
    │   • SceneChanged
    │   • CameraChanged
    │   • MaterialChanged
    │   • TextureLoaded
    │   • BVHBuildCompleted
    │   • FrameCompleted
    │
    ├─ JobSystem
    │   (CPU work distribution)
    │   └─ FiberScheduler
    │       (Async waiting without OS thread blocking)
    │
    ├─ ResourceManager
    │   (Textures, geometry, materials, acceleration structures)
    │
    ├─ Integrator
    │   ├─ CPUPathTracer
    │   │   └─ RayBatches → TileJobs → DirectLoops
    │   │
    │   └─ GPUPathTracer
    │       └─ RayQueues → WavefrontKernels → GPU fences
    │
    └─ GPUBackend
        (Command buffers, queues, synchronization primitives)
        • Command buffers
        • Queues (graphics, compute, transfer)
        • Barriers
        • Fences
        • Timeline semaphores
```

### CPU Path Tracer (Direct Approach)

**Frame structure:**
```cpp
void RenderFrame() {
    // Control plane (low frequency)
    ConsumeSceneEvents();              // Events used here
    UpdateDirtyTransforms();
    RefitOrRebuildBVH();               // Fibers if this waits
    
    // Scheduling (low frequency)
    std::vector<TileJob> tiles = SplitIntoTiles(resolution, tile_size);
    job_system.Enqueue(tiles);
    
    // Data plane (high frequency, hot path)
    // Workers execute directly, no events/signals
    // Only batches and atomic counters for synchronization
    
    WaitForCompletion();
    MergeFilmBuffers();
    DenoiseIfNeeded();
}

// Inside a worker thread executing a tile job:
void RenderTile(Tile tile) {
    for (int y = tile.y0; y < tile.y1; ++y) {
        for (int x = tile.x0; x < tile.x1; ++x) {
            Spectrum L = 0;
            
            for (int s = 0; s < samplesPerPixel; ++s) {
                Ray ray = GenerateCameraRay(x, y, s);
                L += TracePath(ray);  // Direct recursion, no events
            }
            
            film.AddSample(x, y, L);  // Atomic or thread-local
        }
    }
}
```

**Good job granularity:**
- Render tile 16×16
- Render sample batch for tile
- Build BVH node range
- Refit BVH range
- Generate camera rays for tile
- Shade hit batch
- Denoise tile

**Bad job granularity:**
- Trace one ray
- Shade one hit
- Update one pixel
- Sample one BSDF

### GPU Path Tracer (Wavefront Approach)

**Frame structure (inspired by PBRT v4):**
```cpp
void RenderFrame(int bounce_depth) {
    // Generate camera rays (one invocation per pixel)
    DispatchRaygenKernel(resolution.x * resolution.y);
    
    for (int depth = 0; depth < bounce_depth; ++depth) {
        // Trace rays against acceleration structure
        DispatchTraceKernel(active_ray_count);
        
        // Shade hits
        DispatchShadeKernel(hit_count);
        
        // Generate shadow rays
        DispatchShadowKernel(hit_count);
        TraceKernel(shadow_ray_count);
        
        // Accumulate direct lighting
        DispatchAccumulateKernel(hit_count);
        
        // Generate next bounce rays
        DispatchNextBounceKernel(hit_count);
        
        // Compact: remove dead rays, queue survivors
        DispatchCompactKernel(ray_count);
    }
    
    // GPU synchronization only at frame boundary
    gpu_fence.Wait();
    
    // Denoise, present, etc.
}
```

**Synchronization points:**
- Between trace and shade (rays are stationary)
- Between shade and accumulate (sampling complete)
- Between accumulate and next bounce (colors ready)
- Frame boundary (all bounces complete)

**NOT:**
- After each ray
- After each intersection
- After each sample
- After each pixel

### Fiber Usage (Coarse Async Flows)

**Good fiber example:**
```cpp
fiber RenderFrame() {
    // Wait for scene loading
    auto scene_ready = schedule_scene_updates();
    yield_until(scene_ready);  // Fiber suspends, worker continues
    
    // Wait for geometry upload
    auto geometry_ready = schedule_geometry_upload();
    yield_until(geometry_ready);
    
    // Wait for BVH build
    auto bvh_ready = schedule_bvh_build();
    yield_until(bvh_ready);
    
    // Wait for shader compilation
    auto shader_ready = schedule_shader_compile();
    yield_until(shader_ready);
    
    // Now render (this part uses jobs, not fibers)
    submit_gpu_frame();
    
    // Wait for GPU to finish
    auto gpu_done = schedule_gpu_fence(frame_fence);
    yield_until(gpu_done);
    
    // Denoise
    auto denoise_ready = schedule_denoise();
    yield_until(denoise_ready);
    
    present();
}
```

**Bad fiber example:**
```cpp
// DON'T DO THIS
fiber TraceRay(Ray ray) {
    yield trace_intersection(ray);      // Way too fine-grained
    yield shade_material(ray);
    yield spawn_secondary_rays(ray);
    yield accumulate_sample(ray);
}
```

### Event/Signal Usage (System Boundaries Only)

**Good events:**
```cpp
// Emitted at system/phase boundaries
struct SceneChanged { Scene* scene; };
struct CameraChanged { Camera camera; };
struct MaterialChanged { MaterialId id; };
struct TextureLoaded { TextureId id; string filename; };
struct GeometryUploaded { MeshId id; };
struct BVHBuildRequested { BVHId id; };
struct BVHBuildCompleted { BVHId id; uint64_t time_ms; };
struct FrameStarted { uint64_t frame_index; };
struct FrameCompleted { uint64_t frame_index; Spectrum exposure; };
struct GPUFenceCompleted { FenceId id; };
struct ResizeRequested { int width; int height; };
struct ShaderRecompiled { ShaderId id; };
```

**Bad events (DO NOT EMIT):**
```cpp
// Never emit these
struct RayGenerated { Ray ray; };
struct RayHit { Ray ray; HitInfo hit; };
struct RayMissed { Ray ray; };
struct RayBounced { Ray ray; Vec3 direction; };
struct PixelSampled { int x; int y; Spectrum value; };
struct BSDFSampled { BSDF bsdf; Vec3 direction; float pdf; };
struct ShadowRayOccluded { bool occluded; };
```

---

### Lock-Free Data Structures (High-Frequency, Low-Contention)

**For ray/work queues:**
```cpp
// SPSC (Single-Producer Single-Consumer) ring buffer
struct RayQueue {
    static const int CAPACITY = 1 << 20;  // 1M rays
    Ray rays[CAPACITY];
    std::atomic<uint32_t> head, tail;
    
    void Push(const Ray& ray) {
        rays[tail % CAPACITY] = ray;
        tail.store(tail + 1, std::memory_order_release);
    }
    
    bool Pop(Ray& ray) {
        if (head.load(std::memory_order_acquire) < tail) {
            ray = rays[head % CAPACITY];
            head++;
            return true;
        }
        return false;
    }
};

// MPSC (Multi-Producer Single-Consumer) with atomic counter
std::atomic<uint32_t> active_rays(initial_count);
active_rays.fetch_sub(1, std::memory_order_acq_rel);  // Atomic, no lock
if (active_rays == 0) { /* All rays traced, move to next stage */ }
```

---

## 🎯 Scoped Architecture for This Project

### Your Path Tracer Architecture

```
MainRenderLoop
    ↓
RenderGraphBuilder (describes frame)
    ├─ Input collection (keyboard, mouse)
    ├─ Scene update (transform, material changes)
    ├─ Resource uploads (textures, geometry)
    ├─ Acceleration structure builds (BVH, TLAS)
    ├─ Shader compilation
    └─ Render passes (trace, shade, denoise)
    
    ↓ (Event bus: "RenderGraphReady")
    
JobScheduler (executes graph)
    ├─ CPU jobs:
    │   ├─ LoadDICOM (I/O bound, may use fiber)
    │   ├─ PreprocessVolume (CPU windowing)
    │   ├─ BuildBVH (parallel BVH construction)
    │   └─ RenderTile (for CPU path tracer mode)
    │
    └─ GPU jobs:
        ├─ UploadTextures
        ├─ DispatchRaygenKernel
        ├─ DispatchTraceKernel
        ├─ DispatchShadeKernel
        └─ DispatchAccumulateKernel
        
    (Synchronization: atomic counters, GPU fences, NOT per-ray events)
    
    ↓ (Event: "FrameComplete")
    
PostProcessing
    ├─ Denoise (optional)
    ├─ ToneMap
    └─ Display
```

### What Synchronization Primitives to Use

| Need | Primitive | Usage |
|------|-----------|-------|
| CPU work distribution | Job system | Tile/batch jobs |
| Load balancing | Work stealing queue | Idle workers steal from busy workers |
| GPU dispatching | Command buffer | Queue GPU work |
| GPU-GPU sync | Pipeline barrier | GPU waits for GPU (in shader) |
| GPU completion | Fence / timeline semaphore | CPU waits for GPU frame |
| Work batch completion | Atomic counter | Jobs complete, signal next stage |
| Async waiting | Fiber/coroutine | Worker doesn't block OS thread |
| Cross-thread data | Lock-free ring buffer | Uncontended producer-consumer |
| System notification | Event/signal | Scene changed, config updated, etc. |

### DO NOT USE

| Primitive | Why Not | Instead Use |
|-----------|---------|------------|
| OS condition variable | Expensive wakeup | Atomic spin-wait for small data |
| OS mutex in hot path | Contention kills throughput | Lock-free queue or atomic |
| Virtual dispatch per ray | Branch prediction failure | Direct loops + monomorphic calls |
| String-based event routing | Hash table lookup per event | Typed events, compile-time routing |
| Reflection for shaders | Runtime overhead | Compile-time shader binding |
| malloc per ray/hit | Allocator contention | Pre-allocated pools, thread-local |
| Event per object per frame | Routing + dispatch overhead | Batch events, coarse granularity |

---

## 📋 Implementation Checklist (High-Performance Path Tracer)

### Core Infrastructure
- [ ] Job system with work-stealing queues
- [ ] Fiber scheduler for async waits (optional but recommended)
- [ ] Lock-free ring buffers for ray/hit queues
- [ ] Atomic counters for stage synchronization
- [ ] Event bus for coarse-grained notifications

### CPU Path Tracer
- [ ] Tile-based rendering (not per-pixel jobs)
- [ ] Direct ray tracing loops (no callbacks per ray)
- [ ] Thread-local accumulators (no contention)
- [ ] BVH traversal (SIMD-friendly, cache-coherent)
- [ ] BSDF sampling (direct, minimal branching)

### GPU Path Tracer (WebGPU)
- [ ] Wavefront architecture (raytracing → shading → accumulate)
- [ ] Ray compaction between stages
- [ ] Minimal CPU-GPU synchronization (fences at frame boundary)
- [ ] Memory pooling for intermediate buffers
- [ ] Asynchronous command submission

### Volume Ray Casting Integration
- [ ] 3D texture for DICOM data (prebuild, not per-frame)
- [ ] Transfer function as 1D LUT (update only when user changes)
- [ ] GPU windowing shader (not CPU preprocessing)
- [ ] Gradient-based shading (6-tap per sample)

---



### Build System
- ✅ **Dual compile:** Native + WASM from same codebase
- ✅ **Organized outputs:** `build/{native|wasm-webgl|wasm-webgpu}`
- ✅ **CMake auto-detection:** Detects Emscripten + USE_WEBGPU flag

### Rendering Paths
- ✅ **WebGL2 first:** Foundation for learning + compatibility
- ✅ **WebGPU migration:** After WebGL2 proven
- ✅ **Compute shaders:** Phase 4 optimization

### Camera System
- ✅ **Orbit mode:** For medical imaging (radiologist workflow)
- ✅ **WASD mode:** For game engine exploration
- ✅ **Integrated with SDL2:** Works in native and WASM

### GPU Data Processing
- ✅ **Windowing on GPU:** Not CPU-side preprocessing
- ✅ **Gradient computation:** Per-sample in shader
- ✅ **Transfer functions:** 1D lookup texture

---

## 🚀 Success Criteria

### Phase 3 Complete (Volume Ray Caster)
- [ ] Loads a real CT scan from DICOM files
- [ ] Displays volume with transfer function (bone/soft tissue)
- [ ] Interactive orbit camera
- [ ] Multiple rendering modes (DVR, MIP, isosurface)
- [ ] Window/level adjustment in real-time
- [ ] 60 FPS on modern hardware

### Phase 4 Complete (WebGPU)
- [ ] Same output as WebGL2 but using compute shaders
- [ ] Empty space skipping optimization
- [ ] GPU-side histogram + auto-windowing
- [ ] Performance profiling shows expected improvements

### Phase 5 Complete (Ray Tracing)
- [ ] Renders triangle meshes via ray tracing
- [ ] BVH acceleration working
- [ ] Shadow rays + reflections
- [ ] Path tracing with progressive convergence

### Phase 6 Complete (Hybrid)
- [ ] DICOM volume + surgical mesh in single frame
- [ ] Mesh geometry occludes volume correctly
- [ ] Real-time performance (60 FPS at 1080p)

---

## 📝 Related Documentation

- [README.md](../README.md) — Project overview
- [docs/INDEX.md](INDEX.md) — Documentation index
- [docs/QUICKSTART.md](QUICKSTART.md) — Setup guide
- [docs/BUILD_STRUCTURE.md](BUILD_STRUCTURE.md) — Build organization
- [docs/CAMERA_TESTING_GUIDE.md](CAMERA_TESTING_GUIDE.md) — Camera system
- [docs/WEEK4-8_ROADMAP.md](WEEK4-8_ROADMAP.md) — Development phases
- [docs/SHADER_LANGUAGES.md](SHADER_LANGUAGES.md) — GLSL vs WGSL

---

## 🔗 GitHub References

- **DCMTK** (Offis/dcmtk) — DICOM parsing library (C++)
- **GDCM** (malaterre/GDCM) — Alternative DICOM library
- **TCIA** (The Cancer Imaging Archive) — Free DICOM datasets
- **wgpu-native** (gfx-rs/wgpu) — C API for WebGPU (reference implementation)
- **VTK.js** (Kitware/vtk-js) — JavaScript medical imaging (reference)

---

## 📞 Questions & Next Steps

**If you're ready to start Phase 3, Step 3.1:**
1. Do you want me to scaffold the fullscreen quad + ray generation shader?
2. Should I set up the render-to-texture pipeline first?
3. Which build target (WebGL2 native or WASM) do you want to iterate on first?

**Current blockers:** None — build system is ready, can start implementing shaders immediately.

---

**Last Updated:** May 28, 2026  
**Next Review:** Upon Phase 3 Step 1 completion
