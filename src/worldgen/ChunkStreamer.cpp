#include "worldgen/ChunkStreamer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include "world/World.hpp"
#include "worldgen/TerrainGenerator.hpp"
#include "worldgen/FlatGeneratorSettings.hpp"
#include "worldgen/StructureGenerator.hpp"
#include "worldgen/WorldPopulator.hpp"

namespace {

struct GeneratedRegion {
    ChunkCoordinate origin;
    std::vector<std::unique_ptr<Chunk>> chunks;
    std::string error;
};

int floorDiv3(int value) {
    int quotient = value / 3;
    if (value < 0 && value % 3 != 0) --quotient;
    return quotient;
}

ChunkCoordinate regionOrigin(int chunkX, int chunkZ) {
    return {floorDiv3(chunkX + 1) * 3 - 1, floorDiv3(chunkZ + 1) * 3 - 1};
}

std::vector<std::unique_ptr<Chunk>> generatePrivateRegion(
        const WorldConfig& config, int originX, int originZ,
        const std::shared_ptr<int>& identity) {
    thread_local std::weak_ptr<int> workerIdentity;
    thread_local std::unique_ptr<TerrainGenerator> generator;
    thread_local std::unique_ptr<WorldPopulator> populator;
    thread_local std::unique_ptr<StructureGenerator> structures;
    const std::shared_ptr<int> currentIdentity = workerIdentity.lock();
    if (currentIdentity != identity || !generator) {
        generator = std::make_unique<TerrainGenerator>(config);
        populator = std::make_unique<WorldPopulator>(config);
        structures = std::make_unique<StructureGenerator>(config);
        workerIdentity = identity;
    }
    World privateWorld;
    // A 5x5 terrain halo permits the sixteen population origins surrounding
    // the returned 3x3 to perform their vanilla +8..+23 cross-chunk writes.
    // Flat/debug presets without decoration need only the returned region.
    const FlatGeneratorSettings flat = FlatGeneratorSettings::parse(config.generatorOptions);
    const bool needsPopulationHalo = config.worldType != WorldType::DebugAllBlockStates &&
        (config.worldType != WorldType::Flat || !flat.features.empty());
    const int halo = needsPopulationHalo ? 1 : 0;
    const int farEdge = needsPopulationHalo ? 3 : 2;
    for (int z = originZ - halo; z <= originZ + farEdge; ++z)
        for (int x = originX - halo; x <= originX + farEdge; ++x)
            generator->generateChunk(privateWorld, x, z);
    if (needsPopulationHalo) {
        // Vanilla creates structure pieces before lakes, dungeons, biome
        // decoration and freezing. Generate every start whose bounding area
        // can touch the returned region before running those population steps.
        structures->generateStartsIntersecting(privateWorld, originX, originZ,
                                                originX + 2, originZ + 2);
        for (int z = originZ - 1; z <= originZ + 2; ++z)
            for (int x = originX - 1; x <= originX + 2; ++x)
                populator->populate(privateWorld, x, z);
    }

    std::vector<std::unique_ptr<Chunk>> result;
    result.reserve(9);
    for (int z = originZ; z < originZ + 3; ++z)
        for (int x = originX; x < originX + 3; ++x)
            result.push_back(privateWorld.extractChunk(x, z));
    return result;
}

} // namespace

struct ChunkStreamer::CompletionQueue {
    std::mutex mutex;
    std::deque<GeneratedRegion> regions;
};

std::uint64_t ChunkStreamer::key(int chunkX, int chunkZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32U) |
        static_cast<std::uint32_t>(chunkZ);
}

ChunkStreamer::ChunkStreamer(World& world, const WorldConfig& config, int viewDistance)
    : world_(world), config_(config), viewDistance_(viewDistance),
      cacheCapacity_(static_cast<std::size_t>(config.chunkCacheCapacity)),
      generatorIdentity_(std::make_shared<int>(0)), completions_(std::make_shared<CompletionQueue>()),
      workers_(ThreadPool::recommendedWorkerCount()) {}

ChunkStreamer::~ChunkStreamer() = default;

