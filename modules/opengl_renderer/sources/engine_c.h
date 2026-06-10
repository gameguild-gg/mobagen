#pragma once

// ============================================================================
// DICOM Renderer: C API Boundary
// ============================================================================
//
// LEARNING GOAL: Understand how C++ libraries expose themselves to other languages
//
// WHY C API?
// - C has a stable ABI (Application Binary Interface)
// - Every language (Python, JavaScript, Rust, C#) can call C
// - C++ has no stable ABI (name mangling, vtable layout varies)
// - Solution: Wrap C++ in a thin C layer at module boundary
//
// PATTERN (Used by VTK, ITK, scientific libraries):
// 1. C++ classes are internal details (modules/opengl_renderer/sources/*.h/cpp)
// 2. C API is the public interface (this file)
// 3. Language bindings talk only to C API
// 4. C API handles C++ object lifetime (new/delete)
//
// EXAMPLE USAGE FROM PYTHON:
//   from ctypes import *
//   lib = cdll.LoadLibrary('./libdicom_renderer.so')
//   shader = lib.shader_program_create(vert_src.encode(), frag_src.encode())
//   lib.shader_program_use(shader)
//   lib.shader_program_destroy(shader)
//
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles: Client code doesn't see inside, just passes handles around
// This prevents accidental misuse (e.g., calling setUniform on a VertexBuffer)
typedef struct ShaderProgram ShaderProgram;
typedef struct VertexBuffer VertexBuffer;
typedef struct VertexArray VertexArray;
typedef struct Renderer Renderer;

// ============================================================================
// ShaderProgram C API
// ============================================================================

// Create shader program from source
// Parameters:
//   vert_source: Vertex shader source code (null-terminated string)
//   frag_source: Fragment shader source code (null-terminated string)
// Returns: Handle to shader program, or NULL if compilation failed
ShaderProgram* shader_program_create(const char* vert_source,
                                       const char* frag_source);

// Activate shader program for rendering
void shader_program_use(ShaderProgram* program);

// Set a uniform float value
void shader_program_set_uniform_float(ShaderProgram* program,
                                        const char* name,
                                        float value);

// Set a uniform vec3 value (3 floats)
void shader_program_set_uniform_vec3(ShaderProgram* program,
                                       const char* name,
                                       float x, float y, float z);

// Set a uniform mat4 value (16 floats, column-major)
void shader_program_set_uniform_mat4(ShaderProgram* program,
                                       const char* name,
                                       const float* data);

// Destroy shader program, free GPU resources
void shader_program_destroy(ShaderProgram* program);

// ============================================================================
// VertexBuffer C API
// ============================================================================

// Create and upload vertex data to GPU
// Parameters:
//   data: Pointer to vertex data (float array, positions, etc)
//   size_bytes: Number of bytes to copy
// Returns: Handle to vertex buffer, or NULL if allocation failed
VertexBuffer* vertex_buffer_create(const void* data, unsigned long size_bytes);

// Activate buffer for rendering
void vertex_buffer_bind(VertexBuffer* buffer);

// Destroy buffer, free GPU resources
void vertex_buffer_destroy(VertexBuffer* buffer);

// ============================================================================
// VertexArray C API
// ============================================================================

// Create vertex array (describes attribute layout)
VertexArray* vertex_array_create(void);

// Activate VAO for configuration or rendering
void vertex_array_bind(VertexArray* vao);

// Configure how GPU interprets vertex buffer data
// Parameters:
//   vao: Vertex array handle
//   attrib_index: Shader location (layout(location = N))
//   component_count: How many values per vertex (2 for vec2, 3 for vec3)
//   type: GL_FLOAT (4), GL_INT (5), GL_UNSIGNED_BYTE (0x1403), etc
//   offset_bytes: Where in vertex struct this attribute starts
void vertex_array_set_attribute(VertexArray* vao,
                                  unsigned int attrib_index,
                                  int component_count,
                                  unsigned int type,
                                  unsigned int offset_bytes);

// Destroy VAO, free GPU resources
void vertex_array_destroy(VertexArray* vao);

// ============================================================================
// Renderer C API
// ============================================================================

// Create renderer
Renderer* renderer_create(void);

// Set clear color (background)
void renderer_set_clear_color(Renderer* renderer,
                                float r, float g, float b, float a);

// Clear frame buffer
void renderer_clear(Renderer* renderer);

// Draw vertices
// Parameters:
//   renderer: Renderer handle
//   vao: Vertex array (contains buffer + attribute layout)
//   vertex_count: Number of vertices to draw (e.g., 3 for triangle)
void renderer_draw(Renderer* renderer, VertexArray* vao, int vertex_count);

// Set active shader program for subsequent draws
void renderer_set_shader(Renderer* renderer, ShaderProgram* shader);

// Destroy renderer
void renderer_destroy(Renderer* renderer);

// ============================================================================
// Note: GL constants (GL_FLOAT, GL_INT, etc) are defined in <GL/glew.h>
// and <GLES3/gl3.h>, so we don't redefine them here.
// ============================================================================

#ifdef __cplusplus
}  // extern "C"
#endif
