#include "worldgen/WorldPopulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"
#include "worldgen/JavaRandom.hpp"
#include "worldgen/StructureTemplate.hpp"

namespace {

constexpr float pi = 3.14159265358979323846F;

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

bool isAir(World& world, int x, int y, int z) { return blockId(world.getBlock(x, y, z)) == 0; }

bool isLeaves(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Leaves || id == BlockId::Leaves2;
}

bool replaceableByTree(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Air || id == BlockId::Leaves || id == BlockId::Leaves2 ||
        id == BlockId::TallGrass || id == BlockId::Vine;
}

bool treeCanGrowInto(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Air || id == BlockId::Leaves || id == BlockId::Leaves2 ||
        id == BlockId::Grass || id == BlockId::Dirt || id == BlockId::Log ||
        id == BlockId::Log2 || id == BlockId::Sapling || id == BlockId::Vine;
}

bool supportsBush(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Grass || id == BlockId::Dirt || id == BlockId::Farmland;
}

bool supportsDeadBush(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Sand || id == BlockId::Dirt || id == BlockId::HardenedClay ||
        id == BlockId::StainedHardenedClay;
}

bool isWater(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Water || id == BlockId::FlowingWater;
}

bool isLiquid(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Water || id == BlockId::FlowingWater ||
        id == BlockId::Lava || id == BlockId::FlowingLava;
}

bool isSolid(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    if (id == BlockId::Air || isLiquid(state)) return false;
    return BlockRegistry::get(state).fullCube;
}

void nbtU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void nbtString(std::vector<std::uint8_t>& bytes, std::string_view value) {
    nbtU16(bytes, static_cast<std::uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> spawnerNbt(int x, int y, int z, std::string_view mob) {
    std::vector<std::uint8_t> bytes;
    bytes.push_back(10); nbtU16(bytes, 0); // unnamed root compound
    const auto namedString = [&](std::string_view name, std::string_view value) {
        bytes.push_back(8); nbtString(bytes, name); nbtString(bytes, value);
    };
    const auto namedInt = [&](std::string_view name, int value) {
        bytes.push_back(3); nbtString(bytes, name);
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) >> shift));
    };
    namedString("id", "minecraft:mob_spawner");
    namedInt("x", x); namedInt("y", y); namedInt("z", z);
    bytes.push_back(2); nbtString(bytes, "Delay"); nbtU16(bytes, 20);
    bytes.push_back(10); nbtString(bytes, "SpawnData");
    namedString("id", mob); bytes.push_back(0);
    bytes.push_back(0);
    return bytes;
}

int surfaceY(const World& world, int x, int z) {
    for (int y = chunkHeight - 1; y >= 0; --y)
        if (blockId(world.getBlock(x, y, z)) != 0) return y + 1;
    return 0;
}

std::int64_t wrappedPopulationSeed(std::int64_t worldSeed, int chunkX, int chunkZ) {
    JavaRandom seedRandom(worldSeed);
    const std::int64_t oddX = seedRandom.nextLong() / 2LL * 2LL + 1LL;
    const std::int64_t oddZ = seedRandom.nextLong() / 2LL * 2LL + 1LL;
    const std::uint64_t mixed = static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkX)) *
        static_cast<std::uint64_t>(oddX) +
        static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkZ)) *
        static_cast<std::uint64_t>(oddZ);
    return static_cast<std::int64_t>(mixed ^ static_cast<std::uint64_t>(worldSeed));
}

struct DecoratorProfile {
    int trees = 0;
    int flowers = 2;
    int grass = 1;
    int deadBush = 0;
    int reeds = 0;
    int cactus = 0;
    int waterlily = 0;
    int mushrooms = 0;
    int bigMushrooms = 0;
    float extraTreeChance = 0.1F;
};

DecoratorProfile profileFor(int biome) {
    DecoratorProfile profile;
    switch (biome) {
        case 1: case 129: profile.flowers = 4; profile.grass = 10; break; // plains
        case 2: case 17: case 37: case 38: case 39:
        case 130: case 165: case 166: case 167:
            profile.trees = -999; profile.deadBush = biome >= 37 ? 20 : 2;
            profile.reeds = biome >= 37 ? 3 : 50; profile.cactus = biome >= 37 ? 5 : 10;
            profile.flowers = biome >= 37 ? 0 : 2;
            if (biome == 38 || biome == 166) profile.trees = 5;
            break;
        case 3: case 20: case 34: case 131: case 162:
            profile.trees = (biome == 20 || biome == 34) ? 3 : 0; profile.grass = 1; break;
        case 4: case 18: case 27: case 28: case 29: case 132: case 155: case 156: case 157:
            profile.trees = (biome == 29 || biome == 157) ? -999 : 10; profile.grass = 2;
            if (biome == 132) { profile.trees = 6; profile.flowers = 100; profile.grass = 1; }
            break;
        case 5: case 19: case 30: case 31: case 32: case 33: case 133: case 158: case 160: case 161:
            profile.trees = 10; profile.grass = (biome == 32 || biome == 33 || biome == 160 || biome == 161) ? 7 : 1;
            if (profile.grass == 7) { profile.deadBush = 1; profile.mushrooms = 3; }
            else profile.mushrooms = 1;
            break;
        case 6: case 134: profile.trees = 2; profile.flowers = 1; profile.grass = 5;
            profile.deadBush = 1; profile.reeds = 10; profile.waterlily = 4; profile.mushrooms = 8; break;
        case 12: case 13: case 26: case 140: profile.trees = 0; profile.grass = 1; break;
        case 14: case 15: profile.trees = -100; profile.flowers = -100; profile.grass = -100;
            profile.mushrooms = 1; profile.bigMushrooms = 1; break;
        case 21: case 22: case 23: case 149: case 151:
            profile.trees = (biome == 23 || biome == 151) ? 2 : 50;
            profile.grass = 25; profile.flowers = 4; break;
        case 35: case 36: case 163: case 164:
            if (biome == 163 || biome == 164) {
                profile.trees = 2; profile.flowers = 2; profile.grass = 5;
            } else { profile.trees = 1; profile.flowers = 4; profile.grass = 20; }
            break;
        default: break;
    }
    return profile;
}

