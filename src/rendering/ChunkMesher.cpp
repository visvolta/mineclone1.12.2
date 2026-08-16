#include "rendering/ChunkMesher.hpp"

#include <algorithm>
#include <array>

#include "rendering/AtlasLayout.hpp"
#include "world/World.hpp"

namespace {

struct Point { float x, y, z; };
struct Direction { int x, y, z; };

struct VertexLighting {
    float ambientOcclusion = 1.0F;
    float sky = 15.0F;
    float block = 0.0F;
};

constexpr std::array<std::array<Point, 4>, 6> faceCorners = {{
    {{{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}}}, // down
    {{{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}}}, // up
    {{{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}}}, // north
    {{{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}}}, // south
    {{{0,0,0}, {0,0,1}, {0,1,1}, {0,1,0}}}, // west
    {{{1,0,1}, {1,0,0}, {1,1,0}, {1,1,1}}}  // east
}};

constexpr std::array<Direction, 6> directions = {{{0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}}};
// BlockModelRenderer.EnumNeighborInfo shade weights in 1.12.2.
constexpr std::array<float, 6> faceBrightness = {0.5F, 1.0F, 0.8F, 0.8F, 0.6F, 0.6F};

bool shouldRenderFace(BlockState current, BlockState neighbor) {
    if (blockId(neighbor) == 0) return true;
    const auto currentId = static_cast<BlockId>(blockId(current));
    const auto neighborId = static_cast<BlockId>(blockId(neighbor));
    if (currentId == BlockId::Glass && neighborId == BlockId::Glass) return false;
    const bool currentWater = currentId == BlockId::Water || currentId == BlockId::FlowingWater;
    const bool neighborWater = neighborId == BlockId::Water || neighborId == BlockId::FlowingWater;
    const bool currentLava = currentId == BlockId::Lava || currentId == BlockId::FlowingLava;
    const bool neighborLava = neighborId == BlockId::Lava || neighborId == BlockId::FlowingLava;
    if ((currentWater && neighborWater) || (currentLava && neighborLava)) return false;
    return !BlockRegistry::get(neighbor).opaque;
}

bool normalCube(BlockState state) {
    const BlockDefinition& definition = BlockRegistry::get(state);
    return definition.opaque && definition.fullCube;
}

float aoValue(BlockState state) {
    // Block#getAmbientOcclusionLightValue returns 0.2 for a normal cube and
    // 1.0 otherwise in Minecraft 1.12.2.
    return normalCube(state) ? 0.2F : 1.0F;
}

VertexLighting vertexLighting(const SectionSnapshot& snapshot, Face face,
                              int x, int y, int z, const Point& corner) {
    const Direction normal = directions[static_cast<std::size_t>(face)];
    std::array<Direction, 2> tangents{};
    std::size_t tangentIndex = 0;
    if (normal.x == 0) tangents[tangentIndex++] = {corner.x < 0.5F ? -1 : 1, 0, 0};
    if (normal.y == 0) tangents[tangentIndex++] = {0, corner.y < 0.5F ? -1 : 1, 0};
    if (normal.z == 0) tangents[tangentIndex++] = {0, 0, corner.z < 0.5F ? -1 : 1};

    const int baseX = x + normal.x;
    const int baseY = y + normal.y;
    const int baseZ = z + normal.z;
    const Direction first = tangents[0];
    const Direction second = tangents[1];
    const BlockState baseState = snapshot.get(baseX, baseY, baseZ);
    const BlockState firstState = snapshot.get(baseX + first.x, baseY + first.y, baseZ + first.z);
    const BlockState secondState = snapshot.get(baseX + second.x, baseY + second.y, baseZ + second.z);
    // BlockModelRenderer samples translucency one step farther out along the
    // rendered face normal. Sampling the tangent blocks themselves (the old
    // code) incorrectly treated ordinary terrain steps as corner blockers and
    // produced the large black patches seen only on naturally dense terrain.
    const BlockState firstOutwardState = snapshot.get(
        baseX + first.x + normal.x,
        baseY + first.y + normal.y,
        baseZ + first.z + normal.z);
    const BlockState secondOutwardState = snapshot.get(
        baseX + second.x + normal.x,
        baseY + second.y + normal.y,
        baseZ + second.z + normal.z);
    const bool firstOutwardOpaque = normalCube(firstOutwardState);
    const bool secondOutwardOpaque = normalCube(secondOutwardState);

    int cornerX = baseX + first.x + second.x;
    int cornerY = baseY + first.y + second.y;
    int cornerZ = baseZ + first.z + second.z;
    if (firstOutwardOpaque && secondOutwardOpaque) {
        cornerX = baseX + first.x;
        cornerY = baseY + first.y;
        cornerZ = baseZ + first.z;
    }
    const BlockState cornerState = snapshot.get(cornerX, cornerY, cornerZ);
    const float ao = (aoValue(baseState) + aoValue(firstState) +
                      aoValue(secondState) + aoValue(cornerState)) * 0.25F;

    struct LightSample { float sky; float block; };
    const auto sample = [&](int sampleX, int sampleY, int sampleZ) {
        return LightSample{static_cast<float>(snapshot.sky(sampleX, sampleY, sampleZ)),
                           static_cast<float>(snapshot.block(sampleX, sampleY, sampleZ))};
    };
    const LightSample base = sample(baseX, baseY, baseZ);
    const auto sampleOrBase = [&](int sampleX, int sampleY, int sampleZ) {
        const LightSample value = sample(sampleX, sampleY, sampleZ);
        // getAoBrightness substitutes the face value only when the complete
        // packed sky+block light coordinate is zero.
        return value.sky == 0.0F && value.block == 0.0F ? base : value;
    };
    const LightSample firstLight = sampleOrBase(baseX + first.x, baseY + first.y, baseZ + first.z);
    const LightSample secondLight = sampleOrBase(baseX + second.x, baseY + second.y, baseZ + second.z);
    const LightSample cornerLight = sampleOrBase(cornerX, cornerY, cornerZ);
    const float sky = (base.sky + firstLight.sky + secondLight.sky + cornerLight.sky) * 0.25F;
    const float block = (base.block + firstLight.block + secondLight.block + cornerLight.block) * 0.25F;
    return {ao, sky, block};
}

void addFace(MeshData& mesh, const SectionSnapshot& snapshot,
             BlockState state, Face face, int x, int y, int z) {
    const auto id = static_cast<BlockId>(blockId(state));
    const bool side = face == Face::North || face == Face::South ||
        face == Face::West || face == Face::East;
    const auto aboveId = static_cast<BlockId>(blockId(snapshot.get(x, y + 1, z)));
    const bool snowyGrass = id == BlockId::Grass &&
        (aboveId == BlockId::Snow || aboveId == BlockId::SnowLayer);
    const TextureId texture = snowyGrass && side
        ? TextureId::GrassSideSnowed : BlockRegistry::texture(state, face);
    const AtlasBounds uv = atlasBounds(texture);
    const AtlasBounds overlayUv = atlasBounds(TextureId::GrassSideOverlay);
    const bool overlay = !snowyGrass && BlockRegistry::hasGrassOverlay(state, face);

    float tintR = 1.0F;
    float tintG = 1.0F;
    float tintB = 1.0F;
    if (id == BlockId::Grass && !snowyGrass) {
        tintR = 0.57F; tintG = 0.74F; tintB = 0.35F;
    } else if (id == BlockId::Leaves || id == BlockId::Leaves2) {
        tintR = 0.29F; tintG = 0.65F; tintB = 0.20F;
    }

    const float brightness = faceBrightness[static_cast<std::size_t>(face)];
    if (snowyGrass || (!BlockRegistry::isTinted(state, face) && !overlay)) {
        tintR = tintG = tintB = 1.0F;
    }

    const std::array<std::array<float, 2>, 4> baseUvs = {{{uv.u0, uv.v1}, {uv.u1, uv.v1}, {uv.u1, uv.v0}, {uv.u0, uv.v0}}};
    const std::array<std::array<float, 2>, 4> overlayUvs = {{{overlayUv.u0, overlayUv.v1}, {overlayUv.u1, overlayUv.v1}, {overlayUv.u1, overlayUv.v0}, {overlayUv.u0, overlayUv.v0}}};
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto& corners = faceCorners[static_cast<std::size_t>(face)];

    for (std::size_t i = 0; i < 4; ++i) {
        const VertexLighting lighting = vertexLighting(snapshot, face, x, y, z, corners[i]);
        const float vertexShade = brightness * lighting.ambientOcclusion;
        mesh.vertices.push_back({
            x + corners[i].x, y + corners[i].y, z + corners[i].z,
            baseUvs[i][0], baseUvs[i][1], tintR, tintG, tintB,
            overlayUvs[i][0], overlayUvs[i][1], overlay ? 1.0F : 0.0F,
            vertexShade,
            lighting.sky, lighting.block
        });
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

void addModelQuad(MeshData& mesh, const SectionSnapshot& snapshot, BlockState state,
                  int x, int y, int z, const std::array<Point, 4>& corners,
                  bool doubleSided, float shade = 1.0F, Face textureFace = Face::Up) {
    const AtlasBounds uv = atlasBounds(BlockRegistry::texture(state, textureFace));
    const std::array<std::array<float, 2>, 4> uvs = {{{uv.u0, uv.v1}, {uv.u1, uv.v1},
                                                       {uv.u1, uv.v0}, {uv.u0, uv.v0}}};
    const auto id = static_cast<BlockId>(blockId(state));
    const bool tinted = id == BlockId::TallGrass || id == BlockId::Vine ||
        id == BlockId::Waterlily || id == BlockId::Leaves || id == BlockId::Leaves2;
    const float red = tinted ? 0.48F : 1.0F;
    const float green = tinted ? 0.72F : 1.0F;
    const float blue = tinted ? 0.32F : 1.0F;
    float sky = static_cast<float>(snapshot.sky(x, y, z));
    float block = static_cast<float>(snapshot.block(x, y, z));
    if (sky == 0.0F && block == 0.0F) {
        sky = static_cast<float>(snapshot.sky(x, y + 1, z));
        block = static_cast<float>(snapshot.block(x, y + 1, z));
    }
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t index = 0; index < corners.size(); ++index) {
        mesh.vertices.push_back({x + corners[index].x, y + corners[index].y, z + corners[index].z,
            uvs[index][0], uvs[index][1], red, green, blue,
            0.0F, 0.0F, 0.0F, shade, sky, block});
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    if (doubleSided)
        mesh.indices.insert(mesh.indices.end(), {first + 2, first + 1, first, first + 3, first + 2, first});
}

void addCuboid(MeshData& mesh, const SectionSnapshot& snapshot, BlockState state,
               int x, int y, int z, float x0, float y0, float z0,
               float x1, float y1, float z1) {
    const std::array<std::array<Point, 4>, 6> faces{{
        {{{x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}}},
        {{{x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}}},
        {{{x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}}},
        {{{x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}}},
        {{{x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}}},
        {{{x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}}}
    }};
    for (std::size_t face = 0; face < faces.size(); ++face)
        addModelQuad(mesh, snapshot, state, x, y, z, faces[face], false,
                     faceBrightness[face], static_cast<Face>(face));
}

void addCross(MeshData& mesh, const SectionSnapshot& snapshot,
              BlockState state, int x, int y, int z) {
    addModelQuad(mesh, snapshot, state, x, y, z,
        {{{0.14645F,0,0.14645F}, {0.85355F,0,0.85355F},
          {0.85355F,1,0.85355F}, {0.14645F,1,0.14645F}}}, true);
    addModelQuad(mesh, snapshot, state, x, y, z,
        {{{0.14645F,0,0.85355F}, {0.85355F,0,0.14645F},
          {0.85355F,1,0.14645F}, {0.14645F,1,0.85355F}}}, true);
}

void addVine(MeshData& mesh, const SectionSnapshot& snapshot,
             BlockState state, int x, int y, int z) {
    // BlockVine#getStateFromMeta in 1.12.2 maps bits to horizontal grown
    // faces: south=1, west=2, north=4, east=8. Vanilla's baked vine models
    // place each two-sided sheet 0.8/16 from the corresponding block edge.
    constexpr float nearEdge = 0.05F;
    constexpr float farEdge = 0.95F;
    const std::uint8_t metadata = blockMetadata(state);
    bool renderedSide = false;

    if ((metadata & 0x01U) != 0U) { // south
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{0,0,farEdge}, {1,0,farEdge}, {1,1,farEdge}, {0,1,farEdge}}}, true);
        renderedSide = true;
    }
    if ((metadata & 0x02U) != 0U) { // west
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{nearEdge,0,0}, {nearEdge,0,1}, {nearEdge,1,1}, {nearEdge,1,0}}}, true);
        renderedSide = true;
    }
    if ((metadata & 0x04U) != 0U) { // north
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{1,0,nearEdge}, {0,0,nearEdge}, {0,1,nearEdge}, {1,1,nearEdge}}}, true);
        renderedSide = true;
    }
    if ((metadata & 0x08U) != 0U) { // east
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{farEdge,0,1}, {farEdge,0,0}, {farEdge,1,0}, {farEdge,1,1}}}, true);
        renderedSide = true;
    }

    // UP is an actual-state property rather than metadata. BlockVine enables
    // it when the block above exposes a solid down face; normalCube is the
    // closest representation available in the current Stage 1 block model.
    const bool up = normalCube(snapshot.get(x, y + 1, z));
    if (up) {
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{0,farEdge,1}, {1,farEdge,1}, {1,farEdge,0}, {0,farEdge,0}}}, true);
    }

    // The vanilla blockstate resource maps the otherwise-invalid all-false
    // state to vine_1. Preserve that deterministic fallback rather than
    // silently emitting an invisible non-air block.
    if (!renderedSide && !up) {
        addModelQuad(mesh, snapshot, state, x, y, z,
            {{{0,0,farEdge}, {1,0,farEdge}, {1,1,farEdge}, {0,1,farEdge}}}, true);
    }
}

