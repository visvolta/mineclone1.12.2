#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

extern "C" {
#include "blockcraft_bridge.h"
}

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"
#include "worldgen/CaveGenerator.hpp"
#include "worldgen/ChunkGeneratorSettings.hpp"
#include "worldgen/FlatGeneratorSettings.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {

BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

std::uint64_t wrappedMultiply(std::int64_t left, std::int64_t right) {
    return static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right);
}

std::int64_t chunkTerrainSeed(int x, int z) {
    return static_cast<std::int64_t>(wrappedMultiply(x, 341873128712LL) + wrappedMultiply(z, 132897987541LL));
}

void surfaceBlocks(int biomeId, double noise, BlockState& top, BlockState& filler) {
    top = block(BlockId::Grass);
    filler = block(BlockId::Dirt);
    switch (biomeId) {
        case 2: case 16: case 17: case 26: case 130:
            top = filler = block(BlockId::Sand); break;
        case 14: case 15:
            top = block(BlockId::Mycelium); filler = block(BlockId::Dirt); break;
        case 25:
            top = filler = block(BlockId::Stone); break;
        case 37: case 38: case 39: case 165: case 166: case 167:
            top = filler = block(BlockId::HardenedClay); break;
        default: break;
    }
    const bool mutatedHills = biomeId == 131 || biomeId == 162;
    if (mutatedHills && (noise < -1.0 || noise > 2.0)) {
        top = filler = block(BlockId::Gravel);
    } else if ((biomeId == 3 || mutatedHills) && noise > 1.0) {
        top = filler = block(BlockId::Stone);
    }
    if (biomeId == 32 || biomeId == 33 || biomeId == 160 || biomeId == 161) {
        if (noise > 1.75) top = block(BlockId::Dirt, 1);       // coarse dirt
        else if (noise > -0.95) top = block(BlockId::Dirt, 2); // podzol
    }
    if (biomeId == 163 || biomeId == 164) {
        if (noise > 1.75) top = filler = block(BlockId::Stone);
        else if (noise > -0.5) top = block(BlockId::Dirt, 1);
    }
}

void generateMesaColumn(World& world, JavaRandom& random, CbMesaNoiseHandle mesaNoise,
                        const std::array<BlockState, 64>& bands, int worldSeedSeaLevel,
                        int biomeId, int x, int z, double noise) {
    const bool bryce = biomeId == 165;
    const bool forest = biomeId == 38 || biomeId == 166;
    double pillarHeight = 0.0;
    if (bryce) {
        const int mixedX = (x & -16) + (z & 15);
        const int mixedZ = (z & -16) + (x & 15);
        const double pillar = cbSampleMesaPillarNoise(mesaNoise, mixedX * 0.25, mixedZ * 0.25);
        const double base = std::min(std::abs(noise), pillar);
        if (base > 0.0) {
            const double roof = std::abs(cbSampleMesaRoofNoise(
                mesaNoise, mixedX * 0.001953125, mixedZ * 0.001953125));
            pillarHeight = std::min(base * base * 2.5, std::ceil(roof * 50.0) + 14.0) + 64.0;
        }
    }

    const int thickness = static_cast<int>(noise / 3.0 + 3.0 + random.nextDouble() * 0.25);
    const bool solidBand = std::cos(noise / 3.0 * 3.14159265358979323846) > 0.0;
    int remaining = -1;
    bool redSandCap = false;
    int stoneCount = 0;
    const auto bandAt = [&](int y) {
        const int offset = static_cast<int>(std::llround(cbSampleMesaBandOffsetNoise(
            mesaNoise, x / 512.0, x / 512.0) * 2.0));
        return bands[static_cast<std::size_t>((y + offset + 64) % 64)];
    };

    for (int y = 255; y >= 0; --y) {
        if (blockId(world.getBlock(x, y, z)) == 0 && y < static_cast<int>(pillarHeight))
            world.setGeneratedBlock(x, y, z, block(BlockId::Stone));
        if (y <= random.nextInt(5)) {
            world.setGeneratedBlock(x, y, z, block(BlockId::Bedrock));
            continue;
        }
        if (stoneCount >= 15 && !bryce) continue;
        const BlockId current = static_cast<BlockId>(blockId(world.getBlock(x, y, z)));
        if (current == BlockId::Air) {
            remaining = -1;
        } else if (current == BlockId::Stone) {
            if (remaining == -1) {
                redSandCap = false;
                BlockState top = block(BlockId::StainedHardenedClay);
                BlockState filler = block(BlockId::StainedHardenedClay);
                if (thickness <= 0) {
                    top = block(BlockId::Air);
                    filler = block(BlockId::Stone);
                } else if (y >= worldSeedSeaLevel - 4 && y <= worldSeedSeaLevel + 1) {
                    top = block(BlockId::StainedHardenedClay);
                    filler = block(BlockId::StainedHardenedClay);
                }
                if (y < worldSeedSeaLevel && blockId(top) == 0) top = block(BlockId::Water);
                remaining = thickness + std::max(0, y - worldSeedSeaLevel);
                if (y >= worldSeedSeaLevel - 1) {
                    if (forest && y > 86 + thickness * 2)
                        world.setGeneratedBlock(x, y, z,
                            solidBand ? block(BlockId::Dirt, 1) : block(BlockId::Grass));
                    else if (y > worldSeedSeaLevel + 3 + thickness) {
                        if (y >= 64 && y <= 127)
                            world.setGeneratedBlock(x, y, z,
                                solidBand ? block(BlockId::HardenedClay) : bandAt(y));
                        else world.setGeneratedBlock(x, y, z, block(BlockId::StainedHardenedClay, 1));
                    } else {
                        world.setGeneratedBlock(x, y, z, block(BlockId::Sand, 1));
                        redSandCap = true;
                    }
                } else {
                    world.setGeneratedBlock(x, y, z,
                        blockId(filler) == static_cast<std::uint16_t>(BlockId::StainedHardenedClay)
                            ? block(BlockId::StainedHardenedClay, 1) : filler);
                }
            } else if (remaining > 0) {
                --remaining;
                world.setGeneratedBlock(x, y, z,
                    redSandCap ? block(BlockId::StainedHardenedClay, 1) : bandAt(y));
            }
            ++stoneCount;
        }
    }
}

} // namespace

