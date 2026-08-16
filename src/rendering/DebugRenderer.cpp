#include "rendering/DebugRenderer.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <stb_image.h>

#include "blocks/BlockShape.hpp"
#include "world/World.hpp"

namespace {

constexpr std::string_view worldVertex = R"glsl(
#version 330 core
layout (location = 0) in vec3 position;
uniform mat4 transform;
void main() { gl_Position = transform * vec4(position, 1.0); }
)glsl";

constexpr std::string_view overlayVertex = R"glsl(
#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 textureUv;
out vec2 uv;
uniform mat4 transform;
void main() { uv = textureUv; gl_Position = transform * vec4(position, 1.0); }
)glsl";

constexpr std::string_view overlayFragment = R"glsl(
#version 330 core
in vec2 uv;
out vec4 fragmentColor;
uniform sampler2D damageTexture;
void main() {
    vec4 sampled = texture(damageTexture, uv);
    fragmentColor = vec4(sampled.rgb, sampled.a * 0.72);
    if (fragmentColor.a < 0.01) discard;
}
)glsl";

constexpr std::string_view screenVertex = R"glsl(
#version 330 core
layout (location = 0) in vec2 position;
layout (location = 1) in vec2 textureUv;
out vec2 uv;
void main() {
    uv = textureUv;
    gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";

constexpr std::string_view blackFragment = R"glsl(
#version 330 core
out vec4 fragmentColor;
void main() { fragmentColor = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl";

constexpr std::string_view texturedFragment = R"glsl(
#version 330 core
in vec2 uv;
out vec4 fragmentColor;
uniform sampler2D icons;
void main() {
    fragmentColor = texture(icons, uv);
    if (fragmentColor.a < 0.1) discard;
}
)glsl";

} // namespace

DebugRenderer::DebugRenderer()
    : worldShader_(worldVertex, blackFragment), overlayShader_(overlayVertex, overlayFragment),
      screenShader_(screenVertex, texturedFragment) {
    glGenVertexArrays(1, &worldVao_);
    glGenBuffers(1, &worldVbo_);
    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER, 24 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &overlayVao_);
    glGenBuffers(1, &overlayVbo_);
    glBindVertexArray(overlayVao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferData(GL_ARRAY_BUFFER, 36 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &screenVao_);
    glGenBuffers(1, &screenVbo_);
    glBindVertexArray(screenVao_);
    glBindBuffer(GL_ARRAY_BUFFER, screenVbo_);
    glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    const std::filesystem::path iconsPath = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
        "assets/minecraft/textures/gui/icons.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(iconsPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width != 256 || height != 256) {
        if (pixels != nullptr) stbi_image_free(pixels);
        throw std::runtime_error("Could not load the Minecraft 1.12.2 GUI atlas: " + iconsPath.string());
    }
    glGenTextures(1, &iconsTexture_);
    glBindTexture(GL_TEXTURE_2D, iconsTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);

    for (int stage = 0; stage < 10; ++stage) {
        const std::filesystem::path damagePath = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
            ("assets/minecraft/textures/blocks/destroy_stage_" + std::to_string(stage) + ".png");
        int damageWidth = 0;
        int damageHeight = 0;
        int damageChannels = 0;
        unsigned char* damagePixels = stbi_load(damagePath.string().c_str(), &damageWidth, &damageHeight,
                                                &damageChannels, STBI_rgb_alpha);
        if (damagePixels == nullptr || damageWidth != 16 || damageHeight != 16) {
            if (damagePixels != nullptr) stbi_image_free(damagePixels);
            throw std::runtime_error("Could not load Minecraft 1.12.2 destroy texture: " + damagePath.string());
        }
        glGenTextures(1, &destroyTextures_[static_cast<std::size_t>(stage)]);
        glBindTexture(GL_TEXTURE_2D, destroyTextures_[static_cast<std::size_t>(stage)]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, damagePixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        stbi_image_free(damagePixels);
    }
}

DebugRenderer::~DebugRenderer() {
    glDeleteTextures(static_cast<GLsizei>(destroyTextures_.size()), destroyTextures_.data());
    glDeleteTextures(1, &iconsTexture_);
    glDeleteBuffers(1, &screenVbo_);
    glDeleteVertexArrays(1, &screenVao_);
    glDeleteBuffers(1, &overlayVbo_);
    glDeleteVertexArrays(1, &overlayVao_);
    glDeleteBuffers(1, &worldVbo_);
    glDeleteVertexArrays(1, &worldVao_);
}

