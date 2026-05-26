#include "vertex_array.h"
#include "vertex_buffer.h"

namespace engine {

VertexArray::VertexArray() {
    glGenVertexArrays(1, &handle_);
}

VertexArray::~VertexArray() {
    if (handle_ != 0) {
        glDeleteVertexArrays(1, &handle_);
    }
}

void VertexArray::bind() const {
    if (handle_ != 0) {
        glBindVertexArray(handle_);
    }
}

void VertexArray::unbind() {
    glBindVertexArray(0);
}

void VertexArray::setVertexAttribute(GLuint attribIndex,
                                      GLint componentCount,
                                      GLenum type,
                                      GLuint offsetBytes) {
    if (handle_ == 0) {
        return;  // VAO not valid
    }

    // Enable the attribute
    glEnableVertexAttribArray(attribIndex);

    // Tell GPU how to interpret this attribute's data
    // Parameters:
    // - index: Attribute location (0, 1, 2, etc)
    // - size: Components per vertex (2 for vec2, 3 for vec3)
    // - type: Data type (GL_FLOAT, GL_INT, etc)
    // - normalized: GL_FALSE = don't normalize (raw values)
    // - stride: Bytes between consecutive vertex attributes (0 = tightly packed)
    // - pointer: Byte offset within the buffer (cast as void* for GPU address)
    glVertexAttribPointer(
        attribIndex,
        componentCount,
        type,
        GL_FALSE,
        0,  // Stride: 0 means tightly packed (each vertex is just this attribute)
        reinterpret_cast<const void*>(static_cast<intptr_t>(offsetBytes))
    );
}

}  // namespace engine
