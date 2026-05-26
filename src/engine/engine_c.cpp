#include "engine_c.h"
#include "shader_program.h"
#include "vertex_buffer.h"
#include "vertex_array.h"
#include "renderer.h"

// ============================================================================
// ShaderProgram C API Implementation
// ============================================================================

ShaderProgram* shader_program_create(const char* vert_source,
                                       const char* frag_source) {
    if (!vert_source || !frag_source) {
        return nullptr;
    }

    std::string errmsg;
    engine::ShaderProgram* prog = new engine::ShaderProgram(
        std::string(vert_source),
        std::string(frag_source),
        &errmsg
    );

    if (!prog->isValid()) {
        delete prog;
        return nullptr;
    }

    return reinterpret_cast<ShaderProgram*>(prog);
}

void shader_program_use(ShaderProgram* program) {
    if (!program) return;
    reinterpret_cast<engine::ShaderProgram*>(program)->use();
}

void shader_program_set_uniform_float(ShaderProgram* program,
                                        const char* name,
                                        float value) {
    if (!program || !name) return;
    reinterpret_cast<engine::ShaderProgram*>(program)->setUniform(
        std::string(name),
        value
    );
}

void shader_program_set_uniform_vec3(ShaderProgram* program,
                                       const char* name,
                                       float x, float y, float z) {
    if (!program || !name) return;
    reinterpret_cast<engine::ShaderProgram*>(program)->setUniform(
        std::string(name),
        glm::vec3(x, y, z)
    );
}

void shader_program_set_uniform_mat4(ShaderProgram* program,
                                       const char* name,
                                       const float* data) {
    if (!program || !name || !data) return;
    glm::mat4 mat(
        data[0], data[1], data[2], data[3],
        data[4], data[5], data[6], data[7],
        data[8], data[9], data[10], data[11],
        data[12], data[13], data[14], data[15]
    );
    reinterpret_cast<engine::ShaderProgram*>(program)->setUniform(
        std::string(name),
        mat
    );
}

void shader_program_destroy(ShaderProgram* program) {
    if (!program) return;
    delete reinterpret_cast<engine::ShaderProgram*>(program);
}

// ============================================================================
// VertexBuffer C API Implementation
// ============================================================================

VertexBuffer* vertex_buffer_create(const void* data, unsigned long size_bytes) {
    if (!data || size_bytes == 0) {
        return nullptr;
    }

    engine::VertexBuffer* buf = new engine::VertexBuffer(data, size_bytes);
    if (buf->getHandle() == 0) {
        delete buf;
        return nullptr;
    }

    return reinterpret_cast<VertexBuffer*>(buf);
}

void vertex_buffer_bind(VertexBuffer* buffer) {
    if (!buffer) return;
    reinterpret_cast<engine::VertexBuffer*>(buffer)->bind();
}

void vertex_buffer_destroy(VertexBuffer* buffer) {
    if (!buffer) return;
    delete reinterpret_cast<engine::VertexBuffer*>(buffer);
}

// ============================================================================
// VertexArray C API Implementation
// ============================================================================

VertexArray* vertex_array_create(void) {
    engine::VertexArray* vao = new engine::VertexArray();
    if (vao->getHandle() == 0) {
        delete vao;
        return nullptr;
    }
    return reinterpret_cast<VertexArray*>(vao);
}

void vertex_array_bind(VertexArray* vao) {
    if (!vao) return;
    reinterpret_cast<engine::VertexArray*>(vao)->bind();
}

void vertex_array_set_attribute(VertexArray* vao,
                                  unsigned int attrib_index,
                                  int component_count,
                                  unsigned int type,
                                  unsigned int offset_bytes) {
    if (!vao) return;
    reinterpret_cast<engine::VertexArray*>(vao)->setVertexAttribute(
        attrib_index,
        component_count,
        static_cast<GLenum>(type),
        offset_bytes
    );
}

void vertex_array_destroy(VertexArray* vao) {
    if (!vao) return;
    delete reinterpret_cast<engine::VertexArray*>(vao);
}

// ============================================================================
// Renderer C API Implementation
// ============================================================================

Renderer* renderer_create(void) {
    engine::Renderer* renderer = new engine::Renderer();
    return reinterpret_cast<Renderer*>(renderer);
}

void renderer_set_clear_color(Renderer* renderer,
                                float r, float g, float b, float a) {
    if (!renderer) return;
    reinterpret_cast<engine::Renderer*>(renderer)->setClearColor(r, g, b, a);
}

void renderer_clear(Renderer* renderer) {
    if (!renderer) return;
    reinterpret_cast<engine::Renderer*>(renderer)->clear();
}

void renderer_draw(Renderer* renderer, VertexArray* vao, int vertex_count) {
    if (!renderer || !vao) return;
    reinterpret_cast<engine::Renderer*>(renderer)->draw(
        *reinterpret_cast<engine::VertexArray*>(vao),
        static_cast<GLsizei>(vertex_count)
    );
}

void renderer_set_shader(Renderer* renderer, ShaderProgram* shader) {
    if (!renderer) return;
    reinterpret_cast<engine::Renderer*>(renderer)->setShaderProgram(
        reinterpret_cast<engine::ShaderProgram*>(shader)
    );
}

void renderer_destroy(Renderer* renderer) {
    if (!renderer) return;
    delete reinterpret_cast<engine::Renderer*>(renderer);
}
