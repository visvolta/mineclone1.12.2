#pragma once

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

#include "blocks/PlacementRules.hpp"
#include "items/ItemStack.hpp"
#include "world/Raycast.hpp"
#include "world/BlockEntitySystem.hpp"

class ItemRegistry;
class ItemEntitySystem;
class Player;
class LightingEngine;
class World;
class WorldRenderer;

enum class BlockSoundEventType { Hit, Break };
struct BlockSoundEvent { BlockSoundEventType type; glm::ivec3 position; BlockState state; };

class BlockInteraction {
public:
    BlockInteraction(const ItemRegistry& items, BlockEntitySystem& blockEntities, ItemEntitySystem& itemEntities)
        : items_(items), placement_(items), blockEntities_(blockEntities), itemEntities_(itemEntities) {}

    void tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
              Player& player, const glm::vec3& lookDirection,
              bool attacking, bool usingBlock);

    [[nodiscard]] float breakProgress() const { return breakProgress_; }
    [[nodiscard]] std::optional<BlockEntityAction> takeBlockEntityAction() {
        auto result = pendingBlockEntityAction_; pendingBlockEntityAction_.reset(); return result;
    }
    [[nodiscard]] std::vector<BlockSoundEvent> takeSoundEvents() {
        std::vector<BlockSoundEvent> out; out.swap(soundEvents_); return out;
    }

private:
    void commitEdit(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const glm::ivec3& position, BlockState state);
    void applyPlan(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                   const PlacementPlan& plan, bool notifyNeighbors = true);
    void destroyBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                      Player& player, const glm::ivec3& position, BlockState oldState,
                      bool creative);

    const ItemRegistry& items_;
    PlacementRules placement_;
    BlockEntitySystem& blockEntities_;
    ItemEntitySystem& itemEntities_;
    std::optional<BlockEntityAction> pendingBlockEntityAction_;
    std::optional<glm::ivec3> breakingBlock_;
    ItemStack breakingItem_{};
    float breakProgress_ = 0.0F;
    float stepSoundTickCounter_ = 0.0F;
    int attackDelay_ = 0;
    int useDelay_ = 0;
    std::vector<BlockSoundEvent> soundEvents_;
};
