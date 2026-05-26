#include "renderer.h"
#include "shader_program.h"
#include "vertex_array.h"

namespace engine {

void Renderer::setClearColor(float r, float g, float b, float a) {
    clearColor_[0] = r;
    clearColor_[1] = g;
    clearColor_[2] = b;
    clearColor_[3] = a;
    glClearColor(r, g, b, a);
}

void Renderer::clear() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::draw(const VertexArray& vertexArray, GLsizei vertexCount) {
    if (!currentShader_) {
        return;  // Can't draw without a shader
    }

    currentShader_->use();
    vertexArray.bind();
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void Renderer::setShaderProgram(ShaderProgram* program) {
    currentShader_ = program;
}

}  // namespace engine