int treeSpecies(int biome, JavaRandom& random) {
    if (biome == 27 || biome == 28 || biome == 155 || biome == 156) return 2;
    if (biome == 5 || biome == 19 || biome == 30 || biome == 31 || biome == 32 ||
        biome == 33 || biome == 133 || biome == 158 || biome == 160 || biome == 161)
        return 1;
    if (biome == 21 || biome == 22 || biome == 23 || biome == 149 || biome == 151) return 3;
    if (biome == 35 || biome == 36 || biome == 163 || biome == 164) return 4;
    if (biome == 29 || biome == 157) return 5;
    if (biome == 4 || biome == 18 || biome == 132) return random.nextInt(5) == 0 ? 2 : 0;
    if (biome == 3 || biome == 20 || biome == 34 || biome == 131 || biome == 162)
        return random.nextInt(3) > 0 ? 1 : 0;
    return 0;
}

BlockState trunkFor(int species) {
    return species < 4 ? block(BlockId::Log, static_cast<std::uint8_t>(species)) :
        block(BlockId::Log2, static_cast<std::uint8_t>(species - 4));
}

BlockState leavesFor(int species) {
    // CHECK_DECAY=false, DECAYABLE=true maps to bit 0x4 in legacy metadata.
    return species < 4 ? block(BlockId::Leaves, static_cast<std::uint8_t>(species | 4)) :
        block(BlockId::Leaves2, static_cast<std::uint8_t>((species - 4) | 4));
}

void plantPatch(World& world, JavaRandom& random, int x, int y, int z,
                BlockState plant, int attempts, int horizontal, int vertical,
                bool descend) {
    if (descend)
        while ((isAir(world, x, y, z) || isLeaves(world.getBlock(x, y, z))) && y > 0) --y;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const int px = x + random.nextInt(horizontal) - random.nextInt(horizontal);
        const int py = y + random.nextInt(vertical) - random.nextInt(vertical);
        const int pz = z + random.nextInt(horizontal) - random.nextInt(horizontal);
        if (py <= 0 || py >= chunkHeight || !isAir(world, px, py, pz)) continue;
        const auto plantId = static_cast<BlockId>(blockId(plant));
        const bool supported = plantId == BlockId::DeadBush
            ? supportsDeadBush(world.getBlock(px, py - 1, pz))
            : supportsBush(world.getBlock(px, py - 1, pz));
        if (supported)
            world.setGeneratedBlock(px, py, pz, plant);
    }
}

void placeLeafIfAir(World& world, int x, int y, int z, BlockState leaves) {
    if (isAir(world, x, y, z)) world.setGeneratedBlock(x, y, z, leaves);
}

bool generateDarkOak(World& world, JavaRandom& random, int x, int y, int z) {
    const int height = random.nextInt(3) + random.nextInt(2) + 6;
    if (y < 1 || y + height + 1 >= chunkHeight) return false;
    const BlockId soil = static_cast<BlockId>(blockId(world.getBlock(x, y - 1, z)));
    if (soil != BlockId::Grass && soil != BlockId::Dirt) return false;
    for (int dy = 0; dy <= height + 1; ++dy) {
        int radius = 1;
        if (dy == 0) radius = 0;
        if (dy >= height - 1) radius = 2;
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
                if (!treeCanGrowInto(world.getBlock(x + dx, y + dy, z + dz))) return false;
    }

    for (int dx = 0; dx <= 1; ++dx)
        for (int dz = 0; dz <= 1; ++dz)
            world.setGeneratedBlock(x + dx, y - 1, z + dz, block(BlockId::Dirt));
    constexpr std::array<std::array<int, 2>, 4> directions{{
        std::array{0, -1}, std::array{0, 1}, std::array{-1, 0}, std::array{1, 0}}};
    const auto direction = directions[static_cast<std::size_t>(random.nextInt(4))];
    const int bendStart = height - random.nextInt(4);
    int bendLength = 2 - random.nextInt(3);
    int trunkX = x;
    int trunkZ = z;
    const int crownY = y + height - 1;
    const BlockState trunk = block(BlockId::Log2, 1);
    const BlockState leaves = block(BlockId::Leaves2, 5);
    for (int dy = 0; dy < height; ++dy) {
        if (dy >= bendStart && bendLength > 0) {
            trunkX += direction[0];
            trunkZ += direction[1];
            --bendLength;
        }
        const int py = y + dy;
        if (treeCanGrowInto(world.getBlock(trunkX, py, trunkZ))) {
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                    if (treeCanGrowInto(world.getBlock(trunkX + dx, py, trunkZ + dz)))
                        world.setGeneratedBlock(trunkX + dx, py, trunkZ + dz, trunk);
        }
    }

    for (int dx = -2; dx <= 0; ++dx) {
        for (int dz = -2; dz <= 0; ++dz) {
            placeLeafIfAir(world, trunkX + dx, crownY - 1, trunkZ + dz, leaves);
            placeLeafIfAir(world, 1 + trunkX - dx, crownY - 1, trunkZ + dz, leaves);
            placeLeafIfAir(world, trunkX + dx, crownY - 1, 1 + trunkZ - dz, leaves);
            placeLeafIfAir(world, 1 + trunkX - dx, crownY - 1, 1 + trunkZ - dz, leaves);
            if ((dx > -2 || dz > -1) && (dx != -1 || dz != -2)) {
                placeLeafIfAir(world, trunkX + dx, crownY + 1, trunkZ + dz, leaves);
                placeLeafIfAir(world, 1 + trunkX - dx, crownY + 1, trunkZ + dz, leaves);
                placeLeafIfAir(world, trunkX + dx, crownY + 1, 1 + trunkZ - dz, leaves);
                placeLeafIfAir(world, 1 + trunkX - dx, crownY + 1, 1 + trunkZ - dz, leaves);
            }
        }
    }
    if (random.nextBoolean()) {
        placeLeafIfAir(world, trunkX, crownY + 2, trunkZ, leaves);
        placeLeafIfAir(world, trunkX + 1, crownY + 2, trunkZ, leaves);
        placeLeafIfAir(world, trunkX + 1, crownY + 2, trunkZ + 1, leaves);
        placeLeafIfAir(world, trunkX, crownY + 2, trunkZ + 1, leaves);
    }
    for (int dx = -3; dx <= 4; ++dx)
        for (int dz = -3; dz <= 4; ++dz)
            if ((dx != -3 || dz != -3) && (dx != -3 || dz != 4) &&
                (dx != 4 || dz != -3) && (dx != 4 || dz != 4) &&
                (std::abs(dx) < 3 || std::abs(dz) < 3))
                placeLeafIfAir(world, trunkX + dx, crownY, trunkZ + dz, leaves);

    for (int branchX = -1; branchX <= 2; ++branchX) {
        for (int branchZ = -1; branchZ <= 2; ++branchZ) {
            if (branchX >= 0 && branchX <= 1 && branchZ >= 0 && branchZ <= 1) continue;
            if (random.nextInt(3) > 0) continue;
            const int branchLength = random.nextInt(3) + 2;
            for (int offset = 0; offset < branchLength; ++offset)
                if (treeCanGrowInto(world.getBlock(x + branchX, crownY - offset - 1, z + branchZ)))
                    world.setGeneratedBlock(x + branchX, crownY - offset - 1, z + branchZ, trunk);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    placeLeafIfAir(world, trunkX + branchX + dx, crownY, trunkZ + branchZ + dz, leaves);
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::abs(dx) != 2 || std::abs(dz) != 2)
                        placeLeafIfAir(world, trunkX + branchX + dx, crownY - 1,
                                       trunkZ + branchZ + dz, leaves);
        }
    }
    return true;
}

