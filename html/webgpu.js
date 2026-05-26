// ============================================================================
// G3: WebGPU Deferred Rendering Implementation — Educational Code
// ============================================================================
//
// LEARNING GOAL: Understand modern GPU programming with WebGPU
//
// This file demonstrates:
// 1. Browser feature detection
// 2. Async GPU resource initialization
// 3. WGSL shader compilation
// 4. Deferred rendering pipeline (record commands, then submit batch)
// 5. Frame-based rendering loop
//
// KEY LEARNING POINTS:
//
// A. Deferred Rendering Pattern:
//    - Traditional OpenGL:
//      glUseProgram(); glDraw(); // GPU executes immediately
//
//    - Modern WebGPU:
//      encoder.setPipeline(...);
//      encoder.draw(...);
//      queue.submit([encoder.finish()]); // GPU executes batch
//
// B. Async JavaScript:
//    - GPU requests (adapter, device) are async
//    - Use async/await or .then() chains
//    - JavaScript continues while GPU initializes
//
// C. Shader Pipeline:
//    - WGSL: Modern, type-safe, GPU-compute capable
//    - Replaces GLSL for web and some desktop
//    - More structured than older shader languages
//
// ============================================================================

// BROWSER SUPPORT DETECTION
//
// WebGPU is an experimental API. Support varies by browser:
// - Firefox: Enabled by default
// - Chrome: Requires chrome://flags/#enable-unsafe-webgpu
// - Safari: Experimental, partial support
// - Edge: Same as Chrome (uses Chromium)
//
// This function demonstrates the feature detection pattern:
// Always check navigator.gpu before using WebGPU API
function detectWebGPUSupport() {
    if (!navigator.gpu) {
        return {
            supported: false,
            browser: detectBrowser(),
            message: "WebGPU not available in this browser"
        };
    }

    return {
        supported: true,
        browser: detectBrowser(),
        message: "WebGPU supported!"
    };
}

function detectBrowser() {
    const ua = navigator.userAgent;
    if (ua.includes('Chrome') && !ua.includes('Chromium')) {
        return 'Chrome';
    } else if (ua.includes('Firefox')) {
        return 'Firefox';
    } else if (ua.includes('Safari') && !ua.includes('Chrome')) {
        return 'Safari';
    } else if (ua.includes('Edge')) {
        return 'Edge';
    }
    return 'Unknown';
}

