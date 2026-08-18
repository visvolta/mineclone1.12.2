#include <cassert>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/World.hpp"

namespace {
RuntimeBlockEntity makeEntity(RuntimeBlockEntityType type, const glm::ivec3& p, BlockId id, std::uint8_t meta = 0) {
    RuntimeBlockEntity e;
    e.type = type;
    e.position = p;
    e.state = makeBlockState(static_cast<std::uint16_t>(id), meta);
    return e;
}
}

int main() {
    World world;
    (void)world.ensureChunk(0, 0);
    BlockEntitySystem entities;

    // A downward-facing hopper transfers one item per eight-tick transfer cycle.
    auto hopper = makeEntity(RuntimeBlockEntityType::Hopper, {3, 5, 3}, BlockId::Hopper, 0);
    hopper.inventory[0] = ItemStack{5, 2, 0, {}};
    auto chest = makeEntity(RuntimeBlockEntityType::Chest, {3, 4, 3}, BlockId::Chest);
    entities.restore(hopper);
    entities.restore(chest);
    (void)entities.tick(world);
    const auto* hopperAfter = entities.find({3,5,3});
    const auto* chestAfter = entities.find({3,4,3});
    assert(hopperAfter && chestAfter);
    assert(hopperAfter->inventory[0].count == 1);
    assert(chestAfter->inventory[0].itemId == 5 && chestAfter->inventory[0].count == 1);
    assert(hopperAfter->transferCooldown == 8);

    // Brewing stand consumes blaze powder into twenty units of brewing fuel.
    auto brewer = makeEntity(RuntimeBlockEntityType::BrewingStand, {6, 5, 6}, BlockId::BrewingStand);
    brewer.inventory[4] = ItemStack{377, 1, 0, {}};
    entities.restore(brewer);
    (void)entities.tick(world);
    const auto* brewerAfter = entities.find({6,5,6});
    assert(brewerAfter && brewerAfter->brewingFuel == 20);
    assert(brewerAfter->inventory[4].empty());

    // Beacon level calculation recognizes a complete one-layer mineral pyramid.
    auto beacon = makeEntity(RuntimeBlockEntityType::Beacon, {10, 5, 10}, BlockId::Beacon);
    entities.restore(beacon);
    for (int x = 9; x <= 11; ++x)
        for (int z = 9; z <= 11; ++z)
            world.setBlock(x, 4, z, makeBlockState(static_cast<std::uint16_t>(BlockId::IronBlock)));
    (void)entities.tick(world);
    assert(entities.beaconLevels({10,5,10}) >= 1);

    // Ender chest inventory is shared player-side storage, not tile-local inventory.
    std::array<ItemStack,27> ender{};
    ender[0] = ItemStack{264, 3, 0, {}};
    entities.setEnderChestInventory(ender);
    assert(entities.enderChestInventory()[0].itemId == 264);
    assert(entities.enderChestInventory()[0].count == 3);

    std::cout << "Stage 12 functional block-entity tests passed.\n";
    return 0;
}
