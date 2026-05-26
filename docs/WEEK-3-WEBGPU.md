# Week 3 — WebGPU: Modern GPU Programming

## The Challenge

Migrate from **OpenGL immediate-mode** (G1, G2) to **WebGPU deferred-mode** rendering.

Same visual result (blue canvas + teal triangle), but using **modern GPU API** that enables **compute shaders** (required for DICOM volume raytracing).

```
Week 1: ✅ Toolchain
Week 2: ✅ Triangle (OpenGL)
Week 3: ➕ Triangle (WebGPU) — parallel implementation
```

---

## Why WebGPU?

### The Problem with OpenGL (Immediate Mode)

```cpp
glUseProgram(shader);           // ← GPU command executes NOW
glBindVertexArray(vao);         // ← GPU command executes NOW
glDrawArrays(GL_TRIANGLES, ...);// ← GPU command executes NOW
```

**Issues:**
- **CPU overhead:** Each call is an immediate GPU instruction
- **State machine complexity:** 50+ global states to manage
- **No compute shaders:** Can't do GPU-parallel computation (raytracing needs this)
- **Hard to optimize:** GPU driver has to optimize a stream of immediate commands

### The Solution: WebGPU (Deferred Mode)

```wgsl
// Record commands
encoder.beginRenderPass()
  .setPipeline(pipeline)
  .setVertexBuffer(vao)
  .draw(3)
  .end()

// Batch and submit
queue.submit([commandBuffer])
```

**Benefits:**
- **Lower CPU overhead:** Commands are recorded, not executed immediately
- **GPU-friendly:** Driver can optimize the entire batch at once
- **Compute shaders:** Full GPU compute support (parallel raytracing)
- **Modern architecture:** Designed for current & future GPU hardware
- **Cross-platform:** Works on Web (via WebGPU) and desktop (via Dawn, wgpu)

---

## Immediate vs Deferred Rendering

### OpenGL Immediate Mode

```
CPU Code (C++)          GPU Execution
─────────────           ─────────────
glUseProgram()  ──→     [Execute on GPU]
glBindVAO()     ──→     [Execute on GPU]
glDrawArrays()  ──→     [Execute on GPU]
[stall]         ←──     GPU finishes
```

**Problem:** CPU waits for GPU. Not pipeline-efficient.

### WebGPU Deferred Mode

```
CPU Code (C++)                GPU Execution
─────────────                 ─────────────
encoder.setPipeline()    [Record command]
encoder.setVertexBuffer()[Record command]
encoder.draw()           [Record command]
encoder.finish()         [Return buffer]
queue.submit()    ──→    [Execute ALL at once]
[Continue code]          GPU processes batch
```

**Benefit:** CPU continues while GPU processes. Better parallelism.

---

## Architecture: Parallel Implementation

### File Structure

```cpp
#ifdef USE_WEBGPU
    // WebGPU Path (deferred, modern)
    struct AppWebGPU { ... };
    // WGSL shaders
#else
    // OpenGL Path (immediate, classic)
    struct App { ... };
    // GLSL shaders
#endif
```

**Why parallel?**
- ✓ Educational: See both APIs side-by-side
- ✓ Safe: GL fallback always works
- ✓ Validation: Compare outputs to verify correctness
- ✗ Trade-off: Some code duplication

**Build:**
```bash
# GL version (default)
emcc src/main.cpp ...  # Compiles GL path

# WebGPU version
emcc src/main.cpp -DUSE_WEBGPU ...  # Compiles WebGPU path
```

---

## WebGPU Triangle: The Code

### WGSL Shaders (instead of GLSL)

**OpenGL (GLSL ES 3.00):**
```glsl
#version 300 es
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

**WebGPU (WGSL):**
```wgsl
struct VertexInput {
  @location(0) position: vec2f,
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
  return vec4f(in.position, 0.0, 1.0);
}
```

**Key differences:**

| Feature | GLSL | WGSL |
|---------|------|------|
| **Syntax** | C-like | Rust-like |
| **Attributes** | `in vec2 aPos` | `@location(0) position: vec2f` |
| **Functions** | `void main()` | `@vertex fn vs_main()` |
| **Return type** | Implicit `gl_Position` | Explicit return with `@builtin(position)` |
| **Types** | `vec2`, `vec4`, `float` | `vec2f`, `vec4f`, `f32` |
| **Structs** | Not used for I/O | Required for I/O |

### Fragment Shader Comparison

**GLSL:**
```glsl
#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    fragColor = vec4(0.0, 1.0, 0.5, 1.0);
}
```

**WGSL:**
```wgsl
@fragment
fn fs_main() -> @location(0) vec4f {
  return vec4f(0.0, 1.0, 0.5, 1.0);
}
```

**Observations:**
- WGSL removes `precision` qualifier (WebGPU handles it internally)
- Return type is explicit: `@location(0) vec4f`
- Function names are explicit: `@fragment` decorator

---

## WebGPU API Structure

### Device & Queue

```cpp
// Request GPU adapter
wgpuInstanceRequestAdapter(nullptr, nullptr, 
    [](WGPURequestAdapterStatus status, WGPUAdapter adapter, ...) {
        // Callback: adapter received
    }, this);

