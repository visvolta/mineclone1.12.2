#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "core/ThreadPool.hpp"
#include "environment/Environment.hpp"
#include "lighting/LightSolver.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/Player.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/TextureAtlasData.hpp"
#include "rendering/ChunkMesher.hpp"
#include "rendering/CloudGeometry.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"
#include "worldgen/JavaRandom.hpp"
#include "worldgen/BiomeProvider.hpp"
#include "worldgen/ChunkStreamer.hpp"
#include "worldgen/StructureGenerator.hpp"
#include "worldgen/TerrainGenerator.hpp"
#include "worldgen/WorldConfig.hpp"

namespace {

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

std::size_t quadCount(const MeshData& mesh) { return mesh.indices.size() / 6; }

const BlockRenderResources& renderResources() {
    static const BlockRenderResources resources(BLOCKCRAFT_ASSET_ROOT);
    return resources;
}

void testBlockStatePacking() {
    const BlockState oakXAxis = block(BlockId::Log, 4);
    assert(blockId(oakXAxis) == 17);
    assert(blockMetadata(oakXAxis) == 4);
    assert(BlockRegistry::texture(oakXAxis, Face::East) == TextureId::LogOakTop);
    assert(BlockRegistry::texture(oakXAxis, Face::Up) == TextureId::LogOak);
}

void testChunkSections() {
    Chunk chunk(-1, 2);
    assert(chunk.get(3, 70, 9) == block(BlockId::Air));
    assert(chunk.set(3, 70, 9, block(BlockId::Stone)));
    assert(chunk.get(3, 70, 9) == block(BlockId::Stone));
    assert(chunk.section(4) != nullptr);
    assert(chunk.section(4)->nonAirCount() == 1);
    assert(chunk.set(3, 70, 9, block(BlockId::Air)));
    assert(chunk.section(4)->empty());
}

void testSingleBlockSkylightShadow() {
    World world;
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ)
        for (int chunkX = -1; chunkX <= 1; ++chunkX)
            static_cast<void>(world.ensureChunk(chunkX, chunkZ));
    world.setBlock(8, 10, 8, block(BlockId::Stone));

    const ChunkLightingData light = LightSolver::solve(LightSolver::capture(world, 0, 0));
    const auto at = [](const std::vector<std::uint8_t>& values, int x, int y, int z) {
        return values[static_cast<std::size_t>((y * chunkSize + z) * chunkSize + x)];
    };
    assert(at(light.sky, 8, 11, 8) == 15);
    assert(at(light.sky, 8, 10, 8) == 0);
    // Vanilla lateral propagation leaves level 14 immediately beneath a
    // single opaque block; the adjacent open column remains level 15.
    assert(at(light.sky, 8, 9, 8) == 14);
    assert(at(light.sky, 7, 9, 8) == 15);
}

void testSynchronousEditLighting() {
    World world;
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ)
        for (int chunkX = -1; chunkX <= 1; ++chunkX)
            static_cast<void>(world.ensureChunk(chunkX, chunkZ));
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ) {
        for (int chunkX = -1; chunkX <= 1; ++chunkX) {
            ChunkLightingData initial = LightSolver::solve(LightSolver::capture(world, chunkX, chunkZ));
            static_cast<void>(world.findChunk(chunkX, chunkZ)->applyLighting(initial.sky, initial.block));
        }
    }
    ThreadPool workers(2);
    LightingEngine lighting(world, workers);

    world.setBlock(8, 8, 8, block(BlockId::Stone));
    assert(lighting.blockChangedSync(8, 8, 8).size() == 1);
    assert(world.getSkyLight(8, 7, 8) == 14);
    world.setBlock(8, 8, 8, block(BlockId::Air));
    assert(lighting.blockChangedSync(8, 8, 8).size() == 1);
    assert(world.getSkyLight(8, 7, 8) == 15);

    world.setBlock(8, 8, 8, block(BlockId::Lava));
    assert(!lighting.blockChangedSync(8, 8, 8).empty());
    assert(world.getBlockLight(8, 8, 8) == 15);
    assert(world.getBlockLight(9, 8, 8) == 14);
    world.setBlock(8, 8, 8, block(BlockId::Air));
    assert(!lighting.blockChangedSync(8, 8, 8).empty());
    assert(world.getBlockLight(8, 8, 8) == 0);
    assert(world.getBlockLight(9, 8, 8) == 0);
}

