// Prevent SDL2 from redefining main() to SDL_main()
// We want to control the entry point ourselves
#define SDL_MAIN_HANDLED

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

// ============================================================================
// RUNTIME RENDERER SELECTION (Educational)
// ============================================================================
//
// LEARNING: This demonstrates how to abstract different rendering backends
// and switch between them at runtime.
//
// Architecture:
// - RendererType enum: Select between WebGL (G2) and WebGPU (G3)
// - Separate app instances: Each renderer has its own state
// - Runtime switcher: JavaScript calls set_renderer() to switch
// - Unified tick: Calls the appropriate renderer each frame

enum class RendererType {
    RENDERER_WEBGL = 0,
    RENDERER_WEBGPU = 1
};

enum class ShaderVariant {
    VARIANT_TEAL = 1,    // (0.0, 1.0, 0.5, 1.0)
    VARIANT_RED = 2,     // (1.0, 0.0, 0.0, 1.0)
    VARIANT_GREEN = 3,   // (0.0, 1.0, 0.0, 1.0)
    VARIANT_YELLOW = 4   // (1.0, 1.0, 0.0, 1.0)
};

static RendererType g_active_renderer = RendererType::RENDERER_WEBGL;
static ShaderVariant g_active_shader = ShaderVariant::VARIANT_TEAL;
static bool g_renderer_switching = false;
static bool g_shader_recompile = false;

// Forward declarations
class AppWebGL;
class AppWebGPU;
static AppWebGL* g_app_webgl = nullptr;
static AppWebGPU* g_app_webgpu = nullptr;
static bool g_webgl_initialized = false;
static bool g_webgpu_initialized = false;

// ============================================================================
// EXPORTED FUNCTIONS FOR JAVASCRIPT
// ============================================================================

// Helper: Get shader color based on variant
struct Color {
    float r, g, b, a;
};

static Color get_shader_color(ShaderVariant variant) {
    switch (variant) {
        case ShaderVariant::VARIANT_TEAL:   return {0.0f, 1.0f, 0.5f, 1.0f};
        case ShaderVariant::VARIANT_RED:    return {1.0f, 0.0f, 0.0f, 1.0f};
        case ShaderVariant::VARIANT_GREEN:  return {0.0f, 1.0f, 0.0f, 1.0f};
        case ShaderVariant::VARIANT_YELLOW: return {1.0f, 1.0f, 0.0f, 1.0f};
        default: return {0.0f, 1.0f, 0.5f, 1.0f};
    }
}

