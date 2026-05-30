#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

namespace engine {

// Framebuffer: RAII offscreen render target = an FBO + a color-attachment texture.
//
// Normally drawing goes to the screen (the "default framebuffer", id 0). A
// Framebuffer lets you redirect drawing INTO a texture instead:
//   pass 1: fbo.bind();  draw scene      -> pixels land in getColorTexture()
//   pass 2: bindDefault(); draw fullscreen quad sampling that texture -> screen
//
// WHY IT MATTERS: this render-to-texture pattern is the foundation of
// post-processing, the "ping-pong" trick used for GPGPU in WebGL2, and the
// surface a volume ray caster ultimately writes its result into.
//
// This target has a color attachment only (no depth) — enough for 2D / full-
// screen work. A depth attachment can be added when 3D geometry needs it.
class Framebuffer {
public:
    Framebuffer(int width, int height);
    ~Framebuffer();

    void bind() const;            // subsequent draws render INTO this FBO
    static void bindDefault();    // back to the screen (framebuffer 0)

    GLuint getColorTexture() const { return colorTex_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    bool isComplete() const { return complete_; }

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

private:
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool complete_ = false;
};

}  // namespace engine
