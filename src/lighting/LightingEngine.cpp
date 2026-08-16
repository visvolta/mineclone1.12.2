#include "lighting/LightingEngine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

#include "core/ThreadPool.hpp"
#include "lighting/LightSolver.hpp"
#include "world/World.hpp"

struct LightingEngine::CompletionQueue {
    struct Completion {
        Coordinate coordinate;
        std::uint64_t version = 0;
        ChunkLightingData light;
    };
    std::mutex mutex;
    std::deque<Completion> items;
};

std::size_t LightingEngine::CoordinateHash::operator()(const Coordinate& coordinate) const {
    std::size_t hash = static_cast<std::uint32_t>(coordinate.x);
    hash ^= static_cast<std::size_t>(static_cast<std::uint32_t>(coordinate.z)) * 0x9E3779B185EBCA87ULL;
    return hash;
}

LightingEngine::LightingEngine(World& world, ThreadPool& workers)
    : world_(world), workers_(workers), completions_(std::make_shared<CompletionQueue>()) {}

LightingEngine::~LightingEngine() = default;

void LightingEngine::request(int chunkX, int chunkZ) {
    Chunk* chunk = world_.findChunk(chunkX, chunkZ);
    if (chunk == nullptr) return;
    // Keep existing GPU meshes visible during an edit relight, but prevent a
    // new mesh from sampling a partially updated neighborhood.
    chunk->invalidateLighting();
    JobState& state = jobs_[{chunkX, chunkZ}];
    state.desiredVersion = ++nextVersion_;
    state.pending = true;
}

void LightingEngine::chunkLoaded(int chunkX, int chunkZ) {
    // A new chunk changes both direct skylight occlusion and light sources for
    // every touching chunk. The 3x3 invalidation also covers corner AO halos.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            request(chunkX + dx, chunkZ + dz);
}

void LightingEngine::chunkUnloaded(int chunkX, int chunkZ) {
    jobs_.erase({chunkX, chunkZ});
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dz != 0) request(chunkX + dx, chunkZ + dz);
}

std::vector<LightingChange> LightingEngine::blockChangedSync(int blockX, int, int blockZ) {
    const int chunkX = World::floorDiv16(blockX);
    const int chunkZ = World::floorDiv16(blockZ);
    std::vector<LightingChange> changes;
    changes.reserve(9);

    // Player edits are deliberately synchronous. A complete 15-block-halo
    // solve is committed before any mesh is touched, so no frame can observe
    // an invalidated light section or a hole where the old mesh was removed.
    std::array<bool, 9> visited{};
    std::deque<Coordinate> pending;
    pending.push_back({chunkX, chunkZ});
    visited[4] = true;
    const auto enqueue = [&](int targetX, int targetZ) {
        const int dx = targetX - chunkX;
        const int dz = targetZ - chunkZ;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return;
        const std::size_t index = static_cast<std::size_t>((dz + 1) * 3 + (dx + 1));
        if (visited[index] || world_.findChunk(targetX, targetZ) == nullptr) return;
        visited[index] = true;
        pending.push_back({targetX, targetZ});
    };

    while (!pending.empty()) {
        const Coordinate coordinate = pending.front();
        pending.pop_front();
        Chunk* chunk = world_.findChunk(coordinate.x, coordinate.z);
        if (chunk == nullptr) continue;

        JobState& state = jobs_[coordinate];
        const std::uint64_t version = ++nextVersion_;
        state.desiredVersion = version;
        state.pending = false;
        ChunkLightingData result = LightSolver::solve(
            LightSolver::capture(world_, coordinate.x, coordinate.z));
        if (!LightSolver::isCurrent(world_, result)) continue;
        std::uint8_t dirtyBorders = 0;
        const std::uint16_t dirty = chunk->applyLighting(result.sky, result.block, &dirtyBorders);
        state.appliedVersion = version;
        changes.push_back({coordinate.x, coordinate.z, dirty});

        if ((dirtyBorders & LightBorderWest) != 0) enqueue(coordinate.x - 1, coordinate.z);
        if ((dirtyBorders & LightBorderEast) != 0) enqueue(coordinate.x + 1, coordinate.z);
        if ((dirtyBorders & LightBorderNorth) != 0) enqueue(coordinate.x, coordinate.z - 1);
        if ((dirtyBorders & LightBorderSouth) != 0) enqueue(coordinate.x, coordinate.z + 1);
    }
    return changes;
}

