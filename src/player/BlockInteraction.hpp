#pragma once

#include <array>
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
    [[nodiscard]] float breakProgress() const { return breakProgress_; }

private:
    void removeBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                     const glm::ivec3& position);
    void placeBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                    const Player& player, const RaycastHit& hit);

    static constexpr std::array<BlockId, 11> placeableBlocks_ = {
        BlockId::Stone, BlockId::Grass, BlockId::Dirt, BlockId::Cobblestone,
        BlockId::Planks, BlockId::Bedrock, BlockId::Sand, BlockId::Gravel,
        BlockId::Log, BlockId::Leaves, BlockId::Glass
    };

    std::size_t selectedIndex_ = 0;
    std::optional<glm::ivec3> breakingBlock_;
    float breakProgress_ = 0.0F;
    int attackDelay_ = 0;
    int useDelay_ = 0;
};
