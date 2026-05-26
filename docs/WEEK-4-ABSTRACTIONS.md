# Week 4 — Engine Abstractions: Building Like Magnum

## The Challenge

Move from **raw GPU API calls** to a **thin, composable abstraction layer** that enables:
- Using your engine from **multiple languages** (C++, Python, JavaScript, Rust)
- Swapping backends without rewriting application code (OpenGL → WebGPU → Vulkan)
- Clean separation between **engine core** and **application logic**

**Inspiration: Magnum Engine**

Magnum demonstrates how to build a modern graphics engine:
- Thin C++11 wrappers around OpenGL/WebGL
- One class = one GPU object (ShaderProgram, VertexBuffer, etc.)
- C ABI boundary for cross-language bindings
- Browser-deployable via Emscripten
- Reference: https://github.com/mosra/magnum

```
Week 1: ✅ Toolchain
Week 2: ✅ Triangle (OpenGL)
Week 3: ✅ Triangle (WebGPU)
Week 4: ➕ Engine Abstractions (ShaderProgram, VertexBuffer, Texture)
Week 5: ➕ Memory Management (Allocators, Resource Pools)
Week 6: ➕ Compute Shaders (DICOM Raytracing)
```

---

## Why Abstractions?

### Problem: Raw API Calls Scatter Logic

**Current state (Week 3):**
```cpp
// Shader compilation scattered in App::init()
GLuint vert = glCreateShader(GL_VERTEX_SHADER);
glShaderSource(vert, 1, &vertPtr, nullptr);
glCompileShader(vert);

// Buffer creation in App::setupGeometry()
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

// Rendering scattered in App::tick()
glUseProgram(shader);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

**Issues:**
- Logic tied to OpenGL API
- Hard to test
- Hard to swap backends
- No language bindings (C API)
- Can't reuse across projects

### Solution: Thin Abstractions

**With abstractions (Week 4):**
```cpp
// Clear, reusable abstractions
ShaderProgram shader(vertexSource, fragmentSource);
VertexBuffer vertices(data, data.size());
Texture2D texture(width, height, imageData);

// Application logic is separate from GPU details
renderer.setShaderProgram(shader);
renderer.setVertexBuffer(vertices);
renderer.draw();
```

**Benefits:**
- ✅ Decoupled from GPU API
- ✅ Testable
- ✅ Backend-agnostic
- ✅ C API for language bindings
- ✅ Reusable abstractions

---

## Architecture: C ABI Boundary

### Design Pattern (Like Magnum, VTK, ITK)

```
┌─────────────────────────────────────┐
│   Application Layer (C++/Python)    │  ← Can use from any language
├─────────────────────────────────────┤
│  C API Boundary                     │  ← Language neutral
│  (engine_create, engine_render...)  │  ← Stable ABI across versions
├─────────────────────────────────────┤
│  Engine Core (C++ classes)          │  ← Only internal
│  (ShaderProgram, VertexBuffer...)   │  ← Implementation details
├─────────────────────────────────────┤
│  GPU Backend (OpenGL/WebGPU/Vulkan) │  ← Swappable
└─────────────────────────────────────┘
```

### C ABI Benefits

**Why medical imaging libraries (VTK, ITK) use this:**

1. **Language independence**
   ```c
   // C API (stable across versions)
   DicomRenderer* renderer = dicom_renderer_create();
   dicom_renderer_load_volume(renderer, "scan.dcm");
   dicom_renderer_render(renderer);
   dicom_renderer_destroy(renderer);
   ```

2. **Wrappable from Python, JavaScript, Rust**
   ```python
   # Python wrapper (bindings at compile time)
   renderer = DicomRenderer()
   renderer.load_volume("scan.dcm")
   renderer.render()
   
   # JavaScript wrapper (via Emscripten)
   const renderer = Module.DicomRenderer();
   renderer.loadVolume("scan.dcm");
   renderer.render();
   ```

3. **Plugin architecture**
   ```cpp
   // Rust plugins can link against C ABI
   extern "C" {
       fn process_volume(renderer: *mut DicomRenderer);
   }
   ```

4. **Version stability**
   - Even if internals change, C ABI never breaks
   - Old code keeps working

---

## Abstraction Layer Design

### Principle: One Class = One GPU Object

**ShaderProgram Abstraction:**

```cpp
// THIN WRAPPER - directly mirrors GL object
class ShaderProgram {
private:
    GLuint handle_;
    
public:
    ShaderProgram(const std::string& vertSource,
                  const std::string& fragSource);
    