void DebugRenderer::renderOutline(const World& world, const std::optional<RaycastHit>& hit,
                                  const glm::mat4& view, const glm::mat4& projection) {
    if (!hit) return;
    const auto bounds = BlockShapes::selectionBounds(
        world, hit->state, hit->block.x, hit->block.y, hit->block.z);
    if (!bounds) return;

    // RenderGlobal#drawSelectionBox expands the selected AABB slightly so the
    // line never z-fights with the block surface.
    constexpr float inset = 0.002F;
    const float x0 = static_cast<float>(hit->block.x + bounds->minX) - inset;
    const float y0 = static_cast<float>(hit->block.y + bounds->minY) - inset;
    const float z0 = static_cast<float>(hit->block.z + bounds->minZ) - inset;
    const float x1 = static_cast<float>(hit->block.x + bounds->maxX) + inset;
    const float y1 = static_cast<float>(hit->block.y + bounds->maxY) + inset;
    const float z1 = static_cast<float>(hit->block.z + bounds->maxZ) + inset;
    const std::array<float, 72> vertices = {
        x0,y0,z0, x1,y0,z0,  x1,y0,z0, x1,y0,z1,  x1,y0,z1, x0,y0,z1,  x0,y0,z1, x0,y0,z0,
        x0,y1,z0, x1,y1,z0,  x1,y1,z0, x1,y1,z1,  x1,y1,z1, x0,y1,z1,  x0,y1,z1, x0,y1,z0,
        x0,y0,z0, x0,y1,z0,  x1,y0,z0, x1,y1,z0,  x1,y0,z1, x1,y1,z1,  x0,y0,z1, x0,y1,z1
    };

    worldShader_.use();
    worldShader_.setMat4("transform", projection * view);
    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glLineWidth(2.0F);
    glDrawArrays(GL_LINES, 0, 24);
}

void DebugRenderer::renderBreakOverlay(const World& world, const std::optional<RaycastHit>& hit,
                                       float progress, const glm::mat4& view,
                                       const glm::mat4& projection) {
    if (!hit || progress <= 0.0F) return;
    const auto bounds = BlockShapes::selectionBounds(world, hit->state, hit->block.x, hit->block.y, hit->block.z);
    if (!bounds) return;
    const int stage = std::clamp(static_cast<int>(progress * 10.0F), 0, 9);
    constexpr float expand = 0.001F;
    const float x0 = static_cast<float>(hit->block.x + bounds->minX) - expand;
    const float y0 = static_cast<float>(hit->block.y + bounds->minY) - expand;
    const float z0 = static_cast<float>(hit->block.z + bounds->minZ) - expand;
    const float x1 = static_cast<float>(hit->block.x + bounds->maxX) + expand;
    const float y1 = static_cast<float>(hit->block.y + bounds->maxY) + expand;
    const float z1 = static_cast<float>(hit->block.z + bounds->maxZ) + expand;
    const std::array<float, 180> vertices = {{
        x0,y0,z0,0,1, x1,y0,z0,1,1, x1,y1,z0,1,0, x0,y0,z0,0,1, x1,y1,z0,1,0, x0,y1,z0,0,0,
        x1,y0,z1,0,1, x0,y0,z1,1,1, x0,y1,z1,1,0, x1,y0,z1,0,1, x0,y1,z1,1,0, x1,y1,z1,0,0,
        x0,y0,z1,0,1, x0,y0,z0,1,1, x0,y1,z0,1,0, x0,y0,z1,0,1, x0,y1,z0,1,0, x0,y1,z1,0,0,
        x1,y0,z0,0,1, x1,y0,z1,1,1, x1,y1,z1,1,0, x1,y0,z0,0,1, x1,y1,z1,1,0, x1,y1,z0,0,0,
        x0,y1,z0,0,1, x1,y1,z0,1,1, x1,y1,z1,1,0, x0,y1,z0,0,1, x1,y1,z1,1,0, x0,y1,z1,0,0,
        x0,y0,z1,0,1, x1,y0,z1,1,1, x1,y0,z0,1,0, x0,y0,z1,0,1, x1,y0,z0,1,0, x0,y0,z0,0,0
    }};

    overlayShader_.use();
    overlayShader_.setMat4("transform", projection * view);
    overlayShader_.setInt("damageTexture", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, destroyTextures_[static_cast<std::size_t>(stage)]);
    glBindVertexArray(overlayVao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0F, -10.0F);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
}

void DebugRenderer::renderCrosshair(int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    const float width = static_cast<float>(framebufferWidth);
    const float height = static_cast<float>(framebufferHeight);
    const float centerX = width * 0.5F;
    const float centerY = height * 0.5F;
    const float left = 2.0F * (centerX - 7.0F) / width - 1.0F;
    const float right = 2.0F * (centerX + 9.0F) / width - 1.0F;
    const float top = 1.0F - 2.0F * (centerY - 7.0F) / height;
    const float bottom = 1.0F - 2.0F * (centerY + 9.0F) / height;
    constexpr float tile = 16.0F / 256.0F;
    const std::array<float, 16> vertices = {
        left, bottom, 0.0F, tile,
        right, bottom, tile, tile,
        left, top, 0.0F, 0.0F,
        right, top, tile, 0.0F
    };

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ZERO);
    screenShader_.use();
    screenShader_.setInt("icons", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, iconsTexture_);
    glBindVertexArray(screenVao_);
    glBindBuffer(GL_ARRAY_BUFFER, screenVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
