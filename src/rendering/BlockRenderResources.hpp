#pragma once

#include <filesystem>
#include <stdexcept>

#include "rendering/ModelLoader.hpp"
#include "rendering/TextureAtlasData.hpp"

// Immutable CPU-side rendering resources loaded only from the extracted
// Minecraft 1.12.2 resource tree. The legacy fallback sprite may exist inside
// TextureAtlasData for compatibility with its loader, but this resource owner
// refuses to accept it for any registered block state.
class BlockRenderResources {
public:
    explicit BlockRenderResources(const std::filesystem::path& assetRoot,
                                  int maximumTextureSize = TextureAtlasData::maximumSupportedTextureSize)
        : atlas_(assetRoot, maximumTextureSize), models_(assetRoot, atlas_) {
        for (std::uint16_t numericId = 0; numericId <= 255; ++numericId) {
            if (!BlockRegistry::isRegisteredId(numericId)) continue;
            for (std::uint8_t metadata = 0; metadata < 16; ++metadata) {
                const BlockState state = makeBlockState(numericId, metadata);
                for (Face face : {Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East}) {
                    const AtlasSprite& sprite = atlas_.sprite(BlockRegistry::texture(state, face));
                    if (sprite.name == "minecraft:missingno") {
                        throw std::runtime_error(
                            "Registered Minecraft 1.12.2 block resolved to a placeholder texture: " +
                            std::string(BlockRegistry::legacyName(numericId)));
                    }
                }
            }
        }
    }

    [[nodiscard]] const TextureAtlasData& atlas() const { return atlas_; }
    [[nodiscard]] const BlockModelManager& models() const { return models_; }

private:
    TextureAtlasData atlas_;
    BlockModelManager models_;
};
