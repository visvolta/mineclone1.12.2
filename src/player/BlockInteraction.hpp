#pragma once

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks/PlacementRules.hpp"
#include "world/Raycast.hpp"

class ItemRegistry;
class Player;
class LightingEngine;
class World;
class WorldRenderer;

class BlockInteraction {
public:
    explicit BlockInteraction(const ItemRegistry& items) : placement_(items) {}

    void tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
              Player& player, const glm::vec3& lookDirection,
              bool attacking, bool usingBlock);

    [[nodiscard]] float breakProgress() const { return breakProgress_; }

private:
    void commitEdit(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const glm::ivec3& position, BlockState state);
    void applyPlan(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                   const PlacementPlan& plan, bool notifyNeighbors = true);
    void removeBlock(World& world, LightingEngine& lighting,
                     WorldRenderer& renderer, const glm::ivec3& position);

    PlacementRules placement_;
    std::optional<glm::ivec3> breakingBlock_;
    float breakProgress_ = 0.0F;
    int attackDelay_ = 0;
    int useDelay_ = 0;
};
