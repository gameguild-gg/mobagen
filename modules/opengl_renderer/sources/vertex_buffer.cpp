#include "vertex_buffer.h"

namespace engine {

VertexBuffer::VertexBuffer(const void* data, std::size_t sizeBytes)
    : sizeBytes_(sizeBytes) {
    if (!data || sizeBytes == 0) {
        return;  // Invalid input, leave handle_ as 0
    }

    // Request one buffer from GPU
    glGenBuffers(1, &handle_);

    if (handle_ == 0) {
        return;  // GPU allocation failed
    }

    // Make this buffer the active target for buffer operations
    glBindBuffer(GL_ARRAY_BUFFER, handle_);

    // Copy data from CPU RAM to GPU VRAM
    // GL_STATIC_DRAW means: data is set once, used many times
    // Good for meshes that never change (optimal for GPU optimization)
    glBufferData(GL_ARRAY_BUFFER, sizeBytes, data, GL_STATIC_DRAW);

    // Clean up: deactivate the buffer
    // Good practice to leave GL state clean
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

VertexBuffer::~VertexBuffer() {
    if (handle_ != 0) {
        glDeleteBuffers(1, &handle_);
    }
}

void VertexBuffer::bind() const {
    if (handle_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, handle_);
    }
}

void VertexBuffer::unbind() {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace engine