    // Minimal interface
    void use() const;
    void setUniform(const std::string& name, float value);
    void setUniform(const std::string& name, const glm::mat4& value);
    
    ~ShaderProgram();
};

// Implementation
ShaderProgram::ShaderProgram(const std::string& vertSource,
                             const std::string& fragSource) {
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSource.c_str(), nullptr);
    glCompileShader(vert);
    
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSource.c_str(), nullptr);
    glCompileShader(frag);
    
    handle_ = glCreateProgram();
    glAttachShader(handle_, vert);
    glAttachShader(handle_, frag);
    glLinkProgram(handle_);
    
    glDeleteShader(vert);
    glDeleteShader(frag);
}

void ShaderProgram::use() const {
    glUseProgram(handle_);
}

ShaderProgram::~ShaderProgram() {
    glDeleteProgram(handle_);
}
```

**Why this design?**
- ✅ No GL calls scattered in application
- ✅ RAII: Constructor allocates, destructor frees
- ✅ One abstraction class per GL object
- ✅ Easy to test
- ✅ Easy to bind to C API

### Abstraction Suite for Triangle Rendering

| Class | Wraps | Purpose |
|-------|-------|---------|
| `ShaderProgram` | `GLuint` (program) | Compile, link, use shaders |
| `VertexBuffer` | `GLuint` (VBO) | Store vertex data on GPU |
| `VertexArray` | `GLuint` (VAO) | Define vertex attribute layout |
| `Texture2D` | `GLuint` (texture) | 2D image data on GPU |
| `FramebufferObject` | `GLuint` (FBO) | Render to texture |

Each class:
- ✅ Wraps exactly one GPU object
- ✅ Handles creation/deletion (RAII)
- ✅ Provides minimal, focused interface
- ✅ Hides OpenGL details

---

## C API Layer (For Language Bindings)

### C API Boundary

```c
// Include this from Python/JavaScript/Rust
typedef struct DicomRenderer DicomRenderer;

// C ABI: stable across compiler versions
DicomRenderer* dicom_renderer_create(void);
void dicom_renderer_destroy(DicomRenderer* renderer);

void dicom_renderer_set_shader_program(
    DicomRenderer* renderer,
    ShaderProgram* shader);

void dicom_renderer_render(DicomRenderer* renderer);
```

### Why C ABI?

**Binary Compatibility:**
```
C ABI = stable across:
- Compiler versions (gcc 9 → gcc 13)
- Standard library implementations
- Optimization flags
- Platform updates
```

**C++ is NOT binary compatible:**
- Name mangling changes
- Standard library ABI breaks
- Virtual table layout varies
- Template instantiations differ

**Solution: Wrap in C API**
```cpp
// Internal (can change)
namespace engine {
    class ShaderProgram { ... };
}

// C API (stable forever)
extern "C" {
    typedef engine::ShaderProgram ShaderProgram;
    
    ShaderProgram* shader_program_create(
        const char* vert_source,
        const char* frag_source) {
        return new ShaderProgram(vert_source, frag_source);
    }
    
    void shader_program_destroy(ShaderProgram* prog) {
        delete prog;
    }
}
```

Now C bindings work from:
- Python (ctypes/cffi)
- JavaScript (Emscripten/WebAssembly)
- Rust (extern "C")
- Any language with C FFI

---

## Memory Management Strategy

### Phase 1: STD Containers (Until Issues Arise)

**Current approach (Week 4):**
```cpp
class VertexBuffer {
    std::vector<float> vertices_;  // Fine for single triangles
    
public:
    void setData(const std::vector<float>& data) {
        vertices_ = data;  // Reallocates as needed
    }
};
```

**Works for:**
- Small meshes (< 1M vertices)
- Small datasets (< 100MB)
- Development/prototyping

**Breaks when:**
- Loading CT scan: 512×512×300 × 16-bit = 150MB
- Multiple volumes: 300MB+
- Real-time updates: allocations cause stutters

### Phase 2: Allocator Layer (Week 5)

**When Phase 1 breaks:**
```cpp
class VoxelBuffer {
    // Arena allocator: pre-allocate large block
    ArenaAllocator arena_;  // 512MB per volume
    
