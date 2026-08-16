#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "world/Chunk.hpp"

class World;

inline constexpr int meshingSnapshotHalo = 2;
inline constexpr int meshingSnapshotSize = sectionSize + meshingSnapshotHalo * 2;

// Immutable section neighborhood. Vanilla-style AO samples one step beyond
// face-adjacent tangent cells, so a two-block halo is required at section
// boundaries. Capture happens on the main thread; workers only read snapshots.
struct SectionSnapshot {
    std::array<BlockState, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> blocks{};
    std::array<std::uint8_t, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> skyLight{};
    std::array<std::uint8_t, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> blockLight{};

    [[nodiscard]] static constexpr bool contains(int x, int y, int z) noexcept {
        return x >= -meshingSnapshotHalo && x < sectionSize + meshingSnapshotHalo &&
               y >= -meshingSnapshotHalo && y < sectionSize + meshingSnapshotHalo &&
               z >= -meshingSnapshotHalo && z < sectionSize + meshingSnapshotHalo;
    }

    [[nodiscard]] static std::size_t index(int x, int y, int z) noexcept {
        assert(contains(x, y, z));
        return static_cast<std::size_t>(
            ((y + meshingSnapshotHalo) * meshingSnapshotSize +
             (z + meshingSnapshotHalo)) * meshingSnapshotSize +
            (x + meshingSnapshotHalo));
    }

    [[nodiscard]] BlockState get(int x, int y, int z) const noexcept {
        if (!contains(x, y, z)) return makeBlockState(0);
        return blocks[index(x, y, z)];
    }

    [[nodiscard]] std::uint8_t sky(int x, int y, int z) const noexcept {
        if (!contains(x, y, z)) return y >= sectionSize ? 15 : 0;
        return skyLight[index(x, y, z)];
    }

    [[nodiscard]] std::uint8_t block(int x, int y, int z) const noexcept {
        if (!contains(x, y, z)) return 0;
        return blockLight[index(x, y, z)];
    }
};

struct MeshVertex {
    float x, y, z;
    float u, v;
    float red, green, blue;
    float overlayU, overlayV;
    float overlay;
    float shade;
    float skyLight;
    float blockLight;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

using SectionMeshData = std::array<MeshData, 3>;

class ChunkMesher {
public:
    [[nodiscard]] static SectionSnapshot capture(const World& world, int chunkX, int sectionY, int chunkZ);
    [[nodiscard]] static SectionMeshData build(const SectionSnapshot& snapshot);
    [[nodiscard]] static SectionMeshData build(const World& world, int chunkX, int sectionY, int chunkZ);
};