bool generateHugeMushroom(World& world, JavaRandom& random, int x, int y, int z) {
    const bool brown = random.nextBoolean();
    int height = random.nextInt(3) + 4;
    if (random.nextInt(12) == 0) height *= 2;
    if (y < 1 || y + height + 1 >= chunkHeight) return false;
    for (int py = y; py <= y + height + 1; ++py) {
        const int radius = py <= y + 3 ? 0 : 3;
        for (int px = x - radius; px <= x + radius; ++px)
            for (int pz = z - radius; pz <= z + radius; ++pz) {
                const BlockId id = static_cast<BlockId>(blockId(world.getBlock(px, py, pz)));
                if (id != BlockId::Air && id != BlockId::Leaves && id != BlockId::Leaves2) return false;
            }
    }
    const BlockId soil = static_cast<BlockId>(blockId(world.getBlock(x, y - 1, z)));
    if (soil != BlockId::Dirt && soil != BlockId::Grass && soil != BlockId::Mycelium) return false;
    const BlockId cap = brown ? BlockId::BrownMushroomBlock : BlockId::RedMushroomBlock;
    const int firstCapY = brown ? y + height : y + height - 3;
    for (int py = firstCapY; py <= y + height; ++py) {
        int radius = py < y + height ? 2 : 1;
        if (brown) radius = 3;
        const int minX = x - radius;
        const int maxX = x + radius;
        const int minZ = z - radius;
        const int maxZ = z + radius;
        for (int px = minX; px <= maxX; ++px) {
            for (int pz = minZ; pz <= maxZ; ++pz) {
                int meta = 5;
                if (px == minX) --meta;
                else if (px == maxX) ++meta;
                if (pz == minZ) meta -= 3;
                else if (pz == maxZ) meta += 3;
                if (brown || py < y + height) {
                    if ((px == minX || px == maxX) && (pz == minZ || pz == maxZ)) continue;
                    if (px == x - (radius - 1) && pz == minZ) meta = 1;
                    if (px == minX && pz == z - (radius - 1)) meta = 1;
                    if (px == x + (radius - 1) && pz == minZ) meta = 3;
                    if (px == maxX && pz == z - (radius - 1)) meta = 3;
                    if (px == x - (radius - 1) && pz == maxZ) meta = 7;
                    if (px == minX && pz == z + (radius - 1)) meta = 7;
                    if (px == x + (radius - 1) && pz == maxZ) meta = 9;
                    if (px == maxX && pz == z + (radius - 1)) meta = 9;
                }
                if (meta == 5 && py < y + height) meta = 0;
                if (meta != 0 && !BlockRegistry::get(world.getBlock(px, py, pz)).fullCube)
                    world.setGeneratedBlock(px, py, pz, block(cap, static_cast<std::uint8_t>(meta)));
            }
        }
    }
    for (int offset = 0; offset < height; ++offset)
        if (!BlockRegistry::get(world.getBlock(x, y + offset, z)).fullCube)
            world.setGeneratedBlock(x, y + offset, z, block(cap, 10));
    return true;
}

bool generateDoublePlant(World& world, JavaRandom& random, int x, int y, int z,
                         std::uint8_t type) {
    bool placed = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const int px = x + random.nextInt(8) - random.nextInt(8);
        const int py = y + random.nextInt(4) - random.nextInt(4);
        const int pz = z + random.nextInt(8) - random.nextInt(8);
        if (py <= 0 || py >= 255 || !isAir(world, px, py, pz) || !isAir(world, px, py + 1, pz) ||
            !supportsBush(world.getBlock(px, py - 1, pz))) continue;
        world.setGeneratedBlock(px, py, pz, block(BlockId::DoublePlant, type));
        world.setGeneratedBlock(px, py + 1, pz, block(BlockId::DoublePlant, 8));
        placed = true;
    }
    return placed;
}

} // namespace