struct TerrainGenerator::Implementation {
    explicit Implementation(const WorldConfig& input)
        : config(input), settings(ChunkGeneratorSettings::fromConfig(input)),
          biomes(input), caves(input.seed) {
        surfaceNoise = cbCreateSurfaceNoise(input.seed);
        colorNoise = cbCreateColorNoise();
        mesaNoise = cbCreateMesaNoise(input.seed);
        if (surfaceNoise == nullptr || colorNoise == nullptr || mesaNoise == nullptr) throw std::bad_alloc();
        clayBands.fill(block(BlockId::HardenedClay));
        JavaRandom bandRandom(input.seed);
        for (int index = 0; index < 64; ++index) {
            index += bandRandom.nextInt(5) + 1;
            if (index < 64) clayBands[static_cast<std::size_t>(index)] = block(BlockId::StainedHardenedClay, 1);
        }
        const auto stripes = [&](int count, int minimumWidth, int widthRange, std::uint8_t metadata) {
            for (int stripe = 0; stripe < count; ++stripe) {
                const int width = bandRandom.nextInt(widthRange) + minimumWidth;
                const int start = bandRandom.nextInt(64);
                for (int offset = 0; start + offset < 64 && offset < width; ++offset)
                    clayBands[static_cast<std::size_t>(start + offset)] =
                        block(BlockId::StainedHardenedClay, metadata);
            }
        };
        stripes(bandRandom.nextInt(4) + 2, 1, 3, 4);  // yellow
        stripes(bandRandom.nextInt(4) + 2, 2, 3, 12); // brown
        stripes(bandRandom.nextInt(4) + 2, 1, 3, 14); // red
        int whiteY = 0;
        for (int stripe = 0, count = bandRandom.nextInt(3) + 3; stripe < count; ++stripe) {
            whiteY += bandRandom.nextInt(16) + 4;
            if (whiteY >= 64) continue;
            clayBands[static_cast<std::size_t>(whiteY)] = block(BlockId::StainedHardenedClay, 0);
            if (whiteY > 1 && bandRandom.nextBoolean())
                clayBands[static_cast<std::size_t>(whiteY - 1)] = block(BlockId::StainedHardenedClay, 8);
            if (whiteY < 63 && bandRandom.nextBoolean())
                clayBands[static_cast<std::size_t>(whiteY + 1)] = block(BlockId::StainedHardenedClay, 8);
        }
        for (int z = -2; z <= 2; ++z)
            for (int x = -2; x <= 2; ++x)
                weights[static_cast<std::size_t>((x + 2) + (z + 2) * 5)] =
                    10.0F / std::sqrt(static_cast<float>(x * x + z * z) + 0.2F);
        flat = FlatGeneratorSettings::parse(input.generatorOptions);
    }

