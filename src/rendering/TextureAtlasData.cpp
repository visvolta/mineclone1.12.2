#include "rendering/TextureAtlasData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <stb_image.h>

#include "core/Json.hpp"

namespace {

constexpr std::array<const char*, static_cast<std::size_t>(TextureId::FinalCount)> legacyTextureNames = {
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

int nextPowerOfTwo(int value) {
    int result = 1;
    while (result < value) result <<= 1;
    return result;
}

std::vector<std::uint8_t> makeMissingFrame() {
    constexpr int size = 16;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size * size * 4), 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool black = ((x / 4) + (y / 4)) % 2 == 0;
            const std::size_t offset = static_cast<std::size_t>((y * size + x) * 4);
            pixels[offset] = black ? 0U : 255U;
            pixels[offset + 1] = 0U;
            pixels[offset + 2] = black ? 0U : 255U;
            pixels[offset + 3] = 255U;
        }
    }
    return pixels;
}

std::vector<TextureAnimationFrame> readAnimation(const std::filesystem::path& metadataPath,
                                                  int frameCount, bool& interpolate) {
    interpolate = false;
    std::vector<TextureAnimationFrame> sequence;
    if (!std::filesystem::exists(metadataPath)) {
        if (frameCount != 1) {
            throw std::runtime_error("Animated Minecraft texture is missing .mcmeta: " +
                                     metadataPath.string());
        }
        sequence.push_back({0, 1});
        return sequence;
    }

    const JsonValue root = JsonValue::parseFile(metadataPath);
    const JsonValue& animation = root.at("animation");
    if (!animation.isObject()) throw std::runtime_error("Texture animation section is not an object: " + metadataPath.string());
    const int defaultTime = std::max(1, animation.intOr("frametime", 1));
    interpolate = animation.boolOr("interpolate", false);

    if (const JsonValue* frames = animation.find("frames")) {
        if (!frames->isArray()) throw std::runtime_error("Texture animation frames are not an array: " + metadataPath.string());
        for (const JsonValue& frame : frames->asArray()) {
            TextureAnimationFrame entry;
            if (frame.isNumber()) {
                entry.textureFrame = frame.asInt();
                entry.duration = defaultTime;
            } else if (frame.isObject()) {
                entry.textureFrame = frame.at("index").asInt();
                entry.duration = std::max(1, frame.intOr("time", defaultTime));
            } else {
                throw std::runtime_error("Invalid texture animation frame in: " + metadataPath.string());
            }
            if (entry.textureFrame < 0 || entry.textureFrame >= frameCount)
                throw std::runtime_error("Texture animation frame index is outside the PNG: " + metadataPath.string());
            sequence.push_back(entry);
        }
    } else {
        for (int index = 0; index < frameCount; ++index) sequence.push_back({index, defaultTime});
    }

    if (sequence.empty()) throw std::runtime_error("Texture animation has no frames: " + metadataPath.string());
    return sequence;
}

std::string resourceNameFor(const std::filesystem::path& root,
                            const std::filesystem::path& file,
                            std::string_view resourcePrefix) {
    std::filesystem::path relative = std::filesystem::relative(file, root);
    relative.replace_extension();
    std::string name = relative.generic_string();
    return "minecraft:" + std::string(resourcePrefix) + "/" + name;
}

} // namespace

TextureAtlasData::TextureAtlasData(const std::filesystem::path& assetRoot,
                                   int maximumTextureSize) {
    if (maximumTextureSize < 16 || maximumTextureSize > maximumSupportedTextureSize)
        throw std::invalid_argument("Block texture maximum must be between 16 and 128 pixels");

    addMissingSprite();
    const std::filesystem::path textureRoot = assetRoot / "assets/minecraft/textures";
    discoverDirectory(textureRoot / "blocks", "blocks", maximumTextureSize);
    discoverDirectory(textureRoot / "items", "items", maximumTextureSize);
    packSprites();
}

std::string TextureAtlasData::normalizeResourceName(std::string_view resourceName) {
    std::string value(resourceName);
    if (value.empty()) return "minecraft:missingno";
    if (value.front() == '#') return value;
    if (value.find(':') == std::string::npos) value.insert(0, "minecraft:");
    const std::size_t colon = value.find(':');
    std::string path = value.substr(colon + 1);
    if (path.starts_with("textures/")) path.erase(0, 9);
    if (path.ends_with(".png")) path.resize(path.size() - 4);
    return value.substr(0, colon + 1) + path;
}

const AtlasSprite& TextureAtlasData::sprite(std::string_view resourceName) const {
    const std::string normalized = normalizeResourceName(resourceName);
    const auto iterator = spriteIndices_.find(normalized);
    if (iterator != spriteIndices_.end()) return sprites_[iterator->second];
    return sprites_[spriteIndices_.at("minecraft:missingno")];
}

const AtlasSprite& TextureAtlasData::sprite(TextureId legacyTexture) const {
    const std::size_t index = static_cast<std::size_t>(legacyTexture);
    if (index >= legacyTextureNames.size()) return sprite("minecraft:missingno");
    std::string name = legacyTextureNames[index];
    if (name.ends_with(".png")) name.resize(name.size() - 4);
    return sprite("minecraft:blocks/" + name);
}