bool WorldPopulator::generateLake(World& world, JavaRandom& random, int x, int y, int z,
                                  BlockState liquid) const {
    x -= 8;
    z -= 8;
    while (y > 5 && isAir(world, x, y, z)) --y;
    if (y <= 4) return false;
    y -= 4;
    std::array<bool, 2048> carved{};
    const int ellipsoids = random.nextInt(4) + 4;
    for (int shape = 0; shape < ellipsoids; ++shape) {
        const double sx = random.nextDouble() * 6.0 + 3.0;
        const double sy = random.nextDouble() * 4.0 + 2.0;
        const double sz = random.nextDouble() * 6.0 + 3.0;
        const double cx = random.nextDouble() * (16.0 - sx - 2.0) + 1.0 + sx / 2.0;
        const double cy = random.nextDouble() * (8.0 - sy - 4.0) + 2.0 + sy / 2.0;
        const double cz = random.nextDouble() * (16.0 - sz - 2.0) + 1.0 + sz / 2.0;
        for (int lx = 1; lx < 15; ++lx)
            for (int lz = 1; lz < 15; ++lz)
                for (int ly = 1; ly < 7; ++ly) {
                    const double dx = (lx - cx) / (sx / 2.0);
                    const double dy = (ly - cy) / (sy / 2.0);
                    const double dz = (lz - cz) / (sz / 2.0);
                    if (dx * dx + dy * dy + dz * dz < 1.0)
                        carved[static_cast<std::size_t>((lx * 16 + lz) * 8 + ly)] = true;
                }
    }
    const auto at = [&](int lx, int ly, int lz) -> bool {
        return carved[static_cast<std::size_t>((lx * 16 + lz) * 8 + ly)];
    };
    for (int lx = 0; lx < 16; ++lx)
        for (int lz = 0; lz < 16; ++lz)
            for (int ly = 0; ly < 8; ++ly) {
                const bool boundary = !at(lx, ly, lz) &&
                    ((lx < 15 && at(lx + 1, ly, lz)) || (lx > 0 && at(lx - 1, ly, lz)) ||
                     (lz < 15 && at(lx, ly, lz + 1)) || (lz > 0 && at(lx, ly, lz - 1)) ||
                     (ly < 7 && at(lx, ly + 1, lz)) || (ly > 0 && at(lx, ly - 1, lz)));
                if (!boundary) continue;
                const BlockState current = world.getBlock(x + lx, y + ly, z + lz);
                if ((ly >= 4 && isLiquid(current)) ||
                    (ly < 4 && !isSolid(current) && current != liquid)) return false;
            }
    for (int lx = 0; lx < 16; ++lx)
        for (int lz = 0; lz < 16; ++lz)
            for (int ly = 0; ly < 8; ++ly)
                if (at(lx, ly, lz))
                    world.setGeneratedBlock(x + lx, y + ly, z + lz,
                                            ly >= 4 ? block(BlockId::Air) : liquid);

    const bool lava = static_cast<BlockId>(blockId(liquid)) == BlockId::Lava;
    if (lava) {
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz)
                for (int ly = 0; ly < 8; ++ly) {
                    const bool boundary = !at(lx, ly, lz) &&
                        ((lx < 15 && at(lx + 1, ly, lz)) || (lx > 0 && at(lx - 1, ly, lz)) ||
                         (lz < 15 && at(lx, ly, lz + 1)) || (lz > 0 && at(lx, ly, lz - 1)) ||
                         (ly < 7 && at(lx, ly + 1, lz)) || (ly > 0 && at(lx, ly - 1, lz)));
                    if (boundary && (ly < 4 || random.nextInt(2) != 0) &&
                        isSolid(world.getBlock(x + lx, y + ly, z + lz)))
                        world.setGeneratedBlock(x + lx, y + ly, z + lz, block(BlockId::Stone));
                }
    }
    return true;
}

bool WorldPopulator::generateDungeon(World& world, JavaRandom& random, int x, int y, int z) const {
    const int rx = random.nextInt(2) + 2;
    const int rz = random.nextInt(2) + 2;
    int openings = 0;
    for (int dx = -rx - 1; dx <= rx + 1; ++dx)
        for (int dy = -1; dy <= 4; ++dy)
            for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
                const BlockState state = world.getBlock(x + dx, y + dy, z + dz);
                if ((dy == -1 || dy == 4) && !isSolid(state)) return false;
                if ((dx == -rx - 1 || dx == rx + 1 || dz == -rz - 1 || dz == rz + 1) &&
                    dy == 0 && isAir(world, x + dx, y, z + dz) &&
                    isAir(world, x + dx, y + 1, z + dz)) ++openings;
            }
    if (openings < 1 || openings > 5) return false;
    for (int dx = -rx - 1; dx <= rx + 1; ++dx)
        for (int dy = 3; dy >= -1; --dy)
            for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
                const bool inside = dx != -rx - 1 && dx != rx + 1 && dz != -rz - 1 &&
                    dz != rz + 1 && dy != -1 && dy != 4;
                if (inside) world.setGeneratedBlock(x + dx, y + dy, z + dz, block(BlockId::Air));
                else if (isSolid(world.getBlock(x + dx, y + dy, z + dz)))
                    world.setGeneratedBlock(x + dx, y + dy, z + dz,
                        dy == -1 && random.nextInt(4) != 0 ? block(BlockId::MossyCobblestone) :
                                                            block(BlockId::Cobblestone));
            }
    world.setGeneratedBlock(x, y, z, block(BlockId::MobSpawner));
    constexpr std::array<std::string_view, 4> mobs{
        "minecraft:skeleton", "minecraft:zombie", "minecraft:zombie", "minecraft:spider"};
    const std::string_view mob = mobs[static_cast<std::size_t>(random.nextInt(4))];
    world.addGeneratedBlockEntity({x, y, z, "minecraft:mob_spawner", spawnerNbt(x, y, z, mob)});
    return true;
}

bool WorldPopulator::generateSpring(World& world, int x, int y, int z,
                                    BlockState liquid) const {
    const auto isStone = [&](int px, int py, int pz) {
        return static_cast<BlockId>(blockId(world.getBlock(px, py, pz))) == BlockId::Stone;
    };
    if (!isStone(x, y + 1, z) || !isStone(x, y - 1, z)) return false;
    const BlockId current = static_cast<BlockId>(blockId(world.getBlock(x, y, z)));
    if (current != BlockId::Air && current != BlockId::Stone) return false;
    int stone = 0;
    int air = 0;
    constexpr std::array<std::array<int, 2>, 4> offsets{{{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}}};
    for (const auto& offset : offsets) {
        if (isStone(x + offset[0], y, z + offset[1])) ++stone;
        if (isAir(world, x + offset[0], y, z + offset[1])) ++air;
    }
    if (stone == 3 && air == 1) world.setGeneratedBlock(x, y, z, liquid);
    return true;
}

