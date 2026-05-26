#include "shader_program.h"
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>

namespace engine {

ShaderProgram::ShaderProgram(const std::string& vertSource,
                             const std::string& fragSource,
                             std::string* errmsg) {
    // PHASE 1: Compile vertex shader
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSource, errmsg);
    if (!vert) {
        return;  // errmsg already set by compileShader
    }

    // PHASE 2: Compile fragment shader
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSource, errmsg);
    if (!frag) {
        glDeleteShader(vert);  // Clean up vertex shader before returning
        return;
    }

    // PHASE 3: Link program
    handle_ = glCreateProgram();
    glAttachShader(handle_, vert);
    glAttachShader(handle_, frag);
    glLinkProgram(handle_);

    // PHASE 4: Check linking success
    GLint success;
    glGetProgramiv(handle_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(handle_, 512, nullptr, infoLog);
        if (errmsg) {
            *errmsg = std::string("Program linking failed: ") + infoLog;
        } else {
            fprintf(stderr, "Program linking failed: %s\n", infoLog);
        }
        glDeleteProgram(handle_);
        handle_ = 0;
    }

    // PHASE 5: Cleanup individual shaders
    // They're no longer needed after linking — GPU keeps the linked program
    glDeleteShader(vert);
    glDeleteShader(frag);
}

ShaderProgram::~ShaderProgram() {
    if (handle_ != 0) {
        glDeleteProgram(handle_);
    }
}

GLuint ShaderProgram::compileShader(GLenum type,
                                     const std::string& source,
                                     std::string* errmsg) {
    GLuint shader = glCreateShader(type);
    const char* srcPtr = source.c_str();
    glShaderSource(shader, 1, &srcPtr, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);

        // Determine shader type name for error message
        const char* typeName = (type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment";

        if (errmsg) {
            *errmsg = std::string(typeName) + " shader compilation failed: " + infoLog;
        } else {
            fprintf(stderr, "%s shader compilation failed: %s\n", typeName, infoLog);
        }

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

void ShaderProgram::use() const {
    if (handle_ != 0) {
        glUseProgram(handle_);
    }
}

GLint ShaderProgram::getUniformLocation(const std::string& name) const {
    if (handle_ == 0) {
        return -1;
    }
    return glGetUniformLocation(handle_, name.c_str());
}

void ShaderProgram::setUniform(const std::string& name, float value) const {
    GLint loc = getUniformLocation(name);
    if (loc != -1) {
        glUniform1f(loc, value);
    }
}

void ShaderProgram::setUniform(const std::string& name, const glm::vec2& value) const {
    GLint loc = getUniformLocation(name);
    if (loc != -1) {
        glUniform2fv(loc, 1, glm::value_ptr(value));
    }
}

void ShaderProgram::setUniform(const std::string& name, const glm::vec3& value) const {
    GLint loc = getUniformLocation(name);
    if (loc != -1) {
        glUniform3fv(loc, 1, glm::value_ptr(value));
    }
}

void ShaderProgram::setUniform(const std::string& name, const glm::vec4& value) const {
    GLint loc = getUniformLocation(name);
    if (loc != -1) {
        glUniform4fv(loc, 1, glm::value_ptr(value));
    }
}

void ShaderProgram::setUniform(const std::string& name, const glm::mat4& value) const {
    GLint loc = getUniformLocation(name);
    if (loc != -1) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    }
}

}  // namespace engine
