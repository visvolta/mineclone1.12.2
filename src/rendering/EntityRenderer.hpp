#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

class BlockRenderResources;
class EntityManager;
class ItemRegistry;
class TextureAtlas;
struct ItemStack;

class EntityRenderer {
public:
    EntityRenderer(const std::filesystem::path& assetRoot, const ItemRegistry& items,
                       const BlockRenderResources& resources, const TextureAtlas& atlas);
    ~EntityRenderer();
    EntityRenderer(const EntityRenderer&)=delete;
    EntityRenderer& operator=(const EntityRenderer&)=delete;
    void render(const EntityManager& entities,const glm::mat4& view,const glm::mat4& projection,float partialTick);

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
    GLuint xpTexture_=0;
    GLuint xpVao_=0;
    GLuint xpVbo_=0;
    std::unordered_map<std::uint32_t, Geometry> geometries_;
};