bool WorldPopulator::generateFossil(World& world, int chunkX, int chunkZ) const {
    static constexpr std::array<std::string_view, 8> names{
        "fossil_spine_01", "fossil_spine_02", "fossil_spine_03", "fossil_spine_04",
        "fossil_skull_01", "fossil_skull_02", "fossil_skull_03", "fossil_skull_04"};
    static const std::array<StructureTemplate, 8> fossils = [] {
        std::array<StructureTemplate, 8> loaded;
        for (std::size_t index = 0; index < names.size(); ++index) {
            const std::filesystem::path path = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
                "assets/minecraft/structures/fossils" / (std::string(names[index]) + ".nbt");
            loaded[index] = StructureTemplate::load(path.string());
        }
        return loaded;
    }();
    static const std::array<StructureTemplate, 8> coal = [] {
        std::array<StructureTemplate, 8> loaded;
        for (std::size_t index = 0; index < names.size(); ++index) {
            const std::filesystem::path path = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
                "assets/minecraft/structures/fossils" / (std::string(names[index]) + "_coal.nbt");
            loaded[index] = StructureTemplate::load(path.string());
        }
        return loaded;
    }();

    const auto javaIntProduct = [](int left, int right) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(left) * static_cast<std::uint32_t>(right));
    };
    const std::int32_t xSquared = javaIntProduct(chunkX, chunkX);
    const std::int32_t zSquared = javaIntProduct(chunkZ, chunkZ);
    const std::int32_t xTerm = javaIntProduct(xSquared, 4987142);
    const std::int32_t xLinear = javaIntProduct(chunkX, 5947611);
    const std::int64_t chunkSeed = static_cast<std::int64_t>(xTerm) + xLinear +
        static_cast<std::int64_t>(zSquared) * 4392871LL +
        static_cast<std::int64_t>(javaIntProduct(chunkZ, 389711));
    JavaRandom random(static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(config_.seed) + static_cast<std::uint64_t>(chunkSeed)) ^ 987234911ULL));
    const int rotation = random.nextInt(4);
    const int selected = random.nextInt(8);
    const StructureTemplate& fossil = fossils[static_cast<std::size_t>(selected)];
    const int width = fossil.rotatedSizeX(rotation);
    const int depth = fossil.rotatedSizeZ(rotation);
    const int offsetX = random.nextInt(16 - width);
    const int offsetZ = random.nextInt(16 - depth);
    int minimumHeight = 256;
    const int baseX = chunkX * 16;
    const int baseZ = chunkZ * 16;
    // WorldGenFossils 1.12.2 intentionally uses the rotated X size for both
    // loops; preserve that vanilla quirk rather than substituting depth.
    for (int x = 0; x < width; ++x)
        for (int z = 0; z < width; ++z)
            minimumHeight = std::min(minimumHeight, surfaceY(world, baseX + offsetX + x, baseZ + offsetZ + z));
    const int y = std::max(minimumHeight - 15 - random.nextInt(10), 10);

    int originX = baseX + offsetX;
    int originZ = baseZ + offsetZ;
    if (rotation == 1) originX += fossil.sizeZ() - 1;
    else if (rotation == 2) { originX += fossil.sizeX() - 1; originZ += fossil.sizeZ() - 1; }
    else if (rotation == 3) originZ += fossil.sizeX() - 1;
    fossil.place(world, random, originX, y, originZ, rotation, 0.9F, chunkX, chunkZ);
    coal[static_cast<std::size_t>(selected)].place(
        world, random, originX, y, originZ, rotation, 0.1F, chunkX, chunkZ);
    return true;
}

void WorldPopulator::generateOre(World& world, JavaRandom& random, int x, int y, int z,
                                 BlockState state, int size) const {
    const float angle = random.nextFloat() * pi;
    const double x0 = static_cast<float>(x + 8) + std::sin(angle) * size / 8.0F;
    const double x1 = static_cast<float>(x + 8) - std::sin(angle) * size / 8.0F;
    const double z0 = static_cast<float>(z + 8) + std::cos(angle) * size / 8.0F;
    const double z1 = static_cast<float>(z + 8) - std::cos(angle) * size / 8.0F;
    const double y0 = y + random.nextInt(3) - 2;
    const double y1 = y + random.nextInt(3) - 2;
    for (int segment = 0; segment < size; ++segment) {
        const float progress = static_cast<float>(segment) / size;
        const double centerX = std::lerp(x0, x1, static_cast<double>(progress));
        const double centerY = std::lerp(y0, y1, static_cast<double>(progress));
        const double centerZ = std::lerp(z0, z1, static_cast<double>(progress));
        const double randomSize = random.nextDouble() * size / 16.0;
        const double diameter = (std::sin(pi * progress) + 1.0) * randomSize + 1.0;
        const int minX = static_cast<int>(std::floor(centerX - diameter / 2.0));
        const int minY = static_cast<int>(std::floor(centerY - diameter / 2.0));
        const int minZ = static_cast<int>(std::floor(centerZ - diameter / 2.0));
        const int maxX = static_cast<int>(std::floor(centerX + diameter / 2.0));
        const int maxY = static_cast<int>(std::floor(centerY + diameter / 2.0));
        const int maxZ = static_cast<int>(std::floor(centerZ + diameter / 2.0));
        for (int px = minX; px <= maxX; ++px) {
            const double dx = (px + 0.5 - centerX) / (diameter / 2.0);
            if (dx * dx >= 1.0) continue;
            for (int py = minY; py <= maxY; ++py) {
                const double dy = (py + 0.5 - centerY) / (diameter / 2.0);
                if (dx * dx + dy * dy >= 1.0) continue;
                for (int pz = minZ; pz <= maxZ; ++pz) {
                    const double dz = (pz + 0.5 - centerZ) / (diameter / 2.0);
                    if (dx * dx + dy * dy + dz * dz >= 1.0) continue;
                    if (static_cast<BlockId>(blockId(world.getBlock(px, py, pz))) == BlockId::Stone)
                        world.setGeneratedBlock(px, py, pz, state);
                }
            }
        }
    }
}

