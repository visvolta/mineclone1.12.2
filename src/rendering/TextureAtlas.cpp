#include "rendering/TextureAtlas.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <stb_image.h>

#include "rendering/AtlasLayout.hpp"

namespace {

constexpr int tileSize = 16;
constexpr int atlasWidth = atlasColumns * tileSize;
constexpr int atlasHeight = atlasRows * tileSize;

constexpr std::array<const char*, static_cast<std::size_t>(TextureId::FinalCount)> textureNames = {
    "stone.png", "grass_top.png", "grass_side.png", "grass_side_overlay.png",
    "grass_side_snowed.png", "dirt.png", "cobblestone.png", "planks_oak.png", "bedrock.png",
    "sand.png", "gravel.png", "log_oak.png", "log_oak_top.png",
    "leaves_oak.png", "glass.png", "water_still.png", "lava_still.png",
    "sandstone_normal.png", "sandstone_top.png", "sandstone_bottom.png",
    "coal_ore.png", "iron_ore.png", "gold_ore.png", "lapis_ore.png",
    "diamond_ore.png", "redstone_ore.png", "emerald_ore.png", "snow.png",
    "ice.png", "clay.png", "mycelium_side.png", "mycelium_top.png",
    "hardened_clay.png", "hardened_clay_stained_white.png",
    "hardened_clay_stained_orange.png", "hardened_clay_stained_magenta.png",
    "hardened_clay_stained_light_blue.png", "hardened_clay_stained_yellow.png",
    "hardened_clay_stained_lime.png", "hardened_clay_stained_pink.png",
    "hardened_clay_stained_gray.png", "hardened_clay_stained_silver.png",
    "hardened_clay_stained_cyan.png", "hardened_clay_stained_purple.png",
    "hardened_clay_stained_blue.png", "hardened_clay_stained_brown.png",
    "hardened_clay_stained_green.png", "hardened_clay_stained_red.png",
    "hardened_clay_stained_black.png", "red_sandstone_normal.png",
    "red_sandstone_top.png", "red_sandstone_bottom.png",
    "prismarine_rough.png", "sea_lantern.png",
    "stone_granite.png", "stone_granite_smooth.png", "stone_diorite.png",
    "stone_diorite_smooth.png", "stone_andesite.png", "stone_andesite_smooth.png",
    "coarse_dirt.png", "dirt_podzol_side.png", "dirt_podzol_top.png", "red_sand.png",
    "planks_spruce.png", "planks_birch.png", "planks_jungle.png", "planks_acacia.png",
    "planks_big_oak.png", "log_spruce.png", "log_spruce_top.png", "log_birch.png",
    "log_birch_top.png", "log_jungle.png", "log_jungle_top.png", "log_acacia.png",
    "log_acacia_top.png", "log_big_oak.png", "log_big_oak_top.png", "leaves_spruce.png",
    "leaves_birch.png", "leaves_jungle.png", "leaves_acacia.png", "leaves_big_oak.png",
    "tallgrass.png", "fern.png", "deadbush.png", "flower_dandelion.png",
    "flower_rose.png", "flower_blue_orchid.png", "flower_allium.png", "flower_houstonia.png",
    "flower_tulip_red.png", "flower_tulip_orange.png", "flower_tulip_white.png",
    "flower_tulip_pink.png", "flower_oxeye_daisy.png", "mushroom_brown.png",
    "mushroom_red.png", "cactus_side.png", "cactus_top.png", "cactus_bottom.png",
    "reeds.png", "vine.png", "waterlily.png", "pumpkin_side.png", "pumpkin_top.png",
    "pumpkin_face_off.png", "melon_side.png", "melon_top.png", "cobblestone_mossy.png",
    "bone_block_side.png", "bone_block_top.png", "web.png", "wool_colored_white.png",
    "brick.png", "bookshelf.png", "crafting_table_side.png", "crafting_table_top.png",
    "crafting_table_front.png", "furnace_side.png", "furnace_top.png", "planks_oak.png",
    "planks_oak.png", "sponge_wet.png", "obsidian.png", "tnt_side.png",
    "tnt_top.png", "tnt_bottom.png", "stonebrick.png", "stonebrick_mossy.png",
    "stonebrick_cracked.png", "stonebrick_carved.png"
};

} // namespace

TextureAtlas::TextureAtlas() {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(atlasWidth * atlasHeight * 4), 0);
    const std::filesystem::path textureRoot = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
        "assets/minecraft/textures/blocks";

    for (std::size_t index = 0; index < textureNames.size(); ++index) {
        const std::filesystem::path path = textureRoot / textureNames[index];
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* source = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (source == nullptr) {
            throw std::runtime_error("Could not load required Minecraft texture: " + path.string());
        }
        if (width != tileSize || height < tileSize) {
            stbi_image_free(source);
            throw std::runtime_error("Expected a 16-pixel-wide Minecraft 1.12.2 texture: " + path.string());
        }

        const int tileX = static_cast<int>(index) % atlasColumns;
        const int tileY = static_cast<int>(index) / atlasColumns;
        for (int row = 0; row < tileSize; ++row) {
            const std::size_t sourceOffset = static_cast<std::size_t>(row * tileSize * 4);
            const std::size_t destinationOffset = static_cast<std::size_t>(((tileY * tileSize + row) * atlasWidth + tileX * tileSize) * 4);
            std::copy_n(source + sourceOffset, tileSize * 4, pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
        }
        stbi_image_free(source);
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

TextureAtlas::~TextureAtlas() { glDeleteTextures(1, &id_); }

void TextureAtlas::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}
