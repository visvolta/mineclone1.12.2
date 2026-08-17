#include <cassert>
#include <filesystem>

#include "blocks/PlacementRules.hpp"
#include "items/ItemRegistry.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {
BlockState block(BlockId id, std::uint8_t meta = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), meta);
}

RaycastHit topHit(const World& world, glm::ivec3 blockPos) {
    RaycastHit hit;
    hit.block = blockPos;
    hit.adjacent = blockPos + glm::ivec3(0, 1, 0);
    hit.state = world.getBlock(blockPos.x, blockPos.y, blockPos.z);
    hit.face = Face::Up;
    hit.hitPoint = glm::vec3(blockPos) + glm::vec3(0.5F, 1.0F, 0.5F);
    return hit;
}
}

int main() {
    const ItemRegistry items{std::filesystem::path(BLOCKCRAFT_ASSET_ROOT)};
    const PlacementRules rules(items);
    World world;
    Player player({20.5, 80.0, 20.5});

    // Ordinary ItemBlock placement resolves against replaceability/support and
    // preserves the held metadata subtype.
    world.setBlock(0, 63, 0, block(BlockId::Stone));
    RaycastHit hit = topHit(world, {0, 63, 0});
    ItemStack oakStairs{static_cast<std::uint16_t>(BlockId::OakStairs), 64, 0, {}};
    auto stairPlan = rules.placement(world, player, {0.0F, 0.0F, 1.0F}, hit, oakStairs);
    assert(stairPlan && stairPlan->changes.size() == 1);
    assert(static_cast<BlockId>(blockId(stairPlan->changes[0].state)) == BlockId::OakStairs);

    // Doors are one item but atomically place both vanilla block halves.
    ItemStack door{324, 1, 0, {}};
    auto doorPlan = rules.placement(world, player, {0.0F, 0.0F, 1.0F}, hit, door);
    assert(doorPlan && doorPlan->changes.size() == 2);
    assert((blockMetadata(doorPlan->changes[1].state) & 8U) != 0U);

    // ItemSlab merges a compatible clicked half into its double slab block.
    world.setBlock(2, 64, 0, block(BlockId::StoneSlab, 0));
    RaycastHit slabHit = topHit(world, {2, 64, 0});
    slabHit.state = block(BlockId::StoneSlab, 0);
    ItemStack slab{static_cast<std::uint16_t>(BlockId::StoneSlab), 64, 0, {}};
    auto slabPlan = rules.placement(world, player, {0.0F, 0.0F, 1.0F}, slabHit, slab);
    assert(slabPlan && slabPlan->changes.size() == 1);
    assert(static_cast<BlockId>(blockId(slabPlan->changes[0].state)) == BlockId::DoubleStoneSlab);

    // Hopper#getStateForPlacement points into the clicked support (opposite the
    // ItemBlock side); clicking an east face therefore creates WEST output.
    world.setBlock(4, 64, 0, block(BlockId::Stone));
    RaycastHit hopperHit;
    hopperHit.block = {4, 64, 0};
    hopperHit.adjacent = {5, 64, 0};
    hopperHit.state = world.getBlock(4, 64, 0);
    hopperHit.face = Face::East;
    hopperHit.hitPoint = {5.0F, 64.5F, 0.5F};
    ItemStack hopper{static_cast<std::uint16_t>(BlockId::Hopper), 64, 0, {}};
    auto hopperPlan = rules.placement(world, player, {-1.0F, 0.0F, 0.0F}, hopperHit, hopper);
    assert(hopperPlan && blockMetadata(hopperPlan->changes[0].state) == 4U); // WEST

    return 0;
}
