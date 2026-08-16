#include "worldgen/TerrainGenerator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include "blockcraft_bridge.h"
}

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"
#include "worldgen/CaveGenerator.hpp"
#include "worldgen/ChunkGeneratorSettings.hpp"
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

void surfaceBlocks(int biomeId, BlockState& top, BlockState& filler) {
    top = block(BlockId::Grass);
    filler = block(BlockId::Dirt);
    switch (biomeId) {
        case 2: case 16: case 17: case 26: case 35: case 36:
            top = filler = block(BlockId::Sand); break;
        case 14: case 15:
            top = block(BlockId::Mycelium); filler = block(BlockId::Dirt); break;
        case 25:
            top = filler = block(BlockId::Stone); break;
        case 37: case 38: case 39: case 165: case 166: case 167:
            top = filler = block(BlockId::HardenedClay); break;
        default: break;
    }
}

struct FlatLayer {
    int count;
    BlockState state;
};

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        parts.push_back(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

int parseInteger(std::string_view text, int fallback) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : fallback;
}

BlockState flatBlock(std::string_view name, int metadata) {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    const std::array<std::pair<std::string_view, BlockId>, 24> names = {{
        {"air", BlockId::Air}, {"stone", BlockId::Stone}, {"grass", BlockId::Grass},
        {"dirt", BlockId::Dirt}, {"cobblestone", BlockId::Cobblestone}, {"planks", BlockId::Planks},
        {"bedrock", BlockId::Bedrock}, {"water", BlockId::Water}, {"lava", BlockId::Lava},
        {"sand", BlockId::Sand}, {"gravel", BlockId::Gravel}, {"gold_ore", BlockId::GoldOre},
        {"iron_ore", BlockId::IronOre}, {"coal_ore", BlockId::CoalOre}, {"log", BlockId::Log},
        {"leaves", BlockId::Leaves}, {"glass", BlockId::Glass}, {"lapis_ore", BlockId::LapisOre},
        {"sandstone", BlockId::Sandstone}, {"diamond_ore", BlockId::DiamondOre},
        {"redstone_ore", BlockId::RedstoneOre}, {"snow", BlockId::Snow},
        {"clay", BlockId::Clay}, {"hardened_clay", BlockId::HardenedClay}
    }};
    for (const auto& [candidate, id] : names)
        if (name == candidate) return block(id, static_cast<std::uint8_t>(metadata & 15));
    const int numeric = parseInteger(name, -1);
    return numeric >= 0 && numeric <= 4095
        ? makeBlockState(static_cast<std::uint16_t>(numeric), static_cast<std::uint8_t>(metadata & 15))
        : block(BlockId::Air);
}

void parseFlatSettings(std::string_view settings, std::vector<FlatLayer>& layers, int& biome) {
    layers = {{1, block(BlockId::Bedrock)}, {2, block(BlockId::Dirt)}, {1, block(BlockId::Grass)}};
    biome = 1;
    if (settings.empty() || settings == "{}") return;
    const std::vector<std::string_view> fields = split(settings, ';');
    if (fields.size() < 2) return;
    const int version = parseInteger(fields[0], -1);
    if (version < 0 || version > 3) return;
    std::vector<FlatLayer> parsed;
    int height = 0;
    for (std::string_view description : split(fields[1], ',')) {
        const char multiplier = version >= 3 ? '*' : 'x';
        const std::size_t separator = description.find(multiplier);
        int count = 1;
        if (separator != std::string_view::npos) {
            count = std::clamp(parseInteger(description.substr(0, separator), 0), 0, 256 - height);
            description.remove_prefix(separator + 1);
        }
        int metadata = 0;
        std::string_view name = description;
        if (version >= 3) {
            const std::size_t namespaceSeparator = description.find(':');
            const std::size_t metadataSeparator = namespaceSeparator == std::string_view::npos
                ? std::string_view::npos : description.find(':', namespaceSeparator + 1);
            if (metadataSeparator != std::string_view::npos) {
                metadata = parseInteger(description.substr(metadataSeparator + 1), 0);
                name = description.substr(0, metadataSeparator);
            }
        } else {
            const std::size_t metadataSeparator = description.find(':');
            if (metadataSeparator != std::string_view::npos) {
                metadata = parseInteger(description.substr(metadataSeparator + 1), 0);
                name = description.substr(0, metadataSeparator);
            }
        }
        if (count > 0) parsed.push_back({count, flatBlock(name, metadata)});
        height += count;
    }
    if (!parsed.empty()) layers = std::move(parsed);
    if (fields.size() > 2) biome = parseInteger(fields[2], 1);
}

} // namespace

struct TerrainGenerator::Implementation {
    explicit Implementation(const WorldConfig& input)
        : config(input), settings(ChunkGeneratorSettings::fromConfig(input)),
          biomes(input), caves(input.seed) {
        surfaceNoise = cbCreateSurfaceNoise(input.seed);
        if (surfaceNoise == nullptr) throw std::bad_alloc();
        for (int z = -2; z <= 2; ++z)
            for (int x = -2; x <= 2; ++x)
                weights[static_cast<std::size_t>((x + 2) + (z + 2) * 5)] =
                    10.0F / std::sqrt(static_cast<float>(x * x + z * z) + 0.2F);
        parseFlatSettings(input.generatorOptions, flatLayers, flatBiome);
    }

    WorldConfig config;
    ChunkGeneratorSettings settings;
    BiomeProvider biomes;
    CaveGenerator caves;
    ~Implementation() { cbDestroySurfaceNoise(surfaceNoise); }

    CbSurfaceNoiseHandle surfaceNoise = nullptr;
    std::array<float, 25> weights{};
    std::vector<FlatLayer> flatLayers;
    int flatBiome = 1;
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
            for (const FlatLayer& layer : implementation_->flatLayers) {
                for (int count = 0; count < layer.count && y < chunkHeight; ++count, ++y)
                    world.setGeneratedBlock(originX + x, y, originZ + z, layer.state);
            }
            chunk.setBiome(x, z, static_cast<std::uint8_t>(implementation_->flatBiome));
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
        surfaceBlocks(biomeId, top, filler);
        const double surface = cbSampleSurfaceOctaves(implementation_->surfaceNoise,
            (originX + x) * 0.0625, (originZ + z) * 0.0625);
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
