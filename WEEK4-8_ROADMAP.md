# Week 4-8 Development Roadmap

## Overview

This roadmap outlines the path from basic triangle/quad rendering to full DICOM volume rendering via ray marching.

**Current Status**: Week 5 complete (runtime switching + shader variants)
**Next**: Week 4 (Camera + Input), then Week 7-8 (Ray Marching)

---

## Week 4: Camera + Input

### Goals
- Implement two camera modes: **Orbit** (for DICOM viewer) + **WASD** (for game engine)
- Handle SDL2 keyboard/mouse input in both native and WASM
- Update viewport on canvas resize
- Store camera matrices in uniform buffers

### Implementation

#### 1. Camera Class (`engine/camera.h`) ✅
```cpp
class Camera {
    // Two modes:
    // - ORBIT: Mouse drag rotates around focal point (for examining 3D volumes)
    // - WASD: WASD moves, mouse rotates view (for game engine exploration)

    glm::mat4 get_view_matrix() const;
    glm::mat4 get_projection_matrix() const;
    glm::mat4 get_view_projection() const;

    // Input handlers
    void on_key_pressed(int key_code);
    void on_key_released(int key_code);
    void on_mouse_motion(int dx, int dy);
    void on_mouse_wheel(int wheel_y);
    void update(float delta_time);
};
```

#### 2. Keyboard Input (SDL2)
```cpp
// SDL2 events work identically in native and WASM
SDL_Event event;
while (SDL_PollEvent(&event)) {
    switch (event.type) {
        case SDL_KEYDOWN:
            camera.on_key_pressed(event.key.keysym.sym);
            break;
        case SDL_KEYUP:
            camera.on_key_released(event.key.keysym.sym);
            break;
        case SDL_MOUSEMOTION:
            camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
            break;
        case SDL_MOUSEWHEEL:
            camera.on_mouse_wheel(event.wheel.y);
            break;
    }
}
```

#### 3. Canvas Resize Handler ✅
```javascript
// JavaScript: Listen for canvas resize
window.addEventListener('resize', () => {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
    // Call C++ function to update viewport
    Module._on_canvas_resize(canvas.width, canvas.height);
});
```

```cpp
// C++: Update OpenGL viewport
extern "C" EMSCRIPTEN_KEEPALIVE
void on_canvas_resize(int width, int height) {
    glViewport(0, 0, width, height);
    camera.set_viewport(width, height);  // Update projection matrix
}
```

#### 4. Shaders with Camera Matrices
```glsl
// Vertex shader: Use camera matrices
#version 300 es
uniform mat4 view_projection;
layout(location = 0) in vec3 position;

void main() {
    gl_Position = view_projection * vec4(position, 1.0);
}
```

### Testing
- [ ] Orbit camera: Click+drag to rotate around triangle/quad
- [ ] WASD camera: WASD to move, mouse to look around
- [ ] Mouse wheel: Zoom in/out (orbit), speed change (WASD)
- [ ] Resize: Stretch browser window, viewport updates correctly
- [ ] Both renderers: Works identically in G2 and G3

---

## Week 5: Bonus Features (Already Complete ✅)

- ✅ Runtime renderer switching (G2 ↔ G3 without reload)
- ✅ 4 shader color variants (dynamic compilation)
- ✅ Real-time FPS monitoring
- ✅ Different geometry (triangle vs quad)

---

## Week 6: ECS + Resource Management (Planned)

### Goals
- Introduce Entity-Component-System architecture
- Memory allocators (arena, pool)
- Batch rendering optimization

### Key Additions
```cpp
// Simple ECS for managing entities
struct Entity { glm::vec3 position; glm::mat4 transform; };
struct System { virtual void update(Entity& e) = 0; };

// Allocators for GPU resources
class ArenaAllocator { /* ... */ };
class PoolAllocator { /* ... */ };
```

---

## Week 7-8: Ray Marching in Fragment Shader

### Goal: Volume Rendering via Compute in Fragment Shader

Implement ray marching directly in the fragment shader. This is the foundation for DICOM volume rendering.

### Architecture

```
Application
  ├─ Load 3D texture (DICOM slice stack)
  ├─ Pass texture to fragment shader
  └─ Fragment shader: ray march + render

Fragment Shader (GLSL ES or WGSL)
  ├─ For each pixel:
  │   ├─ Compute ray direction from camera
  │   ├─ Step ray through 3D space
  │   ├─ Sample texture at each step
  │   ├─ Accumulate color (volume rendering)
  │   └─ Output final color
  └─ Result: DICOM volume visualization
```

### Step-by-Step Implementation

#### Step 1: Implicit Sphere (No Texture)

**Goal**: Prove ray marching works with math-based geometry.

```wgsl
// WGSL fragment shader: Ray march implicit sphere
@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    // Normalize coordinates to -1..1
    let uv = pos.xy / resolution;
    
    // Ray origin (camera position)
    let ray_origin = camera_pos;
    
    // Ray direction (towards pixel)
    let ray_dir = normalize(compute_ray_direction(uv));
    
    // Ray march: step along ray, sample implicit function
    var color = vec3f(0.0);
    for (var i = 0; i < 64; i++) {
        let p = ray_origin + ray_dir * (f32(i) * 0.01);
        let dist = distance_to_sphere(p);  // Signed distance to sphere
        
        if (abs(dist) < 0.001) {
            // Hit! Sample normal + lighting
            color = vec3f(0.5, 0.7, 1.0);
            break;
        }
    }
    
    return vec4f(color, 1.0);
}

// Signed distance function for implicit sphere
fn distance_to_sphere(p: vec3f) -> f32 {
    return length(p) - 0.5;  // Sphere of radius 0.5
}
```