function createWebGPUWarningElement() {
    const support = detectWebGPUSupport();
    const container = document.body;

    const warningDiv = document.createElement('div');
    warningDiv.id = 'webgpu-warning';
    warningDiv.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        right: 0;
        padding: 15px;
        background: #1a1a1a;
        color: #fff;
        font-family: monospace;
        font-size: 13px;
        z-index: 10000;
        border-bottom: 2px solid #666;
        max-height: 120px;
        overflow-y: auto;
    `;

    if (support.supported) {
        warningDiv.style.background = '#0a3a0a';
        warningDiv.style.borderBottom = '2px solid #0f0';
        warningDiv.innerHTML = `
            <strong style="color: #0f0;">✓ WebGPU Available</strong><br>
            Browser: ${support.browser} | Status: ${support.message}
        `;
    } else {
        warningDiv.style.background = '#3a0a0a';
        warningDiv.style.borderBottom = '2px solid #f00';

        let instructions = '';
        if (support.browser === 'Chrome' || support.browser === 'Edge') {
            instructions = `
                <br><strong>To enable in ${support.browser}:</strong><br>
                1. Open: <code>chrome://flags/#enable-unsafe-webgpu</code><br>
                2. Set <code>Unsafe WebGPU</code> to <strong>Enabled</strong><br>
                3. Restart ${support.browser}
            `;
        } else if (support.browser === 'Safari') {
            instructions = `
                <br><strong>To enable in Safari:</strong><br>
                WebGPU support is experimental. Check preferences or upgrade Safari.
            `;
        } else {
            instructions = `
                <br><strong>Supported Browsers:</strong> Chrome 113+, Firefox, Edge 113+
            `;
        }

        warningDiv.innerHTML = `
            <strong style="color: #f00;">✗ WebGPU Not Detected</strong><br>
            Browser: ${support.browser} | Status: ${support.message}
            ${instructions}
        `;
    }

    container.insertBefore(warningDiv, container.firstChild);
}

// WGSL SHADERS (WebGPU Shading Language)
//
// LEARNING: Compare with GLSL ES 3.00:
//
// GLSL ES 3.00:
//   #version 300 es
//   layout(location = 0) in vec2 aPos;
//   void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
//
// WGSL:
//   struct VertexInput { @location(0) position: vec2f, };
//   @vertex fn vs_main(in: VertexInput) -> @builtin(position) vec4f { ... }
//
// WGSL advantages:
// - Type-safe (vec2f explicitly float32)
// - Structured data (structs instead of loose variables)
// - Compute-capable (@compute decorator for parallel work)
// - Modern syntax (similar to Rust)
//
// LEARNING: GPU Pipeline Stages
//
// 1. VERTEX STAGE (@vertex):
//    - Input: Vertex attributes (position, color, normal, etc)
//    - Output: Clip-space position + other data
//    - Runs once per vertex (parallel on GPU)
//    - Example: Transform vertex position to screen space
//
// 2. FRAGMENT STAGE (@fragment):
//    - Input: Data from vertex stage (interpolated per pixel)
//    - Output: Pixel color
//    - Runs once per pixel (massively parallel)
//    - Example: Sample texture, apply lighting
const WGSL_SHADER = `
struct VertexInput {
  @location(0) position: vec2f,  // Vertex position in normalized device coords
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
  // VERTEX SHADER: Transform vertices
  // Input: 2D position from vertex buffer
  // Output: 4D clip-space position (required for rasterization)
  // The z=0, w=1 tells GPU this is a 2D triangle at depth 0
  return vec4f(in.position, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
  // FRAGMENT SHADER: Color the pixels
  // Input: (none for this simple example)
  // Output: RGBA color at location(0) = render target 0
  // Color: (0.0, 1.0, 0.5, 1.0) = Teal
  // RGBA: (Red, Green, Blue, Alpha)
  return vec4f(0.0, 1.0, 0.5, 1.0);
}
`;

window.webgpuState = {
    device: null,
    queue: null,
    pipeline: null,
    vertexBuffer: null,
    canvas: document.getElementById('canvas'),
    context: null,
    initialized: false
};

// ASYNC GPU INITIALIZATION
//
// LEARNING: GPU resource initialization is asynchronous
//
// Why async?
// - GPU drivers run in OS kernels, not instantly available
// - User might have multiple GPUs, browser must request one
// - Permission/capability checks take time
// - JavaScript async/await is the right pattern
//
// GPU Resource Hierarchy:
// Instance → Adapter → Device → Queue
//
// Instance: Global WebGPU interface (navigator.gpu)
// Adapter: Represents one GPU in the system
// Device: Context for resource creation and command encoding
// Queue: Command submission point for execution
//
async function initWebGPU() {
    // Guard: only initialize once
    if (window.webgpuState.initialized) {
        return true;
    }

    // Feature detection (again, for safety)
    if (!navigator.gpu) {
        console.error("WebGPU not supported in this browser");
        return false;
    }

    try {
        // STEP 1: REQUEST ADAPTER
        // Asks the browser "What GPU should I use?"
        // Returns first available GPU (or specific one if requested)
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            console.error("GPU adapter not found");
            return false;
        }

        // STEP 2: REQUEST DEVICE
        // Creates a context for resource creation and command encoding
        // This is where we'll create buffers, pipelines, shaders
        const device = await adapter.requestDevice();
        window.webgpuState.device = device;

        // QUEUE: Command submission point
        // Submit encoded commands here for GPU execution
        window.webgpuState.queue = device.queue;

        const canvas = window.webgpuState.canvas;
        const context = canvas.getContext('webgpu');
        if (!context) {
            console.error("Could not get WebGPU context");
            return false;
        }

        const format = navigator.gpu.getPreferredCanvasFormat();
        context.configure({
            device: device,
            format: format,
        });
        window.webgpuState.context = context;
        window.webgpuState.format = format;

        // Compile shader
        const shader = device.createShaderModule({
            code: WGSL_SHADER
        });

        // Create vertex buffer
        const vertices = new Float32Array([
            0.0,  0.5,
            -0.5, -0.5,
            0.5, -0.5
        ]);

        const vertexBuffer = device.createBuffer({
            size: vertices.byteLength,
            usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
            mappedAtCreation: true,
        });
        new Float32Array(vertexBuffer.getMappedRange()).set(vertices);
        vertexBuffer.unmap();
        window.webgpuState.vertexBuffer = vertexBuffer;

        // Create pipeline
        const pipeline = device.createRenderPipeline({
            layout: 'auto',
            vertex: {
                module: shader,
                entryPoint: 'vs_main',
                buffers: [{
                    arrayStride: 8,
                    attributes: [{
                        shaderLocation: 0,
                        offset: 0,
                        format: 'float32x2',
                    }],
                }],
            },
            fragment: {
                module: shader,
                entryPoint: 'fs_main',
                targets: [{ format: format }],
            },
            primitive: { topology: 'triangle-list' },
        });
        window.webgpuState.pipeline = pipeline;
        window.webgpuState.initialized = true;
        console.log("WebGPU initialized successfully");
        return true;
    } catch (e) {
        console.error("WebGPU initialization error:", e);
        return false;
    }
}

// RENDER FRAME: DEFERRED RENDERING PIPELINE
//
// LEARNING: This demonstrates the modern GPU command pattern
//
// Steps:
// 1. Create command encoder (start recording)
// 2. Begin render pass (define render target)
// 3. Set pipeline (shader + render state)
// 4. Set geometry (vertex buffer)
// 5. Issue draw call (how many vertices to process)
// 6. End pass (finish recording)
// 7. Submit (GPU executes all at once)
//
// Compare to OpenGL immediate mode:
//   glUseProgram(...);          // Execute now
//   glBindBuffer(...);          // Execute now
//   glDrawArrays(3);            // Execute now, wait for GPU
//
// This approach:
//   [Record all commands]
//   queue.submit([batch])       // Execute all at once
//
function renderFrame() {
    // Guard: ensure we're initialized
    if (!window.webgpuState.device || !window.webgpuState.pipeline) {
        return;
    }

    const state = window.webgpuState;
    const device = state.device;
    const queue = state.queue;
    const context = state.context;

    // STEP 1: CREATE COMMAND ENCODER
    // This records GPU commands (doesn't execute yet)
    const commandEncoder = device.createCommandEncoder();

    // Get the canvas texture to render into
    const textureView = context.getCurrentTexture().createView();

    // STEP 2: BEGIN RENDER PASS
    // Define what we're rendering to (color attachment = framebuffer)
    const renderPass = commandEncoder.beginRenderPass({
        colorAttachments: [{
            view: textureView,                              // Render target
            clearValue: { r: 0.1, g: 0.2, b: 0.5, a: 1.0 }, // Clear color (cornflower blue)
            loadOp: 'clear',                                 // Clear before rendering
            storeOp: 'store',                                // Save result to texture
        }],
    });

    // STEP 3: SET PIPELINE
    // Select which shaders and render state to use
    // Pipeline bundles: vertex shader + fragment shader + blend state + etc
    renderPass.setPipeline(state.pipeline);

    // STEP 4: SET GEOMETRY
    // Tell GPU where the vertex data is
    // slot 0: where to find positions
    // state.vertexBuffer: contains the 3 vertices
    renderPass.setVertexBuffer(0, state.vertexBuffer);

    // STEP 5: DRAW CALL
    // Draw 3 vertices (our triangle)
    // draw(vertexCount, instanceCount, firstVertex, firstInstance)
    // = "Draw 3 vertices, 1 copy, starting at vertex 0, instance 0"
    renderPass.draw(3, 1, 0, 0);

    // STEP 6: END RENDER PASS
    // Stop recording render commands
    renderPass.end();

    // STEP 7: SUBMIT TO GPU
    // Everything above was recording. NOW we submit the batch.
    // GPU receives: [1. Clear screen, 2. Use pipeline, 3. Draw 3 vertices]
    // GPU executes efficiently because it sees the whole batch at once
    queue.submit([commandEncoder.finish()]);
}

// Initialize when page loads
window.addEventListener('DOMContentLoaded', () => {
    // Show WebGPU support status
    createWebGPUWarningElement();

    // Adjust canvas margin if warning is shown
    const canvas = document.getElementById('canvas');
    if (canvas) {
        canvas.style.marginTop = '80px';
    }

    const support = detectWebGPUSupport();
    console.log(`WebGPU Support: ${support.supported ? '✓' : '✗'} (${support.browser})`);
    console.log(`Message: ${support.message}`);

    if (!support.supported) {
        console.warn('WebGPU is not available. Rendering will not work.');
        console.warn(`Browser: ${support.browser}`);
        if (support.browser === 'Chrome' || support.browser === 'Edge') {
            console.warn(`Enable it at: chrome://flags/#enable-unsafe-webgpu`);
        }
        return;
    }

    initWebGPU().then(success => {
        if (success) {
            console.log('WebGPU initialized successfully');
            // Start animation loop
            const animLoop = () => {
                renderFrame();
                requestAnimationFrame(animLoop);
            };
            requestAnimationFrame(animLoop);
        } else {
            console.error('Failed to initialize WebGPU');
        }
    }).catch(e => {
        console.error('WebGPU initialization error:', e);
    });
});

// Export for C++
window.webgpu_render = renderFrame;
window.webgpu_init = initWebGPU;