// Request device from adapter
wgpuAdapterRequestDevice(&deviceDesc,
    [](WGPURequestDeviceStatus status, WGPUDevice device, ...) {
        // Callback: device received
    }, this);

// Get queue (command submission point)
WGPUQueue queue = wgpuDeviceGetQueue(device);
```

**Terminology:**
- **Adapter:** GPU hardware capabilities (like selecting a GPU)
- **Device:** GPU context (allocates resources, creates pipelines)
- **Queue:** Submits command buffers to GPU for execution

### Render Pipeline

```cpp
// Compile shader
WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &desc);

// Layout: vertex attributes
WGPUVertexBufferLayout vertexLayout { /* ... */ };

// Pipeline: shader + state
WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
    device, 
    &pipelineDesc  // Contains shader, vertex layout, etc.
);
```

**Concept:**
- Pipeline = Shader + Vertex Layout + Render State (all bundled)
- In OpenGL, these are separate (`glUseProgram`, `glBindVAO`, glBindBuffer`)
- In WebGPU, they're bundled for efficiency

### Command Recording

```cpp
// Create encoder
WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &desc);

// Begin render pass
WGPURenderPassEncoder renderPass = 
    wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

// Record commands
wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, 24);
wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

// Finish recording
wgpuRenderPassEncoderEnd(renderPass);
WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &desc);

// Submit to GPU
wgpuQueueSubmit(queue, 1, &cmdBuffer);
```

**Flow:**
1. Create encoder (recording context)
2. Begin render pass (define what to render)
3. Set pipeline + buffers + draw
4. End pass & encode finish
5. Submit to queue (GPU executes)

---

## Memory Management: Buffers

### OpenGL Way

```cpp
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
```

### WebGPU Way

```cpp
WGPUBufferDescriptor desc{};
desc.size = sizeof(vertices);
desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
desc.mappedAtCreation = true;  // Allow immediate write

WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);

// Write data
void* ptr = wgpuBufferGetMappedRange(buffer, 0, sizeof(vertices));
memcpy(ptr, vertices, sizeof(vertices));
wgpuBufferUnmap(buffer);  // Flush to GPU
```

**Key differences:**
- **Explicit usage flags:** Declare what the buffer will be used for
- **Mapped creation:** Option to write data immediately
- **Manual unmap:** Explicitly flush to GPU

---

## The Complete Render Loop

### OpenGL (Immediate Mode)

```cpp
void App::tick() {
    glClearColor(0.1f, 0.2f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(shader);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    SDL_GL_SwapWindow(window);
}
```

**Simple, but:**
- CPU calls GPU directly
- GPU state scattered across code
- Hard to parallelize across frames

### WebGPU (Deferred Mode)

```cpp
void AppWebGPU::tick() {
    // Get current texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    
    // Create encoder
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &desc);
    
    // Record render pass
    WGPURenderPassColorAttachment colorAttach{};
    colorAttach.clearValue = {0.1f, 0.2f, 0.5f, 1.0f};
    // ... set up pass
    
    WGPURenderPassEncoder renderPass = 
        wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    
    // Record commands
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, 24);
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPass);
    
    // Submit
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &desc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuSurfacePresent(surface);
    
    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    // ... release other resources
}
```

**Verbose, but:**
- All state is explicit
- Commands are batched
- GPU driver can optimize efficiently

---

## Compilation & Testing

### Build WebGPU Version (Future)

When Emscripten WebGPU headers are available:
```bash
emcc src/main.cpp -DUSE_WEBGPU \
  -O3 -sUSE_SDL=2 \
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
  --shell-file html/shell.html \
  -std=c++23
```

Currently, we've implemented the code structure but need to:
1. Upgrade Emscripten to a version with WebGPU support (or)
2. Use JavaScript FFI to call WebGPU from C++

### Build GL Version (Current, Works)

```bash
emcc src/main.cpp \
  -O3 -sUSE_SDL=2 \
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
  --shell-file html/shell.html \
  -std=c++23
```

Compiles the `#else` branch (GL code). WebGPU code is conditionally compiled but not used.

---

## Comparison: GL vs WebGPU

### Code Complexity