**Expected Output**: 
- Blue sphere in the center
- Lighting based on surface normal
- Smooth edges (continuous surface)

#### Step 2: Texture Sampling (Volume Rendering)

**Goal**: Replace implicit function with texture lookup.

```wgsl
@fragment
fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let uv = pos.xy / resolution;
    let ray_origin = camera_pos;
    let ray_dir = normalize(compute_ray_direction(uv));
    
    var accumulated_color = vec4f(0.0);
    
    // Ray march through volume
    for (var i = 0; i < 128; i++) {
        let step_size = 0.01;
        let p = ray_origin + ray_dir * (f32(i) * step_size);
        
        // Sample 3D texture at this position
        let sample = textureSample(volume_texture, volume_sampler, p);
        
        // Front-to-back compositing
        let alpha = sample.a;
        accumulated_color.rgb += (1.0 - accumulated_color.a) * sample.rgb * alpha;
        accumulated_color.a += (1.0 - accumulated_color.a) * alpha;
        
        // Early exit if fully opaque
        if (accumulated_color.a > 0.99) { break; }
    }
    
    return accumulated_color;
}
```

**Expected Output**: 
- Semitransparent volume visualization
- If texture is DICOM data: soft tissue appears, bones appear denser
- Interactive camera allows exploration

#### Step 3: Optimization + Lighting

```wgsl
// Add advanced features:
// - Adaptive step size (larger steps in empty space)
// - Phong lighting from volume gradients
// - Transfer functions (map intensity → color)
// - Shadows from secondary rays
```

### Data: Loading DICOM into 3D Texture

```cpp
// Pseudo-code: Load DICOM slice stack into 3D texture
class VolumeLoader {
public:
    // Load DICOM series (stack of 2D slices)
    // Stack into single 3D texture (width, height, depth)
    // Upload to GPU as texture

    void load_dicom_series(const std::string& path) {
        std::vector<uint8_t> dicom_data = read_dicom_files(path);
        
        // Create 3D texture
        glm::ivec3 dimensions(512, 512, 256);  // Example: 512x512x256 voxels
        glCreateTextures(GL_TEXTURE_3D, 1, &texture_handle_);
        glTextureStorage3D(texture_handle_, 1, GL_R8, 
                          dimensions.x, dimensions.y, dimensions.z);
        glTextureSubImage3D(texture_handle_, 0, 0, 0, 0,
                           dimensions.x, dimensions.y, dimensions.z,
                           GL_RED, GL_UNSIGNED_BYTE, dicom_data.data());
    }
};
```

### UI Controls for Volume Rendering

```
[Window Resize]     → Update camera + viewport
[Mouse Drag]        → Orbit camera (examine volume)
[Mouse Wheel]       → Zoom in/out
[WASD]              → Move through volume (game engine mode)
[Slider 1]          → Density / Opacity transfer function
[Slider 2]          → Brightness adjustment
[Toggle Light]      → Enable/disable Phong lighting
```

### Expected Timeline

| Week | Task | Output |
|------|------|--------|
| 7 | Ray march implicit sphere | Blue sphere |
| 7 | Switch to texture sampling | Volume outline |
| 8 | Add transfer functions | DICOM visualization |
| 8 | Add lighting + optimization | Production-ready renderer |

---

## Implementation Checklist

### Week 4
- [ ] Camera class with Orbit + WASD modes
- [ ] SDL2 keyboard/mouse input handlers
- [ ] Canvas resize handling
- [ ] Uniform buffer for view/projection matrices
- [ ] Update shaders to use camera matrices
- [ ] Test both renderers with camera

### Week 6
- [ ] Basic ECS structure
- [ ] Memory allocators
- [ ] Batch rendering system

### Week 7-8
- [ ] Ray march implicit sphere shader
- [ ] 3D texture loading
- [ ] Volume sampling in fragment shader
- [ ] Transfer functions
- [ ] Lighting in ray march
- [ ] Optimization (adaptive steps, early exit)
- [ ] DICOM data loader

---

## Learning Concepts

### Week 4: GPU-App Communication
- **Concept**: How does the application control GPU behavior?
- **Answer**: Uniform buffers, texture binding, state changes
- **Lesson**: Camera matrices must be updated every frame

### Week 7-8: Ray Marching
- **Concept**: How do you render a 3D volume on a 2D screen?
- **Answer**: For each pixel, cast a ray into the volume and integrate color
- **Formula**: `color = ∫ sample(ray(t)) * alpha(sample) dt`
- **Lesson**: Rendering isn't just about geometry; it's about data visualization

---

## References

- [Ray Marching Tutorial](https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-generating-camera-rays)
- [Volume Rendering](https://en.wikipedia.org/wiki/Volume_rendering)
- [GLSL Fragment Shaders](https://www.khronos.org/opengl/wiki/Fragment_Shader)
- [3D Textures in OpenGL](https://www.khronos.org/opengl/wiki/Texture)

---

## Git Branches

- `feat/wasm-dicom-raytracing-renderer` ← Current (Week 5 complete)
- `week-4-camera-input` (to create)
- `week-6-ecs-resources` (to create)
- `week-7-8-ray-marching` (to create)

---

## Next Steps

1. **Immediate**: Finish Week 5 (current branch)
2. **Short-term**: Create `week-4-camera-input` branch
3. **Implement**: Camera class + SDL2 input handling
4. **Test**: Verify orbit/WASD cameras work in both G2 and G3
5. **Document**: Add code comments explaining camera math
6. **Later**: Jump to Week 7-8 for ray marching (Week 6 can be skipped for MVP)
