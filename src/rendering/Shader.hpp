#pragma once

#include <string_view>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

class Shader {
public:
    Shader(std::string_view vertexSource, std::string_view fragmentSource);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

    void use() const;
    [[nodiscard]] GLint uniformLocation(const char* name) const;
    void setMat4(const char* name, const glm::mat4& value) const;
    void setMat4(GLint location, const glm::mat4& value) const;
    void setInt(const char* name, int value) const;
    void setInt(GLint location, int value) const;

private:
    static GLuint compile(GLenum type, std::string_view source);

    GLuint id_ = 0;
};