    WorldConfig config;
    ChunkGeneratorSettings settings;
    BiomeProvider biomes;
    CaveGenerator caves;
    ~Implementation() {
        cbDestroySurfaceNoise(surfaceNoise);
        cbDestroyColorNoise(colorNoise);
        cbDestroyMesaNoise(mesaNoise);
    }

    CbSurfaceNoiseHandle surfaceNoise = nullptr;
    CbColorNoiseHandle colorNoise = nullptr;
    CbMesaNoiseHandle mesaNoise = nullptr;
    std::array<BlockState, 64> clayBands{};
    std::array<float, 25> weights{};
    FlatGeneratorSettings flat;
};

TerrainGenerator::TerrainGenerator(const WorldConfig& config)
    : implementation_(std::make_unique<Implementation>(config)) {}

TerrainGenerator::~TerrainGenerator() = default;

std::int64_t TerrainGenerator::seed() const { return implementation_->config.seed; }

void TerrainGenerator::generateChunk(World& world, int chunkX, int chunkZ) {
    Chunk& chunk = world.ensureChunk(chunkX, chunkZ);
    const int originX = chunkX * 16;
    const int originZ = chunkZ * 16;

    if (implementation_->config.worldType == WorldType::DebugAllBlockStates) {
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
            world.setGeneratedBlock(originX + x, 60, originZ + z, block(BlockId::Barrier));
            const int worldX = originX + x;
            const int worldZ = originZ + z;
            if (worldX > 0 && worldZ > 0 && (worldX & 1) != 0 && (worldZ & 1) != 0) {
                const int stateIndex = (worldX / 2) * 45 + worldZ / 2;
                if (stateIndex >= 0 && stateIndex < 4096)
                    world.setGeneratedBlock(originX + x, 70, originZ + z, makeBlockState(static_cast<std::uint16_t>(stateIndex & 4095)));
            }
            chunk.setBiome(x, z, 1);
        }
        return;
    }

    if (implementation_->config.worldType == WorldType::Flat) {
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
            int y = 0;
            for (const FlatLayer& layer : implementation_->flat.layers) {
                for (int count = 0; count < layer.count && y < chunkHeight; ++count, ++y)
                    world.setGeneratedBlock(originX + x, y, originZ + z, layer.state);
            }
            chunk.setBiome(x, z, static_cast<std::uint8_t>(implementation_->flat.biome));
        }
        return;
    }

    const std::vector<std::int32_t> generationBiomes = implementation_->biomes.getBiomesForGeneration(
        chunkX * 4 - 2, chunkZ * 4 - 2, 10, 10);
    const std::vector<std::int32_t> blockBiomes = implementation_->biomes.getBiomes(originX, originZ, 16, 16);
    std::array<double, 825> density{};
    std::size_t densityIndex = 0;

    for (int coarseX = 0; coarseX < 5; ++coarseX) {
        for (int coarseZ = 0; coarseZ < 5; ++coarseZ) {
            float variation = 0.0F;
            float depth = 0.0F;
            float weightTotal = 0.0F;
            const BiomeDefinition& center = BiomeProvider::definition(generationBiomes[static_cast<std::size_t>(coarseX + 2 + (coarseZ + 2) * 10)]);
            for (int offsetX = -2; offsetX <= 2; ++offsetX) {
                for (int offsetZ = -2; offsetZ <= 2; ++offsetZ) {
                    const BiomeDefinition& nearby = BiomeProvider::definition(generationBiomes[
                        static_cast<std::size_t>(coarseX + offsetX + 2 + (coarseZ + offsetZ + 2) * 10)]);
                    float nearbyDepth = implementation_->settings.biomeDepthOffset +
                        nearby.baseHeight * implementation_->settings.biomeDepthWeight;
                    float nearbyVariation = implementation_->settings.biomeScaleOffset +
                        nearby.heightVariation * implementation_->settings.biomeScaleWeight;
                    if (implementation_->config.worldType == WorldType::Amplified && nearbyDepth > 0.0F) {
                        nearbyDepth = 1.0F + nearbyDepth * 2.0F;
                        nearbyVariation = 1.0F + nearbyVariation * 4.0F;
                    }
                    float weight = implementation_->weights[static_cast<std::size_t>((offsetX + 2) + (offsetZ + 2) * 5)] /
                        (nearbyDepth + 2.0F);
                    if (nearby.baseHeight > center.baseHeight) weight *= 0.5F;
                    variation += nearbyVariation * weight;
                    depth += nearbyDepth * weight;
                    weightTotal += weight;
                }
            }
            variation = variation / weightTotal * 0.9F + 0.1F;
            depth = (depth / weightTotal * 4.0F - 1.0F) / 8.0F;

            const int sampleX = chunkX * 4 + coarseX;
            const int sampleZ = chunkZ * 4 + coarseZ;
            double depthNoise = cbSampleDepthNoise(implementation_->surfaceNoise,
                sampleX * implementation_->settings.depthNoiseScaleX,
                sampleZ * implementation_->settings.depthNoiseScaleZ) * 65535.0 / 8000.0;
            if (depthNoise < 0.0) depthNoise = -depthNoise * 0.3;
            depthNoise = depthNoise * 3.0 - 2.0;
            if (depthNoise < 0.0) {
                depthNoise /= 2.0;
                depthNoise = std::max(depthNoise, -1.0);
                depthNoise /= 2.8;
            } else {
                depthNoise = std::min(depthNoise, 1.0) / 8.0;
            }
            double adjustedDepth = static_cast<double>(depth) + depthNoise * 0.2;
            adjustedDepth = adjustedDepth * implementation_->settings.baseSize / 8.0;
            const double base = implementation_->settings.baseSize + adjustedDepth * 4.0;

            for (int coarseY = 0; coarseY < 33; ++coarseY) {
                double falloff = (coarseY - base) * implementation_->settings.stretchY *
                    128.0 / 256.0 / variation;
                if (falloff < 0.0) falloff *= 4.0;
                double value = cbSampleTerrainNoise(implementation_->surfaceNoise,
                    sampleX, coarseY, sampleZ,
                    implementation_->settings.coordinateScale,
                    implementation_->settings.heightScale,
                    implementation_->settings.lowerLimitScale,
                    implementation_->settings.upperLimitScale,
                    implementation_->settings.mainNoiseScaleX,
                    implementation_->settings.mainNoiseScaleY,
                    implementation_->settings.mainNoiseScaleZ) - falloff;
                if (coarseY > 29) {
                    const double blend = static_cast<float>(coarseY - 29) / 3.0F;
                    value = value * (1.0 - blend) - 10.0 * blend;
                }
                density[densityIndex++] = value;
            }
        }
    }

    for (int cellX = 0; cellX < 4; ++cellX) for (int cellZ = 0; cellZ < 4; ++cellZ) {
        const int a = (cellX * 5 + cellZ) * 33;
        const int b = (cellX * 5 + cellZ + 1) * 33;
        const int c = ((cellX + 1) * 5 + cellZ) * 33;
        const int d = ((cellX + 1) * 5 + cellZ + 1) * 33;
        for (int cellY = 0; cellY < 32; ++cellY) {
            for (int subY = 0; subY < 8; ++subY) {
                const double fy = subY / 8.0;
                const double da = std::lerp(density[static_cast<std::size_t>(a + cellY)], density[static_cast<std::size_t>(a + cellY + 1)], fy);
                const double db = std::lerp(density[static_cast<std::size_t>(b + cellY)], density[static_cast<std::size_t>(b + cellY + 1)], fy);
                const double dc = std::lerp(density[static_cast<std::size_t>(c + cellY)], density[static_cast<std::size_t>(c + cellY + 1)], fy);
                const double dd = std::lerp(density[static_cast<std::size_t>(d + cellY)], density[static_cast<std::size_t>(d + cellY + 1)], fy);
                for (int subX = 0; subX < 4; ++subX) for (int subZ = 0; subZ < 4; ++subZ) {
                    const double fx = subX / 4.0;
                    const double fz = subZ / 4.0;
                    const double value = std::lerp(std::lerp(da, dc, fx), std::lerp(db, dd, fx), fz);
                    const int y = cellY * 8 + subY;
                    if (value > 0.0) world.setGeneratedBlock(originX + cellX * 4 + subX, y, originZ + cellZ * 4 + subZ, block(BlockId::Stone));
                    else if (y < implementation_->settings.seaLevel) world.setGeneratedBlock(originX + cellX * 4 + subX, y, originZ + cellZ * 4 + subZ,
                        block(implementation_->settings.useLavaOceans ? BlockId::Lava : BlockId::Water));
                }
            }
        }
    }

    JavaRandom random(chunkTerrainSeed(chunkX, chunkZ));
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) {
        const int biomeId = blockBiomes[static_cast<std::size_t>(x + z * 16)];
        chunk.setBiome(x, z, static_cast<std::uint8_t>(biomeId));
        BlockState top;
        BlockState filler;
        const double surface = cbSampleSurfaceOctaves(implementation_->surfaceNoise,
            (originX + x) * 0.0625, (originZ + z) * 0.0625);
        if (biomeId == 37 || biomeId == 38 || biomeId == 39 || biomeId == 165 ||
            biomeId == 166 || biomeId == 167) {
            generateMesaColumn(world, random, implementation_->mesaNoise,
                implementation_->clayBands, implementation_->settings.seaLevel,
                biomeId, originX + x, originZ + z, surface);
            continue;
        }
        surfaceBlocks(biomeId, surface, top, filler);

        const double swampNoise = cbSampleGrassColorNoise(implementation_->colorNoise,
            (originX + x) * 0.25, (originZ + z) * 0.25);
        if ((biomeId == 6 || biomeId == 134) && swampNoise > 0.0) {
            for (int y = 255; y >= 0; --y) {
                if (blockId(world.getBlock(originX + x, y, originZ + z)) == 0) continue;
                const BlockId found = static_cast<BlockId>(blockId(
                    world.getBlock(originX + x, y, originZ + z)));
                if (y == 62 && found != BlockId::Water && found != BlockId::FlowingWater) {
                    world.setGeneratedBlock(originX + x, y, originZ + z, block(BlockId::Water));
                    if (swampNoise < 0.12)
                        world.setGeneratedBlock(originX + x, y + 1, originZ + z, block(BlockId::Waterlily));
                }
                break;
            }
        }
        const int thickness = static_cast<int>(surface / 3.0 + 3.0 + random.nextDouble() * 0.25);
        int remaining = -1;
        const BlockState biomeTop = top;
        const BlockState biomeFiller = filler;
        for (int y = 255; y >= 0; --y) {
            if (y <= random.nextInt(5)) {
                world.setGeneratedBlock(originX + x, y, originZ + z, block(BlockId::Bedrock));
                continue;
            }
            const BlockState current = world.getBlock(originX + x, y, originZ + z);
            if (blockId(current) == 0) { remaining = -1; continue; }
            if (blockId(current) != static_cast<std::uint16_t>(BlockId::Stone)) continue;
            if (remaining == -1) {
                top = biomeTop;
                filler = biomeFiller;
                if (thickness <= 0) {
                    top = block(BlockId::Air);
                    filler = block(BlockId::Stone);
                } else if (y < implementation_->settings.seaLevel - 4 || y > implementation_->settings.seaLevel + 1) {
                    top = biomeTop;
                    filler = biomeFiller;
                }
                if (y < implementation_->settings.seaLevel && blockId(top) == 0) {
                    top = BiomeProvider::definition(biomeId).temperature < 0.15F
                        ? block(BlockId::Ice) : block(BlockId::Water);
                }
                remaining = thickness;
                if (y >= implementation_->settings.seaLevel - 1) world.setGeneratedBlock(originX + x, y, originZ + z, top);
                else if (y < implementation_->settings.seaLevel - 7 - thickness) {
                    top = block(BlockId::Air);
                    filler = block(BlockId::Stone);
                    world.setGeneratedBlock(originX + x, y, originZ + z, block(BlockId::Gravel));
                }
                else world.setGeneratedBlock(originX + x, y, originZ + z, filler);
            } else if (remaining > 0) {
                --remaining;
                world.setGeneratedBlock(originX + x, y, originZ + z, filler);
                if (remaining == 0 && blockId(filler) == static_cast<std::uint16_t>(BlockId::Sand) && thickness > 1) {
                    remaining = random.nextInt(4) + std::max(0, y - 63);
                    filler = block(BlockId::Sandstone);
                }
            }
        }
    }
    implementation_->caves.generate(world, chunkX, chunkZ,
        implementation_->settings.useCaves, implementation_->settings.useRavines);
}
