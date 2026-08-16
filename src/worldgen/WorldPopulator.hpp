#pragma once

#include <cstdint>

#include "blocks/BlockState.hpp"
#include "worldgen/ChunkGeneratorSettings.hpp"
#include "worldgen/WorldConfig.hpp"

class JavaRandom;
class World;

// Ports the deterministic Overworld population phase that runs after base
// terrain. A population origin writes into the +8..+23 area, exactly like
// ChunkGeneratorOverworld#populate in Java Edition 1.12.2.
class WorldPopulator {
public:
    explicit WorldPopulator(const WorldConfig& config)
        : config_(config), settings_(ChunkGeneratorSettings::fromConfig(config)) {}

    void populate(World& world, int chunkX, int chunkZ) const;

private:
    void decorate(World& world, JavaRandom& random, int chunkX, int chunkZ, int biomeId) const;
    void generateOre(World& world, JavaRandom& random, int x, int y, int z,
                     BlockState state, int size) const;
    void generateTree(World& world, JavaRandom& random, int x, int y, int z,
                      int biomeId, int forcedSpecies = -1) const;
    bool generateLake(World& world, JavaRandom& random, int x, int y, int z,
                      BlockState liquid) const;
    bool generateDungeon(World& world, JavaRandom& random, int x, int y, int z) const;

    WorldConfig config_;
    ChunkGeneratorSettings settings_;
};
