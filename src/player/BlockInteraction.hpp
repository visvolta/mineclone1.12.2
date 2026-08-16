#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include <glm/vec3.hpp>

#include "blocks/BlockRegistry.hpp"
#include "world/Raycast.hpp"

class Player;
class LightingEngine;
class World;
class WorldRenderer;

class BlockInteraction {
public:
    void tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
              const Player& player, const glm::vec3& lookDirection,
              bool attacking, bool usingBlock);
    void selectNumber(int number);
    void scroll(int steps);

    [[nodiscard]] BlockState selectedState() const;
    [[nodiscard]] const BlockDefinition& selectedDefinition() const;
    [[nodiscard]] std::size_t selectedIndex() const { return selectedIndex_; }
    [[nodiscard]] float breakProgress() const { return breakProgress_; }
    [[nodiscard]] static constexpr std::size_t hotbarSize() { return placeableBlocks_.size(); }
    [[nodiscard]] static constexpr BlockId hotbarBlock(std::size_t index) {
        return placeableBlocks_[index < placeableBlocks_.size() ? index : 0];
    }

private:
    void removeBlock(World& world, LightingEngine& lighting,
                     WorldRenderer& renderer, const glm::ivec3& position);
    bool placeBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const Player& player, const glm::vec3& lookDirection, const RaycastHit& hit);
    void commitEdit(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const glm::ivec3& position, BlockState state);

    // Stage 1 keeps Minecraft's nine-slot hotbar. Until the Item/Inventory stage,
    // these are deterministic development stacks chosen to exercise the 1.12.2
    // placement/shape paths implemented in Stages 2-3.
    static constexpr std::array<BlockId, 9> placeableBlocks_ = {
        BlockId::Stone, BlockId::Cobblestone, BlockId::Planks,
        BlockId::StoneSlab, BlockId::OakStairs, BlockId::Fence,
        BlockId::GlassPane, BlockId::WoodenDoor, BlockId::Torch
    };

    std::size_t selectedIndex_ = 0;
    std::optional<glm::ivec3> breakingBlock_;
    float breakProgress_ = 0.0F;
    int attackDelay_ = 0;
    int useDelay_ = 0;
};
