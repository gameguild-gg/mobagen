#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// 16-bit volume formats (WebGL2 has no plain unorm-R16, so it's these two):
//   R16F  = half-float; HARDWARE trilinear (smooth); ~11-bit mantissa (CT-grade).
//   R16UI = exact integer; NEAREST only (manual trilinear in shader); medically exact.
enum class VolumeFormat { R16F, R16UI };

// Texture3D: RAII wrapper around a single-channel 3D texture — a stack of voxels
// sampled by a 3D coordinate. R8 for the synthetic blob; 16-bit (R16F/R16UI) for a
// DICOM scan. Same "fill on CPU, upload once, sample on GPU" pattern, one more dim.
class Texture3D {
public:
    // Tightly-packed single-byte voxels (w * h * d bytes, R8) — the synthetic path.
    Texture3D(int width, int height, int depth, const unsigned char* voxels);
    // 16-bit voxels (DICOM): stored as R16F (filtered) or R16UI (exact).
    Texture3D(int width, int height, int depth, const std::uint16_t* voxels, VolumeFormat fmt);
    ~Texture3D();

    void bind(unsigned int unit = 0) const;
    static void unbind();

    GLuint getHandle() const { return handle_; }
    VolumeFormat format() const { return format_; }   // meaningful for the 16-bit ctor

    Texture3D(const Texture3D&) = delete;
    Texture3D& operator=(const Texture3D&) = delete;

private:
    GLuint handle_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0;
    VolumeFormat format_ = VolumeFormat::R16F;
};

}  // namespace engine
