#include "texture.h"

namespace engine {

Texture2D::Texture2D(int width, int height, const unsigned char* rgbaPixels)
    : width_(width), height_(height) {
    glGenTextures(1, &handle_);
    glBindTexture(GL_TEXTURE_2D, handle_);

    // Sampling + wrapping: linear filtering, clamp to edge.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload pixels. GL_RGBA8 internal format is valid on both GL 3.3 core
    // (native) and GLES3 / WebGL2 (browser).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D() {
    if (handle_ != 0) {
        glDeleteTextures(1, &handle_);
    }
}

void Texture2D::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, handle_);
}

void Texture2D::unbind() {
    glBindTexture(GL_TEXTURE_2D, 0);
}

}  // namespace engine
