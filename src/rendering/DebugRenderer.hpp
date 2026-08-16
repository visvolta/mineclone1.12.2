#pragma once

#include <optional>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

#include "rendering/Shader.hpp"
#include "world/Raycast.hpp"

class DebugRenderer {
public:
    DebugRenderer();
    ~DebugRenderer();
    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    void renderOutline(const std::optional<RaycastHit>& hit, const glm::mat4& view, const glm::mat4& projection);
    void renderCrosshair(int framebufferWidth, int framebufferHeight);

private:
    Shader worldShader_;
    Shader screenShader_;
    GLuint worldVao_ = 0;
    GLuint worldVbo_ = 0;
    GLuint screenVao_ = 0;
    GLuint screenVbo_ = 0;
    GLuint iconsTexture_ = 0;
};
