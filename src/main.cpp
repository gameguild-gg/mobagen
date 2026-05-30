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

// Current canvas drawing-buffer size, pushed from the shell via on_canvas_resize
// (the authoritative size; SDL's window size is stale on the web). 0 = not set.
static int g_canvas_w = 0;
static int g_canvas_h = 0;

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

#ifdef __EMSCRIPTEN__
        // The camera->WGSL bridge: the camera lives here in C++/WASM, but the
        // WebGPU renderer runs in JS. So each frame we marshal the inverse
        // view-projection (16 column-major floats) across the boundary into the
        // camera uniform buffer, then ask JS to record + submit one frame.
        // (shell_webgpu.html does NOT run its own requestAnimationFrame loop.)
        glm::mat4 invVP = glm::inverse(g_camera.get_view_projection());
        const float* m = &invVP[0][0];  // 16 contiguous floats
        EM_ASM({
            if (window.webgpu_set_camera) {
                window.webgpu_set_camera(HEAPF32.subarray($0 >> 2, ($0 >> 2) + 16));
            }
            if (window.webgpu_render) { window.webgpu_render(); }
        }, m);
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
static constexpr const char* FRAG_GLSL =
    "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
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
#include "engine/texture3d.h"
#include "engine/framebuffer.h"
#include "embedded_shaders.h"   // generated from shaders/*.glsl by CMake

// --- Tint (driven by the 1-4 buttons) ----------------------------------------
// Multiplied with the scene texture in the pass-2 blit. Default white shows the
// scene as-is; the buttons tint the whole image.
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

// Synthetic volume: a soft ball, density 1 at the centre falling to 0 at the
// edge. Same "fill on CPU, upload, sample on GPU" pattern as the checker — but
// now 3D. Stored as R8 voxels (density * 255). Tier 3 replaces this with DICOM.
static std::vector<unsigned char> make_volume(int n) {
    std::vector<unsigned char> v(static_cast<size_t>(n) * n * n);
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                // voxel centre in [-1, 1]
                glm::vec3 c = (glm::vec3(x, y, z) / float(n - 1) - 0.5f) * 2.0f;
                float r = glm::length(c);
                float density = glm::clamp(1.0f - r / 0.9f, 0.0f, 1.0f);
                density *= density;                       // softer falloff
                v[(static_cast<size_t>(z) * n + y) * n + x] =
                    static_cast<unsigned char>(density * 255.0f);
            }
        }
    }
    return v;
}

struct AppWebGL {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    // Pass 1 (volume ray cast -> FBO): fullscreen quad + 3D volume texture
    std::unique_ptr<engine::VertexArray> vao;
    std::unique_ptr<engine::VertexBuffer> vbo;
    std::unique_ptr<engine::ShaderProgram> shader;
    std::unique_ptr<engine::Texture3D> volume;

    // Pass 2 (FBO -> screen): fullscreen quad + post shader
    std::unique_ptr<engine::VertexArray> postVao;
    std::unique_ptr<engine::VertexBuffer> postVbo;
    std::unique_ptr<engine::ShaderProgram> postShader;
    std::unique_ptr<engine::Framebuffer> fbo;
    int fbW = 0, fbH = 0;

    engine::Renderer renderer;
    bool running = true;

    bool init();
    void tick();
    void cleanup();

private:
    bool compileShaders();
    bool compilePostShader();
    bool setupGeometry();
    bool setupPostGeometry();
    void ensureFramebuffer(int w, int h);
};

bool AppWebGL::compileShaders() {
    // Source from shaders/raygen.glsl (embedded at build time). The same file
    // holds both stages; we compile it twice with VERTEX_SHADER / FRAGMENT_SHADER
    // defined. The #version / precision header is prepended here so one source
    // serves WebGL2/GLES3 and desktop GL 3.3.
    std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n"   + shaders::RAYGEN_GLSL;
    std::string fragSrc = std::string(FRAG_GLSL) + "#define FRAGMENT_SHADER\n" + shaders::RAYGEN_GLSL;

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
    // Fullscreen quad: pos in NDC (-1..1), uv = (pos + 1) / 2 so the fragment
    // shader can reconstruct clip-space xy as uv*2-1.
    const float verts[] = {
        // pos            uv
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f, -1.0f,    1.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,

        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 1.0f,
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

    // Synthetic 3D volume (64^3) uploaded once; the ray caster samples it.
    const int N = 64;
    std::vector<unsigned char> voxels = make_volume(N);
    volume = std::make_unique<engine::Texture3D>(N, N, N, voxels.data());
    if (!volume || volume->getHandle() == 0) {
        fprintf(stderr, "Failed to create volume texture\n");
        return false;
    }
    return true;
}

// Pass 2 shader: blit the offscreen scene texture onto a fullscreen quad,
// multiplied by a tint. This is the "deliver the render-texture to the screen"
// step; the per-pixel work lives in pass 1.
bool AppWebGL::compilePostShader() {
    // Source from shaders/blit.glsl (both stages, compiled twice).
    std::string vertSrc = std::string(VERT_GLSL) + "#define VERTEX_SHADER\n"   + shaders::BLIT_GLSL;
    std::string fragSrc = std::string(FRAG_GLSL) + "#define FRAGMENT_SHADER\n" + shaders::BLIT_GLSL;

    std::string errmsg;
    auto next = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);
    if (!next->isValid()) {
        fprintf(stderr, "Post shader compile failed: %s\n", errmsg.c_str());
        return false;
    }
    postShader = std::move(next);
    return true;
}

