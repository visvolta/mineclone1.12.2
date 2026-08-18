#include <cassert>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "world/DynamicBlockSystem.hpp"
#include "world/World.hpp"

int main() {

    // Scheduled falling blocks react to an unsupported neighbour state.
    {
        World world;
        (void)world.ensureChunk(0, 0);
        const auto sand = makeBlockState(static_cast<std::uint16_t>(BlockId::Sand));
        world.setBlock(2, 8, 2, sand);
        DynamicBlockSystem ticks(1234);
        ticks.neighborChanged(world, {2, 8, 2});
        assert(ticks.pendingScheduledTicks() == 1);
        (void)ticks.tick(world);
        const auto changed = ticks.tick(world);
        assert(!changed.empty());
        assert(blockId(world.getBlock(2, 0, 2)) == static_cast<std::uint16_t>(BlockId::Sand));
        assert(blockId(world.getBlock(2, 8, 2)) == static_cast<std::uint16_t>(BlockId::Air));
    }

    // Water uses the five-tick scheduled path and flows downward first.
    {
        World world;
        (void)world.ensureChunk(0, 0);
        const auto water = makeBlockState(static_cast<std::uint16_t>(BlockId::Water));
        world.setBlock(4, 10, 4, water);
        DynamicBlockSystem ticks(42);
        ticks.neighborChanged(world, {4, 10, 4});
        for (int i = 0; i < 5; ++i) (void)ticks.tick(world);
        assert(blockId(world.getBlock(4, 9, 4)) == static_cast<std::uint16_t>(BlockId::FlowingWater));
    }

    // Fire next to TNT schedules a fuse rather than detonating synchronously.
    {
        World world;
        (void)world.ensureChunk(0, 0);
        world.setBlock(7, 4, 7, makeBlockState(static_cast<std::uint16_t>(BlockId::TNT)));
        world.setBlock(8, 4, 7, makeBlockState(static_cast<std::uint16_t>(BlockId::Fire)));
        DynamicBlockSystem ticks(9);
        ticks.neighborChanged(world, {7, 4, 7});
        assert(ticks.pendingScheduledTicks() == 1);
        for (int i = 0; i < 79; ++i) (void)ticks.tick(world);
        assert(blockId(world.getBlock(7, 4, 7)) == static_cast<std::uint16_t>(BlockId::TNT));
        (void)ticks.tick(world);
        assert(blockId(world.getBlock(7, 4, 7)) == static_cast<std::uint16_t>(BlockId::Air));
    }

    std::cout << "Stage 11 dynamic-world tests passed.\n";
    return 0;
}