| Aspect | OpenGL | WebGPU |
|--------|--------|--------|
| **Lines of code** | ~250 | ~400 |
| **State management** | Scattered | Bundled in pipeline |
| **Shader compilation** | Simple | Structured (callbacks) |
| **Buffer uploads** | 3 lines | 5-6 lines |
| **Draw call** | 3 lines | 5+ lines |
| **Async patterns** | Synchronous | Callback-based (async) |

### Performance Characteristics

| Aspect | OpenGL | WebGPU |
|--------|--------|--------|
| **CPU overhead** | High (per-call) | Low (batched) |
| **GPU optimization** | Limited | Excellent (batched) |
| **Multi-frame pipelining** | Hard | Natural |
| **Compute shaders** | No | Yes ✓ |

---

## Future: Compute Shaders (G4)

Once WebGPU is working, the **next goal** is adding compute shaders:

```wgsl
// Compute shader: raycast through 3D volume
@compute @workgroup_size(8, 8, 1)
fn raycast(@builtin(global_invocation_id) global_id: vec3u) {
    let pixel_coord = global_id.xy;
    
    // Load volume data
    let voxel = volumeTexture[...]
    
    // Raytrace through volume
    var color = vec3f(0.0);
    for (var step = 0u; step < 256u; step = step + 1u) {
        // Sample along ray
        color += sampleVolume(ray_pos, ray_dir);
    }
    
    // Write result to output texture
    outputTexture[pixel_coord] = vec4f(color, 1.0);
}
```

This is the **gateway to DICOM volume raytracing**:
1. Load 3D DICOM volume into GPU texture
2. Compute shader traces rays in parallel (thousands simultaneously)
3. Output colors to screen
4. Real-time 3D medical visualization

---

## The Code in main.cpp

### Structure

```cpp
#ifdef USE_WEBGPU

// ============================================================================
// WebGPU Path (AppWebGPU struct, WGSL shaders)
// ============================================================================

#else

// ============================================================================
// OpenGL Path (App struct, GLSL shaders)
// ============================================================================

#endif
```

**Key files:**
- `src/main.cpp` — Both implementations side-by-side
- `CMakeLists.txt` — `-DUSE_WEBGPU` toggle
- `docs/WEEK-3-WEBGPU.md` — This guide

---

## Learning Path

### Phase 1: Understand OpenGL Version
1. Read WEEK-2-TRIANGLE.md (immediate mode)
2. Understand the GL code in main.cpp
3. Build & run GL version

### Phase 2: Study WebGPU Concepts
1. Read this document (WEEK-3-WEBGPU.md)
2. Compare GL vs WebGPU side-by-side in main.cpp
3. Understand WGSL syntax

### Phase 3: Compile & Test WebGPU
When Emscripten WebGPU support arrives:
1. Update Emscripten SDK
2. Compile with `-DUSE_WEBGPU`
3. Test in Chrome (WebGPU support required)
4. Verify same triangle renders

### Phase 4: Compute Shaders (G4)
1. Create compute shader (WGSL)
2. Create storage buffers for I/O
3. Dispatch compute work
4. Read back results or display

---

## Troubleshooting

### "error: use of undeclared identifier 'WGPUDevice'"

**Cause:** WebGPU headers not found (Emscripten version too old)

**Fix:** 
```bash
# Update Emscripten
cd ~/emsdk
./emsdk update && ./emsdk install latest && ./emsdk activate latest
```

### "Cannot compile with -DUSE_WEBGPU on current Emscripten"

**Workaround:** Use GL version for now
```bash
emcc src/main.cpp ...  # Omit -DUSE_WEBGPU
```

The code compiles either way. WebGPU path is ready when headers arrive.

---

## Resources

- **WebGPU Specification:** https://www.w3.org/TR/webgpu/
- **WGSL Spec:** https://www.w3.org/TR/WGSL/
- **WebGPU Examples:** https://github.com/gpuweb/gpuweb/wiki/Implementation-Status
- **Learn WebGPU:** https://learner.webgpu.dev/
- **Emscripten WebGPU:** https://emscripten.org/ (check WebGPU section)

---

## Summary: What We've Implemented

✅ **Parallel implementation:** GL (working) + WebGPU (code ready, headers pending)  
✅ **WGSL shaders:** Modern, async-friendly shader language  
✅ **Deferred rendering:** Command recording + batch submission  
✅ **Same API:** Both `AppWebGPU` and `App` have `init()` / `tick()` / `cleanup()`  
✅ **Build toggle:** `-DUSE_WEBGPU` flag to switch implementations  

Next: Upgrade Emscripten when WebGPU headers are available, then migrate to compute shaders for DICOM volume raytracing.
