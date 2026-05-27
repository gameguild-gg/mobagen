#if defined(USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ============================================================================
// G3: WebGPU Path (Modern deferred rendering) — Educational Implementation
// ============================================================================
//
// LEARNING GOAL: Understand how to bridge native code (C++) with modern
// web APIs (JavaScript WebGPU) using Emscripten.
//
// KEY CONCEPTS:
//
// 1. DEFERRED RENDERING vs IMMEDIATE RENDERING
//    - OpenGL (immediate): Each call executes on GPU right away
//    - WebGPU (deferred): Record commands into a buffer, submit batch to GPU
//    - Benefit: GPU driver can optimize the entire batch at once
//
// 2. JAVASCRIPT FFI (Foreign Function Interface)
//    - Emscripten lets C++ call JavaScript via emscripten_run_script()
//    - WebGPU is a JavaScript-first API with native browser support
//    - Instead of waiting for C headers, we use the API directly from JS
//
// 3. EVENT LOOP ARCHITECTURE
//    - emscripten_set_main_loop() schedules our tick() on requestAnimationFrame
//    - This synchronizes with browser's vsync (usually 60 FPS)
//    - Native code would use a while() loop instead
//
// ============================================================================

#include <emscripten.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

// AppWebGPU: Minimal C++ wrapper around JavaScript WebGPU implementation.
//
// Design philosophy: Keep C++ simple, do real work in JavaScript.
// Why? WebGPU is designed for JavaScript. We're not fighting the platform.
//
struct AppWebGPU {
    SDL_Window* window = nullptr;
    bool running = true;

    bool init();      // Called once at startup
    void tick();      // Called every frame by browser
    void cleanup();   // Called at shutdown
};

bool AppWebGPU::init() {
    printf("WebGPU initialization via JavaScript...\n");
    // The actual WebGPU setup (device, queue, pipeline, shaders) happens
    // in html/webgpu.js on DOMContentLoaded. We just need to wait for it.
    //
    // This demonstrates async resource initialization:
    // - C++ starts immediately
    // - JavaScript async/await handles GPU setup
    // - Both coordinate through global state (window.webgpuState)
    return true;
}

void AppWebGPU::tick() {
    // FRAME LOOP PATTERN:
    // 1. Input: Poll events (keyboard, mouse, etc.)
    // 2. Update: Update game state
    // 3. Render: Call JavaScript to render via WebGPU

    // Step 1: INPUT - Check if user wants to quit
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
            running = false;
        }
    }

    // Step 3: RENDER - Call the JavaScript function
    // emscripten_run_script() executes JavaScript code as a string.
    // This is the bridge between C++ and WebGPU.
    //
    // What happens in webgpu_render():
    // 1. Create command encoder (start recording GPU commands)
    // 2. Begin render pass (define what we're rendering to)
    // 3. Set pipeline (select shader + render state)
    // 4. Set vertex buffer (tell GPU where vertex data is)
    // 5. Draw (submit draw call)
    // 6. End pass (finish recording)
    // 7. Submit to queue (GPU executes the batch)
    //
    // This is DEFERRED RENDERING: we record everything, then submit.
    // Compare to OpenGL (immediate): glDraw* executes immediately.
    emscripten_run_script("if(window.webgpu_render) window.webgpu_render();");
}

void AppWebGPU::cleanup() {
    printf("WebGPU cleanup\n");
    // GPU cleanup is handled by JavaScript garbage collection.
    // When window object is destroyed, WebGPU resources are released.
}

// Global pointer for Emscripten callback.
// Emscripten's event loop needs a static function, so we use a global
// to pass our object to it.
static AppWebGPU* g_app_webgpu = nullptr;

#ifdef __EMSCRIPTEN__
// EMSCRIPTEN EVENT LOOP ADAPTER
//
// Emscripten's main loop is driven by browser's requestAnimationFrame.
// This static function acts as a trampoline from Emscripten's callback
// to our C++ object.
//
// How it works:
// 1. Browser calls requestAnimationFrame
// 2. Emscripten calls em_tick_webgpu()
// 3. We call g_app_webgpu->tick()
// 4. Our C++ code runs (including JavaScript calls)
// 5. Next frame, repeat
//
// This pattern is needed because C++ static functions can't have this pointers.
static void em_tick_webgpu() {
    if (g_app_webgpu) {
        g_app_webgpu->tick();
        if (!g_app_webgpu->running) {
            emscripten_cancel_main_loop();  // Stop the event loop
        }
    }
}
#endif

int main() {
    // LESSON: Dual-target application entry point
    //
    // This single main() works on both:
    // - Browser (Emscripten/WASM)
    // - Native desktop (Windows/Linux/macOS)
    //
    // Key difference:
    // - WASM: Event loop is driven by browser (requestAnimationFrame)
    // - Native: We control the loop (while())
    //
    // This teaches the concept of "abstraction" - same high-level code,
    // different low-level event loop mechanisms.

    AppWebGPU app;
    g_app_webgpu = &app;  // Store global pointer for Emscripten

    if (!app.init()) {
        fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }

#ifdef __EMSCRIPTEN__
    // WASM PATH: Let Emscripten/browser drive the event loop
    //
    // emscripten_set_main_loop(function, fps, simulate_infinite_loop)
    // - function: Called on each frame
    // - fps: 0 = use browser's refresh rate (typically 60 FPS)
    // - simulate_infinite_loop: 1 = treat as if it never returns
    //
    // This is why we don't call app.cleanup() here - the loop never ends
    // (from C++ perspective). Cleanup happens when browser closes the page.
    emscripten_set_main_loop(em_tick_webgpu, 0, 1);
#else
    // NATIVE PATH: C++ controls the event loop
    //
    // Traditional desktop application pattern:
    // while (running) { process events, update, render }
    //
    // Difference from WASM:
    // - We call cleanup() explicitly after loop ends
    // - No browser's requestAnimationFrame synchronization
    // - Frame rate determined by how fast we loop (not ideal - needs vsync)
    while (app.running) {
        app.tick();
    }
    app.cleanup();
#endif

    return 0;
}

