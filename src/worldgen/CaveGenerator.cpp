#include "worldgen/CaveGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {

constexpr int range = 8;

BlockState block(BlockId id) {
    return makeBlockState(static_cast<std::uint16_t>(id));
}

std::int64_t multiplyWrapped(std::int64_t left, std::int64_t right) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right));
}

const std::array<float, 65536>& sineTable() {
    static const std::array<float, 65536> values = [] {
        std::array<float, 65536> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
        return result;
    }();
    return values;
}

float mcSin(float value) {
    return sineTable()[static_cast<std::uint32_t>(static_cast<int>(value * 10430.378F)) & 65535U];
}

float mcCos(float value) {
    return sineTable()[(static_cast<std::uint32_t>(static_cast<int>(value * 10430.378F)) + 16384U) & 65535U];
}

bool replaceable(BlockState state, BlockState above) {
    switch (static_cast<BlockId>(blockId(state))) {
        case BlockId::Stone:
        case BlockId::Dirt:
        case BlockId::Grass:
        case BlockId::HardenedClay:
        case BlockId::StainedHardenedClay:
        case BlockId::Sandstone:
        case BlockId::RedSandstone:
        case BlockId::Mycelium:
        case BlockId::SnowLayer:
            return true;
        case BlockId::Sand:
        case BlockId::Gravel:
            return static_cast<BlockId>(blockId(above)) != BlockId::Water &&
                static_cast<BlockId>(blockId(above)) != BlockId::FlowingWater;
        default:
            return false;
    }
}

BlockState biomeTop(const World& world, int worldX, int worldZ) {
    const Chunk* chunk = world.findChunk(World::floorDiv16(worldX), World::floorDiv16(worldZ));
    if (chunk != nullptr) {
        const int biome = chunk->biome(World::floorMod16(worldX), World::floorMod16(worldZ));
        if (biome == 14 || biome == 15) return block(BlockId::Mycelium);
        if (biome == 37 || biome == 38 || biome == 39 || biome >= 165) return block(BlockId::HardenedClay);
    }
    return block(BlockId::Grass);
}