void testNegativeCoordinates() {
    assert(World::floorDiv16(-1) == -1);
    assert(World::floorDiv16(-16) == -1);
    assert(World::floorDiv16(-17) == -2);
    assert(World::floorMod16(-1) == 15);
    World world;
    world.setBlock(-1, 5, -17, block(BlockId::Glass));
    assert(world.getBlock(-1, 5, -17) == block(BlockId::Glass));
}

void testMeshing() {
    World isolated;
    isolated.setBlock(1, 1, 1, block(BlockId::Stone));
    const SectionMeshData isolatedMesh = ChunkMesher::build(isolated, 0, 0, 0, renderResources());
    assert(quadCount(isolatedMesh[0]) == 6);

    isolated.setBlock(2, 1, 1, block(BlockId::Stone));
    const SectionMeshData joinedMesh = ChunkMesher::build(isolated, 0, 0, 0, renderResources());
    assert(quadCount(joinedMesh[0]) == 10);

    World border;
    border.setBlock(15, 1, 1, block(BlockId::Stone));
    border.setBlock(16, 1, 1, block(BlockId::Stone));
    assert(quadCount(ChunkMesher::build(border, 0, 0, 0, renderResources())[0]) == 5);
    assert(quadCount(ChunkMesher::build(border, 1, 0, 0, renderResources())[0]) == 5);

    World glass;
    glass.setBlock(1, 1, 1, block(BlockId::Glass));
    glass.setBlock(2, 1, 1, block(BlockId::Glass));
    assert(quadCount(ChunkMesher::build(glass, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 10);

    World leaves;
    leaves.setBlock(1, 1, 1, block(BlockId::Leaves));
    leaves.setBlock(2, 1, 1, block(BlockId::Leaves));
    assert(BlockRegistry::get(block(BlockId::Leaves)).layer == RenderLayer::CutoutMipped);
    assert(!BlockRegistry::get(block(BlockId::Leaves)).opaque);
    // Fancy leaves render the shared leaf/leaf faces, unlike Fast mode.
    assert(quadCount(ChunkMesher::build(leaves, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::CutoutMipped)]) == 12);

    const SectionSnapshot snapshot = ChunkMesher::capture(leaves, 0, 0, 0);
    const SectionMeshData snapshotMesh = ChunkMesher::build(snapshot, renderResources());
    const SectionMeshData worldMesh = ChunkMesher::build(leaves, 0, 0, 0, renderResources());
    for (std::size_t layer = 0; layer < snapshotMesh.size(); ++layer) {
        assert(snapshotMesh[layer].vertices.size() == worldMesh[layer].vertices.size());
        assert(snapshotMesh[layer].indices == worldMesh[layer].indices);
    }

    World generatedModels;
    generatedModels.setBlock(1, 1, 1, block(BlockId::TallGrass, 1));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 4);
    generatedModels.setBlock(1, 1, 1, block(BlockId::SnowLayer, 3));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 6);
    generatedModels.setBlock(1, 1, 1, block(BlockId::Cactus));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 6);
    generatedModels.setBlock(1, 1, 1, block(BlockId::Vine, 1));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 2);
    // BlockVine metadata: south=1, west=2, north=4, east=8. Each attached
    // side is a two-sided plane in the 1.12.2 baked model.
    generatedModels.setBlock(1, 1, 1, block(BlockId::Vine, 3));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 4);
    generatedModels.setBlock(1, 1, 1, block(BlockId::Vine, 15));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 8);
    generatedModels.setBlock(1, 1, 1, block(BlockId::DoublePlant, 2));
    generatedModels.setBlock(1, 2, 1, block(BlockId::DoublePlant, 8));
    assert(quadCount(ChunkMesher::build(generatedModels, 0, 0, 0, renderResources())[static_cast<std::size_t>(RenderLayer::Cutout)]) == 8);
}

