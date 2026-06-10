#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// Texture2D: thin RAII wrapper around a single GL 2D texture (RGBA8).
//
// DESIGN: one class = one GPU object — matches ShaderProgram / VertexBuffer /
// VertexArray. Constructor uploads pixels, destructor frees the GPU texture.
//
// WHY IT MATTERS: this is the 2D seed of the 3D texture used for DICOM volumes.
// The pattern is identical at every tier: fill pixel/voxel data, upload once,
// then sample it on the GPU by coordinate. A volume is just this with a depth.
class Texture2D {
public:
    // Create from tightly-packed RGBA8 pixels (width * height * 4 bytes).
    Texture2D(int width, int height, const unsigned char* rgbaPixels);
    ~Texture2D();

    // Bind to a texture unit (0 by default) so a sampler uniform can read it.
    void bind(unsigned int unit = 0) const;
    static void unbind();

    GLuint getHandle() const { return handle_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Owns a GPU resource: non-copyable.
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

private:
    GLuint handle_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace engine
