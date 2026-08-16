#include "lighting/LightSolver.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"

namespace {

constexpr std::size_t snapshotCellCount = static_cast<std::size_t>(
    lightingSnapshotWidth * chunkHeight * lightingSnapshotWidth);
constexpr std::size_t chunkCellCount = static_cast<std::size_t>(chunkSize * chunkHeight * chunkSize);

constexpr std::array<std::array<int, 3>, 6> neighbors = {{
    {{-1, 0, 0}}, {{1, 0, 0}}, {{0, -1, 0}},
    {{0, 1, 0}}, {{0, 0, -1}}, {{0, 0, 1}}
}};

[[nodiscard]] std::size_t snapshotIndex(int x, int y, int z) {
    return static_cast<std::size_t>((y * lightingSnapshotWidth + z) * lightingSnapshotWidth + x);
}

[[nodiscard]] std::size_t chunkIndex(int x, int y, int z) {
    return static_cast<std::size_t>((y * chunkSize + z) * chunkSize + x);
}

[[nodiscard]] std::uint8_t propagationOpacity(std::uint8_t opacity, std::uint8_t emission) {
    // World#getRawLight in 1.12.2 lets an opaque light-emitting block retain
    // and spread its own value by treating its opacity as one.
    if (opacity >= 15 && emission > 0) return 1;
    return std::max<std::uint8_t>(1, opacity);
}

void spread(std::vector<std::uint8_t>& light,
            const std::vector<std::uint8_t>& opacity,
            const std::vector<std::uint8_t>& emission,
            std::vector<std::uint32_t>& queue) {
    std::size_t read = 0;
    while (read < queue.size()) {
        const std::size_t cell = queue[read++];
        const int x = static_cast<int>(cell % lightingSnapshotWidth);
        const std::size_t yz = cell / lightingSnapshotWidth;
        const int z = static_cast<int>(yz % lightingSnapshotWidth);
        const int y = static_cast<int>(yz / lightingSnapshotWidth);
        const std::uint8_t source = light[cell];
        if (source <= 1) continue;

        for (const auto& offset : neighbors) {
            const int nx = x + offset[0];
            const int ny = y + offset[1];
            const int nz = z + offset[2];
            if (nx < 0 || nx >= lightingSnapshotWidth ||
                ny < 0 || ny >= chunkHeight ||
                nz < 0 || nz >= lightingSnapshotWidth) continue;
            const std::size_t neighbor = snapshotIndex(nx, ny, nz);
            const std::uint8_t attenuation = propagationOpacity(opacity[neighbor], emission[neighbor]);
            if (attenuation >= source) continue;
            const std::uint8_t candidate = static_cast<std::uint8_t>(source - attenuation);
            if (candidate <= light[neighbor]) continue;
            light[neighbor] = candidate;
            queue.push_back(static_cast<std::uint32_t>(neighbor));
        }
    }
}

} // namespace

LightingSnapshot LightSolver::capture(const World& world, int chunkX, int chunkZ) {
    LightingSnapshot snapshot;
    snapshot.chunkX = chunkX;
    snapshot.chunkZ = chunkZ;
    snapshot.opacity.resize(snapshotCellCount);
    snapshot.emission.resize(snapshotCellCount);

    std::array<const Chunk*, 9> chunks{};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const std::size_t index = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
            chunks[index] = world.findChunk(chunkX + dx, chunkZ + dz);
            snapshot.neighborhoodEpochs[index] = world.chunkEpoch(chunkX + dx, chunkZ + dz);
        }
    }

    const int minimumWorldX = chunkX * chunkSize - lightingHalo;
    const int minimumWorldZ = chunkZ * chunkSize - lightingHalo;
    for (int z = 0; z < lightingSnapshotWidth; ++z) {
        const int worldZ = minimumWorldZ + z;
        const int relativeChunkZ = World::floorDiv16(worldZ) - chunkZ;
        const int localZ = World::floorMod16(worldZ);
        for (int x = 0; x < lightingSnapshotWidth; ++x) {
            const int worldX = minimumWorldX + x;
            const int relativeChunkX = World::floorDiv16(worldX) - chunkX;
            const int localX = World::floorMod16(worldX);
            const Chunk* source = chunks[static_cast<std::size_t>(
                (relativeChunkZ + 1) * 3 + (relativeChunkX + 1))];
            for (int y = 0; y < chunkHeight; ++y) {
                const BlockState state = source ? source->get(localX, y, localZ) : makeBlockState(0);
                const BlockDefinition& definition = BlockRegistry::get(state);
                const std::size_t index = snapshotIndex(x, y, z);
                snapshot.opacity[index] = definition.lightOpacity;
                snapshot.emission[index] = definition.lightValue;
            }
        }
    }
    return snapshot;
}

