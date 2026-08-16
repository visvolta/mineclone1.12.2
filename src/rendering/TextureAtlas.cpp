#include "rendering/TextureAtlas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "rendering/TextureAtlasData.hpp"

namespace {

std::vector<std::uint8_t> paddedFrame(const AtlasSprite& sprite,
                                      const std::vector<std::uint8_t>& first,
                                      const std::vector<std::uint8_t>* second,
                                      float currentWeight) {
    const int padding = TextureAtlasData::paddingPixels;
    const int width = sprite.width + padding * 2;
    const int height = sprite.height + padding * 2;
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width * height * 4));
    currentWeight = std::clamp(currentWeight, 0.0F, 1.0F);
    const float nextWeight = 1.0F - currentWeight;

    for (int y = 0; y < height; ++y) {
        const int sy = std::clamp(y - padding, 0, sprite.height - 1);
        for (int x = 0; x < width; ++x) {
            const int sx = std::clamp(x - padding, 0, sprite.width - 1);
            const std::size_t source = static_cast<std::size_t>((sy * sprite.width + sx) * 4);
            const std::size_t destination = static_cast<std::size_t>((y * width + x) * 4);
            if (second == nullptr) {
                std::memcpy(result.data() + destination, first.data() + source, 4);
                continue;
            }
            // TextureAtlasSprite#updateAnimationInterpolated in 1.12.2 blends
            // RGB and preserves the current frame's alpha channel.
            for (int channel = 0; channel < 3; ++channel) {
                const float value = currentWeight * first[source + static_cast<std::size_t>(channel)] +
                    nextWeight * (*second)[source + static_cast<std::size_t>(channel)];
                result[destination + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
            }
            result[destination + 3] = first[source + 3];
        }
    }
    return result;
}

} // namespace

TextureAtlas::TextureAtlas(const TextureAtlasData& data) : data_(data) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, data.width(), data.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.initialPixels().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    animations_.reserve(data.animatedSprites().size());
    for (const std::size_t spriteIndex : data.animatedSprites())
        animations_.push_back({spriteIndex, -1, -1});
}

TextureAtlas::~TextureAtlas() {
    if (id_ != 0) glDeleteTextures(1, &id_);
}

void TextureAtlas::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

void TextureAtlas::updateAnimations(std::uint64_t rendererTick) {
    if (rendererTick == lastAnimationTick_) return;
    lastAnimationTick_ = rendererTick;
    if (animations_.empty()) return;
    glBindTexture(GL_TEXTURE_2D, id_);

    const auto& sprites = data_.sprites();
    for (AnimationState& state : animations_) {
        const AtlasSprite& sprite = sprites[state.spriteIndex];
        int cycleLength = 0;
        for (const TextureAnimationFrame& frame : sprite.animation) cycleLength += frame.duration;
        if (cycleLength <= 0) continue;
        int cycleTick = static_cast<int>(rendererTick % static_cast<std::uint64_t>(cycleLength));
        int sequenceIndex = 0;
        while (sequenceIndex + 1 < static_cast<int>(sprite.animation.size()) &&
               cycleTick >= sprite.animation[static_cast<std::size_t>(sequenceIndex)].duration) {
            cycleTick -= sprite.animation[static_cast<std::size_t>(sequenceIndex)].duration;
            ++sequenceIndex;
        }
        const TextureAnimationFrame& current = sprite.animation[static_cast<std::size_t>(sequenceIndex)];
        const int nextSequence = (sequenceIndex + 1) % static_cast<int>(sprite.animation.size());
        const TextureAnimationFrame& next = sprite.animation[static_cast<std::size_t>(nextSequence)];

        if (!sprite.interpolate) {
            if (state.lastSequenceIndex == sequenceIndex) continue;
            uploadSpriteFrame(sprite, current.textureFrame, current.textureFrame, 1.0F);
        } else {
            if (state.lastSequenceIndex == sequenceIndex && state.lastTickWithinFrame == cycleTick) continue;
            const float currentWeight = 1.0F - static_cast<float>(cycleTick) /
                static_cast<float>(std::max(1, current.duration));
            uploadSpriteFrame(sprite, current.textureFrame, next.textureFrame, currentWeight);
        }
        state.lastSequenceIndex = sequenceIndex;
        state.lastTickWithinFrame = cycleTick;
    }
}

void TextureAtlas::uploadSpriteFrame(const AtlasSprite& sprite, int firstFrame,
                                     int secondFrame, float currentWeight) {
    if (firstFrame < 0 || firstFrame >= static_cast<int>(sprite.frames.size()) ||
        secondFrame < 0 || secondFrame >= static_cast<int>(sprite.frames.size())) return;
    const auto& first = sprite.frames[static_cast<std::size_t>(firstFrame)];
    const auto* second = firstFrame == secondFrame ? nullptr : &sprite.frames[static_cast<std::size_t>(secondFrame)];
    const std::vector<std::uint8_t> pixels = paddedFrame(sprite, first, second, currentWeight);
    const int padding = TextureAtlasData::paddingPixels;
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    sprite.originX - padding, sprite.originY - padding,
                    sprite.width + padding * 2, sprite.height + padding * 2,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}
