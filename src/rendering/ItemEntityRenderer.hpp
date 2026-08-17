#pragma once

#include <filesystem>
#include <unordered_map>
#include <cstdint>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>

class ItemEntitySystem;
class ItemRegistry;

class ItemEntityRenderer {
public:
    ItemEntityRenderer(const std::filesystem::path& assetRoot,const ItemRegistry& items);
    ~ItemEntityRenderer();
    ItemEntityRenderer(const ItemEntityRenderer&)=delete;
    ItemEntityRenderer& operator=(const ItemEntityRenderer&)=delete;
    void render(const ItemEntitySystem& entities,const glm::mat4& view,const glm::mat4& projection,float partialTick);
private:
    GLuint textureFor(std::uint16_t itemId);
    std::filesystem::path assetRoot_;
    const ItemRegistry& items_;
    GLuint program_=0,vao_=0,vbo_=0;
    std::unordered_map<std::uint16_t,GLuint> textures_;
};
