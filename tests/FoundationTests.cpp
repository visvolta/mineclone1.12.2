#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/vec3.hpp>

#include "blocks/BlockRegistry.hpp"
#include "blocks/BlockShape.hpp"
#include "client/ScaledResolution.hpp"
#include "player/Player.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"

namespace {

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

void testVanillaScaledResolution() {
    const ScaledResolution hd = ScaledResolution::fromDisplay(1920, 1080, 0, false);
    assert(hd.scaleFactor == 4);
    assert(hd.scaledWidth == 480);
    assert(hd.scaledHeight == 270);

    const ScaledResolution small = ScaledResolution::fromDisplay(1280, 720, 0, false);
    assert(small.scaleFactor == 3);
    assert(small.scaledWidth == 427);
    assert(small.scaledHeight == 240);

    const ScaledResolution explicitNormal = ScaledResolution::fromDisplay(1920, 1080, 2, false);
    assert(explicitNormal.scaleFactor == 2);
    const ScaledResolution unicode = ScaledResolution::fromDisplay(1920, 1080, 3, true);
    assert(unicode.scaleFactor == 2);
}

void testSlabAndSnowShapes() {
    World world;
    world.setBlock(0, 0, 0, block(BlockId::StoneSlab, 0));
    BlockShapeSet slab = BlockShapes::collision(world, world.getBlock(0, 0, 0), 0, 0, 0);
    assert(slab.count == 1);
    assert(slab.boxes[0].maxY == 0.5);

    world.setBlock(1, 0, 0, block(BlockId::StoneSlab, 8));
    slab = BlockShapes::collision(world, world.getBlock(1, 0, 0), 1, 0, 0);
    assert(slab.boxes[0].minY == 0.5 && slab.boxes[0].maxY == 1.0);

    world.setBlock(2, 0, 0, block(BlockId::SnowLayer, 0));
    assert(BlockShapes::collision(world, world.getBlock(2, 0, 0), 2, 0, 0).empty());
    const auto snowBounds = BlockShapes::selectionBounds(world, world.getBlock(2, 0, 0), 2, 0, 0);
    assert(snowBounds && snowBounds->maxY == 0.125);
    assert(BlockShapes::isReplaceable(world.getBlock(2, 0, 0)));
}

void testFencePaneAndWallShapes() {
    World world;
    world.setBlock(0, 0, 0, block(BlockId::Fence));
    world.setBlock(1, 0, 0, block(BlockId::Stone));
    const BlockShapeSet fence = BlockShapes::collision(world, world.getBlock(0, 0, 0), 0, 0, 0);
    assert(fence.count == 2);
    assert(fence.boxes[0].maxY == 1.5);

    world.setBlock(3, 0, 0, block(BlockId::GlassPane));
    world.setBlock(3, 0, -1, block(BlockId::Glass));
    world.setBlock(3, 0, 1, block(BlockId::Glass));
    const auto pane = BlockShapes::selectionBounds(world, world.getBlock(3, 0, 0), 3, 0, 0);
    assert(pane && pane->minZ == 0.0 && pane->maxZ == 1.0);
    assert(pane->minX == 0.4375 && pane->maxX == 0.5625);

    world.setBlock(5, 0, 0, block(BlockId::CobblestoneWall));
    world.setBlock(5, 0, -1, block(BlockId::Stone));
    world.setBlock(5, 0, 1, block(BlockId::Stone));
    const auto wall = BlockShapes::selectionBounds(world, world.getBlock(5, 0, 0), 5, 0, 0);
    assert(wall && wall->minX == 0.3125 && wall->maxX == 0.6875);
    assert(wall->maxY == 0.875);
}

void testDoorTrapdoorAndCactusShapes() {
    World world;
    // Lower north-facing closed door + upper left hinge.
    world.setBlock(0, 0, 0, block(BlockId::WoodenDoor, 3));
    world.setBlock(0, 1, 0, block(BlockId::WoodenDoor, 8));
    const auto door = BlockShapes::selectionBounds(world, world.getBlock(0, 0, 0), 0, 0, 0);
    assert(door && door->minZ == 0.8125 && door->maxZ == 1.0);

    world.setBlock(2, 0, 0, block(BlockId::Trapdoor, 0));
    const auto trap = BlockShapes::selectionBounds(world, world.getBlock(2, 0, 0), 2, 0, 0);
    assert(trap && trap->maxY == 0.1875);

    world.setBlock(4, 0, 0, block(BlockId::Cactus));
    const auto cactusSelect = BlockShapes::selectionBounds(world, world.getBlock(4, 0, 0), 4, 0, 0);
    const BlockShapeSet cactusCollision = BlockShapes::collision(world, world.getBlock(4, 0, 0), 4, 0, 0);
    assert(cactusSelect && cactusSelect->maxY == 1.0);
    assert(cactusCollision.count == 1 && cactusCollision.boxes[0].maxY == 0.9375);
}

void testShapeAwareRaycast() {
    World world;
    world.setBlock(0, 0, 0, block(BlockId::StoneSlab));
    const auto slabHit = raycastBlocks(world, {0.5F, 2.0F, 0.5F}, {0.0F, -1.0F, 0.0F}, 5.0F);
    assert(slabHit);
    assert(slabHit->block == glm::ivec3(0, 0, 0));
    assert(slabHit->face == Face::Up);
    assert(std::abs(slabHit->hitPoint.y - 0.5F) < 0.0001F);
    assert(slabHit->adjacent == glm::ivec3(0, 1, 0));

    World torchWorld;
    torchWorld.setBlock(0, 0, 0, block(BlockId::Torch, 5));
    const auto miss = raycastBlocks(torchWorld, {0.05F, 0.3F, -1.0F}, {0.0F, 0.0F, 1.0F}, 3.0F);
    assert(!miss);
    const auto hit = raycastBlocks(torchWorld, {0.5F, 0.3F, -1.0F}, {0.0F, 0.0F, 1.0F}, 3.0F);
    assert(hit && hit->block == glm::ivec3(0, 0, 0));
}

void testPlayerUsesBlockCollisionShapes() {
    World world;
    world.setBlock(0, 0, 0, block(BlockId::StoneSlab));
    Player aboveSlab({0.5, 0.5, 0.5});
    assert(!aboveSlab.intersectsBlock(world, {0, 0, 0}));

    world.setBlock(1, 0, 0, block(BlockId::Torch, 5));
    Player throughTorch({1.5, 0.0, 0.5});
    assert(!throughTorch.intersectsBlock(world, {1, 0, 0}));
}

} // namespace

int main() {
    testVanillaScaledResolution();
    testSlabAndSnowShapes();
    testFencePaneAndWallShapes();
    testDoorTrapdoorAndCactusShapes();
    testShapeAwareRaycast();
    testPlayerUsesBlockCollisionShapes();
    std::cout << "All asset-free Blockcraft foundation tests passed.\n";
    return 0;
}
