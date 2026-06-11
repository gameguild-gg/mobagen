# DICOM Raytracing Renderer — WebGPU Edition

## 📚 Documentation Structure

Start here, then proceed to the detailed weekly guides:

- **[WEEK-1-TOOLCHAIN.md](docs/WEEK-1-TOOLCHAIN.md)** ← Start here!
  - Dual-target architecture (WASM + native)
  - CMake build system explained
  - How to build and run WASM
  - SDL2 + OpenGL context setup
  
- **[WEEK-2-TRIANGLE.md](docs/WEEK-2-TRIANGLE.md)** ← After Week 1
  - Complete graphics pipeline (vertex → fragment → screen)
  - GLSL ES 3.00 shaders explained
  - Vertex buffers, VAO, draw calls
  - Debugging graphics issues

## Overview

This is a **dual-target graphics renderer** that runs both in the browser (via WebAssembly) and as a native desktop app. It's being built toward real-time DICOM volume raytracing, starting with foundational graphics work.

**Current Status:**
- ✅ **G1 (Toolchain)**: Build system works on both native + WASM. Outputs a colored WebGL2 canvas.
- ✅ **G2 (Triangle)**: Draws a triangle using GLSL ES 3.00 shaders, proving the graphics pipeline works.
- 🚧 **G3 (WebGPU)**: Next — migrate away from WebGL to modern WebGPU API.

---

## Why This Architecture?

### 1. **Dual Build (Native + WASM)**

| Target | Why | Tools |
|--------|-----|-------|
| **WASM** (Primary) | Deploy anywhere, no installation, browser-native | Emscripten, WebGL2/WebGPU |
| **Native** (Dev) | Faster iteration, easier debugging, desktop GPU | SDL2, OpenGL 3.3+/Vulkan |

The **same C++ code** compiles to both targets. Conditional compilation (`#ifdef __EMSCRIPTEN__`) handles platform differences.

### 2. **Graphics API Strategy**

We start with **GLES3 (OpenGL ES 3.00)** because:
- **Lowest common denominator**: Works on both WebGL2 (WASM) and OpenGL 3.3+ (native)
- **Syntax compatibility**: Write shaders once, run on both platforms
- **Familiar**: Standard shading language, proven ecosystem

**Upgrade path**: WebGL2 → **WebGPU** (modern, faster, better for compute), native OpenGL → **Vulkan** (modern, lower CPU overhead)

### 3. **CMake + CPM (C++ Package Manager)**

- **CMake**: Single build system for both native and WASM
- **CPM**: Downloads libraries at build time (SDL2, GLEW) — no manual dependencies
- **Emscripten**: Pre-integrated into CMake via special toolchain file

---

## File Structure

```
mobagen/
├── CMakeLists.txt           ← Main build configuration
├── src/
│   └── main.cpp             ← Single source file (G1 + G2 implementation)
├── html/
│   └── shell.html           ← Emscripten HTML template for WASM output
├── external/
│   ├── cpm.cmake            ← Bootstrap CPM (untouched)
│   ├── compilerchecks.cmake ← C++ standard detection (untouched)
│   ├── external.cmake       ← Loads SDL2 + GLEW (native only)
│   ├── sdl.cmake            ← SDL2 download/config (CPM-based)
│   ├── glew.cmake           ← GLEW download/config (CPM-based)
│   └── [other libs].cmake   ← Available for future use
└── build-wasm/              ← WASM build output
    ├── dicom_renderer.html
    ├── dicom_renderer.js
    └── dicom_renderer.wasm
```

---

## The Code: What's Happening

### `CMakeLists.txt` — The Build Recipe

```cmake
set(CXX_STANDARD_TARGET "23" CACHE STRING "CXX standard" FORCE)
```
**Why C++23?**  
Modern features: concepts, ranges, faster compilation. Emscripten supports it.