bool AppWebGL::setupPostGeometry() {
    // Fullscreen quad in NDC (-1..1) with UVs (0..1). Same axis convention as
    // the FBO texture (y up), so the scene is displayed upright (no flip).
    const float verts[] = {
        // pos            uv
        -1.0f,  1.0f,    0.0f, 1.0f,   // top-left
        -1.0f, -1.0f,    0.0f, 0.0f,   // bottom-left
         1.0f, -1.0f,    1.0f, 0.0f,   // bottom-right

        -1.0f,  1.0f,    0.0f, 1.0f,   // top-left
         1.0f, -1.0f,    1.0f, 0.0f,   // bottom-right
         1.0f,  1.0f,    1.0f, 1.0f,   // top-right
    };

    postVbo = std::make_unique<engine::VertexBuffer>(verts, sizeof(verts));
    if (!postVbo || postVbo->getHandle() == 0) {
        fprintf(stderr, "Failed to create post vertex buffer\n");
        return false;
    }
    postVao = std::make_unique<engine::VertexArray>();
    if (!postVao || postVao->getHandle() == 0) {
        fprintf(stderr, "Failed to create post vertex array\n");
        return false;
    }

    const GLsizei stride = 4 * sizeof(float);
    postVao->bind();
    postVbo->bind();
    postVao->setVertexAttribute(0, 2, GL_FLOAT, 0, stride);
    postVao->setVertexAttribute(1, 2, GL_FLOAT, 2 * sizeof(float), stride);
    engine::VertexArray::unbind();
    engine::VertexBuffer::unbind();
    return true;
}

// (Re)create the offscreen target when missing or when the drawable size changed.
void AppWebGL::ensureFramebuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (fbo && fbW == w && fbH == h) return;
    fbo = std::make_unique<engine::Framebuffer>(w, h);
    fbW = w;
    fbH = h;
    if (!fbo->isComplete()) {
        fprintf(stderr, "Framebuffer incomplete at %dx%d\n", w, h);
    }
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

    if (!compileShaders() || !setupGeometry() ||
        !compilePostShader() || !setupPostGeometry()) {
        cleanup();
        return false;
    }

    renderer.setClearColor(0.1f, 0.2f, 0.5f, 1.0f);
    printf("WebGL2 (G2) initialized (render-to-texture pipeline).\n");
    return true;
}

void AppWebGL::tick() {
    process_input(running);
    g_camera.update(measure_delta_seconds());

    // Match the offscreen target + viewport to the CURRENT canvas size.
    // On the web the shell sets the canvas drawing buffer and tells us its size
    // via on_canvas_resize (g_canvas_w/h); SDL's window size is stale there.
    int w = 0, h = 0;
#ifdef __EMSCRIPTEN__
    w = g_canvas_w;
    h = g_canvas_h;
#else
    SDL_GL_GetDrawableSize(window, &w, &h);
#endif
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
    g_camera.set_viewport(w, h);   // keep aspect ratio in sync with the viewport
    ensureFramebuffer(w, h);

    // ---- PASS 1: generate one ray per pixel INTO the offscreen framebuffer ----
    // The camera reaches the shader as the inverse view-projection matrix.
    if (fbo) fbo->bind();
    glViewport(0, 0, fbW, fbH);
    renderer.clear();
    if (shader) {
        shader->use();
        glm::mat4 invVP = glm::inverse(g_camera.get_view_projection());
        shader->setUniform("inv_view_projection", invVP);
        shader->setUniform("uVolume", 0);   // sampler reads texture unit 0
    }
    if (volume) volume->bind(0);
    if (vao) renderer.draw(*vao, 6);  // fullscreen quad -> volume ray cast

    // ---- PASS 2: blit the ray-gen texture to the screen (tinted) ----
    engine::Framebuffer::bindDefault();
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (postShader && fbo && postVao) {
        postShader->use();
        postShader->setUniform("uScene", 0);
        postShader->setUniform("uTint", g_tint);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo->getColorTexture());
        postVao->bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        engine::VertexArray::unbind();
    }

    SDL_GL_SwapWindow(window);
}

void AppWebGL::cleanup() {
    fbo.reset();
    postShader.reset();
    postVao.reset();
    postVbo.reset();
    volume.reset();
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
        g_canvas_w = width;
        g_canvas_h = height;
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
