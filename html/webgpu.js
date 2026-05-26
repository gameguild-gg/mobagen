// WebGPU Initialization and Rendering

const WGSL_SHADER = `
struct VertexInput {
  @location(0) position: vec2f,
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
  return vec4f(in.position, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
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

async function initWebGPU() {
    if (window.webgpuState.initialized) {
        return true;
    }

    if (!navigator.gpu) {
        console.error("WebGPU not supported in this browser");
        return false;
    }

    try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            console.error("GPU adapter not found");
            return false;
        }

        const device = await adapter.requestDevice();
        window.webgpuState.device = device;
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

function renderFrame() {
    if (!window.webgpuState.device || !window.webgpuState.pipeline) {
        return;
    }

    const state = window.webgpuState;
    const device = state.device;
    const queue = state.queue;
    const context = state.context;

    const commandEncoder = device.createCommandEncoder();
    const textureView = context.getCurrentTexture().createView();

    const renderPass = commandEncoder.beginRenderPass({
        colorAttachments: [{
            view: textureView,
            clearValue: { r: 0.1, g: 0.2, b: 0.5, a: 1.0 },
            loadOp: 'clear',
            storeOp: 'store',
        }],
    });

    renderPass.setPipeline(state.pipeline);
    renderPass.setVertexBuffer(0, state.vertexBuffer);
    renderPass.draw(3, 1, 0, 0);
    renderPass.end();

    queue.submit([commandEncoder.finish()]);
}

// Initialize when page loads
window.addEventListener('DOMContentLoaded', () => {
    initWebGPU().then(success => {
        if (success) {
            // Start animation loop
            const animLoop = () => {
                renderFrame();
                requestAnimationFrame(animLoop);
            };
            requestAnimationFrame(animLoop);
        }
    });
});

// Export for C++
window.webgpu_render = renderFrame;
window.webgpu_init = initWebGPU;
