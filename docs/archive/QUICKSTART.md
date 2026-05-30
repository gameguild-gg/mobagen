# DICOM Renderer - Quick Start Guide

## What Is This?

A **WebAssembly-based GPU rendering engine** that lets you compare two different GPU rendering approaches:
- **G2 (WebGL)**: Immediate-mode rendering (OpenGL ES 3.0)
- **G3 (WebGPU)**: Deferred-mode rendering (Modern GPU API)

**Visual Difference**: WebGL renders a **triangle**, WebGPU renders a **quad** — making the renderer switch visually obvious.

---

## Quick Setup

### Option A: Test Without Building (Fast)
```bash
cd e:\repositories\game-guild\mobagen
python -m http.server 8084
```
Then open: **http://localhost:8084/test.html**

This is pure JavaScript (no WASM). Perfect for testing the UI layout and controls.

### Option B: Full WASM Build (Real GPU Rendering)

#### 1. **Set Up Emscripten Environment**
```powershell
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:EMSDK\node\22.16.0_64bit\bin;$env:PATH"
```

#### 2. **Build WebGL Version (G2)**
```bash
cd e:\repositories\game-guild\mobagen
rm -r build/wasm-webgl -Force
emcmake cmake -B build/wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgl
```

#### 3. **Build WebGPU Version (G3)**
```bash
cd e:\repositories\game-guild\mobagen
rm -r build/wasm-webgpu -Force
emcmake cmake -B build/wasm-webgpu -DUSE_WEBGPU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgpu
```

#### 4. **Run Server (WebGL)**
```bash
cd build/wasm-webgl/bin
python -m http.server 8083
```

Or WebGPU on port 8084:
```bash
cd build/wasm-webgpu/bin
python -m http.server 8084
```

#### 5. **Open Browser**
**http://localhost:8083/dicom_renderer.html**

---

## How to Use

### Control Panel (Top-Left)

**🎮 RENDERER SELECTION**
- Click **"WebGL: Triangle"** → Triangle shape (immediate-mode OpenGL)
- Click **"WebGPU: Quad"** → Square shape (deferred-mode WebGPU)
- Active button glows **green**; inactive is **gray**

**🎨 COLOR VARIANTS**
- Click buttons or **press 1/2/3/4**:
  - **1** = Teal (cyan-green)
  - **2** = Red
  - **3** = Green
  - **4** = Yellow

### Stats Panel (Top-Right)
- **Renderer**: Shows current G2/G3
- **FPS**: Real-time frame rate
  - 🟢 Green (55+) = Smooth
  - 🟡 Yellow (30-54) = Playable
  - 🔴 Red (<30) = Slow
- **Frame Time**: Milliseconds per frame

### Testing Workflow
```
1. Start in WebGL (triangle, teal)
2. Press 2 → Triangle turns red
3. Press 3 → Triangle turns green
4. Click "WebGPU: Quad" → Shape changes to square
5. Press 4 → Square turns yellow
6. Click "WebGL: Triangle" → Back to triangle
```

All color changes work identically in both renderers.

---

## What You'll See

### WebGL (G2) - Immediate-Mode
```
🔺 TRIANGLE
- Cornflower blue background
- Teal/Red/Green/Yellow triangle
- Rendered with OpenGL ES 3.0
- CPU-GPU sync happens per draw call (higher overhead)
```

### WebGPU (G3) - Deferred-Mode
```
⬜ QUAD
- Same cornflower blue background
- Teal/Red/Green/Yellow square
- Rendered with modern WebGPU API
- GPU batches commands (lower CPU overhead)
```

**Key Insight**: Both produce identical colors and smooth animation, but use fundamentally different execution patterns.

---

## Architecture

### Files

```
html/
  └── shell.html              # Emscripten HTML template
  
src/
  ├── main.cpp               # C++ code (G2 WebGL + G3 WebGPU bridges)
  └── engine/
      ├── shader_program.*   # GLSL shader compilation
      ├── vertex_buffer.*    # GPU vertex data
      ├── vertex_array.*     # VAO management
      └── renderer.*         # Render state

CMakeLists.txt               # Build configuration
test.html                    # Standalone test (no WASM needed)
```

