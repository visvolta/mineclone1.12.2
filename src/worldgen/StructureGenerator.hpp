#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "worldgen/ChunkGeneratorSettings.hpp"
#include "worldgen/FlatGeneratorSettings.hpp"
#include "worldgen/WorldConfig.hpp"

class BiomeProvider;
class JavaRandom;
class World;

// Deterministic Overworld structure-start coordinator. Starts are derived from
// the same 1.12.2 salts and Java overflow rules and are generated before biome
// decoration, matching ChunkGeneratorOverworld's population order.
class StructureGenerator {
public:
    explicit StructureGenerator(const WorldConfig& config);
    ~StructureGenerator();

    void generateStartsIntersecting(World& world, int minimumChunkX, int minimumChunkZ,
                                    int maximumChunkX, int maximumChunkZ) const;
    [[nodiscard]] bool isStructureStart(int chunkX, int chunkZ) const;

private:
    enum class Type { Mineshaft, Village, Stronghold, DesertPyramid, JungleTemple,
                      SwampHut, Igloo, Monument, Mansion };
    [[nodiscard]] std::vector<Type> startTypes(int chunkX, int chunkZ) const;
    [[nodiscard]] bool startType(int chunkX, int chunkZ, Type& type) const;
    void place(World& world, Type type, int chunkX, int chunkZ) const;

    WorldConfig config_;
    ChunkGeneratorSettings settings_;
    FlatGeneratorSettings flat_;
    std::unique_ptr<BiomeProvider> biomes_;
    std::vector<std::pair<int, int>> strongholds_;
};
