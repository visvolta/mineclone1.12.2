#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

class ThreadPool;
class World;

struct LightingChange {
    int chunkX = 0;
    int chunkZ = 0;
    std::uint16_t dirtySections = 0;
};

// Owns versioned light jobs. Snapshots are captured and results are committed
// on the main thread; workers only operate on immutable byte arrays.
class LightingEngine {
public:
    LightingEngine(World& world, ThreadPool& workers);
    ~LightingEngine();
    LightingEngine(const LightingEngine&) = delete;
    LightingEngine& operator=(const LightingEngine&) = delete;

    void chunkLoaded(int chunkX, int chunkZ);
    void chunkUnloaded(int chunkX, int chunkZ);
    [[nodiscard]] std::vector<LightingChange> blockChangedSync(int blockX, int blockY, int blockZ);
    [[nodiscard]] std::vector<LightingChange> process(double playerX, double playerZ,
                                                       double budgetMilliseconds);
    [[nodiscard]] std::size_t pendingCount() const;

private:
    struct Coordinate {
        int x = 0;
        int z = 0;
        bool operator==(const Coordinate&) const = default;
    };
    struct CoordinateHash {
        std::size_t operator()(const Coordinate& coordinate) const;
    };
    struct JobState {
        std::uint64_t desiredVersion = 0;
        std::uint64_t appliedVersion = 0;
        bool pending = false;
        bool queued = false;
    };
    struct CompletionQueue;

    void request(int chunkX, int chunkZ);
    [[nodiscard]] bool commitOne(std::vector<LightingChange>& changes);
    [[nodiscard]] bool enqueueNearest(double playerX, double playerZ);

    World& world_;
    ThreadPool& workers_;
    std::shared_ptr<CompletionQueue> completions_;
    std::unordered_map<Coordinate, JobState, CoordinateHash> jobs_;
    std::uint64_t nextVersion_ = 0;
    std::size_t queuedCount_ = 0;
};