void ChunkStreamer::prime(double playerX, double playerZ, int radius) {
    const int centerX = World::floorDiv16(static_cast<int>(std::floor(playerX)));
    const int centerZ = World::floorDiv16(static_cast<int>(std::floor(playerZ)));
    std::vector<std::future<GeneratedRegion>> futures;
    std::unordered_set<std::uint64_t> regions;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const ChunkCoordinate origin = regionOrigin(centerX + dx, centerZ + dz);
            if (!regions.insert(key(origin.x, origin.z)).second) continue;
            futures.push_back(workers_.submit(WorkerTaskClass::Terrain, 100000 - (dx * dx + dz * dz),
                [config = config_, origin, identity = generatorIdentity_] {
                    return GeneratedRegion{origin,
                        generatePrivateRegion(config, origin.x, origin.z, identity), {}};
                }));
        }
    }
    for (auto& future : futures) {
        GeneratedRegion region = future.get();
        for (auto& chunk : region.chunks) {
            if (chunk && std::abs(chunk->x() - centerX) <= radius &&
                std::abs(chunk->z() - centerZ) <= radius)
                world_.insertChunk(std::move(chunk));
        }
    }
}

bool ChunkStreamer::schedule(int chunkX, int chunkZ, int priority) {
    const ChunkCoordinate origin = regionOrigin(chunkX, chunkZ);
    const std::uint64_t groupKey = key(origin.x, origin.z);
    if (!generationGroupsQueued_.insert(groupKey).second) return false;
    for (int z = origin.z; z < origin.z + 3; ++z)
        for (int x = origin.x; x < origin.x + 3; ++x)
            generationQueued_.insert(key(x, z));
    const std::shared_ptr<CompletionQueue> completions = completions_;
    workers_.enqueue(WorkerTaskClass::Terrain, priority,
                     [config = config_, origin,
                      identity = generatorIdentity_, completions] {
        GeneratedRegion result{origin, {}, {}};
        try {
            result.chunks = generatePrivateRegion(config, origin.x, origin.z, identity);
        } catch (const std::exception& error) {
            result.error = error.what();
        } catch (...) {
            result.error = "Unknown terrain worker failure";
        }
        std::lock_guard lock(completions->mutex);
        completions->regions.push_back(std::move(result));
    });
    return true;
}

void ChunkStreamer::cacheChunk(std::unique_ptr<Chunk> chunk) {
    if (!chunk) return;
    // MSVC may evaluate the move before the key expression. Capture the
    // coordinates first so cache insertion can never dereference a moved-from
    // unique_ptr while the world approaches the cache/unload threshold.
    const std::uint64_t chunkKey = key(chunk->x(), chunk->z());
    CacheEntry entry{std::move(chunk), ++useCounter_};
    cache_.insert_or_assign(chunkKey, std::move(entry));
}

void ChunkStreamer::trimCache() {
    while (cache_.size() > cacheCapacity_) {
        auto oldest = cache_.end();
        for (auto iterator = cache_.begin(); iterator != cache_.end(); ++iterator) {
            if (oldest == cache_.end() || iterator->second.lastUse < oldest->second.lastUse) oldest = iterator;
        }
        if (oldest == cache_.end()) return;
        cache_.erase(oldest);
    }
}

