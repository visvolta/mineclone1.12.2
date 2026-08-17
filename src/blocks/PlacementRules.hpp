#pragma once

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks/BlockRegistry.hpp"
#include "world/Raycast.hpp"

class ItemRegistry;
class Player;
class World;
struct ItemStack;

struct PlannedBlockChange {
    glm::ivec3 position{};
    BlockState state = makeBlockState(0);
};

struct PlacementPlan {
    std::vector<PlannedBlockChange> changes;
    bool consumeItem = true;
};

class PlacementRules {
public:
    explicit PlacementRules(const ItemRegistry& items) : items_(items) {}

    [[nodiscard]] std::optional<PlacementPlan> placement(
        const World& world, const Player& player, const glm::vec3& lookDirection,
        const RaycastHit& hit, const ItemStack& held) const;

    // Returns an edit plan when the clicked block handles the use action. An
    // engaged plan with no changes still means the activation was consumed.
    [[nodiscard]] std::optional<PlacementPlan> activation(
        const World& world, const Player& player, const glm::vec3& lookDirection,
        const RaycastHit& hit) const;

    // Block#onBlockAdded dispatch point. Stage 6 uses it for state that
    // becomes meaningful immediately after entering the world (notably rail
    // neighbour shape resolution). Dynamic/redstone callbacks extend this same
    // hook in later stages.
    [[nodiscard]] std::vector<PlannedBlockChange> onBlockAdded(
        const World& world, const std::vector<glm::ivec3>& placedPositions) const;

    // Minecraft's notifyNeighborsOfStateChange fan-out starts here. Stage 6
    // implements the ordinary attachment/survival reactions that are visible
    // while building; later redstone/dynamic stages can subscribe to the same
    // hook without changing BlockInteraction.
    [[nodiscard]] std::vector<PlannedBlockChange> neighborReactions(
        const World& world, const std::vector<glm::ivec3>& changedPositions) const;

private:
    const ItemRegistry& items_;
};
