#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

class TextureAtlasData;
struct AtlasSprite;

class TextureAtlas {
public:
    explicit TextureAtlas(const TextureAtlasData& data);
    ~TextureAtlas();
    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;

    void bind(GLuint unit = 0) const;
    void updateAnimations(std::uint64_t rendererTick);

private:
    struct AnimationState {
        std::size_t spriteIndex = 0;
        int lastSequenceIndex = -1;
        int lastTickWithinFrame = -1;
    };

    void uploadSpriteFrame(const AtlasSprite& sprite, int firstFrame, int secondFrame,
                           float currentWeight);

    const TextureAtlasData& data_;
    GLuint id_ = 0;
    std::uint64_t lastAnimationTick_ = static_cast<std::uint64_t>(-1);
    std::vector<AnimationState> animations_;
};
