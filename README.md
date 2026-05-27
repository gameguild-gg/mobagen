# DICOM Renderer: Educational GPU Programming

A WebAssembly-based GPU rendering engine comparing **immediate-mode** (WebGL) vs **deferred-mode** (WebGPU) GPU execution patterns.

## 📚 Learning Goal

Understand how GPU rendering has evolved:
- **Immediate-mode (G2)**: Traditional - execute GPU commands now, one at a time
- **Deferred-mode (G3)**: Modern - batch GPU commands for driver optimization

**Visual Learning**: WebGL renders a **triangle**, WebGPU renders a **square** — making the difference obvious.

## 🚀 Quick Start (5 minutes)

### Option 1: Test Layout Without Building
```bash
python -m http.server 8084
# Open: http://localhost:8084/test.html
```

### Option 2: Full WASM Build
```powershell
# Set Emscripten environment
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:EMSDK\node\22.16.0_64bit\bin;$env:PATH"

# Build
rm -r build-wasm-unified -Force
emcmake cmake -B build-wasm-unified -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-unified

# Run
cd build-wasm-unified\bin
python -m http.server 8083
# Open: http://localhost:8083/dicom_renderer.html
```

See [QUICKSTART.md](QUICKSTART.md) for detailed setup.

## 📖 Documentation

| Document | Purpose |
|----------|---------|
| [QUICKSTART.md](QUICKSTART.md) | **→ Start here** — Setup, usage, troubleshooting |
| [BUILD_WASM.md](BUILD_WASM.md) | Build details, architecture decisions, lessons learned |
| [SHADER_LANGUAGES.md](SHADER_LANGUAGES.md) | GLSL vs WGSL comparison for learning |
| [src/main.cpp](src/main.cpp) | Implementation (heavily commented) |

## 🎮 How to Use

### Controls (Visible in Top-Left)

**Renderer Selection:**
- Click **"WebGL: Triangle"** → Immediate-mode OpenGL (shape = triangle)
- Click **"WebGPU: Quad"** → Deferred-mode WebGPU (shape = square)

**Color Variants:**
- Press **1/2/3/4** or click buttons
  - **1** = Teal
  - **2** = Red
  - **3** = Green
  - **4** = Yellow

**Performance Stats (Top-Right):**
- Current renderer (G2 or G3)
- Real-time FPS with color coding
- Frame time in milliseconds

## 🏗️ Architecture

### Two Rendering Paths in One WASM Binary

```
main.cpp (C++)
├── AppWebGL (G2)
│   ├── SDL2 window
│   ├── OpenGL context
│   ├── Triangle geometry (3 vertices)
│   └── GLSL ES 3.00 shader
│
└── AppWebGPU (G3)
    ├── SDL2 window (events only)
    ├── JavaScript WebGPU rendering
    ├── Quad geometry (4 vertices)
    └── WGSL shader
    
html/shell.html (Emscripten Template)
├── UI: Controls + Stats panels
├── WebGPU initialization (JavaScript)
└── Runtime renderer switching
```

### Runtime Switching

```cpp
// C++ exports these to JavaScript
extern "C" EMSCRIPTEN_KEEPALIVE void set_renderer(const char* name);
extern "C" EMSCRIPTEN_KEEPALIVE void set_shader_variant(int variant);
```

```javascript
// JavaScript calls C++
Module._set_renderer('webgl');      // Switch renderer
Module._set_shader_variant(2);      // Change color
```

Both renderers initialize at startup. The `em_unified_tick()` function routes each frame to the active renderer.

## 🎓 Learning Progression

### Week 4: Foundation (✅ Complete)
- **G1**: Window initialization, OpenGL context setup
- **G2**: Triangle rendering with vertex buffers, VAO, shaders
- **Concepts**: GPU state machines, immediate-mode execution, rasterization pipeline

### Week 5: Comparative Analysis (✅ Complete)
- **Runtime Switching**: Both G2 and G3 in single binary, switch without rebuild
- **Shader Variants**: Dynamic color injection (4 teal/red/green/yellow)
- **Visual Differentiation**: Triangle vs Quad makes renderer change obvious
- **Performance Monitoring**: Real-time FPS, frame time tracking
- **Concepts**: Deferred-mode batching, command buffers, CPU-GPU sync patterns

### Week 6+ (Planned)
- **Compute Shaders**: GPU-parallel DICOM volume raytracing (WebGPU only)
- **Memory Allocators**: Arena/pool allocators for GPU resource management
- **ECS Architecture**: Entity-component-system for scalable geometry
- **Interactive Controls**: 3D camera, mouse/keyboard navigation

## 🔍 Key Insights

### Immediate-Mode (WebGL/G2)
```cpp
glUseProgram(program);              // GPU wait
glUniform4f(color_loc, ...);        // GPU wait
glDrawArrays(GL_TRIANGLES, 0, 3);   // GPU wait - executes now
```
- ✅ Simple to understand
- ❌ High CPU-GPU synchronization overhead
- ❌ Can't use compute shaders