```cmake
if(EMSCRIPTEN)
    # Emscripten-specific flags
    target_link_options(dicom_renderer PRIVATE
        -sUSE_SDL=2                 # Link Emscripten's SDL2 port
        -sMIN_WEBGL_VERSION=2       # Demand WebGL2 (GLES3)
        -sMAX_WEBGL_VERSION=2       # No WebGL1 fallback
        -sFULL_ES3=1                # Enable GLES3 features
        -sALLOW_MEMORY_GROWTH=1     # Heap can grow
    )
else()
    # Native: load real SDL2 + GLEW from CPM
    include(external/external.cmake)
```

**Key insight:** Emscripten provides its own SDL2 port that bridges to WebGL. We tell it "give me WebGL2" and it does. Native builds fetch real SDL2 from GitHub via CPM.

---

### `src/main.cpp` — The Application

#### **Architecture: `App` Struct**

```cpp
struct App {
    SDL_Window* window;
    SDL_GLContext context;
    GLuint vao, vbo, shader;
    
    bool init();        // Setup SDL, GL context, load shaders
    void tick();        // Render frame
    void cleanup();     // Teardown
};
```

Three stages:

| Stage | Does What | Key Code |
|-------|-----------|----------|
| **init()** | Create window, GL context, upload triangle geometry | `SDL_CreateWindow()`, `glGenVertexArrays()`, shader compile |
| **tick()** | Clear screen, draw triangle | `glClear()`, `glDrawArrays(GL_TRIANGLES, 0, 3)` |
| **cleanup()** | Free GL resources, close window | `glDeleteProgram()`, `SDL_DestroyWindow()` |

#### **Platform-Specific Main Loop**

```cpp
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(em_tick, 0, 1);  // Browser: callback-based
#else
    while (app.running) app.tick();            // Desktop: blocking loop
#endif
```

**Why the difference?**
- **WASM**: Browser is event-driven. We can't block the main thread (it'd freeze the tab). Register a callback with Emscripten; it calls `em_tick()` every frame.
- **Native**: We own the thread. Run a `while` loop.

---

### Shaders: GLSL ES 3.00

#### **Version Branching**

```cpp
#ifdef __EMSCRIPTEN__
  static constexpr const char* VERT_GLSL = "#version 300 es\n";
  static constexpr const char* FRAG_GLSL = "#version 300 es\nprecision mediump float;\n";
#else
  static constexpr const char* VERT_GLSL = "#version 330 core\n";
  static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#endif
```

| Platform | Version | Why |
|----------|---------|-----|
| **WebGL2** | `#version 300 es` | ES = Embedded Systems (mobile). WebGL2 = GLES3 |
| **OpenGL Native** | `#version 330 core` | Desktop. `core` = no deprecated features |

**Key difference:** GLES requires `precision mediump float;` in fragment shaders (mobile GPUs are weaker). Desktop OpenGL doesn't need it.

#### **Vertex Shader**

```glsl
#version 300 es
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

- **`layout(location = 0)`**: First vertex attribute = vertex position
- **Input**: 2D screen coordinates (NDC: -1 to +1)
- **Output**: Transformed to homogeneous coords `(x, y, 0, 1)`

#### **Fragment Shader**

```glsl
#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    fragColor = vec4(0.0, 1.0, 0.5, 1.0);  // Teal
}
```

- **Output**: RGBA color for each pixel
- **Flat color**: No lighting, no texture — just teal for every pixel

---

### Geometry: The Triangle

```cpp
float vertices[] = {
    0.0f,  0.5f,    // Top
   -0.5f, -0.5f,    // Bottom-left
    0.5f, -0.5f     // Bottom-right
};
```

**Normalized Device Coordinates (NDC):**
- Range: `-1` to `+1` on both X and Y
- `(0, 0)` = center of screen
- Triangle fills ~25% of screen

```
      (0, 1)
        |
 (-1,0) +------+ (1,0)
        |
      (0,-1)

  Top vertex at (0, 0.5) = halfway to top
```

---

## Building & Running

### **WASM Build** (Current State)

```bash
cd e:/repositories/game-guild/mobagen

# Direct compilation with emcc (no CMake needed for WASM)
~/emsdk/python/3.13.3_64bit/python.exe ~/emsdk/upstream/emscripten/emcc.py \
  src/main.cpp -o build-wasm/dicom_renderer.html \
  -O3 -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
  -std=c++23

# Serve
cd build-wasm
python -m http.server 8080

