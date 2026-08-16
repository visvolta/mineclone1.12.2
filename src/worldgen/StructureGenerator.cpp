#include "worldgen/StructureGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {

constexpr double pi = 3.14159265358979323846;

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

std::int64_t structureSeed(std::int64_t seed, int regionX, int regionZ, int salt) {
    const std::uint64_t value = static_cast<std::uint64_t>(static_cast<std::int64_t>(regionX)) * 341873128712ULL +
        static_cast<std::uint64_t>(static_cast<std::int64_t>(regionZ)) * 132897987541ULL +
        static_cast<std::uint64_t>(seed) + static_cast<std::uint64_t>(salt);
    return static_cast<std::int64_t>(value);
}

int javaRegion(int coordinate, int spacing) {
    if (coordinate < 0) coordinate -= spacing - 1;
    return coordinate / spacing;
}

bool spacingCandidate(std::int64_t seed, int chunkX, int chunkZ, int spacing,
                      int separation, int salt, bool triangular) {
    const int regionX = javaRegion(chunkX, spacing);
    const int regionZ = javaRegion(chunkZ, spacing);
    JavaRandom random(structureSeed(seed, regionX, regionZ, salt));
    const int bound = spacing - separation;
    const int offsetX = triangular ? (random.nextInt(bound) + random.nextInt(bound)) / 2 : random.nextInt(bound);
    const int offsetZ = triangular ? (random.nextInt(bound) + random.nextInt(bound)) / 2 : random.nextInt(bound);
    return chunkX == regionX * spacing + offsetX && chunkZ == regionZ * spacing + offsetZ;
}

int topY(const World& world, int x, int z) {
    for (int y = chunkHeight - 1; y >= 0; --y) {
        const BlockId id = static_cast<BlockId>(blockId(world.getBlock(x, y, z)));
        if (id != BlockId::Air && id != BlockId::Water && id != BlockId::FlowingWater &&
            id != BlockId::Leaves && id != BlockId::Leaves2 && id != BlockId::TallGrass)
            return y + 1;
    }
    return 64;
}

void fill(World& world, int x0, int y0, int z0, int x1, int y1, int z1, BlockState state) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                world.setGeneratedBlock(x, y, z, state);
}

void hollow(World& world, int x0, int y0, int z0, int x1, int y1, int z1,
            BlockState wall, BlockState inside = block(BlockId::Air)) {
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const bool boundary = x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1;
                world.setGeneratedBlock(x, y, z, boundary ? wall : inside);
            }
        }
    }
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendString(std::vector<std::uint8_t>& output, std::string_view value) {
    appendU16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

void appendNamedInt(std::vector<std::uint8_t>& output, std::string_view name, int value) {
    output.push_back(3); // TAG_Int
    appendString(output, name);
    appendU32(output, static_cast<std::uint32_t>(value));
}

void appendNamedString(std::vector<std::uint8_t>& output, std::string_view name,
                       std::string_view value) {
    output.push_back(8); // TAG_String
    appendString(output, name);
    appendString(output, value);
}

std::vector<std::uint8_t> lootChestNbt(int x, int y, int z, std::string_view table,
                                       std::int64_t lootSeed) {
    std::vector<std::uint8_t> output;
    output.reserve(128 + table.size());
    output.push_back(10); // unnamed root TAG_Compound
    appendU16(output, 0);
    appendNamedString(output, "id", "minecraft:chest");
    appendNamedInt(output, "x", x);
    appendNamedInt(output, "y", y);
    appendNamedInt(output, "z", z);
    appendNamedString(output, "LootTable", table);
    output.push_back(4); // TAG_Long
    appendString(output, "LootTableSeed");
    appendU64(output, static_cast<std::uint64_t>(lootSeed));
    output.push_back(0); // TAG_End
    return output;
}

void addLootChest(World& world, int x, int y, int z, std::string_view table,
                  std::int64_t lootSeed) {
    world.setGeneratedBlock(x, y, z, block(BlockId::Chest));
    GeneratedBlockEntity entity{x, y, z, "minecraft:chest",
                                lootChestNbt(x, y, z, table, lootSeed)};
    world.addGeneratedBlockEntity(std::move(entity));
}

} // namespace