void addNonCube(MeshData& mesh, const SectionSnapshot& snapshot,
                BlockState state, int x, int y, int z) {
    switch (BlockRegistry::get(state).shape) {
        case BlockShape::Cross:
            addCross(mesh, snapshot, state, x, y, z);
            break;
        case BlockShape::Vine:
            addVine(mesh, snapshot, state, x, y, z);
            break;
        case BlockShape::Flat:
            addModelQuad(mesh, snapshot, state, x, y, z,
                {{{0,0.015625F,1}, {1,0.015625F,1}, {1,0.015625F,0}, {0,0.015625F,0}}}, true);
            break;
        case BlockShape::SnowLayer:
            addCuboid(mesh, snapshot, state, x, y, z, 0, 0, 0, 1,
                      (static_cast<float>(blockMetadata(state) & 7U) + 1.0F) / 8.0F, 1);
            break;
        case BlockShape::Cactus:
            addCuboid(mesh, snapshot, state, x, y, z, 0.0625F, 0, 0.0625F,
                      0.9375F, 1, 0.9375F);
            break;
        case BlockShape::Cube: break;
    }
}

} // namespace

SectionSnapshot ChunkMesher::capture(const World& world, int chunkX, int sectionY, int chunkZ) {
    SectionSnapshot snapshot;
    const int originY = sectionY * sectionSize;
    const Chunk* center = world.findChunk(chunkX, chunkZ);
    const ChunkSection* section = center == nullptr ? nullptr : center->section(sectionY);
    if (section == nullptr) return snapshot;

    // AO needs edge and corner neighbors as well as the six face halos. Cache
    // the 3x3 chunk pointers so the full snapshot performs no map lookups
    // in its inner loops.
    std::array<const Chunk*, 9> neighbors{};
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            neighbors[static_cast<std::size_t>((dz + 1) * 3 + (dx + 1))] =
                world.findChunk(chunkX + dx, chunkZ + dz);

    for (int z = -meshingSnapshotHalo; z < sectionSize + meshingSnapshotHalo; ++z) {
        const int worldZ = chunkZ * chunkSize + z;
        const int relativeChunkZ = World::floorDiv16(worldZ) - chunkZ;
        const int localZ = World::floorMod16(worldZ);
        for (int x = -meshingSnapshotHalo; x < sectionSize + meshingSnapshotHalo; ++x) {
            const int worldX = chunkX * chunkSize + x;
            const int relativeChunkX = World::floorDiv16(worldX) - chunkX;
            const int localX = World::floorMod16(worldX);
            const Chunk* source = neighbors[static_cast<std::size_t>(
                (relativeChunkZ + 1) * 3 + (relativeChunkX + 1))];
            for (int y = -meshingSnapshotHalo; y < sectionSize + meshingSnapshotHalo; ++y) {
                const int worldY = originY + y;
                const std::size_t destination = SectionSnapshot::index(x, y, z);
                if (source == nullptr || worldY < 0 || worldY >= chunkHeight) {
                    snapshot.blocks[destination] = makeBlockState(0);
                    snapshot.skyLight[destination] = worldY >= chunkHeight || source == nullptr ? 15 : 0;
                    snapshot.blockLight[destination] = 0;
                    continue;
                }
                snapshot.blocks[destination] = source->get(localX, worldY, localZ);
                snapshot.skyLight[destination] = source->skyLight(localX, worldY, localZ);
                snapshot.blockLight[destination] = source->blockLight(localX, worldY, localZ);
            }
        }
    }
    return snapshot;
}

