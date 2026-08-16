#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "blocks/BlockRegistry.hpp"

struct AtlasBounds {
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;

    [[nodiscard]] float u(double modelCoordinate) const {
        return u0 + (u1 - u0) * static_cast<float>(modelCoordinate / 16.0);
    }
    [[nodiscard]] float v(double modelCoordinate) const {
        return v0 + (v1 - v0) * static_cast<float>(modelCoordinate / 16.0);
    }
};

struct TextureAnimationFrame {
    int textureFrame = 0;
    int duration = 1;
};

struct AtlasSprite {
    std::string name;
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    AtlasBounds bounds{};
    bool interpolate = false;
    std::vector<TextureAnimationFrame> animation;
    std::vector<std::vector<std::uint8_t>> frames;

    [[nodiscard]] bool animated() const { return animation.size() > 1 || interpolate; }
};

class TextureAtlasData {
public:
    static constexpr float uvInset = 0.0001F;
    static constexpr int paddingPixels = 1;
    static constexpr int maximumSupportedTextureSize = 128;

    explicit TextureAtlasData(const std::filesystem::path& assetRoot,
                              int maximumTextureSize = maximumSupportedTextureSize);

    [[nodiscard]] const AtlasSprite& sprite(std::string_view resourceName) const;
    [[nodiscard]] const AtlasSprite& sprite(TextureId legacyTexture) const;
    [[nodiscard]] bool contains(std::string_view resourceName) const;
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] const std::vector<std::uint8_t>& initialPixels() const { return pixels_; }
    [[nodiscard]] const std::vector<AtlasSprite>& sprites() const { return sprites_; }
    [[nodiscard]] const std::vector<std::size_t>& animatedSprites() const { return animatedSprites_; }

    [[nodiscard]] static std::string normalizeResourceName(std::string_view resourceName);

private:
    void addMissingSprite();
    void discoverDirectory(const std::filesystem::path& root, std::string_view resourcePrefix,
                           int maximumTextureSize);
    void packSprites();
    void blitFrame(std::vector<std::uint8_t>& destination, const AtlasSprite& sprite,
                   const std::vector<std::uint8_t>& frame) const;

    int width_ = 0;
    int height_ = 0;
    int cellSize_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::vector<AtlasSprite> sprites_;
    std::unordered_map<std::string, std::size_t> spriteIndices_;
    std::vector<std::size_t> animatedSprites_;
};