// ============================================================================
#else
// ============================================================================
// G2: OpenGL Path (Immediate-mode rendering) — Educational Implementation
// ============================================================================
//
// LEARNING GOAL: Compare immediate-mode rendering (OpenGL) with
// deferred-mode rendering (WebGPU) to understand GPU architecture.
//
// KEY CONCEPTS:
//
// 1. IMMEDIATE-MODE RENDERING (OpenGL)
//    - Each glDraw*() call executes on GPU immediately
//    - GPU state is global (state machine with 50+ states)
//    - CPU overhead: context switches between CPU and GPU
//    - Example: glUseProgram() → GPU switches to that shader NOW
//
// 2. DEFERRED-MODE RENDERING (WebGPU)
//    - Record commands into a buffer
//    - Submit entire batch to GPU at once
//    - GPU driver can optimize the whole batch
//    - Example: encoder.setPipeline() → Record command, submit later
//
// 3. SHADER VERSIONS
//    - WASM: GLSL ES 3.00 (mobile/web standard)
//    - Native: GLSL 3.30 core (desktop standard)
//    - WebGPU: WGSL (modern, type-safe, compute-capable)
//
// ============================================================================

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>
#else
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <memory>
#include "engine/shader_program.h"
#include "engine/vertex_buffer.h"
#include "engine/vertex_array.h"

// Shader version selection based on target platform
//
// LESSON: Cross-platform differences
// - WASM uses GLSL ES (Embedded Systems, designed for mobile GPUs)
// - Native uses GLSL core (Desktop, more features)
// Same shader concepts, different syntax for different hardware
#ifdef __EMSCRIPTEN__
static constexpr const char* VERT_GLSL = "#version 300 es\n";
static constexpr const char* FRAG_GLSL = "#version 300 es\nprecision mediump float;\n";
#else
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
static constexpr const char* VERT_GLSL = "#version 330 core\n";
static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#endif

struct App {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    std::unique_ptr<engine::VertexArray> vao;
    std::unique_ptr<engine::VertexBuffer> vbo;
    std::unique_ptr<engine::ShaderProgram> shader;
    bool running = true;

    bool init();
    void tick();
    void cleanup();

private:
    bool compileShaders();
    bool setupGeometry();
};

bool App::compileShaders() {
    std::string vertSrc = std::string(VERT_GLSL) + R"(
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

    std::string fragSrc = std::string(FRAG_GLSL) + R"(
out vec4 fragColor;

void main() {
    fragColor = vec4(0.0, 1.0, 0.5, 1.0);
}
)";

    std::string errmsg;
    shader = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);

    if (!shader->isValid()) {
        fprintf(stderr, "Shader compilation failed: %s\n", errmsg.c_str());
        shader.reset();
        return false;
    }

    return true;
}

bool App::setupGeometry() {
    float vertices[] = {
        0.0f,  0.5f,
        -0.5f, -0.5f,
        0.5f, -0.5f
    };

    // Upload vertex data to GPU using abstraction layer
    vbo = std::make_unique<engine::VertexBuffer>(vertices, sizeof(vertices));
    if (!vbo || vbo->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex buffer\n");
        return false;
    }

    // Create VAO and configure vertex attributes
    vao = std::make_unique<engine::VertexArray>();
    if (!vao || vao->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex array\n");
        return false;
    }

    vao->bind();
    vbo->bind();
    vao->setVertexAttribute(0, 2, GL_FLOAT, 0);  // Attribute 0: 2D position

    engine::VertexArray::unbind();
    engine::VertexBuffer::unbind();

    return true;
}

bool App::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window = SDL_CreateWindow(
        "DICOM Renderer (OpenGL)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(1);

#ifndef __EMSCRIPTEN__
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(err));
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
#endif

    if (!compileShaders()) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    if (!setupGeometry()) {
        shader.reset();
        vao.reset();
        vbo.reset();
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    return true;
}

void App::tick() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
        }
    }

    glClearColor(0.1f, 0.2f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (shader) {
        shader->use();
    }
    if (vao) {
        vao->bind();
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    SDL_GL_SwapWindow(window);
}

void App::cleanup() {
    // unique_ptr destructors handle GPU resource cleanup automatically
    shader.reset();
    vao.reset();
    vbo.reset();

    if (context) {
        SDL_GL_DeleteContext(context);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

static App* g_app = nullptr;

#ifdef __EMSCRIPTEN__
static void em_tick() {
    if (g_app) {
        g_app->tick();
        if (!g_app->running) {
            emscripten_cancel_main_loop();
        }
    }
}
#endif

int main() {
    App app;
    g_app = &app;

    if (!app.init()) {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(em_tick, 0, 1);
#else
    while (app.running) {
        app.tick();
    }
    app.cleanup();
#endif

    return 0;
}

#endif // defined(USE_WEBGPU) && defined(__EMSCRIPTEN__)
