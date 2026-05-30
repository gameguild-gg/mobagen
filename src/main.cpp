// Prevent SDL2 from redefining main() to SDL_main()
// We want to control the entry point ourselves
#define SDL_MAIN_HANDLED

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/camera.h"

// ============================================================================
// SINGLE-RENDERER BUILD MODEL
// ============================================================================
//
// LEARNING: A browser <canvas> can hold exactly ONE context for its lifetime.
// Once getContext('webgl2') is called, getContext('webgpu') returns null (and
// vice versa). So a runtime "switch renderer on the same canvas" is impossible.
//
// Instead, each renderer is its own BUILD, selected at compile time:
//   - USE_WEBGPU OFF (default) -> WebGL2 build  (the learning rung)
//   - USE_WEBGPU ON            -> WebGPU build  (the destination: compute shaders)
//
// Both builds share the same camera + input code below. The renderer-specific
// code is guarded by #ifdef USE_WEBGPU.
//
// CMake produces them in separate output dirs:
//   build/wasm-webgl/bin/dicom_renderer.html
//   build/wasm-webgpu/bin/dicom_renderer.html
// ============================================================================

// ============================================================================
// CAMERA & INPUT (shared by both renderer builds)
// ============================================================================
static engine::Camera g_camera(engine::CameraMode::ORBIT);

// Mouse-look is GATED: the camera only rotates while a mouse button is held.
// This lets the user move the cursor to click UI without spinning the camera.
static bool g_mouse_look_active = false;

// Real frame delta time, measured from SDL's high-resolution counter.
static float measure_delta_seconds() {
    static uint64_t last = SDL_GetPerformanceCounter();
    const uint64_t now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    float dt = static_cast<float>(static_cast<double>(now - last) / freq);
    last = now;
    // Clamp to avoid huge jumps after a stall / tab switch.
    if (dt > 0.1f) dt = 0.1f;
    return dt;
}

// Poll SDL events: quit, keyboard, mouse. Updates the shared camera.
// Works identically in native and Emscripten (SDL2 abstracts the event source).
static void process_input(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_KEYDOWN:
                g_camera.on_key_pressed(event.key.keysym.sym);
                // 'C' toggles between ORBIT (DICOM viewing) and WASD (engine fly).
                if (event.key.keysym.sym == SDLK_c) {
                    engine::CameraMode next =
                        (g_camera.get_mode() == engine::CameraMode::ORBIT)
                            ? engine::CameraMode::WASD
                            : engine::CameraMode::ORBIT;
                    g_camera.set_mode(next);
                    printf("Camera mode: %s\n",
                           next == engine::CameraMode::ORBIT ? "ORBIT" : "WASD");
                }
                break;

            case SDL_KEYUP:
                g_camera.on_key_released(event.key.keysym.sym);
                break;

            case SDL_MOUSEBUTTONDOWN:
                g_mouse_look_active = true;
                break;

            case SDL_MOUSEBUTTONUP:
                g_mouse_look_active = false;
                break;

            case SDL_MOUSEMOTION:
                if (g_mouse_look_active) {
                    g_camera.on_mouse_motion(event.motion.xrel, event.motion.yrel);
                }
                break;

            case SDL_MOUSEWHEEL:
                g_camera.on_mouse_wheel(event.wheel.y);
                break;
        }
    }
}

#ifdef USE_WEBGPU
// ============================================================================
// G3: WebGPU BUILD (deferred-mode, the destination)
// ============================================================================
//
// The WebGPU device, pipeline and draw are implemented in JavaScript
// (html/shell_webgpu.html), because WebGPU is a JS-first API and Emscripten's
// C bindings are still in flux. This C++ side owns the main loop and input;
// it asks JS to render one frame per tick via window.webgpu_render().
//
// Camera->WGSL wiring is intentionally NOT done yet: the current WGSL just
// draws a static triangle. The matrix plumbing arrives when we move to
// fullscreen-quad ray marching (see docs/LEARNING.md, Tier 2).

struct AppWebGPU {
    SDL_Window* window = nullptr;
    bool running = true;

    bool init() {
        printf("WebGPU (G3) build — device/pipeline initialized in JavaScript.\n");
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        // No SDL_WINDOW_OPENGL: the canvas context is owned by WebGPU (JS side).
        // The SDL window exists so SDL can route keyboard/mouse events for input.
        window = SDL_CreateWindow("DICOM Renderer (WebGPU)",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  800, 600, SDL_WINDOW_SHOWN);
        if (!window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return false;
        }
        return true;
    }

