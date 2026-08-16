#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "world/Chunk.hpp"

class BlockRenderResources;
class World;

inline constexpr int meshingSnapshotHalo = 2;
inline constexpr int meshingSnapshotSize = sectionSize + meshingSnapshotHalo * 2;

// Immutable section neighborhood. Minecraft 1.12.2 smooth AO samples cells
// beyond the face-adjacent edge/corner samples, so two blocks of halo are kept.
// Capture happens on the main thread; meshing workers only read this structure.
struct SectionSnapshot {
    int chunkX = 0;
    int sectionY = 0;
    int chunkZ = 0;
    std::array<BlockState, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> blocks{};
    std::array<std::uint8_t, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> skyLight{};
    std::array<std::uint8_t, meshingSnapshotSize * meshingSnapshotSize * meshingSnapshotSize> blockLight{};
    std::array<std::uint8_t, meshingSnapshotSize * meshingSnapshotSize> biomes{};

    [[nodiscard]] static constexpr bool contains(int x, int y, int z) noexcept {
        return x >= -meshingSnapshotHalo && x < sectionSize + meshingSnapshotHalo &&
               y >= -meshingSnapshotHalo && y < sectionSize + meshingSnapshotHalo &&
               z >= -meshingSnapshotHalo && z < sectionSize + meshingSnapshotHalo;
    }

    [[nodiscard]] static constexpr bool containsColumn(int x, int z) noexcept {
        return x >= -meshingSnapshotHalo && x < sectionSize + meshingSnapshotHalo &&
               z >= -meshingSnapshotHalo && z < sectionSize + meshingSnapshotHalo;
    }

    [[nodiscard]] static std::size_t index(int x, int y, int z) noexcept {
        assert(contains(x, y, z));
        return static_cast<std::size_t>(
            ((y + meshingSnapshotHalo) * meshingSnapshotSize +
             (z + meshingSnapshotHalo)) * meshingSnapshotSize +
            (x + meshingSnapshotHalo));
    }

    [[nodiscard]] static std::size_t columnIndex(int x, int z) noexcept {
        assert(containsColumn(x, z));
        return static_cast<std::size_t>((z + meshingSnapshotHalo) * meshingSnapshotSize +
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

    [[nodiscard]] std::uint8_t biome(int x, int z) const noexcept {
        if (!containsColumn(x, z)) return 1; // plains
        return biomes[columnIndex(x, z)];
    }

    [[nodiscard]] int worldX(int localX) const noexcept { return chunkX * chunkSize + localX; }
    [[nodiscard]] int worldY(int localY) const noexcept { return sectionY * sectionSize + localY; }
    [[nodiscard]] int worldZ(int localZ) const noexcept { return chunkZ * chunkSize + localZ; }
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

using SectionMeshData = std::array<MeshData, static_cast<std::size_t>(RenderLayer::Count)>;

class ChunkMesher {
public:
    [[nodiscard]] static SectionSnapshot capture(const World& world, int chunkX, int sectionY, int chunkZ);
    [[nodiscard]] static SectionMeshData build(const SectionSnapshot& snapshot,
                                               const BlockRenderResources& resources);
    [[nodiscard]] static SectionMeshData build(const World& world, int chunkX, int sectionY, int chunkZ,
                                               const BlockRenderResources& resources);
};
