#include "texture3d.h"

namespace engine {

Texture3D::Texture3D(int width, int height, int depth, const unsigned char* voxels)
    : width_(width), height_(height), depth_(depth) {
    glGenTextures(1, &handle_);
    glBindTexture(GL_TEXTURE_3D, handle_);

    // Trilinear filtering, clamp on all three axes.
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Single channel (R8). Valid on GL 3.3 core and GLES3 / WebGL2.
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, width, height, depth, 0,
                 GL_RED, GL_UNSIGNED_BYTE, voxels);

    glBindTexture(GL_TEXTURE_3D, 0);
}

Texture3D::~Texture3D() {
    if (handle_ != 0) glDeleteTextures(1, &handle_);
}

void Texture3D::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_3D, handle_);
}

void Texture3D::unbind() {
    glBindTexture(GL_TEXTURE_3D, 0);
}

}  // namespace engine
