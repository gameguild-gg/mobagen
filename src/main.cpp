#ifdef USE_WEBGPU

// ============================================================================
// WebGPU Path (Modern deferred rendering)
// ============================================================================

#include <emscripten.h>
#include <emscripten/webgpu.h>
#include <webgpu/webgpu.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <string>
#include <vector>

// WGSL Shaders (WebGPU Shading Language)
static constexpr const char* WGSL_SHADER = R"(
// Vertex shader
struct VertexInput {
  @location(0) position: vec2f,
};

struct VertexOutput {
  @builtin(position) position: vec4f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
  var out: VertexOutput;
  out.position = vec4f(in.position, 0.0, 1.0);
  return out;
}

// Fragment shader
@fragment
fn fs_main() -> @location(0) vec4f {
  return vec4f(0.0, 1.0, 0.5, 1.0);
}
)";

struct AppWebGPU {
    SDL_Window* window = nullptr;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPURenderPipeline pipeline;
    WGPUBuffer vertexBuffer;
    WGPUTextureFormat surfaceFormat;
    bool running = true;

    bool init();
    void tick();
    void cleanup();

private:
    bool createSurface();
    bool createDevice();
    bool createPipeline();
    bool uploadGeometry();
};

bool AppWebGPU::createSurface() {
    // Get WebGPU surface from Emscripten canvas
    surface = emscripten_webgpu_get_surface("canvas");
    if (!surface) {
        fprintf(stderr, "Failed to create WebGPU surface\n");
        return false;
    }
    return true;
}

bool AppWebGPU::createDevice() {
    // Request adapter (GPU)
    wgpuInstanceRequestAdapter(
        nullptr,
        nullptr,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
            auto* self = static_cast<AppWebGPU*>(userdata);
            if (status != WGPURequestAdapterStatus_Success) {
                fprintf(stderr, "Failed to request adapter: %s\n", message);
                return;
            }
            self->adapter = adapter;
        },
        this
    );

    if (!adapter) {
        fprintf(stderr, "Adapter is null\n");
        return false;
    }

    // Request device
    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.label = "Main Device";

    wgpuAdapterRequestDevice(
        adapter,
        &deviceDesc,
        [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
            auto* self = static_cast<AppWebGPU*>(userdata);
            if (status != WGPURequestDeviceStatus_Success) {
                fprintf(stderr, "Failed to request device: %s\n", message);
                return;
            }
            self->device = device;
            self->queue = wgpuDeviceGetQueue(device);
        },
        this
    );

    if (!device) {
        fprintf(stderr, "Device is null\n");
        return false;
    }

    // Get surface format
    surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
    return true;
}

bool AppWebGPU::createPipeline() {
    if (!device || !queue) {
        fprintf(stderr, "Device or queue is null\n");
        return false;
    }

    // Compile shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = WGSL_SHADER;

    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) {
        fprintf(stderr, "Failed to create shader module\n");
        return false;
    }

    // Vertex buffer layout
    WGPUVertexBufferLayout vertexLayout{};
    WGPUVertexAttribute vertexAttribute{};
    vertexAttribute.shaderLocation = 0;
    vertexAttribute.offset = 0;
    vertexAttribute.format = WGPUVertexFormat_Float32x2;

    vertexLayout.arrayStride = 8;  // 2 floats × 4 bytes
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &vertexAttribute;

    // Render pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.label = "Pipeline Layout";
    pipelineLayoutDesc.bindGroupLayoutCount = 0;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Fragment state
    WGPUColorTargetState colorTarget{};
    colorTarget.format = surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.label = "Render Pipeline";
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.fragment = &fragmentState;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    return pipeline != nullptr;
}

bool AppWebGPU::uploadGeometry() {
    // Triangle vertices (NDC)
    float vertices[] = {
        0.0f,  0.5f,    // Top
       -0.5f, -0.5f,    // Bottom-left
        0.5f, -0.5f     // Bottom-right
    };

    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.size = sizeof(vertices);
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bufferDesc.mappedAtCreation = true;

    vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    if (!vertexBuffer) {
        fprintf(stderr, "Failed to create vertex buffer\n");
        return false;
    }

    // Copy data to buffer
    void* bufferPtr = wgpuBufferGetMappedRange(vertexBuffer, 0, sizeof(vertices));
    memcpy(bufferPtr, vertices, sizeof(vertices));
    wgpuBufferUnmap(vertexBuffer);

    return true;
}

bool AppWebGPU::init() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Create window
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

    // Initialize WebGPU
    if (!createSurface()) return false;
    if (!createDevice()) return false;
    if (!createPipeline()) return false;
    if (!uploadGeometry()) return false;

    return true;
}

void AppWebGPU::tick() {
    if (!device || !queue || !pipeline) return;

    // Handle events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
        }
    }

    // Configure surface
    WGPUSurfaceConfiguration config{};
    config.device = device;
    config.format = surfaceFormat;
    config.width = 800;
    config.height = 600;
    config.presentMode = WGPUPresentMode_Fifo;

    wgpuSurfaceConfigure(surface, &config);

    // Get current texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        fprintf(stderr, "Failed to get surface texture\n");
        return;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc{};
    encoderDesc.label = "Command Encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = view;
    colorAttachment.clearValue = {0.1f, 0.2f, 0.5f, 1.0f};  // Blue
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor renderPassDesc{};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Draw triangle
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, 24);  // 3 vertices × 8 bytes
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Submit command buffer
    WGPUCommandBufferDescriptor cmdBufferDesc{};
    cmdBufferDesc.label = "Command Buffer";
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);

    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    // Present
    wgpuSurfacePresent(surface);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
}

void AppWebGPU::cleanup() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
    if (adapter) wgpuAdapterRelease(adapter);
    if (surface) wgpuSurfaceRelease(surface);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
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

#endif // USE_WEBGPU
