#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "player/Player.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/RedstoneSystem.hpp"
#include "world/World.hpp"

namespace {
BlockState b(BlockId id, std::uint8_t meta = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), meta);
}

void run(RedstoneSystem& redstone, World& world, BlockEntitySystem& blockEntities,
         Player& player, double& dayTime, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        const auto changed = redstone.tick(world, blockEntities, nullptr, player, dayTime);
        for (const glm::ivec3& p : changed) blockEntities.rescanPosition(world, p);
        (void)blockEntities.tick(world);
        dayTime += 1.0;
    }
}
} // namespace

int main() {
    double dayTime = 0.0;
    Player player({20.5, 4.0, 20.5});

    // 13A: lever -> dust -> lamp.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Lever, 13));
        world.setBlock(2, 1, 1, b(BlockId::RedstoneWire));
        world.setBlock(3, 1, 1, b(BlockId::RedstoneLamp));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 3);
        assert((blockMetadata(world.getBlock(2, 1, 1)) & 15U) > 0U);
        assert(static_cast<BlockId>(blockId(world.getBlock(3, 1, 1))) == BlockId::LitRedstoneLamp);
    }

    // 13A: button duration and pressure plate activation.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::StoneButton, 13));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 21);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) == 0U);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        Player platePlayer({1.5, 1.0, 1.5});
        world.setBlock(1, 0, 1, b(BlockId::Stone));
        world.setBlock(1, 1, 1, b(BlockId::StonePressurePlate));
        rs.scanLoadedWorld(world); run(rs, world, bes, platePlayer, dayTime, 1);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 1U) != 0U);
    }

    // 13A: redstone torch inverter.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::RedstoneBlock));
        world.setBlock(1, 2, 1, b(BlockId::RedstoneTorch, 5));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 3);
        assert(static_cast<BlockId>(blockId(world.getBlock(1, 2, 1))) == BlockId::UnlitRedstoneTorch);
        world.setBlock(1, 1, 1, b(BlockId::Air)); rs.neighborChanged(world, {1, 1, 1});
        run(rs, world, bes, player, dayTime, 3);
        assert(static_cast<BlockId>(blockId(world.getBlock(1, 2, 1))) == BlockId::RedstoneTorch);
    }

    // 13B: repeater delay and repeater locking.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(0, 1, 1, b(BlockId::RedstoneBlock));
        world.setBlock(1, 1, 1, b(BlockId::UnpoweredRepeater, 3)); // east, delay 1
        world.setBlock(2, 1, 1, b(BlockId::RedstoneLamp));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 5);
        assert(static_cast<BlockId>(blockId(world.getBlock(1, 1, 1))) == BlockId::PoweredRepeater);
        assert(static_cast<BlockId>(blockId(world.getBlock(2, 1, 1))) == BlockId::LitRedstoneLamp);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(0, 1, 1, b(BlockId::RedstoneBlock));
        world.setBlock(1, 1, 1, b(BlockId::UnpoweredRepeater, 3));
        world.setBlock(1, 1, 2, b(BlockId::PoweredRepeater, 2)); // outputs north into main repeater
        world.setBlock(1, 1, 3, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 6);
        assert(static_cast<BlockId>(blockId(world.getBlock(1, 1, 1))) == BlockId::UnpoweredRepeater);
    }

    // 13B: comparator container signal and subtraction mode.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(0, 1, 1, b(BlockId::Chest)); bes.rescanPosition(world, {0, 1, 1});
        bes.containerSlot(world, {0, 1, 1}, 0) = ItemStack{1, 64, 0, {}};
        world.setBlock(1, 1, 1, b(BlockId::UnpoweredComparator, 3));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert(rs.comparatorOutputAt({1, 1, 1}) > 0);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(0, 1, 1, b(BlockId::RedstoneBlock));
        world.setBlock(1, 1, 1, b(BlockId::UnpoweredComparator, 7)); // east + subtract
        world.setBlock(1, 1, 2, b(BlockId::PoweredRepeater, 2));
        world.setBlock(1, 1, 3, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert(rs.comparatorOutputAt({1, 1, 1}) == 0);
    }

    // 13B: observer two-tick pulse.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(2, 1, 1, b(BlockId::Observer, 4)); // observes west, outputs east
        world.setBlock(3, 1, 1, b(BlockId::RedstoneLamp));
        rs.scanLoadedWorld(world);
        world.setBlock(1, 1, 1, b(BlockId::Stone)); rs.neighborChanged(world, {1, 1, 1});
        run(rs, world, bes, player, dayTime, 1);
        assert((blockMetadata(world.getBlock(2, 1, 1)) & 8U) != 0U);
        run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(2, 1, 1)) & 8U) == 0U);
    }

    // 13B: normal and inverted daylight detectors expose complementary noon power.
    {
        World world; Chunk& chunk = world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        std::vector<std::uint8_t> sky(16 * 256 * 16, 15), block(16 * 256 * 16, 0);
        (void)chunk.applyLighting(sky, block);
        world.setBlock(1, 10, 1, b(BlockId::DaylightDetector));
        world.setBlock(2, 10, 1, b(BlockId::DaylightDetectorInverted));
        double noon = 6000.0;
        rs.scanLoadedWorld(world); run(rs, world, bes, player, noon, 1);
        assert((blockMetadata(world.getBlock(1, 10, 1)) & 15U) >= 14U);
        assert((blockMetadata(world.getBlock(2, 10, 1)) & 15U) <= 1U);
    }

    // 13C: piston uses a transient moving-piston state, then finishes the push.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Piston, 5));
        world.setBlock(2, 1, 1, b(BlockId::Stone));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 1);
        assert(static_cast<BlockId>(blockId(world.getBlock(3, 1, 1))) == BlockId::PistonExtension);
        run(rs, world, bes, player, dayTime, 1);
        assert(static_cast<BlockId>(blockId(world.getBlock(2, 1, 1))) == BlockId::PistonHead);
        assert(static_cast<BlockId>(blockId(world.getBlock(3, 1, 1))) == BlockId::Stone);
    }

    // 13C: 12-block push limit, immovable obsidian, and tile-entity rejection.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Piston, 5));
        for (int x = 2; x <= 13; ++x) world.setBlock(x, 1, 1, b(BlockId::Stone));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) != 0U);
        assert(static_cast<BlockId>(blockId(world.getBlock(14, 1, 1))) == BlockId::Stone);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Piston, 5));
        for (int x = 2; x <= 14; ++x) world.setBlock(x, 1, 1, b(BlockId::Stone));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) == 0U);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Piston, 5)); world.setBlock(2, 1, 1, b(BlockId::Obsidian));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) == 0U);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::Piston, 5)); world.setBlock(2, 1, 1, b(BlockId::Chest));
        bes.rescanPosition(world, {2, 1, 1}); world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) == 0U);
    }

    // 13C: sticky pull and powered doors/trapdoors/gates.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::StickyPiston, 5)); world.setBlock(2, 1, 1, b(BlockId::Stone));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock)); rs.scanLoadedWorld(world);
        run(rs, world, bes, player, dayTime, 2);
        world.setBlock(1, 1, 2, b(BlockId::Air)); rs.neighborChanged(world, {1, 1, 2});
        run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 8U) == 0U);
        assert(static_cast<BlockId>(blockId(world.getBlock(2, 1, 1))) == BlockId::Stone);
        assert(static_cast<BlockId>(blockId(world.getBlock(3, 1, 1))) == BlockId::Air);
    }
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(1, 1, 1, b(BlockId::WoodenDoor)); world.setBlock(1, 2, 1, b(BlockId::WoodenDoor, 8));
        world.setBlock(2, 1, 1, b(BlockId::WoodenDoor)); world.setBlock(2, 2, 1, b(BlockId::WoodenDoor, 8));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock)); world.setBlock(2, 1, 2, b(BlockId::RedstoneBlock));
        world.setBlock(4, 1, 1, b(BlockId::Trapdoor)); world.setBlock(5, 1, 1, b(BlockId::RedstoneBlock));
        world.setBlock(4, 1, 3, b(BlockId::FenceGate)); world.setBlock(5, 1, 3, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 2);
        assert((blockMetadata(world.getBlock(1, 1, 1)) & 4U) != 0U);
        assert((blockMetadata(world.getBlock(2, 1, 1)) & 4U) != 0U);
        assert((blockMetadata(world.getBlock(4, 1, 1)) & 4U) != 0U);
        assert((blockMetadata(world.getBlock(4, 1, 3)) & 4U) != 0U);
    }

    // 13D: redstone locks hoppers.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(2, 2, 2, b(BlockId::Hopper)); world.setBlock(2, 1, 2, b(BlockId::Chest));
        world.setBlock(3, 2, 2, b(BlockId::RedstoneBlock));
        bes.rescanPosition(world, {2, 2, 2}); bes.rescanPosition(world, {2, 1, 2});
        bes.containerSlot(world, {2, 2, 2}, 0) = ItemStack{1, 1, 0, {}};
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 1);
        assert((blockMetadata(world.getBlock(2, 2, 2)) & 8U) != 0U);
        (void)bes.tick(world);
        assert(bes.containerSlot(world, {2, 2, 2}, 0).count == 1);
    }

    // 13D: dispenser/dropper rising edges consume one stored item.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        world.setBlock(2, 2, 2, b(BlockId::Dispenser, 5)); world.setBlock(2, 2, 3, b(BlockId::RedstoneBlock));
        world.setBlock(5, 2, 2, b(BlockId::Dropper, 5)); world.setBlock(5, 2, 3, b(BlockId::RedstoneBlock));
        bes.rescanPosition(world, {2, 2, 2}); bes.rescanPosition(world, {5, 2, 2});
        bes.containerSlot(world, {2, 2, 2}, 0) = ItemStack{1, 2, 0, {}};
        bes.containerSlot(world, {5, 2, 2}, 0) = ItemStack{1, 2, 0, {}};
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 6);
        assert(bes.containerSlot(world, {2, 2, 2}, 0).count == 1);
        assert(bes.containerSlot(world, {5, 2, 2}, 0).count == 1);
    }

    // 13D: powered/activator rails propagate through an eight-block run.
    {
        World world; (void)world.ensureChunk(0, 0); BlockEntitySystem bes; RedstoneSystem rs;
        for (int x = 1; x <= 8; ++x) world.setBlock(x, 1, 1, b(BlockId::GoldenRail, 1));
        for (int x = 1; x <= 8; ++x) world.setBlock(x, 1, 3, b(BlockId::ActivatorRail, 1));
        world.setBlock(1, 1, 2, b(BlockId::RedstoneBlock));
        rs.scanLoadedWorld(world); run(rs, world, bes, player, dayTime, 3);
        assert((blockMetadata(world.getBlock(8, 1, 1)) & 8U) != 0U);
        assert((blockMetadata(world.getBlock(8, 1, 3)) & 8U) != 0U);
    }

    std::cout << "Stage 13 redstone tests passed.\n";
    return 0;
}