    // Data lives in arena, no reallocations
    Span<uint16_t> voxels_;
    
public:
    void loadVolume(const std::string& path) {
        // Reuses arena, zero allocations
        voxels_ = arena_.allocate(width * height * depth);
        // ... load data into voxels_
    }
};
```

**Benefits:**
- No reallocations during rendering
- Predictable memory layout
- Tight cache locality (better performance)
- Dealloc-on-destruction pattern

### Phase 3: ECS Memory Management (Week 6+)

**For large, dynamic datasets:**
```cpp
// Entity Component System pattern
struct VolumeComponent {
    ArenaAllocator voxel_arena;      // One arena per volume
    Span<uint16_t> voxels;
    Span<uint8_t> transfer_func;
};

struct RaytracingComponent {
    PoolAllocator<RayTask> task_pool; // Reuse ray tasks
};

// Memory is managed by component lifecycle
engine.destroyEntity(volumeEntity);  // Arena freed automatically
```

---

## Learning Path: Week 4

### Phase 1: Understand Abstraction Philosophy

1. **Read Magnum design:**
   - https://doc.magnum.graphics/
   - Focus on: ShaderProgram, Mesh, Texture classes
   - Notice: Each wraps ONE GL object

2. **Study C ABI:**
   - Why medical imaging libraries (VTK, ITK) use C API
   - Language binding mechanics (ctypes, SWIG)
   - Binary compatibility vs source compatibility

3. **Examine this codebase:**
   - Current state: GL calls in App class
   - Goal: Extract into ShaderProgram, VertexBuffer, etc.

### Phase 2: Design Abstractions

1. Create `src/engine/` directory
2. Implement abstractions:
   - `ShaderProgram` (wraps GLuint program)
   - `VertexBuffer` (wraps GLuint VBO)
   - `VertexArray` (wraps GLuint VAO)
   - `Renderer` (high-level rendering)

3. Keep them **thin**:
   - Each class = one GL object
   - No state bundling
   - RAII pattern only

### Phase 3: Create C API Layer

1. Add `src/engine_c.h` (C interface)
2. Implement C binding functions
3. Write Python test to verify bindings work

### Phase 4: Refactor Application

1. Replace `App::init()` GL calls with abstractions
2. Replace `App::tick()` GL calls with abstractions
3. Verify same triangle renders

---

## File Structure (Week 4)

```
src/
├── engine/
│   ├── shader_program.h
│   ├── shader_program.cpp
│   ├── vertex_buffer.h
│   ├── vertex_buffer.cpp
│   ├── vertex_array.h
│   ├── vertex_array.cpp
│   ├── renderer.h
│   ├── renderer.cpp
│   └── engine_c.h          ← C API
├── main.cpp                 ← Uses abstractions, not raw GL
└── ...

docs/
├── WEEK-4-ABSTRACTIONS.md  (this file)
├── WEEK-4-MEMORY-MANAGEMENT.md
└── ...
```

---

## Key Concepts to Master

### 1. Abstraction Layers

**Bad:** GL calls scattered everywhere
```cpp
glUseProgram(shader);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

**Good:** Encapsulated abstractions
```cpp
renderer.setShaderProgram(shader);
renderer.setVertexBuffer(buffer);
renderer.draw();
```

### 2. RAII Pattern

```cpp
class ShaderProgram {
public:
    ShaderProgram(const std::string& vert, const std::string& frag) {
        handle_ = glCreateProgram();  // Acquire
        // ... compile and link
    }
    ~ShaderProgram() {
        glDeleteProgram(handle_);     // Release
    }
};

// Usage (automatic cleanup)
{
    ShaderProgram shader(vertSrc, fragSrc);
    renderer.use(shader);
}  // shader destroyed automatically
```

### 3. C ABI Boundary

**Internal (C++) - can change:**
```cpp
namespace engine {
    class ShaderProgram { 
        // Private implementation
    };
}
```

**External (C) - must be stable:**
```c
typedef void ShaderProgram;  // Opaque handle

ShaderProgram* shader_program_create(const char* v, const char* f);
void shader_program_destroy(ShaderProgram* s);
void shader_program_use(ShaderProgram* s);
```

### 4. Memory Management Strategy

| Phase | Approach | Use Case |
|-------|----------|----------|
| 1 | `std::vector` | < 100MB, infrequent updates |
| 2 | Arena allocator | > 100MB, predictable lifetime |
| 3 | ECS allocators | Dynamic, multi-dataset scenarios |

---

## Implementation Complete: Week 4 Abstractions

### Phase 1 DONE: Core Abstraction Classes

#### ShaderProgram (src/engine/shader_program.h/cpp)
- ✅ Wraps OpenGL shader compilation and linking
- ✅ RAII pattern: Constructor compiles, destructor deletes
- ✅ Interface: `use()`, `setUniform(name, value)`
- ✅ Supports float, vec2, vec3, vec4, mat4 uniforms
- ✅ Error reporting via optional error message parameter
- ✅ Integrated into App via unique_ptr in main.cpp