# Open browser: http://localhost:8080/dicom_renderer.html
```

**Output:**
- `dicom_renderer.html` — The webpage
- `dicom_renderer.js` — Emscripten runtime + your code compiled to JavaScript
- `dicom_renderer.wasm` — Your C++ as binary WASM instructions

### **Native Build** (Planned)

```bash
cmake -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
./build-native/bin/dicom_renderer
```

**Status:** CMake setup is ready; blocked on getting `make` or `ninja` installed on Windows.

---

## What You're Seeing

| What | Why It Looks That Way |
|-----|----------------------|
| Black screen | The HTML body background |
| 800×600 canvas (centered) | SDL/Emscripten defaults; centered by CSS flexbox |
| Cornflower blue background | `glClearColor(0.1, 0.2, 0.5, 1.0)` |
| Teal triangle | Fragment shader outputs `vec4(0.0, 1.0, 0.5, 1.0)` |

---

## Key Concepts to Internalize

### **1. Immediate vs Deferred Rendering**

What we're doing: **Immediate mode** (OpenGL/WebGL style)
```cpp
glDrawArrays(GL_TRIANGLES, 0, 3);  // "Draw now"
```

What's coming (WebGPU): **Deferred** (record commands, submit batch)
```cpp
auto cmd = encoder.beginRenderPass(...);
cmd.draw(3, ...);  // "Record this command"
encoder.finish();  // "Submit the batch"
```

**Why the shift?**  
- Immediate: Simple, fine for learning, but CPU overhead
- Deferred: Modern GPUs prefer batched work; lower CPU overhead, better for large compute

### **2. Shader Compilation Pipeline**

```
Vertex Shader Source (string)
  ↓ glCompileShader()
Compiled Vertex Code (GPU-specific)
  ↓ glAttachShader(program, vert)
Program Object
  ↓ glLinkProgram()
Linked Executable (ready to run)
```

Each frame, `glUseProgram(shader)` activates the linked program.

### **3. Vertex Attributes & VAO**

```
Vertex Data (array in RAM)
  ↓ glBindBuffer() + glBufferData()  [Upload to GPU]
VBO (GPU-side vertex storage)
  ↓ glVertexAttribPointer()          [Tell GPU where fields are]
VAO (Vertex Array Object)
  ↓ glBindVertexArray() + glDrawArrays()
[GPU executes VS once per vertex]
```

**Why this complexity?** GPU memory is separate from CPU memory. We have to explicitly upload and describe the layout.

### **4. Emscripten's Role**

Emscripten translates:
- **C++ → JavaScript** (for the runtime logic)
- **OpenGL calls → WebGL calls** (glDrawArrays → drawArrays)
- **Native memory model → JavaScript arrays** (using typed arrays)

It's a C++ → WebAssembly compiler + OpenGL↔WebGL bridge.

---

## Next: WebGPU Integration (G3)

### **Why WebGPU?**

| Feature | WebGL2 | WebGPU |
|---------|--------|--------|
| **Command Recording** | Immediate (calls execute now) | Deferred (batch, submit later) |
| **Compute Shaders** | No | Yes ✓ |
| **Bindgroups** | Global state (slow to change) | Organized (fast to iterate) |
| **Error Messages** | Vague | Detailed, source-mapped |
| **Performance** | Good | Better (less CPU overhead) |

For **DICOM raytracing**, we need **compute shaders** to:
1. Load 3D volume data
2. Trace rays in parallel (GPU compute)
3. Output colors to a texture
4. Render that texture to screen

WebGL2 can't do step 2 efficiently. WebGPU can.

### **Migration Plan (G3)**

1. **Parallel implementation**: Keep WebGL2 path, add WebGPU path
   ```cpp
   #ifdef USE_WEBGPU
     // WebGPU code
   #else
     // WebGL2 code (current)
   #endif
   ```

2. **Start simple**: Render same triangle, but via WebGPU
   - Learn the pipeline: device → queue → renderPass → draw
   - Verify it works in Chrome (Chromium Edge, Safari, Firefox all have beta support)

3. **Add compute**: Implement a simple compute shader
   - E.g., fill a texture with a radial gradient
   - Render that texture to screen
   - Proof of concept for volume raytracing compute kernel

4. **3D volume data**: Load DICOM dataset, raycast it
   - Read `.dcm` files (or simpler: synthesize test volume)
   - Bind as 3D texture
   - Compute shader traces rays, outputs colors
   - Render to screen

### **Emscripten + WebGPU**

Good news: Emscripten has **wgpu bindings** (Rust's GPU lib) and direct WebGPU support.

```cpp
#include <emscripten/webgpu.h>

