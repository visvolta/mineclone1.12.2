#include "worldgen/FlatGeneratorSettings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <utility>

#include "blocks/BlockRegistry.hpp"

namespace {

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        parts.push_back(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

int integer(std::string_view text, int fallback) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? value : fallback;
}

BlockState namedBlock(std::string_view name, int metadata, bool& valid) {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    static constexpr std::array names{
        std::pair{"air", BlockId::Air}, std::pair{"stone", BlockId::Stone},
        std::pair{"grass", BlockId::Grass}, std::pair{"dirt", BlockId::Dirt},
        std::pair{"cobblestone", BlockId::Cobblestone}, std::pair{"planks", BlockId::Planks},
        std::pair{"bedrock", BlockId::Bedrock}, std::pair{"flowing_water", BlockId::FlowingWater},
        std::pair{"water", BlockId::Water}, std::pair{"flowing_lava", BlockId::FlowingLava},
        std::pair{"lava", BlockId::Lava}, std::pair{"sand", BlockId::Sand},
        std::pair{"gravel", BlockId::Gravel}, std::pair{"gold_ore", BlockId::GoldOre},
        std::pair{"iron_ore", BlockId::IronOre}, std::pair{"coal_ore", BlockId::CoalOre},
        std::pair{"log", BlockId::Log}, std::pair{"leaves", BlockId::Leaves},
        std::pair{"glass", BlockId::Glass}, std::pair{"lapis_ore", BlockId::LapisOre},
        std::pair{"sandstone", BlockId::Sandstone}, std::pair{"diamond_ore", BlockId::DiamondOre},
        std::pair{"redstone_ore", BlockId::RedstoneOre}, std::pair{"snow", BlockId::Snow},
        std::pair{"clay", BlockId::Clay}, std::pair{"hardened_clay", BlockId::HardenedClay},
        std::pair{"stained_hardened_clay", BlockId::StainedHardenedClay},
        std::pair{"red_sandstone", BlockId::RedSandstone}, std::pair{"barrier", BlockId::Barrier}
    };
    for (const auto& [candidate, id] : names) {
        if (name == candidate) {
            valid = true;
            return block(id, static_cast<std::uint8_t>(metadata >= 0 && metadata <= 15 ? metadata : 0));
        }
    }
    const int numeric = integer(name, -1);
    valid = numeric >= 0 && numeric <= 255;
    return valid ? makeBlockState(static_cast<std::uint16_t>(numeric),
                                  static_cast<std::uint8_t>(metadata >= 0 && metadata <= 15 ? metadata : 0))
                 : block(BlockId::Air);
}

FlatGeneratorSettings defaults() {
    FlatGeneratorSettings result;
    result.layers = {{0, 1, block(BlockId::Bedrock)}, {1, 2, block(BlockId::Dirt)},
                     {3, 1, block(BlockId::Grass)}};
    result.features.emplace("village", FlatFeatureOptions{});
    result.seaLevel = 4;
    return result;
}

std::string lowercase(std::string_view text) {
    std::string output(text);
    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return output;
}

} // namespace

bool FlatGeneratorSettings::hasFeature(std::string_view name) const {
    return features.find(name) != features.end();
}

const FlatFeatureOptions* FlatGeneratorSettings::feature(std::string_view name) const {
    const auto found = features.find(name);
    return found == features.end() ? nullptr : &found->second;
}

FlatGeneratorSettings FlatGeneratorSettings::parse(std::string_view settings) {
    if (settings.empty() || settings == "{}") return defaults();
    const std::vector<std::string_view> fields = split(settings, ';');
    const int version = fields.size() == 1 ? 0 : integer(fields[0], 0);
    if (version < 0 || version > 3) return defaults();
    const std::size_t layerField = fields.size() == 1 ? 0 : 1;
    if (layerField >= fields.size() || fields[layerField].empty()) return defaults();

    FlatGeneratorSettings result;
    int height = 0;
    for (std::string_view description : split(fields[layerField], ',')) {
        const char multiplier = version >= 3 ? '*' : 'x';
        const std::size_t separator = description.find(multiplier);
        int count = 1;
        if (separator != std::string_view::npos) {
            count = integer(description.substr(0, separator), -1);
            description.remove_prefix(separator + 1);
        }
        if (count < 0) return defaults();
        count = std::min(count, 256 - height);

        int metadata = 0;
        std::string_view name = description;
        if (version >= 3) {
            const std::size_t firstColon = description.find(':');
            const std::size_t secondColon = firstColon == std::string_view::npos
                ? std::string_view::npos : description.find(':', firstColon + 1);
            if (secondColon != std::string_view::npos) {
                metadata = integer(description.substr(secondColon + 1), 0);
                name = description.substr(0, secondColon);
            }
        } else {
            const std::size_t colon = description.find(':');
            if (colon != std::string_view::npos) {
                metadata = integer(description.substr(colon + 1), 0);
                name = description.substr(0, colon);
            }
        }
        bool valid = false;
        const BlockState state = namedBlock(name, metadata, valid);
        if (!valid) return defaults();
        result.layers.push_back({height, count, state});
        height += count;
    }
    if (result.layers.empty()) return defaults();

    std::size_t field = layerField + 1;
    result.biome = version > 0 && field < fields.size() ? integer(fields[field++], 1) : 1;
    if (version > 0 && field < fields.size()) {
        for (std::string_view description : split(fields[field], ',')) {
            const std::size_t open = description.find('(');
            const std::string name = lowercase(description.substr(0, open));
            if (name.empty()) continue;
            auto& options = result.features[name];
            if (open == std::string_view::npos || description.back() != ')') continue;
            const std::string_view body = description.substr(open + 1, description.size() - open - 2);
            for (std::string_view assignment : split(body, ' ')) {
                const std::size_t equals = assignment.find('=');
                if (equals != std::string_view::npos)
                    options.emplace(lowercase(assignment.substr(0, equals)),
                                    std::string(assignment.substr(equals + 1)));
            }
        }
    } else {
        result.features.emplace("village", FlatFeatureOptions{});
    }

    int pendingAir = 0;
    for (const FlatLayer& layer : result.layers) {
        if (blockId(layer.state) == static_cast<std::uint16_t>(BlockId::Air)) pendingAir += layer.count;
        else { result.seaLevel += layer.count + pendingAir; pendingAir = 0; }
    }
    result.decoration = result.hasFeature("decoration") &&
        !(result.layers.size() == 1 && blockId(result.layers.front().state) == static_cast<std::uint16_t>(BlockId::Air)) &&
        result.biome != 127;
    result.dungeons = result.hasFeature("dungeon");
    result.waterLake = result.hasFeature("lake");
    result.lavaLake = result.hasFeature("lava_lake");
    return result;
}
