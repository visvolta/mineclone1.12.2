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

#include "save/WorldSave.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/World.hpp"
#include "worldgen/FlatGeneratorSettings.hpp"
#include "worldgen/StructureGenerator.hpp"
#include "worldgen/TerrainGenerator.hpp"
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
    const WorldConfig& config, int originX, int originZ, const std::shared_ptr<int>& identity) {
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
    const FlatGeneratorSettings flat = FlatGeneratorSettings::parse(config.generatorOptions);
    const bool needsPopulationHalo = config.worldType != WorldType::DebugAllBlockStates &&
        (config.worldType != WorldType::Flat || !flat.features.empty());
    const int halo = needsPopulationHalo ? 1 : 0;
    const int farEdge = needsPopulationHalo ? 3 : 2;

    for (int z = originZ - halo; z <= originZ + farEdge; ++z)
        for (int x = originX - halo; x <= originX + farEdge; ++x)
            generator->generateChunk(privateWorld, x, z);

    if (needsPopulationHalo) {
        structures->generateStartsIntersecting(privateWorld, originX, originZ, originX + 2, originZ + 2);
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

ChunkStreamer::ChunkStreamer(World& world, const WorldConfig& config, int viewDistance,
                             WorldSave* save, BlockEntitySystem* blockEntities)
    : world_(world), config_(config), viewDistance_(viewDistance),
      cacheCapacity_(static_cast<std::size_t>(config.chunkCacheCapacity)),
      save_(save), blockEntities_(blockEntities), generatorIdentity_(std::make_shared<int>(0)),
      completions_(std::make_shared<CompletionQueue>()),
      workers_(ThreadPool::recommendedWorkerCount()) {}

ChunkStreamer::~ChunkStreamer() {
    try {
        flushCache();
    } catch (...) {
    }
}

bool ChunkStreamer::tryLoadSaved(int chunkX, int chunkZ, bool active,
                                 ChunkStreamChanges* changes) {
    if (save_ == nullptr) return false;
    const std::uint64_t chunkKey = key(chunkX, chunkZ);
    if (!diskChecked_.insert(chunkKey).second) return false;

    std::unique_ptr<Chunk> chunk;
    try {
        chunk = save_->loadChunk(chunkX, chunkZ);
    } catch (const std::exception& error) {
        std::cerr << "Could not load saved chunk " << chunkX << ", " << chunkZ
                  << ": " << error.what() << '\n';
        return false;
    }
    if (!chunk) return false;

    if (active) {
        world_.insertChunk(std::move(chunk));
        if (changes != nullptr) changes->loaded.push_back({chunkX, chunkZ});
    } else {
        cacheChunk(std::move(chunk));
    }
    return true;
}

void ChunkStreamer::prime(double playerX, double playerZ, int radius,
                          const std::function<void(float)>& progress) {
    const int centerX = World::floorDiv16(static_cast<int>(std::floor(playerX)));
    const int centerZ = World::floorDiv16(static_cast<int>(std::floor(playerZ)));
    const int diameter = radius * 2 + 1;
    const int requested = diameter * diameter;
    int completed = 0;
    const auto report = [&] {
        if (progress) progress(requested > 0 ? static_cast<float>(completed) /
                                              static_cast<float>(requested) : 1.0F);
    };
    report();

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            static_cast<void>(tryLoadSaved(centerX + dx, centerZ + dz, true, nullptr));
            if (world_.findChunk(centerX + dx, centerZ + dz) != nullptr) {
                ++completed;
                report();
            }
        }
    }

    std::vector<std::future<GeneratedRegion>> futures;
    std::unordered_set<std::uint64_t> regions;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int chunkX = centerX + dx;
            const int chunkZ = centerZ + dz;
            if (world_.findChunk(chunkX, chunkZ) != nullptr) continue;
            const ChunkCoordinate origin = regionOrigin(chunkX, chunkZ);
            if (!regions.insert(key(origin.x, origin.z)).second) continue;
            futures.push_back(workers_.submit(WorkerTaskClass::Terrain,
                100000 - (dx * dx + dz * dz),
                [config = config_, origin, identity = generatorIdentity_] {
                    return GeneratedRegion{origin,
                        generatePrivateRegion(config, origin.x, origin.z, identity), {}};
                }));
        }
    }

    for (auto& future : futures) {
        GeneratedRegion region = future.get();
        for (auto& chunk : region.chunks) {
            if (!chunk) continue;
            if (std::abs(chunk->x() - centerX) <= radius &&
                std::abs(chunk->z() - centerZ) <= radius &&
                world_.findChunk(chunk->x(), chunk->z()) == nullptr) {
                world_.insertChunk(std::move(chunk));
                ++completed;
                report();
            }
        }
    }
    completed = requested;
    report();
}

