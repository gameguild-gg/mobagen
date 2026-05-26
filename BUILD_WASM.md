# Building WASM Versions (WebGL & WebGPU)

This guide explains how to build and test the WASM versions of the DICOM renderer.

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

### G2: WASM + WebGL (OpenGL ES 3.0)

Uses traditional OpenGL immediate-mode rendering compiled to WebAssembly.

**Build:**
```bash
cd e:/repositories/game-guild/mobagen
emcmake cmake -B build-wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-webgl
```

**Output:** `build-wasm-webgl/bin/dicom_renderer.html`

### G3: WASM + WebGPU (Modern Deferred Rendering)

Modern GPU API with deferred rendering commands. **Requires Chrome/Edge with WebGPU enabled.**

**Build:**
```bash
cd e:/repositories/game-guild/mobagen
emcmake cmake -B build-wasm-webgpu -DCMAKE_BUILD_TYPE=Release -DUSE_WEBGPU=ON
cmake --build build-wasm-webgpu
```

**Output:** `build-wasm-webgpu/bin/dicom_renderer.html`

## Running Locally

WebAssembly **cannot** run from `file://` URLs. Use a local web server:

### Option A: Python (built-in)
```bash
cd build-wasm-webgl/bin
python -m http.server 8080
# Open browser: http://localhost:8080/dicom_renderer.html
```

### Option B: Emscripten's emrun
```bash
emrun --port 8080 build-wasm-webgl/bin/dicom_renderer.html
```

### Option C: Node.js http-server
```bash
npm install -g http-server
cd build-wasm-webgl/bin
http-server -p 8080
```

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

### Challenge 4: Dual-Path Build System
**Problem**: One `main.cpp` with two completely different rendering paths (G2 OpenGL vs G3 WebGPU).

**Solution**: Conditional compilation with preprocessor directives:
```cpp
#if defined(USE_WEBGPU) && defined(__EMSCRIPTEN__)
  // G3: WebGPU path
#else
  // G2: OpenGL path
#endif
```

**Build Commands**:
```bash
# G2: WebGL (default)
emcc src/main.cpp ... -o dicom_renderer.html

# G3: WebGPU
emcc src/main.cpp ... -DUSE_WEBGPU -o dicom_renderer.html
```

**Learning**: Preprocessor-based feature selection works but creates code maintenance burden; future refactor should separate into `main_webgl.cpp` and `main_webgpu.cpp`.

## Next Steps (Week 5+)

- [ ] Separate G2 and G3 into distinct source files for clarity
- [ ] Implement memory allocators (arena, pool) for GPU resources
- [ ] Add ECS-based resource management
- [ ] Implement compute shaders for DICOM volume raytracing
- [ ] Add interactive controls (mouse/keyboard)
- [ ] Optimize WASM binary size (currently 145K-509K)

## References

- [Emscripten Documentation](https://emscripten.org)
- [WebGPU Specification](https://w3c.github.io/webgpu/)
- [SDL2 Emscripten Guide](https://wiki.libsdl.org/SDL2/CategoryEmscripten)
