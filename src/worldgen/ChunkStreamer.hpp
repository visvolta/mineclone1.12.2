#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/ThreadPool.hpp"
#include "worldgen/WorldConfig.hpp"

class BlockEntitySystem;
class Chunk;
class World;
class WorldSave;

struct ChunkCoordinate { int x = 0; int z = 0; };
struct ChunkStreamChanges { std::vector<ChunkCoordinate> loaded; std::vector<ChunkCoordinate> unloaded; };

class ChunkStreamer {
public:
    ChunkStreamer(World& world, const WorldConfig& config, int viewDistance,
                  WorldSave* save = nullptr, BlockEntitySystem* blockEntities = nullptr);
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;

    void prime(double playerX, double playerZ, int radius);
    [[nodiscard]] ChunkStreamChanges update(double playerX, double playerZ,
                                            float forwardX, float forwardZ,
                                            double budgetMilliseconds);
    void flushCache();

    [[nodiscard]] ThreadPool& workers() { return workers_; }
    [[nodiscard]] std::size_t pendingGenerationCount() const { return generationQueued_.size(); }
    [[nodiscard]] std::size_t cachedChunkCount() const { return cache_.size(); }

private:
    struct CompletionQueue;
    struct CacheEntry { std::unique_ptr<Chunk> chunk; std::uint64_t lastUse = 0; };

    [[nodiscard]] static std::uint64_t key(int chunkX, int chunkZ);
    [[nodiscard]] bool schedule(int chunkX, int chunkZ, int priority);
    void cacheChunk(std::unique_ptr<Chunk> chunk);
    void trimCache();
    [[nodiscard]] bool tryLoadSaved(int chunkX, int chunkZ, bool active, ChunkStreamChanges* changes);

    World& world_;
    WorldConfig config_;
    int viewDistance_;
    std::size_t cacheCapacity_ = 128;
    std::uint64_t useCounter_ = 0;
    WorldSave* save_ = nullptr;
    BlockEntitySystem* blockEntities_ = nullptr;
    std::shared_ptr<int> generatorIdentity_;
    std::shared_ptr<CompletionQueue> completions_;
    ThreadPool workers_;
    std::unordered_set<std::uint64_t> generationQueued_;
    std::unordered_set<std::uint64_t> generationGroupsQueued_;
    std::unordered_set<std::uint64_t> diskChecked_;
    std::unordered_map<std::uint64_t, CacheEntry> cache_;
};