bool ChunkStreamer::schedule(int chunkX, int chunkZ, int priority) {
    const ChunkCoordinate origin = regionOrigin(chunkX, chunkZ);
    const std::uint64_t groupKey = key(origin.x, origin.z);
    if (!generationGroupsQueued_.insert(groupKey).second) return false;
    for (int z = origin.z; z < origin.z + 3; ++z)
        for (int x = origin.x; x < origin.x + 3; ++x)
            generationQueued_.insert(key(x, z));

    const auto completions = completions_;
    workers_.enqueue(WorkerTaskClass::Terrain, priority,
        [config = config_, origin, identity = generatorIdentity_, completions] {
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
    const std::uint64_t chunkKey = key(chunk->x(), chunk->z());
    // Do not perform zlib/region-file writes on the render thread merely
    // because a chunk crossed the view-distance boundary. Stage 8 originally
    // did this here and could write dozens of chunks while the player was
    // joining or moving, producing severe frame stalls. Cached chunks remain
    // owned in memory and are persisted on LRU eviction, autosave flush, or
    // clean shutdown.
    cache_.insert_or_assign(chunkKey, CacheEntry{std::move(chunk), ++useCounter_});
}

void ChunkStreamer::trimCache() {
    while (cache_.size() > cacheCapacity_) {
        auto oldest = cache_.end();
        for (auto iterator = cache_.begin(); iterator != cache_.end(); ++iterator) {
            if (oldest == cache_.end() || iterator->second.lastUse < oldest->second.lastUse)
                oldest = iterator;
        }
        if (oldest == cache_.end()) return;
        if (save_ != nullptr && blockEntities_ != nullptr && oldest->second.chunk) {
            try {
                save_->saveChunk(*oldest->second.chunk, *blockEntities_);
                diskChecked_.erase(oldest->first);
            } catch (...) {
            }
        }
        cache_.erase(oldest);
    }
}

void ChunkStreamer::flushCache() {
    if (save_ == nullptr || blockEntities_ == nullptr) return;
    for (auto& [chunkKey, entry] : cache_) {
        if (!entry.chunk) continue;
        save_->saveChunk(*entry.chunk, *blockEntities_);
        diskChecked_.erase(chunkKey);
    }
    // flushCache is a world-close operation. Releasing the entries here also
    // prevents the destructor from writing the same compressed chunks twice.
    cache_.clear();
}

ChunkStreamChanges ChunkStreamer::update(double playerX, double playerZ,
                                         float forwardX, float forwardZ,
                                         double budgetMilliseconds) {
    ChunkStreamChanges changes;
    const auto start = std::chrono::steady_clock::now();
    const auto withinBudget = [&] {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() < budgetMilliseconds;
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
            const auto chunkKey = key(coordinate.x, coordinate.z);
            if (world_.findChunk(coordinate.x, coordinate.z) != nullptr || cache_.contains(chunkKey)) continue;
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
    for (const auto& [unused, chunk] : world_.chunks()) {
        static_cast<void>(unused);
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
    std::sort(cachedCandidates.begin(), cachedCandidates.end(), [=](const auto& left, const auto& right) {
        const int leftX = left.x - centerX, leftZ = left.z - centerZ;
        const int rightX = right.x - centerX, rightZ = right.z - centerZ;
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
        auto iterator = cache_.find(key(coordinate.x, coordinate.z));
        world_.insertChunk(std::move(iterator->second.chunk));
        cache_.erase(iterator);
        changes.loaded.push_back(coordinate);
    }
    trimCache();

    // Disk-backed chunks are checked before terrain generation, but region
    // I/O is intentionally incremental. The old Stage 8 loop could probe the
    // entire (viewDistance+1)^2 area in one frame when opening a world.
    struct DiskCandidate { int x; int z; int distanceSquared; };
    std::vector<DiskCandidate> diskCandidates;
    for (int dz = -prefetchDistance; dz <= prefetchDistance; ++dz) {
        for (int dx = -prefetchDistance; dx <= prefetchDistance; ++dx) {
            const int x = centerX + dx;
            const int z = centerZ + dz;
            const auto chunkKey = key(x, z);
            if (world_.findChunk(x, z) != nullptr || cache_.contains(chunkKey) ||
                generationQueued_.contains(chunkKey) || diskChecked_.contains(chunkKey)) continue;
            diskCandidates.push_back({x, z, dx * dx + dz * dz});
        }
    }
    std::sort(diskCandidates.begin(), diskCandidates.end(), [](const auto& a, const auto& b) {
        return std::tie(a.distanceSquared, a.x, a.z) < std::tie(b.distanceSquared, b.x, b.z);
    });
    constexpr std::size_t maximumDiskChecksPerFrame = 4;
    std::size_t diskChecks = 0;
    for (const DiskCandidate& candidate : diskCandidates) {
        if (diskChecks >= maximumDiskChecksPerFrame || !withinBudget()) break;
        static_cast<void>(tryLoadSaved(candidate.x, candidate.z,
            inside(candidate.x, candidate.z, viewDistance_), &changes));
        ++diskChecks;
    }

    struct Candidate { int x; int z; int priority; };
    std::vector<Candidate> candidates;
    for (int dz = -prefetchDistance; dz <= prefetchDistance; ++dz) {
        for (int dx = -prefetchDistance; dx <= prefetchDistance; ++dx) {
            const int x = centerX + dx;
            const int z = centerZ + dz;
            const auto chunkKey = key(x, z);
            if (world_.findChunk(x, z) != nullptr || cache_.contains(chunkKey) ||
                generationQueued_.contains(chunkKey) || !diskChecked_.contains(chunkKey)) continue;
            const int distanceSquared = dx * dx + dz * dz;
            const float length = std::sqrt(static_cast<float>(std::max(1, distanceSquared)));
            const float forwardBias = (dx * forwardX + dz * forwardZ) / length;
            const bool active = std::abs(dx) <= viewDistance_ && std::abs(dz) <= viewDistance_;
            candidates.push_back({x, z, (active ? 100000 : 0) - distanceSquared * 100 +
                static_cast<int>(forwardBias * 25.0F)});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return std::tie(left.priority, left.x, left.z) > std::tie(right.priority, right.x, right.z);
    });

    const std::size_t maxOutstanding = std::max<std::size_t>(2, workers_.workerCount());
    const std::size_t available = generationGroupsQueued_.size() < maxOutstanding
        ? maxOutstanding - generationGroupsQueued_.size() : 0;
    std::size_t scheduled = 0;
    for (const Candidate& candidate : candidates) {
        if (scheduled >= available) break;
        if (schedule(candidate.x, candidate.z, candidate.priority)) ++scheduled;
    }
    return changes;
}
