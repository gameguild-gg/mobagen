#pragma once

#include <string>
#include <glm/glm.hpp>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// ShaderProgram: Thin RAII wrapper around OpenGL shader program
//
// DESIGN PHILOSOPHY (Magnum-style):
// - One class = one GPU object (the shader program)
// - Constructor acquires GPU resource (glCreateProgram)
// - Destructor releases it (glDeleteProgram)
// - Interface provides only essential operations (use, setUniform)
// - No state bundling — just the program handle
//
// WHY THIS DESIGN?
// - Educational: Shows how to encapsulate GL details behind a clean interface
// - Reusable: Can use from multiple languages (Python, JavaScript, Rust) via C API
// - Testable: No scattered GL calls throughout codebase
// - Backend-agnostic: To switch to Vulkan, only change this class
//
// LEARNING: This is RAII (Resource Acquisition Is Initialization)
// - Acquiring the resource (GPU program) happens in the constructor
// - Releasing it (glDeleteProgram) happens in the destructor
// - If you create this object, cleanup is automatic when scope ends
//
class ShaderProgram {
public:
    // Constructor: Compile vertex and fragment shaders, link them into a program
    //
    // What happens:
    // 1. Create vertex shader object
    // 2. Upload source code
    // 3. Compile it (with error checking)
    // 4. Create fragment shader object
    // 5. Upload source code
    // 6. Compile it (with error checking)
    // 7. Create program object
    // 8. Attach both shaders
    // 9. Link them together
    // 10. Delete individual shader objects (no longer needed after linking)
    //
    // If any step fails, returns false and sets error message in errmsg
    ShaderProgram(const std::string& vertSource,
                  const std::string& fragSource,
                  std::string* errmsg = nullptr);

    // Destructor: Clean up GPU resources
    // Calls glDeleteProgram(handle_), freeing GPU memory
    ~ShaderProgram();

    // Activate this shader program for rendering
    // Equivalent to glUseProgram(handle_)
    void use() const;

    // Set a uniform int value. Also used to bind a sampler to a texture unit:
    // Example: shader.setUniform("uTex", 0)  // sampler reads texture unit 0
    void setUniform(const std::string& name, int value) const;

    // Set a uniform float value in the shader
    // Example: shader.setUniform("brightness", 1.5f)
    void setUniform(const std::string& name, float value) const;

    // Set a uniform vec2 value in the shader
    void setUniform(const std::string& name, const glm::vec2& value) const;

    // Set a uniform vec3 value in the shader
    void setUniform(const std::string& name, const glm::vec3& value) const;

    // Set a uniform vec4 value in the shader
    void setUniform(const std::string& name, const glm::vec4& value) const;

    // Set a uniform 4x4 matrix in the shader
    // Example: shader.setUniform("projection", projectionMatrix)
    void setUniform(const std::string& name, const glm::mat4& value) const;

    // Get the native GL handle (for advanced users)
    // Avoid using this directly — it breaks encapsulation
    GLuint getHandle() const { return handle_; }

    // Check if the program compiled and linked successfully
    bool isValid() const { return handle_ != 0; }

private:
    GLuint handle_ = 0;  // OpenGL program object ID

    // Helper: Compile a shader and return its handle
    // Returns 0 if compilation failed (check errmsg for details)
    static GLuint compileShader(GLenum type,
                                const std::string& source,
                                std::string* errmsg);

    // Helper: Get the location of a uniform variable in the program
    GLint getUniformLocation(const std::string& name) const;
};

}  // namespace engine