    void tick() {
        process_input(running);
        g_camera.update(measure_delta_seconds());

        // Single owner of the frame: ask JS to record + submit one frame.
        // (shell_webgpu.html does NOT run its own requestAnimationFrame loop.)
#ifdef __EMSCRIPTEN__
        emscripten_run_script("if (window.webgpu_render) window.webgpu_render();");
#endif
    }

    void cleanup() {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

#else
// ============================================================================
// G2: WebGL2 / OpenGL BUILD (immediate-mode, the learning rung)
// ============================================================================

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
static constexpr const char* VERT_GLSL = "#version 300 es\n";
static constexpr const char* FRAG_GLSL = "#version 300 es\nprecision mediump float;\n";
#else
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
static constexpr const char* VERT_GLSL = "#version 330 core\n";
static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#endif

#include <vector>
#include "engine/shader_program.h"
#include "engine/vertex_buffer.h"
#include "engine/vertex_array.h"
#include "engine/renderer.h"
#include "engine/texture.h"

// --- Tint (driven by the 1-4 buttons) ----------------------------------------
// Multiplied with the sampled texture in the fragment shader. Default white =
// show the texture's true colors; the buttons tint it. This teaches combining a
// sampler with a uniform (vs WebGPU, which feeds color via a uniform buffer).
static glm::vec4 g_tint(1.0f, 1.0f, 1.0f, 1.0f);

static glm::vec4 variant_color(int n) {
    switch (n) {
        case 1: return {0.0f, 1.0f, 0.5f, 1.0f};  // teal
        case 2: return {1.0f, 0.0f, 0.0f, 1.0f};  // red
        case 3: return {0.0f, 1.0f, 0.0f, 1.0f};  // green
        case 4: return {1.0f, 1.0f, 0.0f, 1.0f};  // yellow
        default: return {1.0f, 1.0f, 1.0f, 1.0f}; // white (no tint)
    }
}

// A checkerboard modulated by a UV gradient, so both tiling AND orientation are
// visible. Same "fill data on the CPU, upload to a texture, sample on the GPU"
// pattern we reuse for the synthetic 3D volume in Tier 2.
static std::vector<unsigned char> make_checker_texture(int size) {
    std::vector<unsigned char> px(static_cast<size_t>(size) * size * 4);
    const int cell = size / 8;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
            const float u = static_cast<float>(x) / (size - 1);
            const float v = static_cast<float>(y) / (size - 1);
            unsigned char* p = &px[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = dark ? 40 : 230;                          // R: checker
            p[1] = static_cast<unsigned char>(v * 255.0f);   // G: vertical gradient
            p[2] = static_cast<unsigned char>(u * 255.0f);   // B: horizontal gradient
            p[3] = 255;
        }
    }
    return px;
}

struct AppWebGL {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    std::unique_ptr<engine::VertexArray> vao;
    std::unique_ptr<engine::VertexBuffer> vbo;
    std::unique_ptr<engine::ShaderProgram> shader;
    std::unique_ptr<engine::Texture2D> texture;
    engine::Renderer renderer;
    bool running = true;

    bool init();
    void tick();
    void cleanup();

private:
    bool compileShaders();
    bool setupGeometry();
};

bool AppWebGL::compileShaders() {
    std::string vertSrc = std::string(VERT_GLSL) + R"(
uniform mat4 view_projection;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
    vUv = aUv;                                       // pass UV to fragment stage
    gl_Position = view_projection * vec4(aPos, 0.0, 1.0);
}
)";

    std::string fragSrc = std::string(FRAG_GLSL) + R"(
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTex;
uniform vec4 uTint;

void main() {
    // The heart of Tier 1.2: read data from a texture at coordinate vUv.
    // In Tier 2 this becomes a 3D texture sampled at points along a ray.
    fragColor = texture(uTex, vUv) * uTint;
}
)";

    std::string errmsg;
    auto next = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);
    if (!next->isValid()) {
        fprintf(stderr, "Shader compile failed: %s\n", errmsg.c_str());
        return false;
    }
    shader = std::move(next);
    renderer.setShaderProgram(shader.get());
    return true;
}