void testStage2RenderingCoverage() {
    const BlockModelManager& models = renderResources().models();
    const auto air = [](int, int, int) { return makeBlockState(0); };
    const auto builtIn = [](BlockId id) {
        switch (id) {
            case BlockId::Air:
            case BlockId::FlowingWater: case BlockId::Water:
            case BlockId::FlowingLava: case BlockId::Lava:
            case BlockId::Bed: case BlockId::PistonExtension:
            case BlockId::Chest: case BlockId::StandingSign: case BlockId::WallSign:
            case BlockId::EndPortal: case BlockId::EnderChest: case BlockId::Skull:
            case BlockId::TrappedChest: case BlockId::Barrier:
            case BlockId::StandingBanner: case BlockId::WallBanner:
            case BlockId::EndGateway: case BlockId::StructureVoid:
            case BlockId::WhiteShulkerBox: case BlockId::OrangeShulkerBox:
            case BlockId::MagentaShulkerBox: case BlockId::LightBlueShulkerBox:
            case BlockId::YellowShulkerBox: case BlockId::LimeShulkerBox:
            case BlockId::PinkShulkerBox: case BlockId::GrayShulkerBox:
            case BlockId::SilverShulkerBox: case BlockId::CyanShulkerBox:
            case BlockId::PurpleShulkerBox: case BlockId::BlueShulkerBox:
            case BlockId::BrownShulkerBox: case BlockId::GreenShulkerBox:
            case BlockId::RedShulkerBox: case BlockId::BlackShulkerBox:
                return true;
            default: return false;
        }
    };

    int modelBacked = 0;
    for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
        if (!BlockRegistry::isRegisteredId(numericId)) continue;
        const auto id = static_cast<BlockId>(numericId);
        if (builtIn(id)) continue;
        bool selected = false;
        for (std::uint8_t meta = 0; meta < 16 && !selected; ++meta) {
            const BlockState state = makeBlockState(numericId, meta);
            const BlockModelState modelState = resolveBlockModelState(state, air);
            if (!models.hasBlockState(modelState.resourceName)) continue;
            selected = !models.select(modelState, 0).empty();
        }
        assert(selected);
        ++modelBacked;
    }
    // 254 registered IDs - 35 vanilla built-in/custom renderer IDs.
    assert(modelBacked == 219);
}

void testFluidRendering() {
    World waterWorld;
    waterWorld.setBlock(1, 1, 1, block(BlockId::Water, 0));
    const SectionMeshData waterMesh = ChunkMesher::build(waterWorld, 0, 0, 0, renderResources());
    const MeshData& translucent = waterMesh[static_cast<std::size_t>(RenderLayer::Translucent)];
    assert(!translucent.indices.empty());
    assert(waterMesh[static_cast<std::size_t>(RenderLayer::Solid)].indices.empty());
    bool hasPartialTop = false;
    for (const MeshVertex& vertex : translucent.vertices) {
        assert(std::isfinite(vertex.y));
        if (vertex.y > 1.0F && vertex.y < 2.0F) hasPartialTop = true;
    }
    assert(hasPartialTop);

    World lavaWorld;
    lavaWorld.setBlock(1, 1, 1, block(BlockId::Lava, 0));
    const SectionMeshData lavaMesh = ChunkMesher::build(lavaWorld, 0, 0, 0, renderResources());
    assert(!lavaMesh[static_cast<std::size_t>(RenderLayer::Solid)].indices.empty());
    assert(lavaMesh[static_cast<std::size_t>(RenderLayer::Translucent)].indices.empty());
    assert(BlockRegistry::get(block(BlockId::Water)).layer == RenderLayer::Translucent);
    assert(BlockRegistry::get(block(BlockId::Lava)).layer == RenderLayer::Solid);
}

void testPartialModelAo() {
    World world;
    world.setBlock(1, 1, 1, block(BlockId::StoneSlab, 0));
    world.setBlock(0, 1, 1, block(BlockId::Stone));
    world.setBlock(1, 1, 0, block(BlockId::Stone));
    const SectionMeshData mesh = ChunkMesher::build(world, 0, 0, 0, renderResources());
    const MeshData& solid = mesh[static_cast<std::size_t>(RenderLayer::Solid)];
    assert(!solid.vertices.empty());
    for (const MeshVertex& vertex : solid.vertices) {
        assert(std::isfinite(vertex.shade));
        assert(vertex.shade >= 0.0F && vertex.shade <= 1.0F);
        assert(std::isfinite(vertex.skyLight));
        assert(std::isfinite(vertex.blockLight));
    }
}