ChunkStreamChanges ChunkStreamer::update(double playerX, double playerZ,
                                         float forwardX, float forwardZ,
                                         double budgetMilliseconds) {
    ChunkStreamChanges changes;
    const auto frameWorkStart = std::chrono::steady_clock::now();
    const auto withinBudget = [&] {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frameWorkStart).count() < budgetMilliseconds;
    };
    const int centerX = World::floorDiv16(static_cast<int>(std::floor(playerX)));
    const int centerZ = World::floorDiv16(static_cast<int>(std::floor(playerZ)));
    const int prefetchDistance = viewDistance_ + 1;
    const auto inside = [=](int x, int z, int distance) {
        return std::abs(x - centerX) <= distance && std::abs(z - centerZ) <= distance;
    };

    bool integratedAny = false;
    while (withinBudget() || !integratedAny) {
        GeneratedRegion result;
        {
            std::lock_guard lock(completions_->mutex);
            if (completions_->regions.empty()) break;
            result = std::move(completions_->regions.front());
            completions_->regions.pop_front();
        }
        integratedAny = true;
        generationGroupsQueued_.erase(key(result.origin.x, result.origin.z));
        for (int z = result.origin.z; z < result.origin.z + 3; ++z)
            for (int x = result.origin.x; x < result.origin.x + 3; ++x)
                generationQueued_.erase(key(x, z));
        if (!result.error.empty()) {
            std::cerr << "Chunk generation failed for region " << result.origin.x << ", "
                      << result.origin.z << ": " << result.error << '\n';
            continue;
        }
        for (auto& chunk : result.chunks) {
            if (!chunk) continue;
            const ChunkCoordinate coordinate{chunk->x(), chunk->z()};
            const std::uint64_t chunkKey = key(coordinate.x, coordinate.z);
            if (world_.findChunk(coordinate.x, coordinate.z) != nullptr || cache_.contains(chunkKey))
                continue;
            if (inside(coordinate.x, coordinate.z, viewDistance_)) {
                world_.insertChunk(std::move(chunk));
                changes.loaded.push_back(coordinate);
            } else {
                cacheChunk(std::move(chunk));
            }
        }
    }

    std::vector<ChunkCoordinate> outside;
    outside.reserve(world_.chunks().size());
    for (const auto& [chunkKey, chunk] : world_.chunks()) {
        static_cast<void>(chunkKey);
        if (!inside(chunk->x(), chunk->z(), viewDistance_)) outside.push_back({chunk->x(), chunk->z()});
    }
    for (const ChunkCoordinate coordinate : outside) {
        if (!withinBudget()) break;
        cacheChunk(world_.extractChunk(coordinate.x, coordinate.z));
        changes.unloaded.push_back(coordinate);
    }

    std::vector<ChunkCoordinate> cachedCandidates;
    for (int dz = -viewDistance_; dz <= viewDistance_; ++dz) {
        for (int dx = -viewDistance_; dx <= viewDistance_; ++dx) {
            const int x = centerX + dx;
            const int z = centerZ + dz;
            if (world_.findChunk(x, z) == nullptr && cache_.contains(key(x, z)))
                cachedCandidates.push_back({x, z});
        }
    }
    std::sort(cachedCandidates.begin(), cachedCandidates.end(), [=](const ChunkCoordinate& left,
                                                                    const ChunkCoordinate& right) {
        const int leftX = left.x - centerX;
        const int leftZ = left.z - centerZ;
        const int rightX = right.x - centerX;
        const int rightZ = right.z - centerZ;
        const int leftDistance = leftX * leftX + leftZ * leftZ;
        const int rightDistance = rightX * rightX + rightZ * rightZ;
        const float leftForward = leftX * forwardX + leftZ * forwardZ;
        const float rightForward = rightX * forwardX + rightZ * forwardZ;
        if (leftDistance != rightDistance) return leftDistance < rightDistance;
        if (leftForward != rightForward) return leftForward > rightForward;
        return std::tie(left.x, left.z) < std::tie(right.x, right.z);
    });
    for (const ChunkCoordinate coordinate : cachedCandidates) {
        if (!withinBudget()) break;
        const auto cached = cache_.find(key(coordinate.x, coordinate.z));
        world_.insertChunk(std::move(cached->second.chunk));
        cache_.erase(cached);
        changes.loaded.push_back(coordinate);
    }
    trimCache();

    struct Candidate { int x; int z; int priority; };
    std::vector<Candidate> candidates;
    for (int dz = -prefetchDistance; dz <= prefetchDistance; ++dz) {
        for (int dx = -prefetchDistance; dx <= prefetchDistance; ++dx) {
            const int x = centerX + dx;
            const int z = centerZ + dz;
            const std::uint64_t chunkKey = key(x, z);
            if (world_.findChunk(x, z) != nullptr || cache_.contains(chunkKey) || generationQueued_.contains(chunkKey)) continue;
            const int distanceSquared = dx * dx + dz * dz;
            const float length = std::sqrt(static_cast<float>(std::max(1, distanceSquared)));
            const float forwardBias = (dx * forwardX + dz * forwardZ) / length;
            const bool active = std::abs(dx) <= viewDistance_ && std::abs(dz) <= viewDistance_;
            const int priority = (active ? 100000 : 0) - distanceSquared * 100 +
                static_cast<int>(forwardBias * 25.0F);
            candidates.push_back({x, z, priority});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return std::tie(left.priority, left.x, left.z) > std::tie(right.priority, right.x, right.z);
    });

    const std::size_t maximumOutstanding = std::max<std::size_t>(2, workers_.workerCount());
    const std::size_t available = generationGroupsQueued_.size() < maximumOutstanding
        ? maximumOutstanding - generationGroupsQueued_.size() : 0;
    std::size_t scheduled = 0;
    for (const Candidate& candidate : candidates) {
        if (scheduled >= available) break;
        if (schedule(candidate.x, candidate.z, candidate.priority)) ++scheduled;
    }

    return changes;
}