### Rendering Paths

**G2: WebGL (C++ Path)**
- SDL2 window + OpenGL context
- Triangle geometry (3 vertices)
- GLSL ES 3.00 fragment shader with dynamic color
- Immediate-mode: `glDraw*()` executes immediately
- Runs in browser via Emscripten

**G3: WebGPU (JavaScript Path)**
- SDL2 window (for event handling)
- Quad geometry (4 vertices in JavaScript)
- WGSL shader with dynamic color
- Deferred-mode: Commands recorded, then batched submit
- JavaScript handles async GPU initialization
- C++ calls `window.webgpu_render()` each frame

### Runtime Switching

```cpp
// C++ exports these functions to JavaScript
extern "C" EMSCRIPTEN_KEEPALIVE void set_renderer(const char* name);
extern "C" EMSCRIPTEN_KEEPALIVE void set_shader_variant(int variant);
```

```javascript
// JavaScript calls C++ functions
Module._set_renderer('webgl');      // Switch to WebGL
Module._set_shader_variant(2);      // Switch to Red color
```

---

## Learning Path

### Week 4: Foundation
- ✅ G1: Window + Context initialization
- ✅ G2: Triangle rendering with OpenGL

### Week 5: Comparison
- ✅ Runtime renderer switching
- ✅ 4 color variants (dynamic shader compilation)
- ✅ Real-time FPS monitoring
- ✅ Different geometry per renderer (triangle vs quad)

### Week 6+: Compute Shaders (Future)
- [ ] DICOM volume data loading
- [ ] Compute shader raytracing
- [ ] Interactive 3D controls

---

## Troubleshooting

### Buttons Not Visible
- Make sure browser window is at least 1024x768
- Try hard refresh (Ctrl+Shift+R)
- Check console for JavaScript errors (F12)

### WebGPU Shows All-Black
- Chrome/Edge: Enable `chrome://flags/#enable-unsafe-webgpu`
- Firefox: Enable `dom.webgpu.enabled` in about:config
- Safari: Check WebGPU experimental features

### Build Fails
- Ensure Emscripten environment is active
- Delete `build-wasm-unified` folder and rebuild
- Check that `CMakeLists.txt` can find SDL2 via Emscripten

### FPS is Low (< 30)
- GPU might be throttling
- Check browser DevTools (F12) for WebGL/WebGPU errors
- Try different renderer to compare

---

## Console Commands

Open DevTools (F12) and try in Console:
```javascript
// Switch renderer
Module._set_renderer('webgl');
Module._set_renderer('webgpu');

// Change color (1=Teal, 2=Red, 3=Green, 4=Yellow)
Module._set_shader_variant(3);

// Check which renderer is active
Module._get_renderer();
```

---

## Key Differences: G2 vs G3

| Aspect | G2 (WebGL) | G3 (WebGPU) |
|--------|-----------|-----------|
| **GPU Pattern** | Immediate-mode | Deferred-mode |
| **Geometry** | Triangle (3 verts) | Quad (4 verts) |
| **Shader Language** | GLSL ES 3.00 | WGSL |
| **CPU-GPU Sync** | High (per draw) | Low (batch) |
| **Future Use** | Rasterization | Compute shaders |
| **Browser Support** | All modern | Chrome 113+, Firefox, Edge 113+ |

---

## Next Steps

1. **Test the layout** with `test.html`
2. **Build WASM** if you want real GPU rendering
3. **Study shaders** in `SHADER_LANGUAGES.md`
4. **Explore the code** - comments explain the learning goals

---

## References

- [Emscripten Docs](https://emscripten.org/)
- [WebGPU Spec](https://w3c.github.io/webgpu/)
- [SDL2 Emscripten](https://wiki.libsdl.org/SDL2/CategoryEmscripten)
- [GLSL ES Spec](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)
