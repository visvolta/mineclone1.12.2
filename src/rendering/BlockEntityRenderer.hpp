#pragma once

#include <array>
#include <filesystem>
#include <unordered_map>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

#include "rendering/Shader.hpp"
#include "world/BlockEntitySystem.hpp"

class World;

class BlockEntityRenderer {
public:
    BlockEntityRenderer(const std::filesystem::path& assetRoot, BlockEntitySystem& entities);
    ~BlockEntityRenderer();
    BlockEntityRenderer(const BlockEntityRenderer&) = delete;
    BlockEntityRenderer& operator=(const BlockEntityRenderer&) = delete;

    void render(const World& world, const glm::mat4& view, const glm::mat4& projection,
                float partialTick);

private:
    GLuint loadTexture(const std::filesystem::path& path, int expectedWidth, int expectedHeight);
    void renderChest(const World& world, const RuntimeBlockEntity& entity,
                     const glm::mat4& transform, float partialTick);
    void renderSign(const World& world, const RuntimeBlockEntity& entity,
                    const glm::mat4& transform);
    void renderBed(const World& world, const RuntimeBlockEntity& entity,
                   const glm::mat4& transform);
    void renderShulker(const World& world, const RuntimeBlockEntity& entity,
                       const glm::mat4& transform, float partialTick);

    BlockEntitySystem& entities_;
    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint chestNormal_ = 0;
    GLuint chestTrapped_ = 0;
    GLuint chestNormalDouble_ = 0;
    GLuint chestTrappedDouble_ = 0;
    GLuint signTexture_ = 0;
    GLuint asciiTexture_ = 0;
    std::array<int, 256> charWidths_{};
    std::array<GLuint, 16> bedTextures_{};
    std::array<GLuint, 16> shulkerTextures_{};
};