void testMeshingSnapshotHalo() {
    World world;
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ)
        for (int chunkX = -1; chunkX <= 1; ++chunkX)
            static_cast<void>(world.ensureChunk(chunkX, chunkZ));

    constexpr std::array<std::array<int, 3>, 8> corners{{
        {{0, 0, 0}}, {{15, 0, 0}}, {{0, 15, 0}}, {{0, 0, 15}},
        {{15, 15, 0}}, {{15, 0, 15}}, {{0, 15, 15}}, {{15, 15, 15}}
    }};
    for (const auto& position : corners)
        world.setBlock(position[0], position[1], position[2], block(BlockId::Stone));

    const SectionMeshData mesh = ChunkMesher::build(world, 0, 0, 0, renderResources());
    assert(!mesh[0].indices.empty());
}

void testWorldChunkTransfer() {
    World world;
    world.setBlock(17, 4, -1, block(BlockId::Stone));
    std::unique_ptr<Chunk> chunk = world.extractChunk(1, -1);
    assert(chunk != nullptr);
    assert(world.findChunk(1, -1) == nullptr);
    world.insertChunk(std::move(chunk));
    assert(world.getBlock(17, 4, -1) == block(BlockId::Stone));
}

void testThreadPool() {
    ThreadPool pool(2);
    auto first = pool.submit(1, [] { return 20; });
    auto second = pool.submit(2, [] { return 22; });
    assert(first.get() + second.get() == 42);
    assert(pool.workerCount() == 2);
    assert(ThreadPool::recommendedWorkerCount() >= 1);
}

void testThreadedChunkPriming() {
    WorldConfig config;
    config.seed = 1;
    config.worldType = WorldType::Flat;
    config.generatorOptions = "3;1*minecraft:bedrock,2*minecraft:dirt,1*minecraft:grass;1";
    World world;
    ChunkStreamer streamer(world, config, 2);
    streamer.prime(0.0, 0.0, 1);
    assert(world.chunkCount() == 9);
    assert(world.getBlock(-1, 3, -1) == block(BlockId::Grass));
}