SectionMeshData ChunkMesher::build(const SectionSnapshot& snapshot) {
    SectionMeshData result;

    for (int y = 0; y < sectionSize; ++y) {
        for (int z = 0; z < sectionSize; ++z) {
            for (int x = 0; x < sectionSize; ++x) {
                const BlockState state = snapshot.get(x, y, z);
                if (blockId(state) == 0) continue;
                const BlockDefinition& definition = BlockRegistry::get(state);
                MeshData& mesh = result[static_cast<std::size_t>(definition.layer)];
                if (!definition.fullCube) {
                    addNonCube(mesh, snapshot, state, x, y, z);
                    continue;
                }

                for (std::size_t faceIndex = 0; faceIndex < directions.size(); ++faceIndex) {
                    const Direction offset = directions[faceIndex];
                    const BlockState neighbor = snapshot.get(x + offset.x, y + offset.y, z + offset.z);
                    if (shouldRenderFace(state, neighbor)) {
                        addFace(mesh, snapshot, state, static_cast<Face>(faceIndex), x, y, z);
                    }
                }
            }
        }
    }
    return result;
}

SectionMeshData ChunkMesher::build(const World& world, int chunkX, int sectionY, int chunkZ) {
    return build(capture(world, chunkX, sectionY, chunkZ));
}
