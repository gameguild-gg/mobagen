#pragma once

#include <vector>
#include <cstddef>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// VertexBuffer: Thin RAII wrapper around OpenGL vertex buffer object (VBO)
//
// DESIGN PHILOSOPHY:
// - One class = one GPU object (the VBO)
// - Store vertex data in GPU memory for fast rendering
// - Data is immutable after creation (STATIC_DRAW)
// - Interface provides upload, bind, and vertex count
//
// WHY THIS DESIGN?
// - Encapsulates GPU buffer lifetime management
// - Prevents scattered glGenBuffers/glBindBuffer/glBufferData calls
// - RAII: GPU memory freed automatically on destruction
// - Reusable through language bindings (C API)
//
// LEARNING: GPU Memory Hierarchy
// - CPU RAM (std::vector): Slow for GPU, must copy to GPU
// - GPU VRAM (VBO): Fast for GPU, persistent until freed
// - Ring buffer pattern (advanced): Reuse VBO for streaming data
// Currently using simple approach: allocate once, never update
//
class VertexBuffer {
public:
    // Constructor: Upload vertex data to GPU memory
    //
    // What happens:
    // 1. glGenBuffers() - Request GPU memory from driver
    // 2. glBindBuffer() - Activate this buffer for writing
    // 3. glBufferData() - Copy data from RAM to GPU VRAM
    // 4. glBindBuffer(0) - Deactivate (leave in clean state)
    //
    // Parameters:
    // - data: Pointer to vertex data (e.g., float array of positions)
    // - sizeBytes: Number of bytes to copy (e.g., 3 floats × 4 bytes = 12 bytes per vertex)
    //
    // Why copy data? GPU has its own memory (VRAM). Rendering reads from there,
    // not from CPU RAM. This one-time copy is worth the speed gains.
    VertexBuffer(const void* data, std::size_t sizeBytes);

    // Destructor: Free GPU memory
    // Calls glDeleteBuffers(), returning memory to driver
    ~VertexBuffer();

    // Activate this buffer for rendering
    // Equivalent to glBindBuffer(GL_ARRAY_BUFFER, handle_)
    // Call this before issuing draw calls that use this data
    void bind() const;

    // Deactivate all vertex buffers
    // Equivalent to glBindBuffer(GL_ARRAY_BUFFER, 0)
    // Optional - leave last binding active if convenient
    static void unbind();

    // Get the native GL handle (for advanced users)
    // Avoid using this directly — it breaks encapsulation
    GLuint getHandle() const { return handle_; }

    // Get the size of buffer data in bytes
    std::size_t getSizeBytes() const { return sizeBytes_; }

private:
    GLuint handle_ = 0;           // OpenGL VBO object ID
    std::size_t sizeBytes_ = 0;   // Size of buffer data in bytes
};

}  // namespace engine
