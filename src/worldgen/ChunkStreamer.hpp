#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/ThreadPool.hpp"
#include "worldgen/WorldConfig.hpp"

class Chunk;
class World;

struct ChunkCoordinate {
    int x = 0;
    int z = 0;
};

struct ChunkStreamChanges {
    std::vector<ChunkCoordinate> loaded;
    std::vector<ChunkCoordinate> unloaded;
};

// Coordinates terrain jobs, completed-chunk integration, and a bounded LRU
// cache. Workers only touch private Worlds; the live World remains main-thread
// owned and OpenGL never crosses the thread boundary.
class ChunkStreamer {
public:
    ChunkStreamer(World& world, const WorldConfig& config, int viewDistance);
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;

    void prime(double playerX, double playerZ, int radius);
    [[nodiscard]] ChunkStreamChanges update(double playerX, double playerZ,
                                            float forwardX, float forwardZ,
                                            double budgetMilliseconds);

    [[nodiscard]] ThreadPool& workers() { return workers_; }
    [[nodiscard]] std::size_t pendingGenerationCount() const { return generationQueued_.size(); }
    [[nodiscard]] std::size_t cachedChunkCount() const { return cache_.size(); }

private:
    struct CompletionQueue;
    struct CacheEntry {
        std::unique_ptr<Chunk> chunk;
        std::uint64_t lastUse = 0;
    };

    [[nodiscard]] static std::uint64_t key(int chunkX, int chunkZ);
    [[nodiscard]] bool schedule(int chunkX, int chunkZ, int priority);
    void cacheChunk(std::unique_ptr<Chunk> chunk);
    void trimCache();

    World& world_;
    WorldConfig config_;
    int viewDistance_;
    std::size_t cacheCapacity_ = 128;
    std::uint64_t useCounter_ = 0;
    std::shared_ptr<int> generatorIdentity_;
    std::shared_ptr<CompletionQueue> completions_;
    ThreadPool workers_;
    std::unordered_set<std::uint64_t> generationQueued_;
    std::unordered_set<std::uint64_t> generationGroupsQueued_;
    std::unordered_map<std::uint64_t, CacheEntry> cache_;
};