static const char* get_shader_name(ShaderVariant variant) {
    switch (variant) {
        case ShaderVariant::VARIANT_TEAL:   return "Teal";
        case ShaderVariant::VARIANT_RED:    return "Red";
        case ShaderVariant::VARIANT_GREEN:  return "Green";
        case ShaderVariant::VARIANT_YELLOW: return "Yellow";
        default: return "Unknown";
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void set_renderer(const char* renderer_name) {
        printf("Switching renderer to: %s\n", renderer_name);
        if (strcmp(renderer_name, "webgl") == 0) {
            g_active_renderer = RendererType::RENDERER_WEBGL;
            g_renderer_switching = true;
        } else if (strcmp(renderer_name, "webgpu") == 0) {
            g_active_renderer = RendererType::RENDERER_WEBGPU;
            g_renderer_switching = true;
        }
    }

    EMSCRIPTEN_KEEPALIVE
    const char* get_renderer() {
        return (g_active_renderer == RendererType::RENDERER_WEBGL) ? "webgl" : "webgpu";
    }

    EMSCRIPTEN_KEEPALIVE
    const char* get_available_renderers() {
        return "webgl,webgpu";
    }

    EMSCRIPTEN_KEEPALIVE
    void set_shader_variant(int variant_num) {
        if (variant_num >= 1 && variant_num <= 4) {
            g_active_shader = static_cast<ShaderVariant>(variant_num);
            g_shader_recompile = true;
            Color c = get_shader_color(g_active_shader);
            printf("Switching shader to: %s (RGB: %.1f, %.1f, %.1f)\n",
                   get_shader_name(g_active_shader), c.r, c.g, c.b);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    int get_shader_variant() {
        return static_cast<int>(g_active_shader);
    }

    EMSCRIPTEN_KEEPALIVE
    const char* get_available_shaders() {
        return "1:Teal,2:Red,3:Green,4:Yellow";
    }
}
#endif

// ============================================================================
// G2: OpenGL Path (Immediate-mode rendering)
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

#include <cstdint>
#include <memory>
#include "engine/shader_program.h"
#include "engine/vertex_buffer.h"
#include "engine/vertex_array.h"

struct AppWebGL {
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

bool AppWebGL::compileShaders() {
    Color c = get_shader_color(g_active_shader);

    std::string vertSrc = std::string(VERT_GLSL) + R"(
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

    // Fragment shader with dynamic color
    char fragColorStr[128];
    snprintf(fragColorStr, sizeof(fragColorStr),
             "out vec4 fragColor;\n\nvoid main() {\n    fragColor = vec4(%.1f, %.1f, %.1f, %.1f);\n}\n",
             c.r, c.g, c.b, c.a);

    std::string fragSrc = std::string(FRAG_GLSL) + fragColorStr;

    std::string errmsg;
    shader = std::make_unique<engine::ShaderProgram>(vertSrc, fragSrc, &errmsg);

    if (!shader->isValid()) {
        fprintf(stderr, "Shader compilation failed: %s\n", errmsg.c_str());
        shader.reset();
        return false;
    }

    return true;
}

bool AppWebGL::setupGeometry() {
    // G2 geometry: Triangle (3 vertices)
    // Positioned in normalized device coordinates (-1 to 1)
    float vertices[] = {
        0.0f,  0.5f,    // Top
        -0.5f, -0.5f,   // Bottom-left
        0.5f, -0.5f     // Bottom-right
    };

    vbo = std::make_unique<engine::VertexBuffer>(vertices, sizeof(vertices));
    if (!vbo || vbo->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex buffer\n");
        return false;
    }

    vao = std::make_unique<engine::VertexArray>();
    if (!vao || vao->getHandle() == 0) {
        fprintf(stderr, "Failed to create vertex array\n");
        return false;
    }

    vao->bind();
    vbo->bind();
    vao->setVertexAttribute(0, 2, GL_FLOAT, 0);
    engine::VertexArray::unbind();
    engine::VertexBuffer::unbind();

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

    printf("WebGL (G2) initialized successfully\n");
    return true;
}

void AppWebGL::tick() {
    // Recompile shader if variant changed
    if (g_shader_recompile) {
        g_shader_recompile = false;
        compileShaders();
    }

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

void AppWebGL::cleanup() {
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

// ============================================================================
// G3: WebGPU Path (Deferred rendering)
// ============================================================================
//
// LEARNING: WebGPU rendering is implemented in JavaScript (in html/shell.html)
// This AppWebGPU class just manages the SDL window and calls window.webgpu_render()
// each frame. The actual geometry (quad) and shader pipeline are defined in JS.
//
// Visual difference from G2: WebGPU renders a QUAD instead of a triangle.
// This makes renderer switching visually obvious during learning.

struct AppWebGPU {
    SDL_Window* window = nullptr;
    bool running = true;

    bool init();
    void tick();
    void cleanup();
};

bool AppWebGPU::init() {
    printf("WebGPU (G3) initialization via JavaScript...\n");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "DICOM Renderer (WebGPU)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    return true;
}

void AppWebGPU::tick() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
            running = false;
        }
    }

    emscripten_run_script("if(window.webgpu_render) window.webgpu_render();");
}

void AppWebGPU::cleanup() {
    printf("WebGPU cleanup\n");
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

// ============================================================================
// UNIFIED TICK FUNCTION
// ============================================================================

#ifdef __EMSCRIPTEN__
void em_unified_tick() {
    // Handle renderer switching
    if (g_renderer_switching) {
        g_renderer_switching = false;
        printf("Renderer switch requested\n");
    }

    // Tick the active renderer
    if (g_active_renderer == RendererType::RENDERER_WEBGL) {
        if (g_app_webgl && g_app_webgl->running) {
            g_app_webgl->tick();
        }
    } else if (g_active_renderer == RendererType::RENDERER_WEBGPU) {
        if (g_app_webgpu && g_app_webgpu->running) {
            g_app_webgpu->tick();
        }
    }

    // Check if either renderer is still running
    bool any_running = false;
    if (g_app_webgl) any_running |= g_app_webgl->running;
    if (g_app_webgpu) any_running |= g_app_webgpu->running;

    if (!any_running) {
        emscripten_cancel_main_loop();
    }
}
#endif

// ============================================================================
// MAIN
// ============================================================================

int main() {
    printf("====================================\n");
    printf("DICOM Renderer - Runtime Renderer Selection\n");
    printf("====================================\n");

#ifdef __EMSCRIPTEN__
    // Initialize both renderers
    printf("\nInitializing renderers...\n");

    g_app_webgl = new AppWebGL();
    if (!g_app_webgl->init()) {
        fprintf(stderr, "Failed to initialize WebGL\n");
        delete g_app_webgl;
        g_app_webgl = nullptr;
    } else {
        g_webgl_initialized = true;
    }

    g_app_webgpu = new AppWebGPU();
    if (!g_app_webgpu->init()) {
        fprintf(stderr, "Failed to initialize WebGPU\n");
        delete g_app_webgpu;
        g_app_webgpu = nullptr;
    } else {
        g_webgpu_initialized = true;
    }

    if (!g_webgl_initialized && !g_webgpu_initialized) {
        fprintf(stderr, "Failed to initialize any renderer\n");
        return 1;
    }

    printf("✓ Available renderers: ");
    if (g_webgl_initialized) printf("WebGL ");
    if (g_webgpu_initialized) printf("WebGPU ");
    printf("\n");

    printf("✓ Active renderer: %s\n", get_renderer());
    printf("\nStarting render loop...\n");
    printf("Use set_renderer('webgl') or set_renderer('webgpu') to switch\n\n");

    emscripten_set_main_loop(em_unified_tick, 0, 1);
#else
    // Native build: Use WebGL only
    AppWebGL app;
    if (!app.init()) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }

    while (app.running) {
        app.tick();
    }
    app.cleanup();
#endif

    return 0;
}
