# Building WASM Versions (WebGL & WebGPU)

This guide explains how to build and test the WASM versions of the DICOM renderer.

**→ Start here**: [QUICKSTART.md](QUICKSTART.md) for the fastest way to get running.

This document covers the technical build details, architecture decisions, and implementation lessons.

## Prerequisites

### 1. Install Emscripten SDK

**Windows (PowerShell):**
```powershell
# Clone Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install latest version
.\emsdk.bat install latest
.\emsdk.bat activate latest

# Add to PATH (Windows will show the command, or manually add emsdk folder)
```

**Linux/macOS (Bash):**
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### 2. Verify Installation
```bash
emcc --version
emcmake cmake --version
```

## Build Targets

### Build Organization

Builds are now organized in `build/` with clear subdirectories:
- **`build/native/bin`** — Native build (Windows/Linux desktop app)
- **`build/wasm-webgl/bin`** — WASM WebGL build (G2: Immediate-mode, ES3.0)
- **`build/wasm-webgpu/bin`** — WASM WebGPU build (G3: Deferred-mode, modern)

### Option 1: Unified Build (Both G2+G3, Runtime Switchable)

For easy A/B comparison, build both renderers in one binary:

**Build WebGL (G2):**
```bash
cd e:/repositories/game-guild/mobagen
rm -r build/wasm-webgl -Force
emcmake cmake -B build/wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgl
```

**Build WebGPU (G3):**
```bash
cd e:/repositories/game-guild/mobagen
rm -r build/wasm-webgpu -Force
emcmake cmake -B build/wasm-webgpu -DUSE_WEBGPU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgpu
```

**Output:** 
- `build/wasm-webgl/bin/dicom_renderer.html` (~500KB WASM + 400KB JS)
- `build/wasm-webgpu/bin/dicom_renderer.html` (~500KB WASM + 400KB JS)

**Why Separate Builds?**
- **Clear Isolation**: G2 and G3 code paths don't interfere
- **Easier Debugging**: Single renderer per build
- **Performance Comparison**: Test each separately
- **Educational**: See pure WebGL vs. pure WebGPU implementation

### Legacy: Separate Builds (G2 and G3 individually)

For isolated testing or studying a single renderer:

**G2 Only (WebGL):**
```bash
emcmake cmake -B build-wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-webgl
# Output: build-wasm-webgl/bin/dicom_renderer.html
```

**G3 Only (WebGPU):**
```bash
emcmake cmake -B build-wasm-webgpu -DCMAKE_BUILD_TYPE=Release -DUSE_WEBGPU=ON
cmake --build build-wasm-webgpu
# Output: build-wasm-webgpu/bin/dicom_renderer.html
```

## Running Locally

WebAssembly **cannot** run from `file://` URLs. Use a local web server:

### Option A: Python (built-in)
```bash
cd build-wasm-unified/bin
python -m http.server 8080
# Open browser: http://localhost:8080/dicom_renderer.html
```

### Option B: Emscripten's emrun
```bash
emrun --port 8080 build-wasm-unified/bin/dicom_renderer.html
```

### Option C: Node.js http-server
```bash
npm install -g http-server
cd build-wasm-unified/bin
http-server -p 8080
```

## Interactive Controls

The unified WASM renderer includes:

### Renderer Switcher
- **Keyboard**: Press `1` for WebGL (G2), `2` for WebGPU (G3)
- **UI Buttons**: Click "WebGL" or "WebGPU" buttons (top-left)
- **Visual Feedback**: Active renderer button is highlighted

**Use Case**: Compare immediate-mode vs. deferred-mode rendering on the same geometry. Performance metrics update dynamically.

### Shader Variants (4 colors)
- **Keyboard**: Press `3` (Red), `4` (Green), `5` (Yellow), or `6` (Teal)
- **UI Buttons**: Click color buttons (top-left, under renderer selector)
- **Effect**: Fragment shader recompiles with new RGBA values injected dynamically

**Use Case**: Verify both renderers produce identical colors; observe any visual differences.

### Performance Stats (top-right)
- **Current Renderer**: Shows active renderer name (WebGL or WebGPU)
- **FPS**: Frames per second, updated every 500ms
- **Frame Time**: Milliseconds per frame
- **WebGPU Support**: Browser capability indicator

**Color Coding**:
- 🟢 Green (55+ FPS): Smooth
- 🟡 Yellow (30-54 FPS): Playable
- 🔴 Red (<30 FPS): Slow

## What to Expect

### G2: WebGL Version (Immediate-Mode Rendering)
- **Visual Output**: Blue background (RGB 0.1, 0.2, 0.5) + teal triangle (RGB 0.0, 1.0, 0.5)
- **Architecture**: OpenGL ES 3.0 compiled to WASM via Emscripten
- **GPU Pattern**: Immediate-mode (each `glDraw*()` executes immediately)
- **CPU Overhead**: Higher - GPU context switches on every call
- **Browser Support**: All modern browsers with WebGL 2.0 support