void addTunnel(World& world, std::int64_t seed, int targetX, int targetZ,
               double x, double y, double z, float size, float yaw, float pitch,
               int step, int maximumStep, double verticalScale) {
    const double centerX = targetX * 16 + 8;
    const double centerZ = targetZ * 16 + 8;
    float yawVelocity = 0.0F;
    float pitchVelocity = 0.0F;
    JavaRandom random(seed);
    if (maximumStep <= 0) {
        const int length = range * 16 - 16;
        maximumStep = length - random.nextInt(length / 4);
    }
    bool room = false;
    if (step == -1) {
        step = maximumStep / 2;
        room = true;
    }
    const int branchStep = random.nextInt(maximumStep / 2) + maximumStep / 4;
    const bool gentlePitch = random.nextInt(6) == 0;

    for (; step < maximumStep; ++step) {
        const double horizontalRadius = 1.5 + mcSin(static_cast<float>(step) * static_cast<float>(std::numbers::pi) /
            static_cast<float>(maximumStep)) * size;
        const double verticalRadius = horizontalRadius * verticalScale;
        const float cosPitch = mcCos(pitch);
        x += mcCos(yaw) * cosPitch;
        y += mcSin(pitch);
        z += mcSin(yaw) * cosPitch;
        pitch *= gentlePitch ? 0.92F : 0.7F;
        pitch += pitchVelocity * 0.1F;
        yaw += yawVelocity * 0.1F;
        pitchVelocity *= 0.9F;
        yawVelocity *= 0.75F;
        pitchVelocity += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
        yawVelocity += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;

        if (!room && step == branchStep && size > 1.0F) {
            addTunnel(world, random.nextLong(), targetX, targetZ, x, y, z,
                random.nextFloat() * 0.5F + 0.5F, yaw - static_cast<float>(std::numbers::pi / 2.0),
                pitch / 3.0F, step, maximumStep, 1.0);
            addTunnel(world, random.nextLong(), targetX, targetZ, x, y, z,
                random.nextFloat() * 0.5F + 0.5F, yaw + static_cast<float>(std::numbers::pi / 2.0),
                pitch / 3.0F, step, maximumStep, 1.0);
            return;
        }
        if (!room && random.nextInt(4) == 0) continue;
        const double dx = x - centerX;
        const double dz = z - centerZ;
        const double remaining = maximumStep - step;
        const double maximumDistance = size + 18.0F;
        if (dx * dx + dz * dz - remaining * remaining > maximumDistance * maximumDistance) return;
        if (x < centerX - 16.0 - horizontalRadius * 2.0 || x > centerX + 16.0 + horizontalRadius * 2.0 ||
            z < centerZ - 16.0 - horizontalRadius * 2.0 || z > centerZ + 16.0 + horizontalRadius * 2.0) continue;

        int minX = std::max(0, static_cast<int>(std::floor(x - horizontalRadius)) - targetX * 16 - 1);
        int maxX = std::min(16, static_cast<int>(std::floor(x + horizontalRadius)) - targetX * 16 + 1);
        int minY = std::max(1, static_cast<int>(std::floor(y - verticalRadius)) - 1);
        int maxY = std::min(248, static_cast<int>(std::floor(y + verticalRadius)) + 1);
        int minZ = std::max(0, static_cast<int>(std::floor(z - horizontalRadius)) - targetZ * 16 - 1);
        int maxZ = std::min(16, static_cast<int>(std::floor(z + horizontalRadius)) - targetZ * 16 + 1);

        bool touchesWater = false;
        for (int localX = minX; !touchesWater && localX < maxX; ++localX) {
            for (int localZ = minZ; !touchesWater && localZ < maxZ; ++localZ) {
                for (int scanY = maxY + 1; scanY >= minY - 1; --scanY) {
                    const BlockId scan = static_cast<BlockId>(blockId(
                        world.getBlock(targetX * 16 + localX, scanY, targetZ * 16 + localZ)));
                    if (scanY >= 0 && scanY < 256 &&
                        (scan == BlockId::Water || scan == BlockId::FlowingWater)) {
                        touchesWater = true;
                        break;
                    }
                    if (scanY != minY - 1 && localX != minX && localX != maxX - 1 &&
                        localZ != minZ && localZ != maxZ - 1) scanY = minY;
                }
            }
        }
        if (touchesWater) continue;

        for (int localX = minX; localX < maxX; ++localX) {
            const int worldX = targetX * 16 + localX;
            const double normalizedX = (worldX + 0.5 - x) / horizontalRadius;
            for (int localZ = minZ; localZ < maxZ; ++localZ) {
                const int worldZ = targetZ * 16 + localZ;
                const double normalizedZ = (worldZ + 0.5 - z) / horizontalRadius;
                bool foundSurface = false;
                if (normalizedX * normalizedX + normalizedZ * normalizedZ >= 1.0) continue;
                for (int carveY = maxY; carveY > minY; --carveY) {
                    const double normalizedY = ((carveY - 1) + 0.5 - y) / verticalRadius;
                    if (normalizedY <= -0.7 || normalizedX * normalizedX + normalizedY * normalizedY + normalizedZ * normalizedZ >= 1.0) continue;
                    const BlockState current = world.getBlock(worldX, carveY, worldZ);
                    const BlockState above = world.getBlock(worldX, carveY + 1, worldZ);
                    if (static_cast<BlockId>(blockId(current)) == BlockId::Grass ||
                        static_cast<BlockId>(blockId(current)) == BlockId::Mycelium) foundSurface = true;
                    if (!replaceable(current, above)) continue;
                    world.setGeneratedBlock(worldX, carveY, worldZ, carveY - 1 < 10 ? block(BlockId::Lava) : block(BlockId::Air));
                    if (foundSurface && static_cast<BlockId>(blockId(world.getBlock(worldX, carveY - 1, worldZ))) == BlockId::Dirt)
                        world.setGeneratedBlock(worldX, carveY - 1, worldZ, biomeTop(world, worldX, worldZ));
                }
            }
        }
        if (room) break;
    }
}

