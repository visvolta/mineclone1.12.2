#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "worldgen/WorldConfig.hpp"

struct BiomeDefinition {
    std::int32_t id;
    float baseHeight;
    float heightVariation;
    float temperature;
    float rainfall;
    bool snowy;
};

class BiomeProvider {
public:
    explicit BiomeProvider(const WorldConfig& config);
    ~BiomeProvider();
    BiomeProvider(const BiomeProvider&) = delete;
    BiomeProvider& operator=(const BiomeProvider&) = delete;

    [[nodiscard]] std::vector<std::int32_t> getBiomes(int x, int z, int width, int height) const;
    [[nodiscard]] std::vector<std::int32_t> getBiomesForGeneration(int x, int z, int width, int height) const;
    [[nodiscard]] static const BiomeDefinition& definition(std::int32_t id);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
