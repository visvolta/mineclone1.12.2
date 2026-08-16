#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "blocks/BlockState.hpp"

struct FlatLayer {
    int minimumY = 0;
    int count = 0;
    BlockState state = 0;
};

using FlatFeatureOptions = std::map<std::string, std::string, std::less<>>;

struct FlatGeneratorSettings {
    std::vector<FlatLayer> layers;
    int biome = 1;
    std::map<std::string, FlatFeatureOptions, std::less<>> features;
    int seaLevel = 0;
    bool decoration = false;
    bool dungeons = false;
    bool waterLake = false;
    bool lavaLake = false;

    [[nodiscard]] bool hasFeature(std::string_view name) const;
    [[nodiscard]] const FlatFeatureOptions* feature(std::string_view name) const;

    static FlatGeneratorSettings parse(std::string_view settings);
};