StructureGenerator::StructureGenerator(const WorldConfig& config)
    : config_(config), biomes_(std::make_unique<BiomeProvider>(config)) {
    JavaRandom random(config.seed);
    double angle = random.nextDouble() * pi * 2.0;
    int ring = 0;
    int inRing = 0;
    int spread = 3;
    strongholds_.reserve(128);
    for (int index = 0; index < 128; ++index) {
        const double distance = 128.0 + ring * 192.0 + (random.nextDouble() - 0.5) * 80.0;
        int chunkX = static_cast<int>(std::llround(std::cos(angle) * distance));
        int chunkZ = static_cast<int>(std::llround(std::sin(angle) * distance));

        // BiomeProvider#findBiomePosition uses generation-scale cells and
        // reservoir sampling over every allowed positive-height biome.
        const int minX = ((chunkX << 4) + 8 - 112) >> 2;
        const int minZ = ((chunkZ << 4) + 8 - 112) >> 2;
        const int maxX = ((chunkX << 4) + 8 + 112) >> 2;
        const int maxZ = ((chunkZ << 4) + 8 + 112) >> 2;
        const int width = maxX - minX + 1;
        const int height = maxZ - minZ + 1;
        const auto candidates = biomes_->getBiomesForGeneration(minX, minZ, width, height);
        int accepted = 0;
        for (std::size_t cell = 0; cell < candidates.size(); ++cell) {
            if (BiomeProvider::definition(candidates[cell]).baseHeight <= 0.0F) continue;
            if (accepted++ == 0 || random.nextInt(accepted) == 0) {
                const int blockX = (minX + static_cast<int>(cell % width)) << 2;
                const int blockZ = (minZ + static_cast<int>(cell / width)) << 2;
                chunkX = blockX >> 4;
                chunkZ = blockZ >> 4;
            }
        }
        strongholds_.emplace_back(chunkX, chunkZ);
        angle += pi * 2.0 / spread;
        if (++inRing == spread) {
            ++ring;
            inRing = 0;
            spread += 2 * spread / (ring + 1);
            spread = std::min(spread, 128 - index - 1);
            angle += random.nextDouble() * pi * 2.0;
        }
    }
}

StructureGenerator::~StructureGenerator() = default;

bool StructureGenerator::startType(int chunkX, int chunkZ, Type& type) const {
    const int biome = biomes_->getBiomes(chunkX * 16 + 8, chunkZ * 16 + 8, 1, 1).front();
    if (spacingCandidate(config_.seed, chunkX, chunkZ, 80, 20, 10387319, true) &&
        (biome == 29 || biome == 157)) { type = Type::Mansion; return true; }
    if (spacingCandidate(config_.seed, chunkX, chunkZ, 32, 5, 10387313, true) && biome == 24) {
        type = Type::Monument; return true;
    }
    if (spacingCandidate(config_.seed, chunkX, chunkZ, 32, 8, 10387312, false) &&
        (biome == 1 || biome == 2 || biome == 5 || biome == 35)) {
        type = Type::Village; return true;
    }
    if (spacingCandidate(config_.seed, chunkX, chunkZ, 32, 8, 14357617, false)) {
        if (biome == 2 || biome == 17) { type = Type::DesertPyramid; return true; }
        if (biome == 21 || biome == 22) { type = Type::JungleTemple; return true; }
        if (biome == 6) { type = Type::SwampHut; return true; }
        if (biome == 12 || biome == 30) { type = Type::Igloo; return true; }
    }
    if (std::find(strongholds_.begin(), strongholds_.end(), std::pair{chunkX, chunkZ}) != strongholds_.end()) {
        type = Type::Stronghold; return true;
    }

    JavaRandom seedRandom(config_.seed);
    const std::int64_t first = seedRandom.nextLong();
    const std::int64_t second = seedRandom.nextLong();
    const std::uint64_t mixed = static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkX)) *
        static_cast<std::uint64_t>(first) ^
        static_cast<std::uint64_t>(static_cast<std::int64_t>(chunkZ)) * static_cast<std::uint64_t>(second) ^
        static_cast<std::uint64_t>(config_.seed);
    JavaRandom mineRandom(static_cast<std::int64_t>(mixed));
    static_cast<void>(mineRandom.nextInt());
    if (mineRandom.nextDouble() < 0.004 && mineRandom.nextInt(80) < std::max(std::abs(chunkX), std::abs(chunkZ))) {
        type = Type::Mineshaft; return true;
    }
    return false;
}

bool StructureGenerator::isStructureStart(int chunkX, int chunkZ) const {
    Type type{};
    return config_.generateStructures && startType(chunkX, chunkZ, type);
}

