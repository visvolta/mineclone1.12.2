#pragma once

#include <filesystem>

#include "rendering/ModelLoader.hpp"
#include "rendering/TextureAtlasData.hpp"

// Immutable CPU-side rendering resources. They are loaded once from the
// extracted Minecraft resource tree and are safe to read from meshing workers.
class BlockRenderResources {
public:
    explicit BlockRenderResources(const std::filesystem::path& assetRoot,
                                  int maximumTextureSize = TextureAtlasData::maximumSupportedTextureSize)
        : atlas_(assetRoot, maximumTextureSize), models_(assetRoot, atlas_) {}

    [[nodiscard]] const TextureAtlasData& atlas() const { return atlas_; }
    [[nodiscard]] const BlockModelManager& models() const { return models_; }

private:
    TextureAtlasData atlas_;
    BlockModelManager models_;
};