void generateFromSource(World& world, JavaRandom& random, int sourceX, int sourceZ, int targetX, int targetZ) {
    int count = random.nextInt(random.nextInt(random.nextInt(15) + 1) + 1);
    if (random.nextInt(7) != 0) count = 0;
    for (int cave = 0; cave < count; ++cave) {
        const double x = sourceX * 16 + random.nextInt(16);
        const double y = random.nextInt(random.nextInt(120) + 8);
        const double z = sourceZ * 16 + random.nextInt(16);
        int tunnels = 1;
        if (random.nextInt(4) == 0) {
            addTunnel(world, random.nextLong(), targetX, targetZ, x, y, z,
                1.0F + random.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5);
            tunnels += random.nextInt(4);
        }
        for (int tunnel = 0; tunnel < tunnels; ++tunnel) {
            const float yaw = random.nextFloat() * static_cast<float>(std::numbers::pi * 2.0);
            const float pitch = (random.nextFloat() - 0.5F) * 2.0F / 8.0F;
            float size = random.nextFloat() * 2.0F + random.nextFloat();
            if (random.nextInt(10) == 0) size *= random.nextFloat() * random.nextFloat() * 3.0F + 1.0F;
            addTunnel(world, random.nextLong(), targetX, targetZ, x, y, z, size, yaw, pitch, 0, 0, 1.0);
        }
    }
}

void addRavine(World& world, std::int64_t seed, int targetX, int targetZ,
               double x, double y, double z, float size, float yaw, float pitch) {
    JavaRandom random(seed);
    const double centerX = targetX * 16 + 8;
    const double centerZ = targetZ * 16 + 8;
    const int lengthBase = range * 16 - 16;
    const int maximumStep = lengthBase - random.nextInt(lengthBase / 4);
    int step = 0;
    float yawVelocity = 0.0F;
    float pitchVelocity = 0.0F;
    std::array<float, 256> verticalWeights{};
    float weight = 1.0F;
    for (int index = 0; index < 256; ++index) {
        if (index == 0 || random.nextInt(3) == 0) weight = 1.0F + random.nextFloat() * random.nextFloat();
        verticalWeights[static_cast<std::size_t>(index)] = weight * weight;
    }

    for (; step < maximumStep; ++step) {
        double horizontalRadius = 1.5 + mcSin(static_cast<float>(step) * static_cast<float>(std::numbers::pi) /
            static_cast<float>(maximumStep)) * size;
        double verticalRadius = horizontalRadius * 3.0;
        horizontalRadius *= random.nextFloat() * 0.25 + 0.75;
        verticalRadius *= random.nextFloat() * 0.25 + 0.75;
        const float cosPitch = mcCos(pitch);
        x += mcCos(yaw) * cosPitch;
        y += mcSin(pitch);
        z += mcSin(yaw) * cosPitch;
        pitch *= 0.7F;
        pitch += pitchVelocity * 0.05F;
        yaw += yawVelocity * 0.05F;
        pitchVelocity = pitchVelocity * 0.8F + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
        yawVelocity = yawVelocity * 0.5F + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;
        if (random.nextInt(4) == 0) continue;

        const double dx = x - centerX;
        const double dz = z - centerZ;
        const double remaining = maximumStep - step;
        const double maximumDistance = size + 18.0F;
        if (dx * dx + dz * dz - remaining * remaining > maximumDistance * maximumDistance) return;
        if (x < centerX - 16.0 - horizontalRadius * 2.0 || x > centerX + 16.0 + horizontalRadius * 2.0 ||
            z < centerZ - 16.0 - horizontalRadius * 2.0 || z > centerZ + 16.0 + horizontalRadius * 2.0) continue;

        const int minX = std::max(0, static_cast<int>(std::floor(x - horizontalRadius)) - targetX * 16 - 1);
        const int maxX = std::min(16, static_cast<int>(std::floor(x + horizontalRadius)) - targetX * 16 + 1);
        const int minY = std::max(1, static_cast<int>(std::floor(y - verticalRadius)) - 1);
        const int maxY = std::min(248, static_cast<int>(std::floor(y + verticalRadius)) + 1);
        const int minZ = std::max(0, static_cast<int>(std::floor(z - horizontalRadius)) - targetZ * 16 - 1);
        const int maxZ = std::min(16, static_cast<int>(std::floor(z + horizontalRadius)) - targetZ * 16 + 1);
        bool touchesWater = false;
        for (int localX = minX; !touchesWater && localX < maxX; ++localX) {
            for (int localZ = minZ; !touchesWater && localZ < maxZ; ++localZ) {
                for (int scanY = maxY + 1; scanY >= minY - 1; --scanY) {
                    const BlockId scan = static_cast<BlockId>(blockId(
                        world.getBlock(targetX * 16 + localX, scanY, targetZ * 16 + localZ)));
                    if (scanY >= 0 && scanY < 256 &&
                        (scan == BlockId::Water || scan == BlockId::FlowingWater)) {
                        touchesWater = true;
                        break;
                    }
                    if (scanY != minY - 1 && localX != minX && localX != maxX - 1 &&
                        localZ != minZ && localZ != maxZ - 1) scanY = minY;
                }
            }
        }
        if (touchesWater) continue;

        for (int localX = minX; localX < maxX; ++localX) {
            const int worldX = targetX * 16 + localX;
            const double normalizedX = (worldX + 0.5 - x) / horizontalRadius;
            for (int localZ = minZ; localZ < maxZ; ++localZ) {
                const int worldZ = targetZ * 16 + localZ;
                const double normalizedZ = (worldZ + 0.5 - z) / horizontalRadius;
                bool foundSurface = false;
                if (normalizedX * normalizedX + normalizedZ * normalizedZ >= 1.0) continue;
                for (int carveY = maxY; carveY > minY; --carveY) {
                    const double normalizedY = ((carveY - 1) + 0.5 - y) / verticalRadius;
                    if ((normalizedX * normalizedX + normalizedZ * normalizedZ) *
                        verticalWeights[static_cast<std::size_t>(carveY - 1)] + normalizedY * normalizedY / 6.0 >= 1.0) continue;
                    const BlockState current = world.getBlock(worldX, carveY, worldZ);
                    const BlockId id = static_cast<BlockId>(blockId(current));
                    if (id == BlockId::Grass) foundSurface = true;
                    if (id != BlockId::Stone && id != BlockId::Dirt && id != BlockId::Grass) continue;
                    world.setGeneratedBlock(worldX, carveY, worldZ, carveY - 1 < 10 ? block(BlockId::Lava) : block(BlockId::Air));
                    if (foundSurface && static_cast<BlockId>(blockId(world.getBlock(worldX, carveY - 1, worldZ))) == BlockId::Dirt)
                        world.setGeneratedBlock(worldX, carveY - 1, worldZ, biomeTop(world, worldX, worldZ));
                }
            }
        }
    }
}