void WorldPopulator::generateTree(World& world, JavaRandom& random, int x, int y, int z,
                                  int biomeId, int forcedSpecies) const {
    const int species = forcedSpecies >= 0 ? forcedSpecies : treeSpecies(biomeId, random);
    if (species == 5) {
        static_cast<void>(generateDarkOak(world, random, x, y, z));
        return;
    }
    int height = species == 1 ? random.nextInt(4) + 6 : random.nextInt(3) + (species == 2 ? 5 : 4);
    if (species == 3) height = random.nextInt(4) + 7;
    if (species == 4) height = random.nextInt(3) + random.nextInt(3) + 5;
    if (species == 5) height = random.nextInt(3) + 6;
    if (y < 1 || y + height + 1 >= chunkHeight || !supportsBush(world.getBlock(x, y - 1, z))) return;

    for (int py = y; py <= y + height + 1; ++py) {
        const int radius = py == y ? 0 : (py >= y + height - 1 ? 2 : 1);
        for (int px = x - radius; px <= x + radius; ++px)
            for (int pz = z - radius; pz <= z + radius; ++pz)
                if (!replaceableByTree(world.getBlock(px, py, pz))) return;
    }
    world.setGeneratedBlock(x, y - 1, z, block(BlockId::Dirt));
    const BlockState trunk = trunkFor(species);
    const BlockState leaves = leavesFor(species);

    if (species == 1) {
        int radius = random.nextInt(2);
        int threshold = 1;
        int reset = 0;
        for (int layer = 0; layer <= height - 1; ++layer) {
            const int py = y + height - layer;
            for (int px = x - radius; px <= x + radius; ++px)
                for (int pz = z - radius; pz <= z + radius; ++pz)
                    if (std::abs(px - x) != radius || std::abs(pz - z) != radius || radius <= 0)
                        if (!BlockRegistry::get(world.getBlock(px, py, pz)).opaque)
                            world.setGeneratedBlock(px, py, pz, leaves);
            if (radius >= threshold) { radius = reset; reset = 1; ++threshold; if (threshold > 3) threshold = 3; }
            else ++radius;
        }
    } else if (species == 4) {
        for (int py = y + height - 2; py <= y + height; ++py) {
            const int radius = py == y + height ? 1 : 3;
            for (int px = x - radius; px <= x + radius; ++px)
                for (int pz = z - radius; pz <= z + radius; ++pz)
                    if (std::abs(px - x) != radius || std::abs(pz - z) != radius)
                        if (replaceableByTree(world.getBlock(px, py, pz)))
                            world.setGeneratedBlock(px, py, pz, leaves);
        }
    } else {
        for (int py = y + height - 3; py <= y + height; ++py) {
            const int relative = py - (y + height);
            const int radius = 1 - relative / 2;
            for (int px = x - radius; px <= x + radius; ++px)
                for (int pz = z - radius; pz <= z + radius; ++pz)
                    if (std::abs(px - x) != radius || std::abs(pz - z) != radius ||
                        random.nextInt(2) != 0 || relative == 0)
                        if (replaceableByTree(world.getBlock(px, py, pz)))
                            world.setGeneratedBlock(px, py, pz, leaves);
        }
    }
    for (int offset = 0; offset < height; ++offset)
        if (replaceableByTree(world.getBlock(x, y + offset, z)))
            world.setGeneratedBlock(x, y + offset, z, trunk);
}