### Deferred-Mode (WebGPU/G3)
```javascript
const encoder = device.createCommandEncoder();
const pass = encoder.beginRenderPass({...});
pass.setPipeline(pipeline);
pass.draw(4);                       // Record, don't execute
pass.end();
queue.submit([encoder.finish()]);   // Execute batch at once
```
- ✅ Lower CPU overhead (batching)
- ✅ GPU driver can optimize entire batch
- ✅ Supports compute shaders
- ⚠️ More complex API

### Visual Comparison
| Aspect | Immediate (G2) | Deferred (G3) |
|--------|---------------|--------------|
| Shape | 🔺 Triangle | ⬜ Quad |
| Color | Identical in both | Identical in both |
| FPS | Usually 60 (GPU-limited) | Usually 60 (GPU-limited) |
| CPU Load | Higher per draw | Lower (batched) |

## 🛠️ Technical Details

### Building
- **Emscripten**: Compiles C++ to WebAssembly
- **CMake**: Dual-target (native + WASM)
- **SDL2**: Cross-platform windowing
- **GLEW**: OpenGL extensions (native only)

### Rendering
- **G2**: OpenGL ES 3.0 (immediate-mode state machine)
- **G3**: WebGPU (deferred-mode command buffers)
- **Shaders**: GLSL ES 3.00 (G2) vs WGSL (G3)
- **Geometry**: Triangle (G2) vs Quad (G3)

### Browser Support
| Browser | G2 (WebGL) | G3 (WebGPU) |
|---------|-----------|-----------|
| Chrome | ✅ All versions | ✅ 113+ |
| Firefox | ✅ All versions | ⏳ Behind flag |
| Edge | ✅ All versions | ✅ 113+ |
| Safari | ✅ 14+ | ❌ Not yet |

## 📁 Project Structure

```
.
├── src/
│   ├── main.cpp                 # Dual-path rendering (G2 + G3)
│   └── engine/
│       ├── shader_program.*     # GLSL compilation
│       ├── vertex_buffer.*      # GPU vertex data
│       ├── vertex_array.*       # VAO management
│       ├── renderer.*           # Render state abstraction
│       └── engine_c.*           # C API boundary
│
├── html/
│   └── shell.html               # Emscripten HTML template
│                                 # (UI controls, WebGPU JS)
│
├── test.html                    # Standalone test (pure JS)
│
├── CMakeLists.txt               # Build configuration
│
├── README.md                    # This file
├── QUICKSTART.md                # Fast setup guide
├── BUILD_WASM.md                # Build details + architecture
├── SHADER_LANGUAGES.md          # GLSL vs WGSL learning guide
│
└── build-wasm-unified/          # WASM build output
    └── bin/
        ├── dicom_renderer.html  # Final WASM app
        ├── dicom_renderer.wasm  # WebAssembly binary
        └── dicom_renderer.js    # Emscripten runtime
```

## 🐛 Troubleshooting

**Buttons not visible?**
- Hard refresh browser (Ctrl+Shift+R)
- Ensure window is large enough (1024x768+)
- Check console (F12) for errors

**WebGPU shows black?**
- Enable in Chrome: `chrome://flags/#enable-unsafe-webgpu`
- Enable in Firefox: `dom.webgpu.enabled` in `about:config`
- Ensure Chrome 113+ / Edge 113+

**Build fails?**
- Verify Emscripten environment is set
- Delete `build-wasm-unified` and rebuild
- Check `CMakeLists.txt` paths

**Low FPS?**
- GPU might be throttling
- Check browser DevTools for GL errors
- Compare FPS between G2 and G3

## 📚 Learning Resources

- [Emscripten Guide](https://emscripten.org/)
- [WebGPU Specification](https://w3c.github.io/webgpu/)
- [GLSL ES 3.0 Spec](https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)
- [SDL2 Documentation](https://wiki.libsdl.org/SDL2/APIByCategory)
- [GPU Architecture Fundamentals](https://github.com/gpuweb/gpuweb/wiki)

## 🎯 Next Steps

1. **Immediate**: Open [test.html](test.html) to see the UI
2. **Short-term**: Build WASM version (follow QUICKSTART.md)
3. **Learning**: Read SHADER_LANGUAGES.md to understand GLSL vs WGSL
4. **Advanced**: Study BUILD_WASM.md architecture decisions
5. **Future**: Add compute shaders for DICOM volume raytracing

## 📝 Notes

This is a learning-focused project. Every architectural choice is documented with:
- **What**: What was built
- **Why**: Why this approach
- **Learning**: What it teaches about GPU programming

Comments in code explain the reasoning, not just the mechanics.

---

**Status**: Week 5 complete (runtime switching, shader variants, performance monitoring)
**Next**: Week 6+ (compute shaders for DICOM raytracing)
**Branch**: `feat/wasm-dicom-raytracing-renderer`
