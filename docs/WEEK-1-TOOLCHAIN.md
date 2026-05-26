# Week 1 — Toolchain Setup & Colored Canvas

## The Challenge

Build a **dual-target graphics renderer** that compiles to both:

- 🌐 **WebAssembly** (runs in browser via Emscripten)
- 🖥️ **Native Desktop** (SDL2 + OpenGL 3.3+)

...and proves it works by displaying a **colored canvas**.

---

## Why This Matters

### The Problem We're Solving

DICOM volume raytracing requires GPU power. But:

- Desktop development is slow (compile, link, wait)
- Web deployment is crucial (no install, run anywhere)
- We need **both** for a real application

### The Solution: Dual Compilation

```
Single C++ codebase
    ↓
    ├─→ Emscripten → WebAssembly + JavaScript
    └─→ CMake + native compiler → Desktop exe
```

Same code, two targets. This is the **foundation** for everything else.

---

## Architecture Overview

### Build System Flow

```
Source Code (C++23)
    ↓
    ├─ [WASM Path] ───────────────────────────────────────────┐
    │  emcc (Emscripten compiler)                             │
    │  └─ Input: src/main.cpp, GLES3 headers                  │
    │  └─ Flags: -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -std=c++23 │
    │  └─ Output: dicom_renderer.wasm + .js glue code         │
    │  └─ Result: HTML file ready to serve                    │
    │                                                         │
    └─→ Browser: http://localhost:8080/dicom_renderer.html    │
    │                                                         │
    ├─ [Native Path] ──────────────────────────────────────── ┐
    │  CMake + C++ compiler (gcc/clang/MSVC)                  │
    │  └─ Input: src/main.cpp, SDL2, OpenGL 3.3+              │
    │  └─ CPM downloads SDL2 + GLEW automatically             │
    │  └─ Output: dicom_renderer.exe                          │
    │                                                         │
    └─→ Desktop: ./build-native/bin/dicom_renderer.exe        │
```

### Key Components

| Component                        | Purpose                  | Runs Where  |
| -------------------------------- | ------------------------ | ----------- |
| **CMakeLists.txt**               | Master build recipe      | Both paths  |
| **src/main.cpp**                 | Single C++ source        | Both paths  |
| **external/sdl.cmake**           | Downloads SDL2           | Native only |
| **external/glew.cmake**          | Downloads OpenGL wrapper | Native only |
| **html/shell.html**              | Emscripten HTML template | WASM only   |
| **CMake (native) + emcc (WASM)** | Compilers                | Both        |

---

## CMakeLists.txt — The Build Recipe

### What It Does

```cmake
cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
include(external/cpm.cmake)
```

**Translation:** "Requires CMake 3.16.3+. First, load the C++ Package Manager."

```cmake
project(DicomRenderer VERSION 0.1.0 LANGUAGES C CXX)
set(CXX_STANDARD_TARGET "23" CACHE STRING "CXX standard" FORCE)
include(external/compilerchecks.cmake)
```

**Translation:** "Project is called DicomRenderer. Use C++23. Check compiler supports it."

### The Critical Split: `if(EMSCRIPTEN)`

```cmake
if(EMSCRIPTEN)
    # This code runs when building for WASM
    target_link_options(dicom_renderer PRIVATE
        -O3                          # Optimize for speed
        -sUSE_SDL=2                  # Link Emscripten's SDL2 port
        -sMIN_WEBGL_VERSION=2        # Demand WebGL2 (= GLES3)
        -sMAX_WEBGL_VERSION=2        # Don't fall back to WebGL1
        -sFULL_ES3=1                 # Enable all GLES3 features
        -sALLOW_MEMORY_GROWTH=1      # Heap can grow as needed
    )
else()
    # This code runs when building for desktop
    include(external/external.cmake)  # Load SDL2 + GLEW
    find_package(OpenGL REQUIRED)
    target_link_libraries(dicom_renderer PRIVATE
        SDL2-static OpenGL::GL libglew_static)
endif()
```

**Key insight:**

- **WASM:** Emscripten provides SDL2 as a "port" that bridges to WebGL
- **Native:** We download real SDL2 + GLEW (OpenGL loader) from GitHub via CPM

---

## Building & Running the WASM Version

### Step 1: Set Up Emscripten

```bash
# Clone Emscripten SDK (one-time setup)
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk

# Install and activate latest version
./emsdk install latest
./emsdk activate latest

# Load environment (do this every time before compiling)
source ./emsdk_env.sh
```

### Step 2: Compile to WASM

