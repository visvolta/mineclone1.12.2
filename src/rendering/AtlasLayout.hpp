#pragma once

#include "blocks/BlockRegistry.hpp"

struct AtlasBounds {
    float u0;
    float v0;
    float u1;
    float v1;
};

inline constexpr int atlasColumns = 8;
inline constexpr int atlasContentPixels = 16;
inline constexpr int atlasBorderPixels = 1;
inline constexpr int atlasTilePixels = atlasContentPixels + atlasBorderPixels * 2;
inline constexpr int atlasRows =
    (static_cast<int>(TextureId::FinalCount) + atlasColumns - 1) / atlasColumns;
inline constexpr float atlasInset = 0.0001F;

constexpr AtlasBounds atlasBounds(TextureId texture) {
    const int index = static_cast<int>(texture);
    const int column = index % atlasColumns;
    const int row = index / atlasColumns;
    const float width = static_cast<float>(atlasColumns * atlasTilePixels);
    const float height = static_cast<float>(atlasRows * atlasTilePixels);
    const float left = static_cast<float>(column * atlasTilePixels + atlasBorderPixels) / width;
    const float top = static_cast<float>(row * atlasTilePixels + atlasBorderPixels) / height;
    const float right = static_cast<float>(column * atlasTilePixels + atlasBorderPixels + atlasContentPixels) / width;
    const float bottom = static_cast<float>(row * atlasTilePixels + atlasBorderPixels + atlasContentPixels) / height;
    return {
        left + atlasInset, top + atlasInset,
        right - atlasInset, bottom - atlasInset
    };
}