void WorldPopulator::decorate(World& world, JavaRandom& random, int chunkX, int chunkZ,
                              int biomeId) const {
    const int originX = chunkX * chunkSize;
    const int originZ = chunkZ * chunkSize;

    const bool roofedForest = biomeId == 29 || biomeId == 157;
    if (roofedForest) {
        for (int gridX = 0; gridX < 4; ++gridX) {
            for (int gridZ = 0; gridZ < 4; ++gridZ) {
                const int x = originX + gridX * 4 + 9 + random.nextInt(3);
                const int z = originZ + gridZ * 4 + 9 + random.nextInt(3);
                const int y = surfaceY(world, x, z);
                if (random.nextInt(20) == 0) {
                    static_cast<void>(generateHugeMushroom(world, random, x, y, z));
                    continue;
                }
                int species;
                if (random.nextInt(3) > 0) species = 5;
                else if (random.nextInt(5) != 0) species = random.nextInt(10) == 0 ? 0 : 0;
                else species = 2;
                generateTree(world, random, x, y, z, biomeId, species);
            }
        }
    }

    const bool forest = biomeId == 4 || biomeId == 18 || biomeId == 27 || biomeId == 28 ||
        biomeId == 29 || biomeId == 132 || biomeId == 155 || biomeId == 156 || biomeId == 157;
    if (forest) {
        int patches = random.nextInt(5) - 3;
        if (biomeId == 132) patches += 2;
        for (int patch = 0; patch < patches; ++patch) {
            constexpr std::array<std::uint8_t, 3> types{1, 4, 5};
            const std::uint8_t type = types[static_cast<std::size_t>(random.nextInt(3))];
            for (int attempt = 0; attempt < 5; ++attempt) {
                const int x = originX + random.nextInt(16) + 8;
                const int z = originZ + random.nextInt(16) + 8;
                const int maximum = surfaceY(world, x, z) + 32;
                if (maximum > 0 && generateDoublePlant(world, random, x,
                    random.nextInt(maximum), z, type)) break;
            }
        }
    }

    const auto ore = [&](int count, int size, int minimum, int maximum, BlockState state) {
        if (maximum < minimum) std::swap(minimum, maximum);
        else if (maximum == minimum) {
            if (minimum < 255) ++maximum;
            else --minimum;
        }
        for (int attempt = 0; attempt < count; ++attempt)
            generateOre(world, random, originX + random.nextInt(16),
                        minimum + random.nextInt(maximum - minimum),
                        originZ + random.nextInt(16), state, size);
    };
    ore(settings_.dirtCount, settings_.dirtSize, settings_.dirtMinHeight,
        settings_.dirtMaxHeight, block(BlockId::Dirt));
    ore(settings_.gravelCount, settings_.gravelSize, settings_.gravelMinHeight,
        settings_.gravelMaxHeight, block(BlockId::Gravel));
    ore(settings_.dioriteCount, settings_.dioriteSize, settings_.dioriteMinHeight,
        settings_.dioriteMaxHeight, block(BlockId::Stone, 3));
    ore(settings_.graniteCount, settings_.graniteSize, settings_.graniteMinHeight,
        settings_.graniteMaxHeight, block(BlockId::Stone, 1));
    ore(settings_.andesiteCount, settings_.andesiteSize, settings_.andesiteMinHeight,
        settings_.andesiteMaxHeight, block(BlockId::Stone, 5));
    ore(settings_.coalCount, settings_.coalSize, settings_.coalMinHeight,
        settings_.coalMaxHeight, block(BlockId::CoalOre));
    ore(settings_.ironCount, settings_.ironSize, settings_.ironMinHeight,
        settings_.ironMaxHeight, block(BlockId::IronOre));
    ore(settings_.goldCount, settings_.goldSize, settings_.goldMinHeight,
        settings_.goldMaxHeight, block(BlockId::GoldOre));
    ore(settings_.redstoneCount, settings_.redstoneSize, settings_.redstoneMinHeight,
        settings_.redstoneMaxHeight, block(BlockId::RedstoneOre));
    ore(settings_.diamondCount, settings_.diamondSize, settings_.diamondMinHeight,
        settings_.diamondMaxHeight, block(BlockId::DiamondOre));
    for (int attempt = 0; attempt < settings_.lapisCount; ++attempt) {
        generateOre(world, random, originX + random.nextInt(16),
                    random.nextInt(settings_.lapisSpread) + random.nextInt(settings_.lapisSpread) +
                        settings_.lapisCenterHeight - settings_.lapisSpread,
                    originZ + random.nextInt(16), block(BlockId::LapisOre), settings_.lapisSize);
    }
    if (biomeId == 37 || biomeId == 38 || biomeId == 39 || biomeId == 165 ||
        biomeId == 166 || biomeId == 167)
        ore(20, settings_.goldSize, 32, 80, block(BlockId::GoldOre));

    if (biomeId == 3 || biomeId == 20 || biomeId == 34 || biomeId == 131 || biomeId == 162) {
        for (int count = 3 + random.nextInt(6); count > 0; --count) {
            const int x = originX + random.nextInt(16);
            const int y = 4 + random.nextInt(28);
            const int z = originZ + random.nextInt(16);
            if (static_cast<BlockId>(blockId(world.getBlock(x, y, z))) == BlockId::Stone)
                world.setGeneratedBlock(x, y, z, block(BlockId::EmeraldOre));
        }
    }

    const DecoratorProfile profile = profileFor(biomeId);
    int trees = profile.trees;
    if (random.nextFloat() < profile.extraTreeChance) ++trees;
    for (int attempt = 0; attempt < trees; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        generateTree(world, random, x, surfaceY(world, x, z), z, biomeId);
    }
    for (int attempt = 0; attempt < profile.bigMushrooms; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        static_cast<void>(generateHugeMushroom(world, random, x, surfaceY(world, x, z), z));
    }
    for (int attempt = 0; attempt < profile.flowers; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        const int maximum = surfaceY(world, x, z) + 32;
        if (maximum <= 0) continue;
        const bool dandelion = random.nextInt(3) > 0;
        const BlockState flower = dandelion ? block(BlockId::YellowFlower) :
            block(BlockId::RedFlower, static_cast<std::uint8_t>(random.nextInt(9)));
        plantPatch(world, random, x, random.nextInt(maximum), z, flower, 64, 8, 4, false);
    }
    for (int attempt = 0; attempt < profile.grass; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        const int maximum = surfaceY(world, x, z) * 2;
        if (maximum > 0)
            plantPatch(world, random, x, random.nextInt(maximum), z,
                       block(BlockId::TallGrass, biomeId == 5 || biomeId == 19 ? 2 : 1),
                       128, 8, 4, true);
    }
    for (int attempt = 0; attempt < profile.deadBush; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        const int maximum = surfaceY(world, x, z) * 2;
        if (maximum > 0) plantPatch(world, random, x, random.nextInt(maximum), z,
                                    block(BlockId::DeadBush), 4, 8, 4, true);
    }
    for (int attempt = 0; attempt < profile.mushrooms; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        plantPatch(world, random, x, surfaceY(world, x, z), z,
                   random.nextInt(4) == 0 ? block(BlockId::BrownMushroom) : block(BlockId::RedMushroom),
                   64, 8, 4, false);
    }
    for (int attempt = 0; attempt < profile.waterlily; ++attempt) {
        const int baseX = originX + random.nextInt(16) + 8;
        const int baseZ = originZ + random.nextInt(16) + 8;
        int baseY = surfaceY(world, baseX, baseZ);
        while (baseY > 0 && isAir(world, baseX, baseY - 1, baseZ)) --baseY;
        for (int spread = 0; spread < 10; ++spread) {
            const int x = baseX + random.nextInt(8) - random.nextInt(8);
            const int y = baseY + random.nextInt(4) - random.nextInt(4);
            const int z = baseZ + random.nextInt(8) - random.nextInt(8);
            if (y > 0 && y < chunkHeight && isAir(world, x, y, z) &&
                isWater(world.getBlock(x, y - 1, z)))
                world.setGeneratedBlock(x, y, z, block(BlockId::Waterlily));
        }
    }
    const auto reeds = [&](int attempts) {
        for (int attempt = 0; attempt < attempts; ++attempt) {
            const int baseX = originX + random.nextInt(16) + 8;
            const int baseZ = originZ + random.nextInt(16) + 8;
            const int maximum = surfaceY(world, baseX, baseZ) * 2;
            if (maximum <= 0) continue;
            const int baseY = random.nextInt(maximum);
            for (int spread = 0; spread < 20; ++spread) {
                const int x = baseX + random.nextInt(4) - random.nextInt(4);
                const int z = baseZ + random.nextInt(4) - random.nextInt(4);
                const int y = baseY;
                if (!isAir(world, x, y, z)) continue;
                const bool besideWater = isWater(world.getBlock(x - 1, y - 1, z)) ||
                    isWater(world.getBlock(x + 1, y - 1, z)) ||
                    isWater(world.getBlock(x, y - 1, z - 1)) || isWater(world.getBlock(x, y - 1, z + 1));
                const auto soil = static_cast<BlockId>(blockId(world.getBlock(x, y - 1, z)));
                if (!besideWater || (soil != BlockId::Grass && soil != BlockId::Dirt &&
                                     soil != BlockId::Sand && soil != BlockId::Reeds)) continue;
                const int height = 2 + random.nextInt(random.nextInt(3) + 1);
                for (int dy = 0; dy < height && isAir(world, x, y + dy, z); ++dy)
                    world.setGeneratedBlock(x, y + dy, z, block(BlockId::Reeds));
            }
        }
    };
    reeds(profile.reeds + 10);

    if (random.nextInt(32) == 0) {
        const int baseX = originX + random.nextInt(16) + 8;
        const int baseZ = originZ + random.nextInt(16) + 8;
        const int maximum = surfaceY(world, baseX, baseZ) * 2;
        if (maximum > 0)
            plantPatch(world, random, baseX, random.nextInt(maximum), baseZ,
                       block(BlockId::Pumpkin, static_cast<std::uint8_t>(random.nextInt(4))),
                       64, 8, 4, false);
    }

    for (int attempt = 0; attempt < profile.cactus; ++attempt) {
        const int baseX = originX + random.nextInt(16) + 8;
        const int baseZ = originZ + random.nextInt(16) + 8;
        const int maximum = surfaceY(world, baseX, baseZ) * 2;
        if (maximum <= 0) continue;
        const int baseY = random.nextInt(maximum);
        for (int spread = 0; spread < 10; ++spread) {
            const int x = baseX + random.nextInt(8) - random.nextInt(8);
            const int y = baseY + random.nextInt(4) - random.nextInt(4);
            const int z = baseZ + random.nextInt(8) - random.nextInt(8);
            if (!isAir(world, x, y, z) ||
                static_cast<BlockId>(blockId(world.getBlock(x, y - 1, z))) != BlockId::Sand ||
                !isAir(world, x - 1, y, z) || !isAir(world, x + 1, y, z) ||
                !isAir(world, x, y, z - 1) || !isAir(world, x, y, z + 1))
                continue;
            const int height = 1 + random.nextInt(random.nextInt(3) + 1);
            for (int dy = 0; dy < height && isAir(world, x, y + dy, z); ++dy)
                world.setGeneratedBlock(x, y + dy, z, block(BlockId::Cactus));
        }
    }
    // BiomeDecorator#genDecorations ends with these exact spring attempts.
    // The nested height draws are significant because they also advance the
    // population RNG when no spring can be placed.
    for (int attempt = 0; attempt < 50; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        const int upper = random.nextInt(248) + 8;
        const int y = random.nextInt(upper);
        generateSpring(world, x, y, z, block(BlockId::FlowingWater));
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int x = originX + random.nextInt(16) + 8;
        const int z = originZ + random.nextInt(16) + 8;
        const int y = random.nextInt(random.nextInt(random.nextInt(240) + 8) + 8);
        generateSpring(world, x, y, z, block(BlockId::FlowingLava));
    }

    if ((biomeId == 6 || biomeId == 134) && random.nextInt(64) == 0)
        generateFossil(world, chunkX, chunkZ);

    // Vanilla's final freeze/snow pass covers the population area's +8 square.
    for (int dz = 0; dz < 16; ++dz) {
        for (int dx = 0; dx < 16; ++dx) {
            const int x = originX + 8 + dx;
            const int z = originZ + 8 + dz;
            const int y = surfaceY(world, x, z);
            const Chunk* chunk = world.findChunk(World::floorDiv16(x), World::floorDiv16(z));
            const int biome = chunk ? chunk->biome(World::floorMod16(x), World::floorMod16(z)) : biomeId;
            const float temperature = BiomeProvider::definition(biome).temperature -
                std::max(0, y - 64) * 0.05F / 30.0F;
            if (temperature >= 0.15F) continue;
            if (y > 0 && isWater(world.getBlock(x, y - 1, z)))
                world.setGeneratedBlock(x, y - 1, z, block(BlockId::Ice));
            else if (isAir(world, x, y, z) && supportsBush(world.getBlock(x, y - 1, z)))
                world.setGeneratedBlock(x, y, z, block(BlockId::SnowLayer));
        }
    }
}