void testThreadedPrefetchCaching() {
    WorldConfig config;
    config.seed = 1;
    config.worldType = WorldType::Flat;
    config.generatorOptions = "3;1*minecraft:bedrock,2*minecraft:dirt,1*minecraft:grass;1";
    config.chunkCacheCapacity = 128;
    World world;
    ChunkStreamer streamer(world, config, 2);
    streamer.prime(0.0, 0.0, 1);

    for (int step = 0; step < 10000; ++step) {
        static_cast<void>(streamer.update(0.0, 0.0, 0.0F, -1.0F, 10.0));
        if (world.chunkCount() == 25 && streamer.cachedChunkCount() > 0 &&
            streamer.pendingGenerationCount() == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(world.chunkCount() == 25);
    assert(streamer.cachedChunkCount() > 0);
    assert(streamer.cachedChunkCount() <= static_cast<std::size_t>(config.chunkCacheCapacity));
    assert(streamer.pendingGenerationCount() == 0);
}

void testAtlasInsetAndGenerator() {
    const TextureAtlasData& atlas = renderResources().atlas();
    const AtlasSprite& stone = atlas.sprite("minecraft:blocks/stone");
    assert(stone.width == 16 && stone.height == 16);
    const float rawU = static_cast<float>(stone.originX) / static_cast<float>(atlas.width());
    const float rawV = static_cast<float>(stone.originY) / static_cast<float>(atlas.height());
    assert(std::abs(stone.bounds.u0 - rawU - TextureAtlasData::uvInset) < 0.0000001F);
    assert(std::abs(stone.bounds.v0 - rawV - TextureAtlasData::uvInset) < 0.0000001F);
    assert(stone.bounds.u(0.0) == stone.bounds.u0);
    assert(stone.bounds.u(16.0) == stone.bounds.u1);
    const AtlasSprite& water = atlas.sprite("minecraft:blocks/water_still");
    const AtlasSprite& lava = atlas.sprite("minecraft:blocks/lava_still");
    assert(water.frames.size() > 1 && water.animation.size() > 1);
    assert(lava.frames.size() > 1 && lava.animation.size() > 1);

    const BlockModelManager& models = renderResources().models();
    assert(models.hasBlockState("stone"));
    const auto stoneModels = models.select({"stone", {}, {}}, 0);
    assert(stoneModels.size() == 1 && stoneModels.front()->quads.size() == 6);

    BlockModelState fenceState{"fence", {
        {"north", "false"}, {"east", "false"},
        {"south", "false"}, {"west", "false"}
    }, {}};
    const auto fencePost = models.select(fenceState, 0);
    std::size_t fencePostQuads = 0;
    for (const BakedBlockModel* model : fencePost) fencePostQuads += model->quads.size();
    fenceState.properties["north"] = "true";
    const auto fenceNorth = models.select(fenceState, 0);
    std::size_t fenceNorthQuads = 0;
    for (const BakedBlockModel* model : fenceNorth) fenceNorthQuads += model->quads.size();
    assert(fenceNorthQuads > fencePostQuads);
    WorldConfig flat;
    flat.seed = 1;
    flat.worldType = WorldType::Flat;
    flat.generatorOptions = "3;1*minecraft:bedrock,59*minecraft:stone,3*minecraft:dirt,1*minecraft:grass;1";
    World world;
    TerrainGenerator generator(flat);
    for (int chunkZ = -1; chunkZ <= 1; ++chunkZ)
        for (int chunkX = -1; chunkX <= 1; ++chunkX)
            generator.generateChunk(world, chunkX, chunkZ);
    assert(world.chunks().size() == 9);
    assert(world.getBlock(0, 0, 0) == block(BlockId::Bedrock));
    assert(world.getBlock(0, 63, 0) == block(BlockId::Grass));
}

void testVanillaBlockHardness() {
    assert(BlockRegistry::get(block(BlockId::Stone)).hardness == 1.5F);
    assert(BlockRegistry::get(block(BlockId::Cobblestone)).hardness == 2.0F);
    assert(BlockRegistry::get(block(BlockId::Log)).hardness == 2.0F);
    assert(BlockRegistry::get(block(BlockId::Leaves)).hardness == 0.2F);
    assert(BlockRegistry::get(block(BlockId::Bedrock)).hardness < 0.0F);
    assert(BlockRegistry::get(block(BlockId::Stone)).requiresTool);
    assert(!BlockRegistry::get(block(BlockId::Dirt)).requiresTool);
    assert(BlockRegistry::get(block(BlockId::Torch)).lightValue == 14);
    assert(BlockRegistry::get(block(BlockId::Glowstone)).lightValue == 15);
    assert(BlockRegistry::get(block(BlockId::Magma)).lightValue == 3);
}

void testPlayerPhysicsAndFlight() {
    World world;
    for (int z = -2; z <= 2; ++z)
        for (int x = -2; x <= 2; ++x)
            world.setBlock(x, 0, z, block(BlockId::Stone));

    Player player({0.0, 1.0, 0.0});
    const glm::vec3 forward{0.0F, 0.0F, -1.0F};
    player.tick(world, {}, forward);
    player.tick(world, {}, forward);
    assert(player.onGround());
    assert(player.feetPosition().y == 1.0);

    PlayerInput jump;
    jump.jump = true;
    jump.jumpPressed = true;
    player.tick(world, jump, forward);
    assert(player.feetPosition().y > 1.4 && player.feetPosition().y < 1.43);

    player.toggleGameMode();
    assert(player.gameMode() == GameMode::Creative);
    player.tick(world, jump, forward);
    PlayerInput released;
    player.tick(world, released, forward);
    player.tick(world, jump, forward);
    assert(player.flying());
    player.toggleGameMode();
    assert(player.gameMode() == GameMode::Survival);
    assert(!player.flying());
}

void testJavaCompatibility() {
    JavaRandom random(0);
    assert(random.nextInt() == -1155484576);
    assert(random.nextInt(1000) == 948);
    assert(random.nextLong() == 4437113781045784766LL);
    assert(random.nextDouble() == 0.6374174253501083);
    assert(random.nextFloat() == 0.5504369735717773F);

    assert(javaStringHash("Blockcraft") == -1011332653);
    assert(javaStringHash("Minecraft") == -1595926131);
    assert(javaStringHash("\xF0\x9F\x98\x80") == 1772899);
    assert(parseMinecraftSeed("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    assert(parseMinecraftSeed("Blockcraft") == -1011332653);
}

void testReferenceTerrainFixture() {
    // Compared with a clean Minecraft 1.12.2 server world for seed "Blockcraft".
    WorldConfig config;
    config.seedText = "Blockcraft";
    config.seed = -1011332653LL;
    config.worldType = WorldType::Default;

    BiomeProvider provider(config);
    const std::vector<std::int32_t> biomes = provider.getBiomes(-32, 0, 16, 16);
    for (int index = 0; index < 64; ++index) assert(biomes[static_cast<std::size_t>(index)] == 29);

    World world;
    TerrainGenerator generator(config);
    generator.generateChunk(world, -2, 0);
    const auto highest = [&](int x, int z) {
        for (int y = 255; y >= 0; --y)
            if (blockId(world.getBlock(x, y, z)) != 0) return y;
        return -1;
    };
    assert(highest(-32, 0) == 77);
    assert(highest(-24, 8) == 75);
    assert(highest(-17, 15) == 68);
    assert(world.getBlock(-24, 75, 8) == block(BlockId::Grass));
    assert(world.getBlock(-24, 74, 8) == block(BlockId::Dirt));
}

void testReferenceStructureSeeds() {
    // Vanilla MapGenBase/MapGenStructure fixture for seed "Blockcraft". This
    // catches signed overflow or a missing pre-spawn nextInt() in start tests.
    WorldConfig config;
    config.seed = -1011332653LL;
    StructureGenerator structures(config);
    assert(structures.isStructureStart(-4, 13));
    assert(!structures.isStructureStart(0, 0));
    config.generateStructures = false;
    StructureGenerator disabled(config);
    assert(!disabled.isStructureStart(-4, 13));
}

void testWorldTypesAndOptions() {
    assert(generatorOptionInt(R"({"fixedBiome":4})", "fixedBiome", -1) == 4);
    assert(generatorOptionBool(R"({"useCaves":false})", "useCaves", true) == false);

    WorldConfig flat;
    flat.seed = 1;
    flat.worldType = WorldType::Flat;
    flat.generatorOptions = "3;1*minecraft:bedrock,3*minecraft:stone,1*minecraft:grass;4;village";
    World flatWorld;
    TerrainGenerator flatGenerator(flat);
    flatGenerator.generateChunk(flatWorld, 0, 0);
    assert(flatWorld.getBlock(0, 0, 0) == block(BlockId::Bedrock));
    assert(flatWorld.getBlock(0, 3, 0) == block(BlockId::Stone));
    assert(flatWorld.getBlock(0, 4, 0) == block(BlockId::Grass));
    assert(flatWorld.findChunk(0, 0)->biome(0, 0) == 4);

    WorldConfig customized;
    customized.seed = 1;
    customized.worldType = WorldType::Customized;
    customized.generatorOptions = R"({"fixedBiome":2})";
    BiomeProvider customizedProvider(customized);
    const std::vector<std::int32_t> fixed = customizedProvider.getBiomes(0, 0, 8, 8);
    assert(std::all_of(fixed.begin(), fixed.end(), [](int biomeId) { return biomeId == 2; }));

    // These point samples are 1.12 biome-layer fixtures. They exercise signed
    // seeds and distant negative coordinates, where Java overflow mistakes are
    // most likely to appear.
    struct BiomeFixture {
        std::int64_t seed;
        std::array<int, 3> expected;
    };
    constexpr std::array fixtures{
        BiomeFixture{0, {4, 7, 0}},
        BiomeFixture{1, {0, 1, 2}},
        BiomeFixture{-123456789, {19, 3, 5}},
        BiomeFixture{9876543212345LL, {6, 130, 0}}
    };
    constexpr std::array<std::array<int, 2>, 3> points{{{{0, 0}}, {{1000, -1000}}, {{-2048, 3072}}}};
    for (const BiomeFixture& fixture : fixtures) {
        WorldConfig normal;
        normal.seed = fixture.seed;
        BiomeProvider provider(normal);
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto value = provider.getBiomes(points[index][0], points[index][1], 1, 1);
            assert(value.front() == fixture.expected[index]);
        }
    }

    WorldConfig large;
    large.seed = 1;
    large.worldType = WorldType::LargeBiomes;
    BiomeProvider largeProvider(large);
    assert(largeProvider.getBiomes(1000, -1000, 1, 1).front() == 3);

    WorldConfig default11;
    default11.seed = 1;
    default11.worldType = WorldType::Default11;
    BiomeProvider default11Provider(default11);
    assert(default11Provider.getBiomes(800, -10000, 1, 1).front() == 4);

    WorldConfig debug;
    debug.seed = 1;
    debug.worldType = WorldType::DebugAllBlockStates;
    World debugWorld;
    TerrainGenerator(debug).generateChunk(debugWorld, 0, 0);
    assert(debugWorld.getBlock(0, 60, 0) == block(BlockId::Barrier));
    assert(blockId(debugWorld.getBlock(3, 70, 1)) != 0);

    WorldConfig normalMountains;
    normalMountains.seed = -123456789;
    World normalWorld;
    TerrainGenerator(normalMountains).generateChunk(normalWorld, 62, -63);
    WorldConfig amplified = normalMountains;
    amplified.worldType = WorldType::Amplified;
    World amplifiedWorld;
    TerrainGenerator(amplified).generateChunk(amplifiedWorld, 62, -63);
    bool different = false;
    for (int y = 0; y < chunkHeight && !different; ++y)
        different = normalWorld.getBlock(1000, y, -1000) != amplifiedWorld.getBlock(1000, y, -1000);
    assert(different);
}

void testFancyCloudGeometry() {
    const std::vector<EnvironmentVertex> vertices = buildFancyCloudGeometry();
    // 64 top/bottom pairs plus the four vanilla side-strip groups.
    assert(vertices.size() == 8448);
    assert(vertices.size() / 3 == 2816);
}

void testVanillaEnvironment() {
    assert(std::abs(Environment::celestialAngle(6000.0) - 0.0F) < 0.0001F);
    assert(std::abs(Environment::celestialAngle(18000.0) - 0.5F) < 0.0001F);
    assert(Environment::starBrightness(0.0F) == 0.0F);
    assert(Environment::starBrightness(0.5F) == 0.5F);

    WorldConfig config;
    config.seed = 1;
    config.initialWorldTime = 6000;
    config.dayCycleSeconds = 600.0;
    config.weatherOverride = WeatherOverride::Thunder;
    World world;
    Chunk& chunk = world.ensureChunk(0, 0);
    chunk.setBiome(0, 0, 1);
    Environment environment(config);
    for (int tick = 0; tick < 101; ++tick) environment.tick(world);
    assert(environment.worldTime() == 6202.0);
    EnvironmentFrame frame = environment.sample(world, {0.5F, 70.0F, 0.5F},
                                                  {0.0F, 0.0F, -1.0F}, 0.0F);
    assert(frame.rainStrength > 0.99F);
    assert(frame.thunderStrength > 0.99F);

    world.setBlock(0, 70, 0, block(BlockId::Water));
    frame = environment.sample(world, {0.5F, 70.5F, 0.5F}, {0.0F, 0.0F, -1.0F}, 0.0F);
    assert(frame.underwater && frame.fogMode == FogMode::Exponential);
    assert(frame.fogDensity == 0.1F);
    world.setBlock(0, 70, 0, block(BlockId::Lava));
    frame = environment.sample(world, {0.5F, 70.5F, 0.5F}, {0.0F, 0.0F, -1.0F}, 0.0F);
    assert(frame.inLava && frame.fogDensity == 2.0F);
}

} // namespace

int main() {
    testBlockStatePacking();
    testChunkSections();
    testSingleBlockSkylightShadow();
    testSynchronousEditLighting();
    testNegativeCoordinates();
    testMeshing();
    testStage2RenderingCoverage();
    testFluidRendering();
    testPartialModelAo();
    testMeshingSnapshotHalo();
    testWorldChunkTransfer();
    testThreadPool();
    testThreadedChunkPriming();
    testThreadedPrefetchCaching();
    testAtlasInsetAndGenerator();
    testVanillaBlockHardness();
    testPlayerPhysicsAndFlight();
    testJavaCompatibility();
    testReferenceTerrainFixture();
    testReferenceStructureSeeds();
    testWorldTypesAndOptions();
    testFancyCloudGeometry();
    testVanillaEnvironment();
    std::cout << "All Blockcraft core tests passed.\n";
    return 0;
}
