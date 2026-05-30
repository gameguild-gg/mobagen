#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// Texture3D: RAII wrapper around a single-channel 3D texture (R8) — a stack of
// voxels sampled by a 3D coordinate. This is where the volume lives: a synthetic
// blob now, a DICOM scan later. Same "fill on CPU, upload once, sample on GPU"
// pattern as Texture2D, with one more dimension.
class Texture3D {
public:
    // Create from tightly-packed single-byte voxels (w * h * d bytes, R8).
    Texture3D(int width, int height, int depth, const unsigned char* voxels);
    ~Texture3D();

    void bind(unsigned int unit = 0) const;
    static void unbind();

    GLuint getHandle() const { return handle_; }

    Texture3D(const Texture3D&) = delete;
    Texture3D& operator=(const Texture3D&) = delete;

private:
    GLuint handle_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0;
};

}  // namespace engine