void WorldPopulator::populate(World& world, int chunkX, int chunkZ) const {
    if (config_.worldType == WorldType::DebugAllBlockStates) return;
    JavaRandom random(wrappedPopulationSeed(config_.seed, chunkX, chunkZ));
    const int biomeX = chunkX * chunkSize + 16;
    const int biomeZ = chunkZ * chunkSize + 16;
    const Chunk* biomeChunk = world.findChunk(World::floorDiv16(biomeX), World::floorDiv16(biomeZ));
    const int biome = biomeChunk ? biomeChunk->biome(World::floorMod16(biomeX), World::floorMod16(biomeZ)) : 1;

    const int originX = chunkX * chunkSize;
    const int originZ = chunkZ * chunkSize;
    if (config_.worldType == WorldType::Flat) {
        // ChunkGeneratorFlat uses feature presence, not the customized-world
        // booleans/chances, and suppresses both lake types when a village was
        // generated. Structure generation reports that condition separately;
        // until its piece graph is shared here, preserve the exact non-village
        // feature order and random draws.
        if (flat_.waterLake && random.nextInt(4) == 0)
            generateLake(world, random, originX + random.nextInt(16) + 8, random.nextInt(256),
                         originZ + random.nextInt(16) + 8, block(BlockId::Water));
        if (flat_.lavaLake && random.nextInt(8) == 0) {
            const int x = originX + random.nextInt(16) + 8;
            const int y = random.nextInt(random.nextInt(248) + 8);
            const int z = originZ + random.nextInt(16) + 8;
            if (y < flat_.seaLevel || random.nextInt(10) == 0)
                generateLake(world, random, x, y, z, block(BlockId::Lava));
        }
        if (flat_.dungeons) {
            for (int attempt = 0; attempt < 8; ++attempt)
                generateDungeon(world, random, originX + random.nextInt(16) + 8,
                                random.nextInt(256), originZ + random.nextInt(16) + 8);
        }
        if (flat_.decoration) decorate(world, random, chunkX, chunkZ, biome);
        return;
    }
    const bool desert = biome == 2 || biome == 17;
    if (settings_.useWaterLakes && !desert && random.nextInt(settings_.waterLakeChance) == 0) {
        generateLake(world, random, originX + random.nextInt(16) + 8, random.nextInt(256),
                     originZ + random.nextInt(16) + 8, block(BlockId::Water));
    }
    if (settings_.useLavaLakes && random.nextInt(settings_.lavaLakeChance / 10) == 0) {
        const int x = originX + random.nextInt(16) + 8;
        const int y = random.nextInt(random.nextInt(248) + 8);
        const int z = originZ + random.nextInt(16) + 8;
        if (y < settings_.seaLevel || random.nextInt(settings_.lavaLakeChance / 8) == 0)
            generateLake(world, random, x, y, z, block(BlockId::Lava));
    }
    if (settings_.useDungeons) {
        for (int attempt = 0; attempt < settings_.dungeonChance; ++attempt)
            generateDungeon(world, random, originX + random.nextInt(16) + 8,
                            random.nextInt(256), originZ + random.nextInt(16) + 8);
    }
    decorate(world, random, chunkX, chunkZ, biome);
}
