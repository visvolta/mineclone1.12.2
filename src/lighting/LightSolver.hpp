#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "world/Chunk.hpp"

class World;

// A 15-block halo is sufficient because vanilla light is at most level 15 and
// loses at least one level per step. Sources outside this snapshot cannot
// contribute a positive value to the center chunk.
inline constexpr int lightingHalo = 15;
inline constexpr int lightingSnapshotWidth = chunkSize + lightingHalo * 2;

struct LightingSnapshot {
    int chunkX = 0;
    int chunkZ = 0;
    std::array<std::uint64_t, 9> neighborhoodEpochs{};
    std::vector<std::uint8_t> opacity;
    std::vector<std::uint8_t> emission;
};

struct ChunkLightingData {
    int chunkX = 0;
    int chunkZ = 0;
    std::array<std::uint64_t, 9> neighborhoodEpochs{};
    std::vector<std::uint8_t> sky;
    std::vector<std::uint8_t> block;
};

class LightSolver {
public:
    [[nodiscard]] static LightingSnapshot capture(const World& world, int chunkX, int chunkZ);
    [[nodiscard]] static ChunkLightingData solve(LightingSnapshot snapshot);
    [[nodiscard]] static bool isCurrent(const World& world, const ChunkLightingData& result);
};
