#pragma once

#include <cstdint>
#include <memory>

#include "worldgen/WorldConfig.hpp"

class World;

class TerrainGenerator {
public:
    explicit TerrainGenerator(const WorldConfig& config);
    ~TerrainGenerator();
    TerrainGenerator(const TerrainGenerator&) = delete;
    TerrainGenerator& operator=(const TerrainGenerator&) = delete;

    void generateChunk(World& world, int chunkX, int chunkZ);
    [[nodiscard]] std::int64_t seed() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
