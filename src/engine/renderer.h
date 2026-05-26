#pragma once

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// Forward declarations
class ShaderProgram;
class VertexArray;

// Renderer: High-level rendering interface
//
// DESIGN PHILOSOPHY:
// - Encapsulates the full rendering pipeline
// - Takes abstractions (ShaderProgram, VertexArray) as input
// - Handles the full sequence: clear → bind → draw → swap
//
// WHY THIS CLASS?
// - Centralizes rendering logic (not scattered in tick())
// - Provides a clean interface for different rendering techniques
// - Prepares for advanced techniques (layers, post-processing, multiple passes)
// - Educational: Shows how modern renderers are structured
//
// USAGE PATTERN:
//   Renderer renderer;
//   renderer.setShaderProgram(myShader);
//   renderer.clear();
//   renderer.draw(myVertexArray, vertexCount);
//
class Renderer {
public:
    Renderer() = default;

    // Set clear color (background)
    // Parameters are normalized (0.0 to 1.0)
    // Example: renderer.setClearColor(0.1f, 0.2f, 0.5f, 1.0f);
    void setClearColor(float r, float g, float b, float a = 1.0f);

    // Clear the frame buffer
    // Fills entire screen with clear color
    void clear();

    // Draw using the given vertex array
    // Precondition: ShaderProgram must be set first via setShaderProgram()
    // Parameters:
    // - vertexArray: The VAO containing vertex buffer setup
    // - vertexCount: How many vertices to draw (e.g., 3 for a triangle)
    void draw(const VertexArray& vertexArray, GLsizei vertexCount);

    // Set shader program for subsequent draw calls
    void setShaderProgram(ShaderProgram* program);

private:
    ShaderProgram* currentShader_ = nullptr;

    // Cached clear color
    GLfloat clearColor_[4] = {0.1f, 0.2f, 0.5f, 1.0f};
};

}  // namespace engine
