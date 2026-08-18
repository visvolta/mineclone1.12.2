#include <cassert>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "save/Nbt.hpp"
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


    // Stage 12.5 persists the remaining scheduled delay through vanilla TileTicks.
    {
        World world;
        Chunk& chunk = world.ensureChunk(0, 0);
        const auto water = makeBlockState(static_cast<std::uint16_t>(BlockId::Water));
        world.setBlock(4, 10, 4, water);

        // Unknown/non-runtime ticks must survive a sync unchanged.
        nbt::Document foreign;
        foreign.root = nbt::Tag(nbt::Compound{
            {"i", nbt::Tag(std::string("minecraft:redstone_wire"))},
            {"t", nbt::Tag(std::int32_t{7})}, {"p", nbt::Tag(std::int32_t{0})},
            {"x", nbt::Tag(std::int32_t{1})}, {"y", nbt::Tag(std::int32_t{2})},
            {"z", nbt::Tag(std::int32_t{3})}});
        chunk.addScheduledTick(nbt::encode(foreign));

        DynamicBlockSystem ticks(17);
        ticks.neighborChanged(world, {4, 10, 4});
        assert(ticks.gameTime() == 0);
        (void)ticks.tickScheduled(world);
        (void)ticks.tickScheduled(world);
        assert(ticks.gameTime() == 2);
        (void)ticks.tickRandom(world);
        assert(ticks.gameTime() == 2); // random phase does not advance the clock
        ticks.syncChunkScheduledTicks(chunk);

        bool sawWater = false;
        bool sawForeign = false;
        for (const auto& encoded : chunk.scheduledTicks()) {
            const auto decoded = nbt::decode(encoded);
            const auto& compound = decoded.root.compound();
            const nbt::Tag* id = nbt::find(compound, "i");
            if (id != nullptr && id->type == nbt::Type::String) {
                if (nbt::string(compound, "i") == "minecraft:redstone_wire") sawForeign = true;
            } else if (nbt::integer(compound, "i", -1) == static_cast<std::int64_t>(BlockId::Water)) {
                sawWater = true;
                assert(nbt::integer(compound, "t", -1) == 3);
            }
        }
        assert(sawWater && sawForeign);

        // Recreate the chunk/system as a process restart would. The imported
        // remaining delay must complete after exactly three more scheduled phases.
        World reloaded;
        Chunk& reloadedChunk = reloaded.ensureChunk(0, 0);
        reloadedChunk.replaceScheduledTicks(chunk.scheduledTicks());
        reloaded.setBlock(4, 10, 4, water);
        DynamicBlockSystem restored(17);
        restored.scanChunk(reloaded, 0, 0);
        assert(restored.pendingScheduledTicks() == 1);
        (void)restored.tickScheduled(reloaded);
        (void)restored.tickScheduled(reloaded);
        assert(blockId(reloaded.getBlock(4, 9, 4)) == static_cast<std::uint16_t>(BlockId::Air));
        (void)restored.tickScheduled(reloaded);
        assert(blockId(reloaded.getBlock(4, 9, 4)) == static_cast<std::uint16_t>(BlockId::FlowingWater));
    }

    std::cout << "Stage 11/12.5 dynamic-world tests passed.\n";
    return 0;
}