bool AppWebGL::setupGeometry() {
    // A quad as two triangles. Each vertex is (x, y, u, v) interleaved.
    const float verts[] = {
        // pos            uv
        -0.5f,  0.5f,    0.0f, 0.0f,   // top-left
        -0.5f, -0.5f,    0.0f, 1.0f,   // bottom-left
         0.5f, -0.5f,    1.0f, 1.0f,   // bottom-right

        -0.5f,  0.5f,    0.0f, 0.0f,   // top-left
         0.5f, -0.5f,    1.0f, 1.0f,   // bottom-right
         0.5f,  0.5f,    1.0f, 0.0f,   // top-right
    };

    vbo = std::make_unique<engine::VertexBuffer>(verts, sizeof(verts));
    if (!vbo || vbo->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex buffer\n");
        return false;
    }
    vao = std::make_unique<engine::VertexArray>();
    if (!vao || vao->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex array\n");
        return false;
    }

    const GLsizei stride = 4 * sizeof(float);
    vao->bind();
    vbo->bind();
    vao->setVertexAttribute(0, 2, GL_FLOAT, 0, stride);                  // position
    vao->setVertexAttribute(1, 2, GL_FLOAT, 2 * sizeof(float), stride);  // uv
    engine::VertexArray::unbind();
    engine::VertexBuffer::unbind();

    // Procedural texture — no asset or PNG decoder needed. Swappable for a real
    // PNG via stb_image later (see docs/LEARNING.md, Tier 1.2).
    const int texSize = 256;
    std::vector<unsigned char> pixels = make_checker_texture(texSize);
    texture = std::make_unique<engine::Texture2D>(texSize, texSize, pixels.data());
    if (!texture || texture->getHandle() == 0) {
        fprintf(stderr, "Failed to create texture\n");
        return false;
    }
    return true;
}

bool AppWebGL::init() {
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

    window = SDL_CreateWindow("DICOM Renderer (WebGL2)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
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

    if (!compileShaders() || !setupGeometry()) {
        cleanup();
        return false;
    }

    renderer.setClearColor(0.1f, 0.2f, 0.5f, 1.0f);
    printf("WebGL2 (G2) initialized.\n");
    return true;
}

void AppWebGL::tick() {
    process_input(running);
    g_camera.update(measure_delta_seconds());

    renderer.clear();

    // The Renderer binds the shader + VAO and issues the draw. We set the
    // per-frame uniforms on the active program first, and bind the texture to
    // unit 0 (which the sampler uniform uTex reads from).
    if (shader) {
        shader->use();
        shader->setUniform("view_projection", g_camera.get_view_projection());
        shader->setUniform("uTint", g_tint);
        shader->setUniform("uTex", 0);
    }
    if (texture) {
        texture->bind(0);
    }
    if (vao) {
        renderer.draw(*vao, 6);  // 6 vertices = two triangles = one quad
    }

    SDL_GL_SwapWindow(window);
}

void AppWebGL::cleanup() {
    shader.reset();
    vao.reset();
    vbo.reset();
    if (context) SDL_GL_DeleteContext(context);
    if (window)  SDL_DestroyWindow(window);
    SDL_Quit();
}

#endif  // USE_WEBGPU

// ============================================================================
// EXPORTED C FUNCTIONS FOR JAVASCRIPT (Emscripten)
// ============================================================================
#ifdef __EMSCRIPTEN__
extern "C" {
    // Called by the shell when the canvas is resized.
    EMSCRIPTEN_KEEPALIVE
    void on_canvas_resize(int width, int height) {
        g_camera.set_viewport(width, height);
#ifndef USE_WEBGPU
        glViewport(0, 0, width, height);
#endif
        // WebGPU build reconfigures its context in JS.
    }

#ifndef USE_WEBGPU
    // Tint control (WebGL build only — WebGPU does color in JS). Sets the vec4
    // multiplied with the sampled texture; out-of-range resets to white.
    EMSCRIPTEN_KEEPALIVE
    void set_shader_variant(int variant_num) {
        g_tint = variant_color(variant_num);
    }
#endif
}
#endif

// ============================================================================
// MAIN
// ============================================================================

static const char* renderer_name() {
#ifdef USE_WEBGPU
    return "WebGPU (G3)";
#else
    return "WebGL2 (G2)";
#endif
}

#ifdef USE_WEBGPU
using App = AppWebGPU;
#else
using App = AppWebGL;
#endif

static App* g_app = nullptr;

#ifdef __EMSCRIPTEN__
static void em_tick() {
    if (g_app && g_app->running) {
        g_app->tick();
    } else {
        emscripten_cancel_main_loop();
    }
}
#endif

int main() {
    printf("====================================\n");
    printf("DICOM Renderer — %s build\n", renderer_name());
    printf("====================================\n");

    static App app;
    g_app = &app;

    if (!app.init()) {
        fprintf(stderr, "Failed to initialize %s\n", renderer_name());
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