bool LightingEngine::commitOne(std::vector<LightingChange>& changes) {
    CompletionQueue::Completion completion;
    {
        std::lock_guard lock(completions_->mutex);
        if (completions_->items.empty()) return false;
        completion = std::move(completions_->items.front());
        completions_->items.pop_front();
    }
    if (queuedCount_ > 0) --queuedCount_;

    const auto iterator = jobs_.find(completion.coordinate);
    if (iterator == jobs_.end()) return true;
    JobState& state = iterator->second;
    state.queued = false;
    Chunk* chunk = world_.findChunk(completion.coordinate.x, completion.coordinate.z);
    const bool current = completion.version == state.desiredVersion && chunk != nullptr &&
        LightSolver::isCurrent(world_, completion.light);
    if (current) {
        std::uint8_t dirtyBorders = 0;
        const std::uint16_t dirty = chunk->applyLighting(
            completion.light.sky, completion.light.block, &dirtyBorders);
        state.appliedVersion = completion.version;
        state.pending = false;
        changes.push_back({completion.coordinate.x, completion.coordinate.z, dirty});

        // The worker solved an immutable neighborhood snapshot. If committing
        // it changed a shared edge, converge only the affected cardinal
        // neighbor on a fresh snapshot instead of leaving a permanent seam.
        const Coordinate coordinate = completion.coordinate;
        if ((dirtyBorders & LightBorderWest) != 0) request(coordinate.x - 1, coordinate.z);
        if ((dirtyBorders & LightBorderEast) != 0) request(coordinate.x + 1, coordinate.z);
        if ((dirtyBorders & LightBorderNorth) != 0) request(coordinate.x, coordinate.z - 1);
        if ((dirtyBorders & LightBorderSouth) != 0) request(coordinate.x, coordinate.z + 1);
    } else if (chunk != nullptr && state.appliedVersion < state.desiredVersion) {
        state.pending = true;
    } else {
        jobs_.erase(iterator);
    }
    return true;
}

bool LightingEngine::enqueueNearest(double playerX, double playerZ) {
    const std::size_t maximumOutstanding = std::max<std::size_t>(1, workers_.workerCount());
    if (queuedCount_ >= maximumOutstanding) return false;

    auto best = jobs_.end();
    double bestDistance = std::numeric_limits<double>::max();
    for (auto iterator = jobs_.begin(); iterator != jobs_.end(); ++iterator) {
        const JobState& state = iterator->second;
        if (!state.pending || state.queued || world_.findChunk(iterator->first.x, iterator->first.z) == nullptr)
            continue;
        const double centerX = iterator->first.x * chunkSize + chunkSize * 0.5;
        const double centerZ = iterator->first.z * chunkSize + chunkSize * 0.5;
        const double dx = centerX - playerX;
        const double dz = centerZ - playerZ;
        const double distance = dx * dx + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = iterator;
        }
    }
    if (best == jobs_.end()) return false;

    const Coordinate coordinate = best->first;
    JobState& state = best->second;
    const std::uint64_t version = state.desiredVersion;
    LightingSnapshot snapshot = LightSolver::capture(world_, coordinate.x, coordinate.z);
    state.pending = false;
    state.queued = true;
    ++queuedCount_;
    const int priority = 300000 - static_cast<int>(std::min(bestDistance, 200000.0));
    const std::shared_ptr<CompletionQueue> completions = completions_;
    workers_.enqueue(WorkerTaskClass::Lighting, priority,
        [coordinate, version, snapshot = std::move(snapshot), completions]() mutable {
            CompletionQueue::Completion completion{
                coordinate, version, LightSolver::solve(std::move(snapshot))};
            std::lock_guard lock(completions->mutex);
            completions->items.push_back(std::move(completion));
        });
    return true;
}

std::vector<LightingChange> LightingEngine::process(double playerX, double playerZ,
                                                     double budgetMilliseconds) {
    std::vector<LightingChange> changes;
    const auto start = std::chrono::steady_clock::now();
    bool preferCompletion = true;
    while (true) {
        bool worked = preferCompletion ? commitOne(changes) : enqueueNearest(playerX, playerZ);
        if (!worked) worked = preferCompletion ? enqueueNearest(playerX, playerZ) : commitOne(changes);
        if (!worked) break;
        preferCompletion = !preferCompletion;
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= budgetMilliseconds) break;
    }
    return changes;
}

std::size_t LightingEngine::pendingCount() const {
    return static_cast<std::size_t>(std::count_if(jobs_.begin(), jobs_.end(),
        [](const auto& entry) { return entry.second.pending || entry.second.queued; }));
}
