#include <cassert>
#include <cmath>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemStack.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"

namespace {

BlockState block(BlockId id, std::uint8_t meta = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), meta);
}

RaycastHit hitAt(int x, int y, int z, BlockState state) {
    RaycastHit hit;
    hit.block = {x, y, z};
    hit.state = state;
    return hit;
}

} // namespace

int main() {
    World world;
    world.ensureChunk(0, 0);

    // Normal double chest: both halves are runtime block entities and expose
    // one deterministic 54-slot InventoryLargeChest ordering.
    world.setBlock(1, 64, 1, block(BlockId::Chest, 2));
    world.setBlock(2, 64, 1, block(BlockId::Chest, 2));
    BlockEntitySystem entities;
    entities.scanLoadedWorld(world);
    assert(entities.find({1,64,1}) != nullptr);
    assert(entities.find({2,64,1}) != nullptr);
    assert(entities.containerSlotCount(world, {1,64,1}) == 54);
    assert(entities.containerSlotCount(world, {2,64,1}) == 54);
    entities.containerSlot(world, {1,64,1}, 0) = ItemStack{264, 3, 0, {}};
    assert(entities.containerSlot(world, {2,64,1}, 0).itemId == 264);

    // Chest obstruction applies to both halves of a large chest.
    world.setBlock(2, 65, 1, block(BlockId::Stone));
    assert(!entities.activate(world, hitAt(1,64,1,block(BlockId::Chest,2))).has_value());
    world.setBlock(2, 65, 1, block(BlockId::Air));
    const auto chestAction = entities.activate(world, hitAt(1,64,1,block(BlockId::Chest,2)));
    assert(chestAction && chestAction->type == BlockEntityActionType::OpenChest);

    // Chest lid animation follows the vanilla 0.1/tick progress field.
    entities.beginViewing(*chestAction);
    for (int i = 0; i < 10; ++i) entities.tick(world);
    assert(std::abs(entities.animation({1,64,1}, 1.0F) - 1.0F) < 0.0001F);
    entities.endViewing(*chestAction);
    for (int i = 0; i < 10; ++i) entities.tick(world);
    assert(std::abs(entities.animation({1,64,1}, 1.0F)) < 0.0001F);

    // Sign runtime text survives updates to the block entity instance.
    world.setBlock(4, 64, 4, block(BlockId::StandingSign, 7));
    entities.blockChanged(world, {4,64,4}, block(BlockId::Air), block(BlockId::StandingSign,7));
    auto* lines = entities.signLines({4,64,4});
    assert(lines != nullptr);
    (*lines)[0] = "Minecraft";
    assert(entities.signLines({4,64,4})->at(0) == "Minecraft");

    // Bed color comes from ItemBed metadata, matching TileEntityBed.
    world.setBlock(5, 64, 5, block(BlockId::Bed, 2));
    entities.blockChanged(world, {5,64,5}, block(BlockId::Air), block(BlockId::Bed,2));
    entities.placedFromItem({5,64,5}, block(BlockId::Bed,2), ItemStack{355,1,11,{}});
    assert(entities.find({5,64,5})->color == 11);

    // Closed shulker boxes cannot open into a normal cube in their facing
    // direction. Meta 5 is EAST in EnumFacing index order.
    world.setBlock(7, 64, 7, block(BlockId::WhiteShulkerBox, 5));
    entities.blockChanged(world, {7,64,7}, block(BlockId::Air), block(BlockId::WhiteShulkerBox,5));
    world.setBlock(8, 64, 7, block(BlockId::Stone));
    assert(!entities.activate(world, hitAt(7,64,7,block(BlockId::WhiteShulkerBox,5))).has_value());
    world.setBlock(8, 64, 7, block(BlockId::Air));
    const auto shulkerAction = entities.activate(world, hitAt(7,64,7,block(BlockId::WhiteShulkerBox,5)));
    assert(shulkerAction && shulkerAction->type == BlockEntityActionType::OpenShulker);
    assert(entities.containerSlotCount(world, {7,64,7}) == 27);

    return 0;
}