```bash
cd e:\repositories\game-guild\mobagen

# Direct compilation (no CMake needed for WASM)
~/emsdk/python/3.13.3_64bit/python.exe \
  ~/emsdk/upstream/emscripten/emcc.py \
  src/main.cpp -o build-wasm/dicom_renderer.html \
  -O3 \
  -sUSE_SDL=2 \
  -sMIN_WEBGL_VERSION=2 \
  -sMAX_WEBGL_VERSION=2 \
  -sFULL_ES3=1 \
  -sASSERTIONS=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  --shell-file html/shell.html \
  -std=c++23
```

**What happens:**

1. Emscripten downloads SDL2 port (first time only)
2. Compiles your C++ to WASM bytecode
3. Generates JavaScript glue code to initialize the program
4. Wraps it in your HTML shell

**Output:**

```
build-wasm/
  ├── dicom_renderer.html   (457 bytes)  — The webpage
  ├── dicom_renderer.js     (233 KB)    — JavaScript runtime
  └── dicom_renderer.wasm   (420 KB)    — Compiled binary
```

### Step 3: Serve & Test

```bash
cd build-wasm

# Start HTTP server (WASM requires HTTP, not file://)
python -m http.server 8080

# Open browser: http://localhost:8080/dicom_renderer.html
```

**What you see:**

- Black page background
- Centered 800×600 canvas
- **Cornflower blue canvas** (`glClearColor(0.1, 0.2, 0.5)`)

**Success criteria:**

- ✅ Canvas loads without errors
- ✅ Blue color is clearly visible
- ✅ No error messages in browser console (F12)

---

## Understanding the HTML Shell

### `html/shell.html` — The Webpage Template

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <title>DICOM Renderer</title>
    <style>
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
      }
      body {
        background: #111; /* Dark gray */
        display: flex;
        justify-content: center; /* Center horizontally */
        align-items: center; /* Center vertically */
        height: 100vh; /* Full viewport height */
      }
      canvas {
        display: block;
      }
    </style>
  </head>
  <body>
    <!-- This canvas is where SDL2 renders -->
    <canvas id="canvas" oncontextmenu="event.preventDefault()"></canvas>

    <script>
      // Tell Emscripten which canvas to use
      var Module = {
        canvas: document.getElementById("canvas"),
      };
    </script>

    <!-- Emscripten replaces {{{ SCRIPT }}} with the actual loader -->
    {{{ SCRIPT }}}
  </body>
</html>
```

**How it works:**

1. **`<canvas id="canvas">`** — Where WebGL renders
2. **`Module = { canvas: ... }`** — Tells Emscripten where to draw
3. **`{{{ SCRIPT }}}`** — Emscripten's placeholder; it inserts `<script src="dicom_renderer.js"></script>` here

---

## Platform-Specific Code: How We Handle Differences

### The Problem

- **WASM** can't block the main thread (browser will freeze)
- **Native** owns the thread, can run a blocking loop

### The Solution: Conditional Compilation

```cpp
#ifdef __EMSCRIPTEN__
    // WASM path
    #include <emscripten.h>
    #include <SDL2/SDL.h>
    #include <GLES3/gl3.h>
#else
    // Native path
    #include <GL/glew.h>
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_opengl.h>
#endif
```

**Why the order matters:**

- **GLEW must come first** on native (it loads GL function pointers)
- **GLES3 headers on WASM** (Emscripten's built-in GL headers)

### Main Loop Differences

```cpp
static App* g_app = nullptr;

#ifdef __EMSCRIPTEN__
static void em_tick() {
    g_app->tick();
}

int main() {
    App app;
    g_app = &app;
    app.init();

    // Browser: register a callback
    emscripten_set_main_loop(em_tick, 0, 1);
    // ^ Never returns; browser calls em_tick() every frame

    return 0;
}
#else
int main() {
    App app;
    g_app = &app;
    app.init();

    // Desktop: blocking loop
    while (app.running) {
        app.tick();
    }
    app.cleanup();

    return 0;
}
#endif
```

**Key difference:**

- **WASM:** Can't block. Register callback `em_tick()`. Browser calls it each frame.
- **Native:** Run blocking `while` loop. We control the frame rate.

---

## SDL2 Context Setup

### What SDL Does

SDL = Simple DirectMedia Layer. It abstracts:

- Window creation
- Event handling (keyboard, mouse, close button)
- OpenGL context creation

### Context Setup: The Code

```cpp
// 1. Initialize SDL video system
SDL_Init(SDL_INIT_VIDEO);

// 2. Set context profile (GLES3 for WASM, OpenGL core for native)
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