ChunkLightingData LightSolver::solve(LightingSnapshot snapshot) {
    assert(snapshot.opacity.size() == snapshotCellCount);
    assert(snapshot.emission.size() == snapshotCellCount);
    std::vector<std::uint8_t> sky(snapshotCellCount, 0);
    std::vector<std::uint8_t> block(snapshot.emission);
    std::vector<std::uint32_t> queue;
    queue.reserve(snapshotCellCount / 8);

    // Chunk#generateSkylightMap begins with level 15 at the top of every
    // column. Once attenuated, even air costs one level until darkness.
    for (int z = 0; z < lightingSnapshotWidth; ++z) {
        for (int x = 0; x < lightingSnapshotWidth; ++x) {
            std::uint8_t level = 15;
            for (int y = chunkHeight - 1; y >= 0; --y) {
                const std::size_t index = snapshotIndex(x, y, z);
                std::uint8_t attenuation = snapshot.opacity[index];
                if (attenuation == 0 && level != 15) attenuation = 1;
                level = attenuation >= level ? 0 : static_cast<std::uint8_t>(level - attenuation);
                sky[index] = level;
            }
        }
    }

    // Only enqueue direct-sky cells that can improve a neighbor. This gives
    // the same fixed point as enqueueing every open-sky cell without creating
    // a half-million-entry queue above flat terrain.
    for (int y = 0; y < chunkHeight; ++y) {
        for (int z = 0; z < lightingSnapshotWidth; ++z) {
            for (int x = 0; x < lightingSnapshotWidth; ++x) {
                const std::size_t index = snapshotIndex(x, y, z);
                const std::uint8_t source = sky[index];
                if (source <= 1) continue;
                bool useful = false;
                for (const auto& offset : neighbors) {
                    const int nx = x + offset[0];
                    const int ny = y + offset[1];
                    const int nz = z + offset[2];
                    if (nx < 0 || nx >= lightingSnapshotWidth || ny < 0 || ny >= chunkHeight ||
                        nz < 0 || nz >= lightingSnapshotWidth) continue;
                    const std::size_t neighbor = snapshotIndex(nx, ny, nz);
                    const std::uint8_t attenuation = propagationOpacity(
                        snapshot.opacity[neighbor], snapshot.emission[neighbor]);
                    if (attenuation < source && source - attenuation > sky[neighbor]) {
                        useful = true;
                        break;
                    }
                }
                if (useful) queue.push_back(static_cast<std::uint32_t>(index));
            }
        }
    }
    spread(sky, snapshot.opacity, snapshot.emission, queue);

    queue.clear();
    for (std::size_t index = 0; index < block.size(); ++index)
        if (block[index] > 1) queue.push_back(static_cast<std::uint32_t>(index));
    spread(block, snapshot.opacity, snapshot.emission, queue);

    ChunkLightingData result;
    result.chunkX = snapshot.chunkX;
    result.chunkZ = snapshot.chunkZ;
    result.neighborhoodEpochs = snapshot.neighborhoodEpochs;
    result.sky.resize(chunkCellCount);
    result.block.resize(chunkCellCount);
    for (int y = 0; y < chunkHeight; ++y) {
        for (int z = 0; z < chunkSize; ++z) {
            for (int x = 0; x < chunkSize; ++x) {
                const std::size_t source = snapshotIndex(x + lightingHalo, y, z + lightingHalo);
                const std::size_t destination = chunkIndex(x, y, z);
                result.sky[destination] = sky[source];
                result.block[destination] = block[source];
            }
        }
    }
    return result;
}

bool LightSolver::isCurrent(const World& world, const ChunkLightingData& result) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const std::size_t index = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
            if (world.chunkEpoch(result.chunkX + dx, result.chunkZ + dz) != result.neighborhoodEpochs[index])
                return false;
        }
    }
    return true;
}
