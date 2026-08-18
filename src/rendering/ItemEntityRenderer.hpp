#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

class BlockRenderResources;
class ItemEntitySystem;
class ItemRegistry;
class TextureAtlas;
struct ItemStack;

class ItemEntityRenderer {
public:
    ItemEntityRenderer(const std::filesystem::path& assetRoot, const ItemRegistry& items,
                       const BlockRenderResources& resources, const TextureAtlas& atlas);
    ~ItemEntityRenderer();
    ItemEntityRenderer(const ItemEntityRenderer&)=delete;
    ItemEntityRenderer& operator=(const ItemEntityRenderer&)=delete;
    void render(const ItemEntitySystem& entities,const glm::mat4& view,const glm::mat4& projection,float partialTick);

private:
    struct Geometry {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
        bool gui3d = false;
    };

    [[nodiscard]] const Geometry& geometryFor(const ItemStack& stack);
    [[nodiscard]] std::uint32_t geometryKey(const ItemStack& stack) const;

    std::filesystem::path assetRoot_;
    const ItemRegistry& items_;
    const BlockRenderResources& resources_;
    const TextureAtlas& atlas_;
    GLuint program_=0;
    std::unordered_map<std::uint32_t, Geometry> geometries_;
};
