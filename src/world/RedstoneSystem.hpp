#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"

class BlockEntitySystem;
class ItemEntitySystem;
class Player;
class World;

// Stage 13 redstone runtime. The system intentionally remains block/world driven
// (matching 1.12.2's neighbour-query model) rather than building a graph solver.
class RedstoneSystem {
public:
    RedstoneSystem() = default;

    void scanLoadedWorld(const World& world);
    void scanChunk(const World& world, int chunkX, int chunkZ);
    void neighborChanged(const World& world, const glm::ivec3& position);

    [[nodiscard]] std::vector<glm::ivec3> tick(
        World& world, BlockEntitySystem& blockEntities, ItemEntitySystem* itemEntities,
        const Player& player, double dayTime);

    [[nodiscard]] int getWeakPower(const World& world, const glm::ivec3& source,
                                   const glm::ivec3& toward,
                                   const BlockEntitySystem* blockEntities = nullptr) const;
    [[nodiscard]] int getStrongPower(const World& world, const glm::ivec3& source,
                                     const glm::ivec3& toward,
                                     const BlockEntitySystem* blockEntities = nullptr) const;
    [[nodiscard]] bool canProvidePower(BlockState state) const;
    [[nodiscard]] bool isBlockPowered(const World& world, const glm::ivec3& position,
                                      const BlockEntitySystem* blockEntities = nullptr) const;
    [[nodiscard]] int comparatorOutputAt(const glm::ivec3& position) const;
    [[nodiscard]] std::uint64_t gameTime() const { return gameTime_; }

private:
    enum class DelayedKind : std::uint8_t {
        ButtonRelease,
        RepeaterTransition,
        ComparatorRefresh,
        TorchRefresh,
        LampOff,
        ObserverOff,
        PistonRefresh,
        DispenserFire
    };

    struct DelayedUpdate {
        std::uint64_t due = 0;
        std::uint64_t sequence = 0;
        glm::ivec3 position{};
        DelayedKind kind = DelayedKind::TorchRefresh;
        BlockState expected = 0;
        bool targetPowered = false;
    };
    struct Later {
        bool operator()(const DelayedUpdate& a, const DelayedUpdate& b) const {
            if (a.due != b.due) return a.due > b.due;
            return a.sequence > b.sequence;
        }
    };

    struct PistonMotion {
        glm::ivec3 piston{};
        glm::ivec3 front{};
        BlockState movedState = 0;
        bool hasMovedState = false;
        bool retracting = false;
    };

    void markAround(const glm::ivec3& position);
    void registerRecurring(const World& world, const glm::ivec3& position);
    void schedule(const glm::ivec3& position, DelayedKind kind, int delay,
                  BlockState expected, bool targetPowered = false);
    bool set(World& world, const glm::ivec3& position, BlockState state,
             std::vector<glm::ivec3>& changed);
    void processDelayed(World& world, BlockEntitySystem& blockEntities,
                        ItemEntitySystem* itemEntities, std::vector<glm::ivec3>& changed);
    void updateDirty(World& world, BlockEntitySystem& blockEntities,
                     ItemEntitySystem* itemEntities, const Player& player,
                     double dayTime, std::vector<glm::ivec3>& changed);
    void updateWireNetwork(World& world, BlockEntitySystem& blockEntities,
                           const std::vector<glm::ivec3>& seeds,
                           std::vector<glm::ivec3>& changed);
    void updateBlock(World& world, BlockEntitySystem& blockEntities,
                     ItemEntitySystem* itemEntities, const Player& player,
                     double dayTime, const glm::ivec3& position,
                     std::vector<glm::ivec3>& changed);

    [[nodiscard]] int directPower(const World& world, const glm::ivec3& source,
                                  const glm::ivec3& toward,
                                  const BlockEntitySystem* blockEntities) const;
    [[nodiscard]] int maxNeighborPower(const World& world, const glm::ivec3& position,
                                       const BlockEntitySystem* blockEntities,
                                       bool includeWires = true) const;
    [[nodiscard]] int comparatorInput(const World& world, const glm::ivec3& position,
                                      BlockState state, const BlockEntitySystem& blockEntities) const;
    [[nodiscard]] int containerSignal(const World& world, const glm::ivec3& position,
                                      const BlockEntitySystem& blockEntities) const;
    [[nodiscard]] bool railPowered(const World& world, const glm::ivec3& position,
                                   const BlockEntitySystem& blockEntities, int depth) const;
    [[nodiscard]] bool repeaterLocked(const World& world, const glm::ivec3& position,
                                      BlockState state, const BlockEntitySystem& blockEntities) const;
    [[nodiscard]] bool pistonCanMove(const World& world, const BlockEntitySystem& blockEntities,
                                     const glm::ivec3& position, const glm::ivec3& direction,
                                     std::vector<glm::ivec3>& line) const;
    void extendPiston(World& world, BlockEntitySystem& blockEntities,
                      const glm::ivec3& position, BlockState state,
                      std::vector<glm::ivec3>& changed);
    void retractPiston(World& world, BlockEntitySystem& blockEntities,
                       const glm::ivec3& position, BlockState state,
                       std::vector<glm::ivec3>& changed);
    void fireDispenser(World& world, BlockEntitySystem& blockEntities,
                       ItemEntitySystem* itemEntities, const glm::ivec3& position,
                       BlockState state);

    [[nodiscard]] static std::uint64_t key(const glm::ivec3& position);

    std::unordered_set<std::uint64_t> dirtyKeys_;
    std::unordered_set<std::uint64_t> observerTriggerKeys_;
    std::unordered_set<std::uint64_t> recurringKeys_;
    std::vector<glm::ivec3> dirtyPositions_;
    std::vector<glm::ivec3> recurringPositions_;
    std::priority_queue<DelayedUpdate, std::vector<DelayedUpdate>, Later> delayed_;
    std::unordered_map<std::uint64_t, int> comparatorOutputs_;
    std::unordered_map<std::uint64_t, bool> previousPower_;
    std::unordered_map<std::uint64_t, PistonMotion> pistonMotions_;
    std::uint64_t gameTime_ = 0;
    std::uint64_t sequence_ = 0;
};