void StructureGenerator::place(World& world, Type type, int chunkX, int chunkZ) const {
    const int centerX = chunkX * 16 + 8;
    const int centerZ = chunkZ * 16 + 8;
    JavaRandom random(structureSeed(config_.seed, chunkX, chunkZ, 0));
    if (type == Type::Mineshaft) {
        const int y = 18 + random.nextInt(30);
        fill(world, centerX - 24, y, centerZ - 1, centerX + 24, y + 2, centerZ + 1, block(BlockId::Air));
        fill(world, centerX - 1, y, centerZ - 24, centerX + 1, y + 2, centerZ + 24, block(BlockId::Air));
        for (int offset = -24; offset <= 24; offset += 5) {
            fill(world, centerX + offset, y, centerZ - 1, centerX + offset, y + 2, centerZ - 1, block(BlockId::Fence));
            fill(world, centerX + offset, y, centerZ + 1, centerX + offset, y + 2, centerZ + 1, block(BlockId::Fence));
            world.setGeneratedBlock(centerX + offset, y + 2, centerZ, block(BlockId::Planks));
        }
        addLootChest(world, centerX + 4, y, centerZ,
                     "minecraft:chests/abandoned_mineshaft", random.nextLong());
        return;
    }
    if (type == Type::Stronghold) {
        const int y = 28;
        hollow(world, centerX - 18, y, centerZ - 18, centerX + 18, y + 6, centerZ + 18,
               block(BlockId::StoneBrick, static_cast<std::uint8_t>(random.nextInt(3))));
        fill(world, centerX - 2, y + 1, centerZ - 18, centerX + 2, y + 4, centerZ + 18, block(BlockId::Air));
        fill(world, centerX - 18, y + 1, centerZ - 2, centerX + 18, y + 4, centerZ + 2, block(BlockId::Air));
        for (int x = centerX - 2; x <= centerX + 2; ++x)
            for (int z = centerZ + 8; z <= centerZ + 12; ++z)
                if (x == centerX - 2 || x == centerX + 2 || z == centerZ + 8 || z == centerZ + 12)
                    world.setGeneratedBlock(x, y + 1, z, block(BlockId::EndPortalFrame));
        addLootChest(world, centerX - 10, y + 1, centerZ - 10,
                     "minecraft:chests/stronghold_corridor", random.nextLong());
        return;
    }
    if (type == Type::Monument) {
        const int y = 39;
        hollow(world, centerX - 28, y, centerZ - 28, centerX + 28, y + 22, centerZ + 28,
               block(BlockId::Prismarine));
        for (int floor = 0; floor < 3; ++floor) {
            const int fy = y + 3 + floor * 6;
            fill(world, centerX - 24, fy, centerZ - 24, centerX + 24, fy, centerZ + 24,
                 block(BlockId::Prismarine, 1));
            fill(world, centerX - 2, fy + 1, centerZ - 25, centerX + 2, fy + 4, centerZ + 25,
                 block(BlockId::Water));
            fill(world, centerX - 25, fy + 1, centerZ - 2, centerX + 25, fy + 4, centerZ + 2,
                 block(BlockId::Water));
        }
        for (int dx : {-20, -10, 0, 10, 20})
            for (int dz : {-20, -10, 0, 10, 20})
                world.setGeneratedBlock(centerX + dx, y + 5, centerZ + dz, block(BlockId::SeaLantern));
        fill(world, centerX - 3, y + 7, centerZ - 3, centerX + 3, y + 10, centerZ + 3, block(BlockId::Sponge));
        return;
    }

    const int ground = topY(world, centerX, centerZ);
    if (type == Type::Village) {
        fill(world, centerX - 24, ground - 1, centerZ - 1, centerX + 24, ground - 1, centerZ + 1, block(BlockId::Gravel));
        fill(world, centerX - 1, ground - 1, centerZ - 24, centerX + 1, ground - 1, centerZ + 24, block(BlockId::Gravel));
        hollow(world, centerX - 3, ground, centerZ - 3, centerX + 3, ground + 4, centerZ + 3, block(BlockId::Cobblestone));
        fill(world, centerX - 1, ground + 1, centerZ - 3, centerX + 1, ground + 3, centerZ - 3, block(BlockId::Air));
        constexpr std::array<std::array<int, 2>, 4> houses{{{{-16,-12}}, {{12,-16}}, {{12,10}}, {{-16,12}}}};
        for (const auto& offset : houses) {
            const int hx = centerX + offset[0];
            const int hz = centerZ + offset[1];
            const int hy = topY(world, hx, hz);
            hollow(world, hx - 4, hy, hz - 3, hx + 4, hy + 4, hz + 3, block(BlockId::Planks));
            fill(world, hx - 4, hy, hz - 3, hx + 4, hy, hz + 3, block(BlockId::Cobblestone));
            fill(world, hx - 1, hy + 1, hz - 3, hx + 1, hy + 2, hz - 3, block(BlockId::Air));
            world.setGeneratedBlock(hx + 2, hy + 1, hz + 1, block(BlockId::CraftingTable));
        }
        return;
    }
    if (type == Type::DesertPyramid) {
        for (int level = 0; level < 10; ++level)
            fill(world, centerX - 10 + level, ground + level, centerZ - 10 + level,
                 centerX + 10 - level, ground + level, centerZ + 10 - level, block(BlockId::Sandstone));
        hollow(world, centerX - 6, ground + 1, centerZ - 6, centerX + 6, ground + 6, centerZ + 6,
               block(BlockId::Sandstone));
        fill(world, centerX - 1, ground - 12, centerZ - 1, centerX + 1, ground, centerZ + 1, block(BlockId::Air));
        fill(world, centerX - 2, ground - 13, centerZ - 2, centerX + 2, ground - 13, centerZ + 2, block(BlockId::Sandstone));
        world.setGeneratedBlock(centerX, ground - 12, centerZ, block(BlockId::StonePressurePlate));
        fill(world, centerX - 1, ground - 14, centerZ - 1, centerX + 1, ground - 14, centerZ + 1, block(BlockId::TNT));
        addLootChest(world, centerX + 2, ground - 12, centerZ,
                     "minecraft:chests/desert_pyramid", random.nextLong());
        addLootChest(world, centerX - 2, ground - 12, centerZ,
                     "minecraft:chests/desert_pyramid", random.nextLong());
        return;
    }
    if (type == Type::JungleTemple) {
        hollow(world, centerX - 6, ground, centerZ - 7, centerX + 5, ground + 9, centerZ + 7,
               block(random.nextInt(3) == 0 ? BlockId::Cobblestone : BlockId::MossyCobblestone));
        fill(world, centerX - 4, ground + 1, centerZ - 5, centerX + 3, ground + 7, centerZ + 5, block(BlockId::Air));
        addLootChest(world, centerX + 3, ground + 1, centerZ + 4,
                     "minecraft:chests/jungle_temple", random.nextLong());
        addLootChest(world, centerX - 3, ground + 1, centerZ - 4,
                     "minecraft:chests/jungle_temple", random.nextLong());
        return;
    }
    if (type == Type::SwampHut) {
        fill(world, centerX - 3, ground, centerZ - 4, centerX + 3, ground, centerZ + 4, block(BlockId::Planks, 1));
        for (int dx : {-3, 3}) for (int dz : {-4, 4})
            fill(world, centerX + dx, ground - 2, centerZ + dz, centerX + dx, ground + 3, centerZ + dz, block(BlockId::Log, 1));
        hollow(world, centerX - 3, ground + 1, centerZ - 4, centerX + 3, ground + 4, centerZ + 4, block(BlockId::Planks, 1));
        world.setGeneratedBlock(centerX + 1, ground + 1, centerZ, block(BlockId::CraftingTable));
        world.setGeneratedBlock(centerX - 1, ground + 1, centerZ, block(BlockId::FlowerPot));
        return;
    }
    if (type == Type::Igloo) {
        hollow(world, centerX - 3, ground, centerZ - 4, centerX + 3, ground + 4, centerZ + 3, block(BlockId::Snow));
        fill(world, centerX - 2, ground + 1, centerZ - 3, centerX + 2, ground + 3, centerZ + 2, block(BlockId::Air));
        world.setGeneratedBlock(centerX + 2, ground + 1, centerZ + 1, block(BlockId::Furnace));
        world.setGeneratedBlock(centerX - 2, ground + 1, centerZ + 1, block(BlockId::CraftingTable));
        return;
    }
    if (type == Type::Mansion) {
        const int x0 = centerX - 30;
        const int z0 = centerZ - 22;
        hollow(world, x0, ground, z0, x0 + 60, ground + 18, z0 + 44, block(BlockId::Planks, 5));
        for (int floor = 0; floor < 3; ++floor) {
            const int fy = ground + floor * 6;
            fill(world, x0 + 1, fy, z0 + 1, x0 + 59, fy, z0 + 43, block(BlockId::Planks, 5));
            for (int wall = 10; wall < 60; wall += 10)
                fill(world, x0 + wall, fy + 1, z0 + 1, x0 + wall, fy + 5, z0 + 43, block(BlockId::Planks, 5));
        }
        fill(world, centerX - 2, ground + 1, z0, centerX + 2, ground + 4, z0, block(BlockId::Air));
        addLootChest(world, centerX + 20, ground + 7, centerZ + 12,
                     "minecraft:chests/woodland_mansion", random.nextLong());
    }
}

void StructureGenerator::generateStartsIntersecting(World& world, int minimumChunkX,
        int minimumChunkZ, int maximumChunkX, int maximumChunkZ) const {
    if (!config_.generateStructures) return;
    // Mansions and monuments can extend four chunks from their start. Scan a
    // conservative eight-chunk start halo so every returned target section is
    // complete regardless of which region job produced it.
    for (int chunkZ = minimumChunkZ - 8; chunkZ <= maximumChunkZ + 8; ++chunkZ) {
        for (int chunkX = minimumChunkX - 8; chunkX <= maximumChunkX + 8; ++chunkX) {
            Type type{};
            if (startType(chunkX, chunkZ, type)) place(world, type, chunkX, chunkZ);
        }
    }
}
