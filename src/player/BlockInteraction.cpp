#include "player/BlockInteraction.hpp"

#include <algorithm>

#include "lighting/LightingEngine.hpp"
#include "player/Player.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/World.hpp"

namespace {

BlockState stateForPlacement(BlockId id, const RaycastHit& hit) {
    std::uint8_t metadata = 0;
    if (id == BlockId::Log) {
        const glm::ivec3 normal = hit.adjacent - hit.block;
        if (normal.x != 0) metadata = 4;
        else if (normal.z != 0) metadata = 8;
    }
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

bool samePosition(const glm::ivec3& left, const glm::ivec3& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

} // namespace

BlockState BlockInteraction::selectedState() const {
    return makeBlockState(static_cast<std::uint16_t>(placeableBlocks_[selectedIndex_]));
}

const BlockDefinition& BlockInteraction::selectedDefinition() const {
    return BlockRegistry::get(selectedState());
}

void BlockInteraction::selectNumber(int number) {
    if (number >= 1 && number <= 9) selectedIndex_ = static_cast<std::size_t>(number - 1);
}

void BlockInteraction::scroll(int steps) {
    if (steps == 0) return;
    const int count = static_cast<int>(placeableBlocks_.size());
    int index = static_cast<int>(selectedIndex_) - steps;
    index %= count;
    if (index < 0) index += count;
    selectedIndex_ = static_cast<std::size_t>(index);
}

void BlockInteraction::removeBlock(World& world, LightingEngine& lighting,
                                   WorldRenderer& renderer, const glm::ivec3& position) {
    world.setBlock(position.x, position.y, position.z, makeBlockState(static_cast<std::uint16_t>(BlockId::Air)));
    const std::vector<LightingChange> changes =
        lighting.blockChangedSync(position.x, position.y, position.z);
    renderer.blockChangedSync(position.x, position.y, position.z, changes);
}

void BlockInteraction::placeBlock(World& world, LightingEngine& lighting,
                                 WorldRenderer& renderer, const Player& player, const RaycastHit& hit) {
    if (hit.adjacent.y < 0 || hit.adjacent.y >= chunkHeight || player.intersectsBlock(hit.adjacent)) return;
    if (blockId(world.getBlock(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z)) != 0) return;
    world.setBlock(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z,
                   stateForPlacement(placeableBlocks_[selectedIndex_], hit));
    const std::vector<LightingChange> changes =
        lighting.blockChangedSync(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z);
    renderer.blockChangedSync(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z, changes);
}

void BlockInteraction::tick(World& world, LightingEngine& lighting, WorldRenderer& renderer, const Player& player,
                            const glm::vec3& lookDirection, bool attacking, bool usingBlock) {
    if (useDelay_ > 0) --useDelay_;
    const float reach = player.gameMode() == GameMode::Creative ? 5.0F : 4.5F;
    const auto hit = raycastBlocks(world, player.eyePosition(), lookDirection, reach);

    if (usingBlock && useDelay_ == 0 && hit) {
        placeBlock(world, lighting, renderer, player, *hit);
        useDelay_ = 4;
    }

    if (!attacking || !hit) {
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (attackDelay_ > 0) {
        --attackDelay_;
        return;
    }

    if (player.gameMode() == GameMode::Creative) {
        removeBlock(world, lighting, renderer, hit->block);
        attackDelay_ = 5;
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (!breakingBlock_ || !samePosition(*breakingBlock_, hit->block)) {
        breakingBlock_ = hit->block;
        breakProgress_ = 0.0F;
    }

    const BlockDefinition& definition = BlockRegistry::get(hit->state);
    if (definition.hardness < 0.0F) return;
    const float divisor = definition.requiresTool ? 100.0F : 30.0F;
    breakProgress_ += 1.0F / definition.hardness / divisor;
    if (breakProgress_ >= 1.0F) {
        removeBlock(world, lighting, renderer, hit->block);
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        attackDelay_ = 5;
    }
}
