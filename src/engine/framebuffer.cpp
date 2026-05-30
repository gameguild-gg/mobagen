#include "framebuffer.h"

namespace engine {

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height) {
    // 1) Create the color-attachment texture (no pixel data — the GPU fills it).
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2) Create the FBO and attach the texture as color attachment 0.
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_, 0);

    complete_ = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);  // restore the default framebuffer
}

Framebuffer::~Framebuffer() {
    if (colorTex_ != 0) glDeleteTextures(1, &colorTex_);
    if (fbo_ != 0)      glDeleteFramebuffers(1, &fbo_);
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void Framebuffer::bindDefault() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace engine