void generateRavineFromSource(World& world, JavaRandom& random, int sourceX, int sourceZ, int targetX, int targetZ) {
    if (random.nextInt(50) != 0) return;
    const double x = sourceX * 16 + random.nextInt(16);
    const double y = random.nextInt(random.nextInt(40) + 8) + 20;
    const double z = sourceZ * 16 + random.nextInt(16);
    const float yaw = random.nextFloat() * static_cast<float>(std::numbers::pi * 2.0);
    const float pitch = (random.nextFloat() - 0.5F) * 2.0F / 8.0F;
    const float size = (random.nextFloat() * 2.0F + random.nextFloat()) * 2.0F;
    addRavine(world, random.nextLong(), targetX, targetZ, x, y, z, size, yaw, pitch);
}

} // namespace

void CaveGenerator::generate(World& world, int chunkX, int chunkZ, bool caves, bool ravines) const {
    JavaRandom seedRandom(worldSeed_);
    const std::int64_t xMultiplier = seedRandom.nextLong();
    const std::int64_t zMultiplier = seedRandom.nextLong();
    if (caves) for (int sourceX = chunkX - range; sourceX <= chunkX + range; ++sourceX) {
        for (int sourceZ = chunkZ - range; sourceZ <= chunkZ + range; ++sourceZ) {
            const std::int64_t seed = multiplyWrapped(sourceX, xMultiplier) ^
                multiplyWrapped(sourceZ, zMultiplier) ^ worldSeed_;
            JavaRandom random(seed);
            generateFromSource(world, random, sourceX, sourceZ, chunkX, chunkZ);
        }
    }
    if (ravines) for (int sourceX = chunkX - range; sourceX <= chunkX + range; ++sourceX) {
        for (int sourceZ = chunkZ - range; sourceZ <= chunkZ + range; ++sourceZ) {
            const std::int64_t seed = multiplyWrapped(sourceX, xMultiplier) ^
                multiplyWrapped(sourceZ, zMultiplier) ^ worldSeed_;
            JavaRandom random(seed);
            generateRavineFromSource(world, random, sourceX, sourceZ, chunkX, chunkZ);
        }
    }
}