#### VertexBuffer (src/engine/vertex_buffer.h/cpp)
- ✅ Wraps OpenGL VBO (vertex buffer object)
- ✅ Uploads vertex data to GPU (STATIC_DRAW)
- ✅ Interface: `bind()`, static `unbind()`
- ✅ Tracks buffer size for debugging
- ✅ Integrated into App via unique_ptr in main.cpp

#### VertexArray (src/engine/vertex_array.h/cpp)
- ✅ Wraps OpenGL VAO (vertex array object)
- ✅ Configures vertex attribute layout
- ✅ Interface: `bind()`, `setVertexAttribute(index, count, type, offset)`
- ✅ Supports multiple attributes per vertex (future multi-attribute meshes)
- ✅ Integrated into App via unique_ptr in main.cpp

#### Renderer (src/engine/renderer.h/cpp)
- ✅ High-level rendering interface
- ✅ Encapsulates: clear, draw, shader setup
- ✅ Interface: `setClearColor()`, `clear()`, `draw(vao, count)`, `setShaderProgram()`
- ✅ Ready for future features: layers, post-processing, multiple passes
- ✅ Educational: Shows how modern renderers are structured

### Phase 2 DONE: C API Boundary

#### engine_c.h
- ✅ C API surface (no C++ exposed)
- ✅ Opaque handles for all types
- ✅ Complete API coverage: shader, buffer, array, renderer
- ✅ Standard GL constants defined (GL_FLOAT, etc)
- ✅ Ready for Python ctypes, Rust FFI, WebAssembly

#### engine_c.cpp
- ✅ C++ ↔ C bridging
- ✅ Memory management via new/delete
- ✅ Handle casting and validation
- ✅ String conversion (C null-terminated → std::string)
- ✅ Error propagation (nullptr on failure)

### Phase 3 IN PROGRESS: Application Refactoring

#### main.cpp Updates
- ✅ Replaced raw `GLuint shader` with `unique_ptr<ShaderProgram>`
- ✅ Replaced raw `GLuint vao` with `unique_ptr<VertexArray>`
- ✅ Replaced raw `GLuint vbo` with `unique_ptr<VertexBuffer>`
- ✅ Refactored `App::compileShaders()` to use abstraction
- ✅ Refactored `App::setupGeometry()` to use abstractions
- ✅ Simplified `App::tick()`: raw GL calls → `shader->use()`, `vao->bind()`
- ✅ Simplified `App::cleanup()`: automatic via unique_ptr destructors
- ✅ All GL state management now encapsulated

### File Structure (Implemented)
```
src/
├── engine/
│   ├── shader_program.h/cpp       ← Compiles, links, uses shaders
│   ├── vertex_buffer.h/cpp        ← Uploads vertex data to GPU
│   ├── vertex_array.h/cpp         ← Defines vertex attribute layout
│   ├── renderer.h/cpp              ← High-level rendering pipeline
│   └── engine_c.h/cpp              ← C API for language bindings
├── main.cpp                        ← Uses abstractions, not raw GL
└── ...
```

### Build Integration (Implemented)
- ✅ Added GLM (math library) via CMake CPM
- ✅ Engine sources added to CMakeLists.txt
- ✅ Compilation successful (native build passes)
- ✅ No GPU errors, proper RAII cleanup

### Next Steps

1. **This week:** Test compilation with all abstractions ✅
2. **Next week:** Write Python bindings test to verify C API works
3. **Week 6:** Create Renderer-based application refactoring (optional - App can stay as is)
4. **Week 6+:** Implement Phase 2 memory allocators (arena, pool)

---

## Resources

**Magnum Engine:**
- https://github.com/mosra/magnum - Study the source
- https://doc.magnum.graphics/ - Design philosophy

**C ABI & Bindings:**
- https://en.cppreference.com/w/c/language/extern - C ABI
- https://docs.python.org/3/library/ctypes.html - Python bindings
- https://www.swig.org/ - Language binding tool

**Memory Management:**
- https://www.gamedev.net/tutorials/programming/general/understanding-memory-allocation/ - Memory pools
- https://github.com/SanderMertens/flecs - ECS reference
- https://www.valgrind.org/ - Memory profiling

**Medical Imaging:**
- https://itk.org/ - ITK architecture (C++ with C API)
- https://www.vtk.org/ - VTK architecture (similar pattern)
