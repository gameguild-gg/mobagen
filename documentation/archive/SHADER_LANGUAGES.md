# GPU Shaders: GLSL ES 3.0 vs WGSL

This document compares GLSL ES 3.0 (used in WebGL via G2) and WGSL (used in WebGPU via G3). Both produce identical visual output in this project—use the color variants and renderer switcher to observe their equivalence.

## Quick Comparison

| Aspect | GLSL ES 3.0 | WGSL |
|--------|-----------|------|
| **Year** | 2012 | 2021 |
| **Target** | OpenGL ES 3.0 (mobile/web) | WebGPU (modern GPUs) |
| **Syntax** | C-like with vectors | Rust-like with structs |
| **Type Safety** | Loose (implicit conversions) | Strict (explicit types) |
| **Compute Shaders** | No | Yes |
| **Memory Layout** | Implicit | Explicit (`std140`, etc.) |

## The Triangle Shader

Both renderers draw the same triangle with the same colors. Here's how each implements it.

### GLSL ES 3.0 (Immediate-Mode: G2/WebGL)

**Vertex Shader** (`#version 300 es`):
```glsl
#version 300 es
layout(location = 0) in vec2 aPos;

void main() {
    // Input: 2D position in normalized device coordinates (-1 to 1)
    // Output: Homogeneous clip-space position
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

**Fragment Shader** (generated with color variant):
```glsl
#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    // Output: RGBA color (0.0 to 1.0 per channel)
    fragColor = vec4(0.0, 1.0, 0.5, 1.0);  // Teal
}
```

**Key Features**:
- **Loosely typed**: `vec4()` constructor accepts `float` or `int`, compiler coerces
- **Built-in variables**: `gl_Position` (output), `gl_FragCoord` (input)
- **Layout qualifiers**: `in`, `out`, `layout(location = 0)`
- **No structs needed**: Per-vertex data flows as individual attributes
- **Precision hints**: `precision mediump float` tells GPU to use 16-bit floats (for mobile)

### WGSL (Deferred-Mode: G3/WebGPU)

**Shader Module** (vertex + fragment in one file):
```wgsl
struct VertexInput {
  @location(0) position: vec2f,
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
  // Input: Struct with explicit types
  // Output: Builtin position (required for rasterization)
  return vec4f(in.position, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
  // Output: Float vec4 at render target location 0
  return vec4f(0.0, 1.0, 0.5, 1.0);  // Teal
}
```

**Key Features**:
- **Strongly typed**: `vec2f` (32-bit float vec2), `vec4f` (32-bit float vec4) — no ambiguity
- **Attributes as structs**: `VertexInput` bundles all per-vertex data
- **Decorators**: `@vertex`, `@fragment`, `@location(0)`, `@builtin(position)` are explicit
- **Function-based entry points**: `fn vs_main`, `fn fs_main` with return types
- **No global precision**: All types are explicit; 32-bit is default
- **Compute-capable**: Can use `@compute fn` for parallel GPU work (WebGL cannot)

## Evolution of Thinking

### GLSL ES 3.0: "State Machine Thinking"

GLSL was designed when GPUs were fixed-function state machines:

```cpp
// C++ (Immediate Mode)
glUseProgram(program);           // Set program state
glUniform4f(color_loc, ...);     // Set uniform state
glBindBuffer(...);               // Set buffer state
glDrawArrays(GL_TRIANGLES, ...); // Execute with current state
// GPU processes immediately!
```

```glsl
// GLSL: Implicitly accesses global state
uniform vec4 color;              // Global from C++
in vec2 aPos;                    // Implicit per-vertex data flow
out vec4 fragColor;              // Implicit output

void main() {
    fragColor = color;           // Access globals without declaring dependency
}
```

**Learning**: GLSL treats the GPU as a programmable state machine. Vertex/fragment shaders are entry points executed within a global context (uniforms, textures, attributes).

### WGSL: "Command Buffer Thinking"

WGSL was designed for modern GPUs with deferred rendering:

```javascript
// JavaScript (Deferred Mode)
const encoder = device.createCommandEncoder();
const pass = encoder.beginRenderPass({...});
pass.setPipeline(pipeline);      // Record pipeline state
pass.setVertexBuffer(0, buffer); // Record buffer binding
pass.draw(3);                    // Record draw command
pass.end();
queue.submit([encoder.finish()]); // Submit batch to GPU
// GPU optimizes whole batch, then processes!
```

```wgsl
// WGSL: Explicitly declares all inputs/outputs
struct VertexInput {
  @location(0) position: vec2f,  // This is where data flows from
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
  return vec4f(in.position, 0.0, 1.0);  // Return value is output
}
```

**Learning**: WGSL treats shaders as pure functions. Inputs are function parameters, outputs are return values. The GPU driver can analyze the entire pipeline before execution.

## Type System Comparison

### Numbers

| Operation | GLSL ES 3.0 | WGSL |
|-----------|-----------|------|
| 32-bit float | `float` | `f32` |
| 32-bit int | `int` | `i32` |
| 32-bit uint | `uint` | `u32` |
| Implicit cast | `vec4(0.0)` → `vec4(0, 0, 0, 0)` | ❌ Error: `vec4f(0)` is wrong |

**Example**:
```glsl
// GLSL: Loose typing
float x = 0;        // OK, int → float coercion
vec4 c = vec4(0);   // OK, int → float in all components
```

```wgsl
// WGSL: Strict typing
var x: f32 = 0;         // Error: 0 is i32, not f32
var x: f32 = 0.0;       // OK
var c = vec4f(0, 0, 0, 0);    // Error: 0 is i32
var c = vec4f(0.0, 0.0, 0.0, 0.0);  // OK
```

### Vectors

| Operation | GLSL | WGSL |
|-----------|------|------|
| Create | `vec2(x, y)` | `vec2f(x, y)` |
| Swizzle | `v.xyz` | `v.xyz` |
| Length | `length(v)` | `length(v)` |
| Dot product | `dot(a, b)` | `dot(a, b)` |

**Example** (identical in both):
```glsl
vec2 pos = vec2(0.5, 0.25);
float r = length(pos);              // GLSL
// Same:
var pos = vec2f(0.5, 0.25);
let r = length(pos);                // WGSL
```

### Matrices

| Operation | GLSL | WGSL |
|-----------|------|------|
| Create 4x4 | `mat4(...)` | `mat4x4f(...)` |
| Multiply | `matrix * vector` | `matrix * vector` |
| Inverse | `inverse(m)` | ❌ Not standard (use CPU) |

**Example**:
```glsl
// GLSL: Implicit matrix/vector math
vec4 clip_pos = mvp * vec4(position, 1.0);
```

```wgsl
// WGSL: Same syntax, but types are explicit
let clip_pos: vec4f = mvp * vec4f(position, 1.0);
```

## Attributes and Uniforms

### GLSL: Global State

```glsl
#version 300 es

// Per-vertex input (from vertex buffer)
layout(location = 0) in vec2 aPos;

// Constant for all vertices (set by CPU)
uniform mat4 mvp;
uniform vec4 color;

// Per-vertex output (interpolated across triangle)
out vec4 vColor;

void main() {
    gl_Position = mvp * vec4(aPos, 0.0, 1.0);
    vColor = color;
}
```

**Flow**: CPU sets state (`glUniform4f(color_loc, ...)`) → Shader reads globals → GPU executes

### WGSL: Structured Binding

```wgsl
struct VertexInput {
  @location(0) position: vec2f,
  @location(1) color: vec3f,
};

struct VertexOutput {
  @builtin(position) clip_pos: vec4f,
  @location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
  var out: VertexOutput;
  out.clip_pos = vec4f(in.position, 0.0, 1.0);
  out.color = in.color;
  return out;
}
```

**Flow**: GPU driver analyzes function signature → Knows exactly what data flows where → Optimizes accordingly

## Performance Implications

### Immediate-Mode (GLSL/OpenGL): G2 WebGL

```cpp
// Each call context-switches GPU
for (size_t i = 0; i < 1000; i++) {
    glUniform4f(color_loc, colors[i].r, ...);  // CPU→GPU wait
    glDrawArrays(GL_TRIANGLES, 0, 3);          // GPU wait for CPU
    glReadPixels(...);                          // GPU→CPU stall ❌
}
```

**Cost**: 3000 CPU-GPU context switches, synchronization points, driver overhead

### Deferred-Mode (WGSL/WebGPU): G3 WebGPU

```javascript
// Batch all commands, submit once
const encoder = device.createCommandEncoder();
for (let i = 0; i < 1000; i++) {
    const pass = encoder.beginRenderPass({...});
    pass.setPipeline(pipeline);
    pass.draw(3);
    pass.end();
}
queue.submit([encoder.finish()]);  // Single submit ✅
```

**Cost**: 1 CPU-GPU sync point, driver optimizes entire batch at once

**Measured in This Project**:
- On simple geometry (1 triangle): Both achieve 60 FPS (GPU-limited, not CPU-bound)
- With 1000+ triangles: WebGPU would show CPU overhead advantage
- Goal: DICOM volume raytracing via compute shaders (WebGPU only)

## Language Features

### GLSL ES 3.0 Limitations

- **No structs**: Use individual varyings and uniforms
- **No array of structs**: Painful for per-vertex data with >3 attributes
- **No compute**: Rasterization-only
- **No push constants**: Must use uniform buffers
- **Implicit conversions**: Can hide type errors

### WGSL Capabilities (for Future)

- **Compute shaders**: `@compute fn` for parallel GPU work
- **Atomic operations**: `atomicAdd()`, `atomicLoad()` for GPU-wide synchronization
- **Storage buffers**: `storage` texture/buffer for read-write data
- **Structured data**: Full struct support with proper alignment
- **Entry points**: Multiple shaders per module

**Example Compute Shader** (future DICOM raytracing):
```wgsl
@compute @workgroup_size(16, 16)
fn raytrace(@builtin(global_invocation_id) gid: vec3u) {
  let pixel = gid.xy;
  let ray_dir = compute_ray_direction(pixel);
  let color = march_ray_through_volume(ray_dir);
  textureStore(output_texture, pixel, color);
}
```

## Testing in This Project

Use the runtime renderer switcher to compare:

1. **Visual Equivalence**: Both renderers produce identical triangle (teal on blue)
2. **Shader Variants**: Press `3-6` to cycle colors; both update correctly
3. **Performance**: Check FPS in top-right; note immediate vs. deferred tradeoffs

**Try This**:
- Switch to WebGL → Press `3` (red)
- Switch to WebGPU → Press `3` again
- Observe: Identical red triangle in both renderers
- **Lesson**: Different shading language, same GPU result

## Learning Path

1. **Week 4**: GLSL ES 3.0 fundamentals (vertex/fragment, varyings, uniforms)
2. **Week 5**: WGSL equivalents (structs, decorators, entry points)
3. **Week 6**: Compute shaders (WGSL only; enables raytracing)
4. **Week 7+**: Optimize based on measured performance

## References

- [GLSL ES 3.0 Specification](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)
- [WGSL Specification](https://www.w3.org/TR/wgsl/)
- [WebGPU Standard](https://w3c.github.io/webgpu/)
- [Learn WebGPU](https://learngpgpu.com/)