WGPUDevice device = emscripten_webgpu_get_device();
WGPUQueue queue = wgpuDeviceGetQueue(device);
// ... WebGPU code
```

Alternatively, use **Emscripten's JavaScript interop** to call WebGPU from JS and C++.

---

## How to Study This

### **Phase 1: Understand the Graphics**
1. Run the current code in a browser. Watch it render.
2. Modify `glClearColor(0.1, 0.2, 0.5, 1.0)` to `(1, 0, 0, 1)` (red). Rebuild. See the change.
3. Modify the triangle color in the fragment shader. See it change.
4. Change triangle vertices. See it move/scale.

**Goal:** Internalize that shaders + geometry = rendered image.

### **Phase 2: Understand the Platform Split**
1. Look at `#ifdef __EMSCRIPTEN__` blocks in `src/main.cpp`.
2. Understand: Why do we need `emscripten_set_main_loop`?
3. Why can't we use `while(running)` in WASM?

### **Phase 3: Understand GLSL**
1. Read the vertex shader line by line. Understand what `layout(location = 0)` means.
2. Understand NDC (Normalized Device Coordinates). Why does `(0.5, 0.5)` appear at top-right?
3. Modify `precision mediump float` to `precision highp float`. Does it change rendering?

### **Phase 4: WebGPU (Next Phase)**
1. Read WebGPU spec intro (20 min): https://www.w3.org/TR/webgpu/
2. Understand: What's a `WGPURenderPass`? What's a `bindgroup`?
3. Port the triangle to WebGPU in a separate `.cpp` file. Compare the code.

---

## Quick Reference: Code Locations

| What | Where |
|-----|-------|
| Application logic | `src/main.cpp:App struct` |
| Shader sources | `src/main.cpp:VERT_GLSL`, `FRAG_GLSL` |
| Build config | `CMakeLists.txt` |
| Platform flags | `CMakeLists.txt:if(EMSCRIPTEN)` |
| HTML template | `html/shell.html` |
| WASM output | `build-wasm/` |

---

## Common Questions

**Q: Why `#version 300 es` and not `#version 100`?**  
A: GLES3 is newer, supports modern features (integer types, `in`/`out`). GLES2 is older. WebGL2 = GLES3.

**Q: Why separate `VERT_GLSL` and `FRAG_GLSL` headers?**  
A: Fragment shaders need `precision` declaration; vertex shaders don't (on GLES). Easier to manage separately.

**Q: Can I debug the WASM binary?**  
A: Yes! Chrome DevTools has WASM debugging. Emscripten generates source maps. Breakpoints work.

**Q: What happens if I call a GL function before `glewInit()`?**  
A: Crash or undefined behavior. GLEW loads function pointers; without it, they're null.

**Q: Why is the native build blocked on `make`?**  
A: CMake needs a generator to write makefiles/VS projects. We have CMake but no make. Next step: install Ninja or Visual Studio.

---

## Next Steps

1. ✅ Understand current code by reading this doc + browsing `src/main.cpp`
2. 🔨 Experiment: Modify colors/geometry, rebuild, observe changes
3. 📚 Learn WebGPU: Read spec, look at examples
4. 🚀 G3 Implementation: Parallel WebGPU triangle, then compute shader

---

## Resources

- **Emscripten**: https://emscripten.org/docs/
- **WebGL2**: https://www.khronos.org/webgl/wiki/Getting_Started_with_WebGL
- **GLSL ES 3.00**: https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
- **WebGPU**: https://www.w3.org/TR/webgpu/
- **NDC**: https://learnopengl.com/Getting-started/Coordinate-Systems

**Happy learning!** 🚀