// 3. Create window
SDL_Window* window = SDL_CreateWindow(
    "DICOM Renderer",
    SDL_WINDOWPOS_CENTERED,    // Center on screen
    SDL_WINDOWPOS_CENTERED,
    800, 600,                  // Resolution
    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
);

// 4. Create OpenGL context
SDL_GLContext context = SDL_GL_CreateContext(window);
SDL_GL_SetSwapInterval(1);  // VSync on
```

### Load OpenGL Functions (Native Only)

```cpp
#ifndef __EMSCRIPTEN__
    // Native: Load function pointers
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "GLEW init failed: %s\n", glewGetErrorString(err));
        return false;
    }
#endif
// WASM: Emscripten handles this automatically
```

**Why?**

- OpenGL is a huge API with hundreds of functions
- On native, we need to load pointers to these functions at runtime
- GLEW (OpenGL Extension Wrangler) does this automatically
- Emscripten handles it for us in WASM

---

## The Colored Canvas: What's Actually Happening

### Frame Rendering Loop

```cpp
void App::tick() {
    // 1. Handle events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            running = false;
    }

    // 2. Clear screen to blue
    glClearColor(0.1f, 0.2f, 0.5f, 1.0f);  // RGBA: (red, green, blue, alpha)
    glClear(GL_COLOR_BUFFER_BIT);

    // 3. Swap buffers (show frame)
    SDL_GL_SwapWindow(window);
}
```

### Color Values Explained

`glClearColor(0.1f, 0.2f, 0.5f, 1.0f)`

| Component | Value | Meaning           |
| --------- | ----- | ----------------- |
| Red       | 0.1   | 10% red (minimal) |
| Green     | 0.2   | 20% green (low)   |
| Blue      | 0.5   | 50% blue (strong) |
| Alpha     | 1.0   | 100% opaque       |

**Result:** Cornflower blue (more blue than red/green)

### Try It Yourself

Modify the values:

```cpp
glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Pure red
glClearColor(0.0f, 1.0f, 0.0f, 1.0f);  // Pure green
glClearColor(0.0f, 0.0f, 1.0f, 1.0f);  // Pure blue
glClearColor(1.0f, 1.0f, 1.0f, 1.0f);  // White
glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // Black
```

Rebuild and see the change immediately!

---

## Debugging: When Things Go Wrong

### WASM Debugging in Chrome

1. **Open DevTools:** F12
2. **Sources tab:** You can see C++ source (Emscripten generates source maps)
3. **Console tab:** WebGL/JavaScript errors appear here
4. **Performance tab:** Frame time, memory usage

### Common Issues

| Problem                                          | Cause                              | Fix                                         |
| ------------------------------------------------ | ---------------------------------- | ------------------------------------------- |
| Black canvas                                     | GL context not created             | Check `SDL_GL_CreateContext()` return value |
| Error in console: "Failed to initialize context" | Old graphics hardware              | Requires WebGL2 (2013+ GPU)                 |
| "GLEW init failed" (native)                      | Missing display or headless system | Ensure you have a monitor connected         |
| Server returns 404                               | WASM files not found               | Check `build-wasm/` directory contents      |
| Compilation hangs                                | First emcc run (downloads SDL2)    | Wait 30-60 seconds, check network           |

---

## Summary: Week 1 Checklist

- [ ] **Understand the dual-target concept** — One code, two platforms
- [ ] **Understand CMakeLists.txt** — The `if(EMSCRIPTEN)` split
- [ ] **Understand build flow** — emcc for WASM, CMake for native
- [ ] **Build and run WASM** — See colored canvas in browser
- [ ] **Debug in browser** — Familiar with DevTools F12
- [ ] **Modify colors** — Rebuild and see changes live
- [ ] **Understand SDL2 setup** — Window, context, event loop
- [ ] **Understand conditional compilation** — `#ifdef __EMSCRIPTEN__`

---

## Next Week Preview

Week 2 adds **geometry and shaders:**

- Upload a triangle (3 vertices) to the GPU
- Write vertex and fragment shaders
- Render the triangle with colors

This proves the full graphics pipeline works — not just a cleared canvas, but actual geometry.

---

## Resources

- **Emscripten Documentation:** https://emscripten.org/docs/
- **WebGL2 Specification:** https://www.khronos.org/webgl/
- **SDL2 Documentation:** https://wiki.libsdl.org/
- **OpenGL 3.3 Reference:** https://www.khronos.org/registry/OpenGL/specs/gl/glspec33.core.pdf
