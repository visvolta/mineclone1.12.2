#pragma once

#include <cstdint>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"

class Chunk;
class World;

// Stage 11 runtime block ticking.  This intentionally mirrors the two vanilla
// mechanisms (scheduled ticks + random ticks) instead of giving each feature a
// bespoke frame update path.
class DynamicBlockSystem {
public:
    explicit DynamicBlockSystem(std::uint64_t seed = 0);

    void schedule(const glm::ivec3& position, BlockState expectedState, int delayTicks, int priority = 0);
    void scanChunk(World& world, int chunkX, int chunkZ);
    void neighborChanged(World& world, const glm::ivec3& position);

    // Explicit phase split used by the Stage 12.5 game-tick pipeline.
    // tickScheduled advances gameTime exactly once; tickRandom does not.
    [[nodiscard]] std::vector<glm::ivec3> tickScheduled(World& world);
    [[nodiscard]] std::vector<glm::ivec3> tickRandom(World& world);
    [[nodiscard]] std::vector<glm::ivec3> tick(World& world); // compatibility wrapper

    // Vanilla-compatible TileTicks bridge. Runtime-managed ticks are imported
    // from Chunk::scheduledTicks and written back before chunk persistence;
    // unknown vanilla ticks are preserved byte-for-byte.
    void syncChunkScheduledTicks(Chunk& chunk) const;
    void syncLoadedChunkScheduledTicks(World& world) const;
    [[nodiscard]] std::uint64_t gameTime() const { return gameTime_; }
    [[nodiscard]] std::size_t pendingScheduledTicks() const { return scheduled_.size(); }

private:
    struct ScheduledTick {
        std::uint64_t due = 0;
        std::uint64_t sequence = 0;
        glm::ivec3 position{};
        BlockState expectedState = 0;
        int priority = 0;
    };
    struct Later {
        bool operator()(const ScheduledTick& a, const ScheduledTick& b) const {
            if (a.due != b.due) return a.due > b.due;
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.sequence > b.sequence;
        }
    };

    void scheduledTick(World& world, const ScheduledTick& tick, std::vector<glm::ivec3>& changed);
    void randomTick(World& world, const glm::ivec3& position, BlockState state,
                    std::vector<glm::ivec3>& changed);
    void tickFluid(World& world, const glm::ivec3& p, BlockState state, bool lava,
                   std::vector<glm::ivec3>& changed);
    void tickFalling(World& world, const glm::ivec3& p, BlockState state,
                     std::vector<glm::ivec3>& changed);
    void tickPlant(World& world, const glm::ivec3& p, BlockState state,
                   std::vector<glm::ivec3>& changed);
    void tickFire(World& world, const glm::ivec3& p, BlockState state,
                  std::vector<glm::ivec3>& changed);
    void explode(World& world, const glm::ivec3& center, std::vector<glm::ivec3>& changed);
    bool set(World& world, const glm::ivec3& p, BlockState state, std::vector<glm::ivec3>& changed);
    [[nodiscard]] bool canReplace(BlockState state) const;
    [[nodiscard]] bool hasWaterNearby(const World& world, const glm::ivec3& p, int radius) const;
    [[nodiscard]] bool hasLogNearby(const World& world, const glm::ivec3& p, int radius) const;
    [[nodiscard]] bool flammable(BlockState state) const;
    void growTree(World& world, const glm::ivec3& p, BlockState sapling, std::vector<glm::ivec3>& changed);

    std::priority_queue<ScheduledTick, std::vector<ScheduledTick>, Later> scheduled_;
    std::unordered_set<std::uint64_t> scheduledKeys_;
    std::mt19937 random_;
    std::uint64_t gameTime_ = 0;
    std::uint64_t sequence_ = 0;
};