bool TextureAtlasData::contains(std::string_view resourceName) const {
    return spriteIndices_.contains(normalizeResourceName(resourceName));
}

void TextureAtlasData::addMissingSprite() {
    AtlasSprite sprite;
    sprite.name = "minecraft:missingno";
    sprite.width = 16;
    sprite.height = 16;
    sprite.frames.push_back(makeMissingFrame());
    sprite.animation.push_back({0, 1});
    spriteIndices_.emplace(sprite.name, sprites_.size());
    sprites_.push_back(std::move(sprite));
}

void TextureAtlasData::discoverDirectory(const std::filesystem::path& root,
                                         std::string_view resourcePrefix,
                                         int maximumTextureSize) {
    if (!std::filesystem::exists(root)) return;
    std::vector<std::filesystem::path> pngs;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") pngs.push_back(entry.path());
    }
    std::sort(pngs.begin(), pngs.end());

    for (const std::filesystem::path& path : pngs) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* source = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (source == nullptr) throw std::runtime_error("Could not load Minecraft texture: " + path.string());
        if (width <= 0 || width > maximumTextureSize || height <= 0 || height % width != 0) {
            stbi_image_free(source);
            throw std::runtime_error("Unsupported block/item texture dimensions (maximum 128px square frames): " + path.string());
        }

        const int frameCount = height / width;
        AtlasSprite sprite;
        sprite.name = resourceNameFor(root, path, resourcePrefix);
        sprite.width = width;
        sprite.height = width;
        sprite.animation = readAnimation(path.string() + ".mcmeta", frameCount, sprite.interpolate);
        sprite.frames.reserve(static_cast<std::size_t>(frameCount));
        const std::size_t frameBytes = static_cast<std::size_t>(width * width * 4);
        for (int frame = 0; frame < frameCount; ++frame) {
            const auto* begin = source + static_cast<std::ptrdiff_t>(frameBytes * static_cast<std::size_t>(frame));
            sprite.frames.emplace_back(begin, begin + static_cast<std::ptrdiff_t>(frameBytes));
        }
        stbi_image_free(source);

        const auto [iterator, inserted] = spriteIndices_.emplace(sprite.name, sprites_.size());
        if (!inserted) continue;
        sprites_.push_back(std::move(sprite));
    }
}

void TextureAtlasData::packSprites() {
    if (sprites_.empty()) throw std::runtime_error("No Minecraft textures were discovered");
    int maximumWidth = 16;
    for (const AtlasSprite& sprite : sprites_) maximumWidth = std::max(maximumWidth, sprite.width);
    cellSize_ = maximumWidth + paddingPixels * 2;

    const int count = static_cast<int>(sprites_.size());
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
    const int rows = (count + columns - 1) / columns;
    width_ = nextPowerOfTwo(columns * cellSize_);
    height_ = nextPowerOfTwo(rows * cellSize_);
    if (width_ > 8192 || height_ > 8192)
        throw std::runtime_error("Minecraft block/item texture atlas exceeds 8192x8192");

    pixels_.assign(static_cast<std::size_t>(width_ * height_ * 4), 0);
    animatedSprites_.clear();
    for (std::size_t index = 0; index < sprites_.size(); ++index) {
        AtlasSprite& sprite = sprites_[index];
        const int column = static_cast<int>(index) % columns;
        const int row = static_cast<int>(index) / columns;
        sprite.originX = column * cellSize_ + paddingPixels;
        sprite.originY = row * cellSize_ + paddingPixels;
        sprite.bounds = {
            static_cast<float>(sprite.originX) / static_cast<float>(width_) + uvInset,
            static_cast<float>(sprite.originY) / static_cast<float>(height_) + uvInset,
            static_cast<float>(sprite.originX + sprite.width) / static_cast<float>(width_) - uvInset,
            static_cast<float>(sprite.originY + sprite.height) / static_cast<float>(height_) - uvInset
        };
        const int firstFrame = sprite.animation.empty() ? 0 : sprite.animation.front().textureFrame;
        blitFrame(pixels_, sprite, sprite.frames[static_cast<std::size_t>(firstFrame)]);
        if (sprite.animated()) animatedSprites_.push_back(index);
    }
}

void TextureAtlasData::blitFrame(std::vector<std::uint8_t>& destination,
                                 const AtlasSprite& sprite,
                                 const std::vector<std::uint8_t>& frame) const {
    const int paddedWidth = sprite.width + paddingPixels * 2;
    const int paddedHeight = sprite.height + paddingPixels * 2;
    for (int py = 0; py < paddedHeight; ++py) {
        const int sourceY = std::clamp(py - paddingPixels, 0, sprite.height - 1);
        const int destinationY = sprite.originY - paddingPixels + py;
        for (int px = 0; px < paddedWidth; ++px) {
            const int sourceX = std::clamp(px - paddingPixels, 0, sprite.width - 1);
            const int destinationX = sprite.originX - paddingPixels + px;
            const std::size_t sourceOffset = static_cast<std::size_t>((sourceY * sprite.width + sourceX) * 4);
            const std::size_t destinationOffset = static_cast<std::size_t>((destinationY * width_ + destinationX) * 4);
            std::memcpy(destination.data() + destinationOffset, frame.data() + sourceOffset, 4);
        }
    }
}