**Educational Concept**: Demonstrates traditional immediate-mode GPU rendering where C++ calls execute on GPU right away, blocking until GPU processes commands.

### G3: WebGPU Version (Deferred-Mode Rendering)
- **Visual Output**: Identical to G2 (blue background + teal triangle)
- **Architecture**: C++ calls JavaScript WebGPU API via `emscripten_run_script()`
- **GPU Pattern**: Deferred-mode (record commands, submit batch)
- **CPU Overhead**: Lower - GPU driver optimizes entire batch at once
- **Browser Support**: Chrome 113+, Edge 113+, Firefox (with `dom.webgpu.enabled` flag)

**Educational Concept**: Demonstrates modern deferred-mode GPU rendering where commands are recorded in a buffer and submitted as a batch for GPU optimization. Foundation for compute shaders (future DICOM raytracing).

**Implementation Details**:
- WGSL shaders (WebGPU's type-safe shader language) instead of GLSL
- JavaScript bridge handles async GPU resource initialization (adapter, device, queue)
- Emscripten `emscripten_run_script()` calls `window.webgpu_render()` each frame
- WebGPU JavaScript code inlined in HTML shell to avoid asset loading issues

## Browser Requirements

| Browser | G2 (WebGL) | G3 (WebGPU) |
|---------|-----------|-----------|
| Chrome | ✅ All versions | ✅ 113+ |
| Firefox | ✅ All versions | ⏳ Behind flag |
| Edge | ✅ All versions | ✅ 113+ |
| Safari | ✅ 14+ | ❌ Not yet |

### Enable WebGPU in Firefox

1. Navigate to `about:config`
2. Search for `dom.webgpu.enabled`
3. Set to `true`

## Project Structure

```
html/
  ├── shell.html          # Emscripten HTML template (loads WASM + JS)
  └── webgpu.js           # WebGPU renderer (G3 path)

src/
  ├── main.cpp            # Dual-path: WebGL (G2) and WebGPU (G3)
  ├── engine/
  │   ├── shader_program.h/cpp
  │   ├── vertex_buffer.h/cpp
  │   ├── vertex_array.h/cpp
  │   ├── renderer.h/cpp
  │   └── engine_c.h/cpp  (C API boundary)

CMakeLists.txt             # Dual-target configuration
```

## Architecture: Dual-Path Implementation (G2 vs G3)

### Why Two Rendering Paths?

**Educational Goal**: Understand the evolution from immediate-mode to deferred-mode GPU rendering.

| Aspect | G2 (WebGL) | G3 (WebGPU) |
|--------|-----------|-----------|
| **GPU Command Pattern** | Immediate-mode (execute now) | Deferred-mode (record, submit batch) |
| **CPU-GPU Sync** | High (context switch per draw) | Low (batch optimization) |
| **Shader Language** | GLSL ES 3.00 | WGSL (WebGPU Shading Language) |
| **API Bridge** | Native C++ OpenGL calls | C++ → JavaScript → WebGPU API |
| **Code Complexity** | Lower (direct GL calls) | Higher (async GPU init, JS bridge) |
| **Performance** | Suitable for simple scenes | Optimized for complex workloads |
| **Next Step** | Rasterization rendering | Compute shader support |

### Why This Matters

- **WebGL (G2)** teaches GPU basics: state machines, immediate execution, blocking behavior
- **WebGPU (G3)** teaches modern GPU design: command buffers, batching, lower CPU overhead
- **DICOM Raytracing Goal**: Compute shaders (only possible with deferred API like WebGPU)

## Key Differences: Native vs WASM

| Aspect | Native | WASM |
|--------|--------|------|
| **Entry Point** | `main()` | `em_tick()` callback via `emcripten_set_main_loop` |
| **Window System** | SDL2 native | Emscripten SDL2 port + HTML canvas |
| **GL Headers** | GL/glew.h (desktop) | GLES3/gl3.h (mobile/web) |
| **Event Loop** | While loop | Browser's requestAnimationFrame |
| **File Access** | Native filesystem | Virtual filesystem (Emscripten) |
| **JavaScript Bridge** | None | `emscripten_run_script()` for WebGPU |

## Troubleshooting

### CMake not finding Emscripten

Ensure Emscripten environment is active:

```bash
# Linux/macOS
source ~/emsdk/emsdk_env.sh

# Windows (PowerShell)
& "$env:LOCALAPPDATA\emsdk\emsdk_env.ps1"
```

### WebGPU not available in browser

- Chrome: Use Chrome Canary or latest stable (v113+)
- Firefox: Enable `dom.webgpu.enabled` in about:config
- Edge: Use version 113+

### WASM file too large

The `.wasm` file includes SDL2, GLEW, and GLM. This is expected (~5-10 MB). Optimize with:

```bash
emcmake cmake -B build-wasm-webgl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-Os"  # Additional optimization
```

## Implementation Decisions & Lessons Learned

### Challenge 1: WebGPU JavaScript Bridge
**Problem**: WebGPU is not available as C++ headers; it's a JavaScript-first API.

**Solution**: Use Emscripten's `emscripten_run_script()` to call JavaScript from C++:
```cpp
emscripten_run_script("if(window.webgpu_render) window.webgpu_render();");
```

**Why**: WebGPU is designed for JavaScript. Waiting for native C++ bindings would delay progress.

**Learning**: Cross-language FFI (Foreign Function Interface) is powerful for rapid prototyping.

### Challenge 2: Async GPU Initialization
**Problem**: GPU resources (adapter, device) are async in WebGPU, but C++ tick loop is synchronous.

**Solution**: JavaScript handles async/await; C++ just calls `window.webgpu_render()` each frame.

**Pattern**:
```javascript
// JavaScript: async GPU init
async function initWebGPU() {
    const device = await adapter.requestDevice();
    // ... setup complete
}

// C++: sync rendering call
emscripten_run_script("if(window.webgpu_render) window.webgpu_render();");
```

**Learning**: Async APIs require delegation to the event loop language (JavaScript in browsers).

### Challenge 3: Asset Loading in WASM
**Problem**: HTML shell tries to load external `webgpu.js` file; server can't resolve relative paths.

**Solution**: Inline WebGPU JavaScript directly in HTML shell template.

**Before**:
```html
<script src="webgpu.js"></script>  <!-- Fails: 404 in WASM context -->
```

**After**:
```html
<script>
// Full WebGPU implementation inlined here
window.webgpu_render = function() { ... }
</script>
```

**Learning**: WASM assets benefit from self-contained HTML; external includes have path resolution issues.

### Challenge 4: Runtime Renderer Switching
**Problem**: User wanted to compare G2 (WebGL) and G3 (WebGPU) on the same hardware without rebuilding.

**Initial Solution**: Separate builds with preprocessor selection:
```cpp
#if defined(USE_WEBGPU) && defined(__EMSCRIPTEN__)
  // G3: WebGPU path
#else
  // G2: OpenGL path
#endif
```

**Final Solution**: Unified WASM with runtime switching:
```cpp
// Global state
enum class RendererType { RENDERER_WEBGL = 0, RENDERER_WEBGPU = 1 };
static RendererType g_active_renderer = RendererType::RENDERER_WEBGL;
static AppWebGL* g_app_webgl = nullptr;
static AppWebGPU* g_app_webgpu = nullptr;

// Exported to JavaScript
extern "C" EMSCRIPTEN_KEEPALIVE
void set_renderer(const char* renderer_name) {
    if (strcmp(renderer_name, "webgl") == 0) {
        g_active_renderer = RendererType::RENDERER_WEBGL;
    } else if (strcmp(renderer_name, "webgpu") == 0) {
        g_active_renderer = RendererType::RENDERER_WEBGPU;
    }
}

// Unified tick routes to active renderer
void em_unified_tick() {
    if (g_active_renderer == RendererType::RENDERER_WEBGL) {
        g_app_webgl->tick();
    } else if (g_active_renderer == RendererType::RENDERER_WEBGPU) {
        g_app_webgpu->tick();
    }
}
```

**JavaScript Integration**:
```javascript
function switchRenderer(name) {
    Module._set_renderer(name);
    // Update UI to show active renderer
}
```

**Architecture Benefits**:
- ✅ Both renderers compiled into single WASM binary
- ✅ Runtime switching via exported C functions callable from JS
- ✅ No page reload required
- ✅ Enables A/B testing and direct performance comparison
- ✅ Foundation for user-selectable rendering backends

**Learning**: C/WASM and JavaScript interop enables flexible UI-driven selection without rebuild complexity.

### Challenge 5: Dynamic Shader Compilation with Variants
**Problem**: User wanted to see color differences between renderers without rebuilding; needed 4 variants for visual comparison.

**Solution**: Generate fragment shader source dynamically with color values injected at runtime:

```cpp
enum class ShaderVariant {
    VARIANT_TEAL = 1,    // (0.0, 1.0, 0.5, 1.0)
    VARIANT_RED = 2,     // (1.0, 0.0, 0.0, 1.0)
    VARIANT_GREEN = 3,   // (0.0, 1.0, 0.0, 1.0)
    VARIANT_YELLOW = 4   // (1.0, 1.0, 0.0, 1.0)
};

static Color get_shader_color(ShaderVariant variant) {
    switch (variant) {
        case ShaderVariant::VARIANT_TEAL:   return {0.0f, 1.0f, 0.5f, 1.0f};
        case ShaderVariant::VARIANT_RED:    return {1.0f, 0.0f, 0.0f, 1.0f};
        // ...
    }
}

bool AppWebGL::compileShaders() {
    Color c = get_shader_color(g_active_shader);
    
    // Build fragment shader with dynamic color
    char fragColorStr[128];
    snprintf(fragColorStr, sizeof(fragColorStr),
             "out vec4 fragColor;\n\nvoid main() {\n    fragColor = vec4(%.1f, %.1f, %.1f, %.1f);\n}\n",
             c.r, c.g, c.b, c.a);
    
    std::string fragSrc = std::string(FRAG_GLSL) + fragColorStr;
    shader = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);
}
```

**Exported to JavaScript**:
```cpp
extern "C" EMSCRIPTEN_KEEPALIVE
void set_shader_variant(int variant_num) {
    if (variant_num >= 1 && variant_num <= 4) {
        g_active_shader = static_cast<ShaderVariant>(variant_num);
        g_shader_recompile = true;  // Signal tick() to recompile
    }
}
```

**Tick-Time Recompilation**:
```cpp
void AppWebGL::tick() {
    if (g_shader_recompile) {
        g_shader_recompile = false;
        compileShaders();  // Recompile with new color
    }
    // ... render frame
}
```

**Architecture Benefits**:
- ✅ No rebuild required to test new colors
- ✅ Immediate visual feedback (recompiles on tick)
- ✅ Demonstrates GPU shader compilation flexibility
- ✅ Enables per-renderer color testing

**Learning**: String formatting + shader recompilation allows runtime variation without code changes; useful for parameter sweeps and A/B testing.

### Challenge 6: Real-Time Performance Monitoring
**Problem**: User needed to know which renderer was active and what FPS each achieved for comparative analysis.

**Solution**: JavaScript PerformanceStats class tracking frame timing:

```javascript
class PerformanceStats {
    update() {
        const now = performance.now();
        const delta = now - this.lastTime;
        this.frameTime = delta;
        this.frameCount++;
        
        if (now - this.lastFpsTime >= 500) {
            this.fps = Math.round((this.frameCount / (now - this.lastFpsTime)) * 1000);
            this.frameCount = 0;
            this.lastFpsTime = now;
        }
    }
}
```

**UI Display** (top-right corner):
```
✓ WebGPU Available
Renderer: WebGL
FPS: 60
Frame: 16.7ms
```

**Color-Coded Feedback**:
- 🟢 Green (55+ FPS): Excellent performance
- 🟡 Yellow (30-54 FPS): Acceptable performance
- 🔴 Red (<30 FPS): Performance concern

**Integration with Renderer Switching**:
```javascript
function switchRenderer(name) {
    Module._set_renderer(name);
    stats.updateRenderer(name);  // Update display
}
```

**Architecture Benefits**:
- ✅ Real-time FPS tracking via `performance.now()`
- ✅ Visual feedback shows active renderer
- ✅ Enables performance comparison between G2 and G3
- ✅ Browser WebGPU support indicator

**Learning**: Low-overhead performance metrics enable data-driven optimization; updating every 500ms prevents jank.

## Learning Progression (Week 4-5 Curriculum)

This codebase is structured as an educational journey through GPU rendering:

### Week 4: Foundation
- **G1**: Basic initialization (SDL2 window, GL context)
- **G2**: Triangle rendering with immediate-mode OpenGL
- **Concepts**: GPU state machines, vertex buffers, shader compilation, rasterization pipeline

### Week 5: Comparative Analysis
- **Runtime Switching**: Compare G2 and G3 without rebuilding
- **Shader Variants**: Test color rendering across both paths
- **Performance Metrics**: Measure and visualize FPS differences
- **Concepts**: Immediate vs. deferred rendering, CPU-GPU sync, command batching

### Week 6+ (Planned)
- **Compute Shaders**: DICOM volume raytracing
- **Memory Allocation**: Arena/pool allocators for GPU resources
- **ECS Architecture**: Scalable entity-component-system for geometry management
- **Interactive Controls**: Mouse/keyboard for scene exploration

## Next Steps (Week 6+)

- [ ] Separate G2 and G3 into distinct source files (main_webgl.cpp, main_webgpu.cpp) for clarity
- [ ] Implement memory allocators (arena, pool) for GPU resources
- [ ] Add ECS-based resource management
- [ ] Implement compute shaders for DICOM volume raytracing
- [ ] Add interactive controls (mouse/keyboard for 3D navigation)
- [ ] Optimize WASM binary size (currently 513KB WASM, 414KB JS)

## References

- [Emscripten Documentation](https://emscripten.org)
- [WebGPU Specification](https://w3c.github.io/webgpu/)
- [SDL2 Emscripten Guide](https://wiki.libsdl.org/SDL2/CategoryEmscripten)
