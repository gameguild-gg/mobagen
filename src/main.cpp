#if defined(USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ============================================================================
// WebGPU Path (Modern deferred rendering) — Emscripten only
// ============================================================================

#include <emscripten.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

// WebGPU is initialized via JavaScript (html/webgpu.js)
// The shader and rendering pipeline are set up in JavaScript

// WebGPU is loaded from html/webgpu.js - JavaScript will set up window.webgpu_init and window.webgpu_render

struct AppWebGPU {
    SDL_Window* window = nullptr;
    bool running = true;

    bool init();
    void tick();
    void cleanup();
};

bool AppWebGPU::init() {
    printf("WebGPU initialization via JavaScript...\n");
    // JavaScript will auto-initialize on DOMContentLoaded
    // Just wait a moment for it to set up
    return true;
}

void AppWebGPU::tick() {
    // Handle keyboard events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
            running = false;
        }
    }

    // Call JavaScript render function
    emscripten_run_script("if(window.webgpu_render) window.webgpu_render();");
}

void AppWebGPU::cleanup() {
    printf("WebGPU cleanup\n");
}

static AppWebGPU* g_app_webgpu = nullptr;

#ifdef __EMSCRIPTEN__
static void em_tick_webgpu() {
    if (g_app_webgpu) {
        g_app_webgpu->tick();
        if (!g_app_webgpu->running) {
            emscripten_cancel_main_loop();
        }
    }
}
#endif

int main() {
    AppWebGPU app;
    g_app_webgpu = &app;

    if (!app.init()) {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(em_tick_webgpu, 0, 1);
#else
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
// OpenGL Path (Immediate-mode rendering - original code)
// ============================================================================

#include <emscripten.h>
#include <SDL2/SDL.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <cstdio>
#include <string>

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
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint shader = 0;
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

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    const char* vertPtr = vertSrc.c_str();
    glShaderSource(vert, 1, &vertPtr, nullptr);
    glCompileShader(vert);

    int success;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vert, 512, nullptr, infoLog);
        fprintf(stderr, "Vertex shader compilation failed: %s\n", infoLog);
        glDeleteShader(vert);
        return false;
    }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragPtr = fragSrc.c_str();
    glShaderSource(frag, 1, &fragPtr, nullptr);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(frag, 512, nullptr, infoLog);
        fprintf(stderr, "Fragment shader compilation failed: %s\n", infoLog);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    shader = glCreateProgram();
    glAttachShader(shader, vert);
    glAttachShader(shader, frag);
    glLinkProgram(shader);

    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shader, 512, nullptr, infoLog);
        fprintf(stderr, "Program linking failed: %s\n", infoLog);
        glDeleteShader(vert);
        glDeleteShader(frag);
        glDeleteProgram(shader);
        shader = 0;
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

bool App::setupGeometry() {
    float vertices[] = {
        0.0f,  0.5f,
        -0.5f, -0.5f,
        0.5f, -0.5f
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

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
        glDeleteProgram(shader);
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

    glUseProgram(shader);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    SDL_GL_SwapWindow(window);
}

void App::cleanup() {
    if (shader) {
        glDeleteProgram(shader);
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
    }
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
