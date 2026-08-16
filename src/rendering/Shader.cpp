#include "rendering/Shader.hpp"

#include <stdexcept>
#include <string>

#include <glm/gtc/type_ptr.hpp>

GLuint Shader::compile(GLenum type, std::string_view source) {
    const GLuint shader = glCreateShader(type);
    const char* sourcePointer = source.data();
    const GLint sourceLength = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourcePointer, &sourceLength);
    glCompileShader(shader);

    GLint succeeded = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &succeeded);
    if (succeeded == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader compilation failed: " + log);
}

Shader::Shader(std::string_view vertexSource, std::string_view fragmentSource) {
    const GLuint vertexShader = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentSource);

    id_ = glCreateProgram();
    glAttachShader(id_, vertexShader);
    glAttachShader(id_, fragmentShader);
    glLinkProgram(id_);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint succeeded = GL_FALSE;
    glGetProgramiv(id_, GL_LINK_STATUS, &succeeded);
    if (succeeded == GL_TRUE) {
        return;
    }

    GLint logLength = 0;
    glGetProgramiv(id_, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(logLength), '\0');
    glGetProgramInfoLog(id_, logLength, nullptr, log.data());
    glDeleteProgram(id_);
    id_ = 0;
    throw std::runtime_error("Shader linking failed: " + log);
}

Shader::~Shader() {
    glDeleteProgram(id_);
}

void Shader::use() const {
    glUseProgram(id_);
}

GLint Shader::uniformLocation(const char* name) const {
    return glGetUniformLocation(id_, name);
}

void Shader::setMat4(const char* name, const glm::mat4& value) const {
    setMat4(uniformLocation(name), value);
}

void Shader::setMat4(GLint location, const glm::mat4& value) const {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setInt(const char* name, int value) const {
    setInt(uniformLocation(name), value);
}

void Shader::setInt(GLint location, int value) const {
    glUniform1i(location, value);
}
