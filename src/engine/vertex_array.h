#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// Forward declaration
class VertexBuffer;

// VertexArray: Thin RAII wrapper around OpenGL vertex array object (VAO)
//
// DESIGN PHILOSOPHY:
// - One class = one GPU object (the VAO)
// - VAO is a "state capture" object: records which buffers are bound
// - VAO + VertexBuffer + ShaderProgram = ready to render
//
// WHY THIS DESIGN?
// - VAO eliminates bind/unbind boilerplate in render loop
// - Instead of: bind buffer, set vertex attrib pointer, then draw
// - We do: VAO remembers the setup, just call bind/draw
// - Modern OpenGL requirement: can't use shaders without a VAO
//
// LEARNING: GPU Object State
// - VAO is not data (no vertex positions)
// - VAO is metadata: "Which buffer? What format? Which attributes?"
// - Example: Triangle has 3 vertices, each with 2D position (8 bytes)
//   VAO says: "Slot 0 reads 2 floats from VBO, offset 0, stride 8"
//
// VERTEX ATTRIBUTE LAYOUT:
// Each vertex can have multiple attributes (position, color, normal, etc)
// Example layout for a 2D position:
//   struct Vertex { float x, y; };  // 8 bytes total
//   layout(location = 0) in vec2 aPos;  // Read from attribute 0
//
// Method call:
//   vao.setVertexAttribute(
//       0,           // Attribute location (matches shader layout)
//       2,           // Components (2 for vec2)
//       GL_FLOAT,    // Type
//       0            // Offset in bytes from start of vertex
//   );
//
class VertexArray {
public:
    // Constructor: Create an empty VAO
    // Device immediately issues glGenVertexArrays()
    VertexArray();

    // Destructor: Delete the VAO
    ~VertexArray();

    // Activate this VAO for configuration or rendering
    // Calls glBindVertexArray(handle_)
    // After binding, you can:
    // 1. Configure attributes: setVertexAttribute()
    // 2. Bind a vertex buffer: vertex_buffer.bind()
    // 3. Issue draw calls: glDrawArrays()
    void bind() const;

    // Deactivate all VAOs
    // Equivalent to glBindVertexArray(0)
    static void unbind();

    // Configure a vertex attribute
    //
    // This tells GPU how to interpret data in the vertex buffer
    //
    // Parameters:
    // - attribIndex: Shader location (layout(location = N) in vec2 aPos;)
    // - componentCount: How many values per vertex (2 for vec2, 3 for vec3)
    // - type: GL_FLOAT, GL_INT, GL_UNSIGNED_BYTE, etc
    // - offsetBytes: Where in the vertex struct this attribute starts
    //
    // Example for triangle (2D position):
    //   struct Vertex { float x, y; };
    //   vao.setVertexAttribute(0, 2, GL_FLOAT, 0);
    //   GPU will read: vertex[i].x (offset 0), vertex[i].y (offset 4)
    //
    // Example for more complex vertex:
    //   struct Vertex {
    //       float x, y;              // Position (offset 0, 8 bytes)
    //       uint8_t r, g, b, a;      // Color (offset 8, 4 bytes)
    //   };
    //   vao.setVertexAttribute(0, 2, GL_FLOAT, 0);        // Position
    //   vao.setVertexAttribute(1, 4, GL_UNSIGNED_BYTE, 8); // Color
    //
    void setVertexAttribute(GLuint attribIndex,
                            GLint componentCount,
                            GLenum type,
                            GLuint offsetBytes);

    // Get the native GL handle
    GLuint getHandle() const { return handle_; }

private:
    GLuint handle_ = 0;  // OpenGL VAO object ID
};

}  // namespace engine
