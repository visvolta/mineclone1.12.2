#pragma once

#include <optional>

#include <glm/vec3.hpp>

#include "world/Raycast.hpp"

class ItemRegistry;
class Player;
class LightingEngine;
class World;
class WorldRenderer;
struct ItemStack;

class BlockInteraction {
public:
    explicit BlockInteraction(const ItemRegistry& items) : items_(items) {}

    void tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
              Player& player, const glm::vec3& lookDirection,
              bool attacking, bool usingBlock);

    [[nodiscard]] float breakProgress() const { return breakProgress_; }

private:
    void removeBlock(World& world, LightingEngine& lighting,
                     WorldRenderer& renderer, const glm::ivec3& position);
    bool placeBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    Player& player, const glm::vec3& lookDirection,
                    const RaycastHit& hit, ItemStack& held);
    void commitEdit(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const glm::ivec3& position, BlockState state);

    const ItemRegistry& items_;
    std::optional<glm::ivec3> breakingBlock_;
    float breakProgress_ = 0.0F;
    int attackDelay_ = 0;
    int useDelay_ = 0;
};
