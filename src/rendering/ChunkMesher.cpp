#include "rendering/ChunkMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "rendering/BiomeColors.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/ModelLoader.hpp"
#include "rendering/TextureAtlasData.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"

namespace {

struct Direction { int x, y, z; };

struct VertexLighting {
    float ambientOcclusion = 1.0F;
    float sky = 15.0F;
    float block = 0.0F;
};

constexpr std::array<Direction, 6> directions = {{{0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}}};
// FaceBakery#getFaceBrightness in Minecraft 1.12.2.
constexpr std::array<float, 6> faceBrightness = {0.5F, 1.0F, 0.8F, 0.8F, 0.6F, 0.6F};

Direction direction(Face face) { return directions[static_cast<std::size_t>(face)]; }

bool isWater(BlockId id) { return id == BlockId::Water || id == BlockId::FlowingWater; }
bool isLava(BlockId id) { return id == BlockId::Lava || id == BlockId::FlowingLava; }

bool shouldRenderFace(BlockState current, BlockState neighbor) {
    if (blockId(neighbor) == 0) return true;
    const auto currentId = static_cast<BlockId>(blockId(current));
    const auto neighborId = static_cast<BlockId>(blockId(neighbor));

    // BlockBreakable suppresses faces between equal transparent blocks.
    if ((currentId == BlockId::Glass && neighborId == BlockId::Glass) ||
        (currentId == BlockId::StainedGlass && neighborId == BlockId::StainedGlass &&
         blockMetadata(current) == blockMetadata(neighbor)) ||
        (currentId == BlockId::Ice && neighborId == BlockId::Ice)) return false;
    if ((isWater(currentId) && isWater(neighborId)) ||
        (isLava(currentId) && isLava(neighborId))) return false;
    return !BlockRegistry::get(neighbor).opaque;
}

bool normalCube(BlockState state) {
    const BlockDefinition& definition = BlockRegistry::get(state);
    return definition.opaque && definition.fullCube;
}

float aoValue(BlockState state) {
    // Block#getAmbientOcclusionLightValue: normal cubes use 0.2, others 1.0.
    return normalCube(state) ? 0.2F : 1.0F;
}

VertexLighting cornerLighting(const SectionSnapshot& snapshot, Face face,
                              int x, int y, int z, const glm::vec3& corner) {
    const Direction normal = direction(face);
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

    const bool firstOutwardOpaque = normalCube(snapshot.get(
        baseX + first.x + normal.x, baseY + first.y + normal.y, baseZ + first.z + normal.z));
    const bool secondOutwardOpaque = normalCube(snapshot.get(
        baseX + second.x + normal.x, baseY + second.y + normal.y, baseZ + second.z + normal.z));

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
        return value.sky == 0.0F && value.block == 0.0F ? base : value;
    };
    const LightSample firstLight = sampleOrBase(baseX + first.x, baseY + first.y, baseZ + first.z);
    const LightSample secondLight = sampleOrBase(baseX + second.x, baseY + second.y, baseZ + second.z);
    const LightSample cornerLight = sampleOrBase(cornerX, cornerY, cornerZ);
    return {
        ao,
        (base.sky + firstLight.sky + secondLight.sky + cornerLight.sky) * 0.25F,
        (base.block + firstLight.block + secondLight.block + cornerLight.block) * 0.25F
    };
}

VertexLighting vertexLighting(const SectionSnapshot& snapshot, Face face,
                              int x, int y, int z, const glm::vec3& corner,
                              bool smoothAo) {
    const Direction normal = direction(face);
    if (!smoothAo) {
        const int sampleX = x + normal.x;
        const int sampleY = y + normal.y;
        const int sampleZ = z + normal.z;
        float sky = static_cast<float>(snapshot.sky(sampleX, sampleY, sampleZ));
        float block = static_cast<float>(snapshot.block(sampleX, sampleY, sampleZ));
        if (sky == 0.0F && block == 0.0F) {
            sky = static_cast<float>(snapshot.sky(x, y, z));
            block = static_cast<float>(snapshot.block(x, y, z));
        }
        return {1.0F, sky, block};
    }

    // BlockModelRenderer's AmbientOcclusionFace interpolates the four full-face
    // corner samples for partial model quads (slabs, stairs, cauldrons, etc.).
    // Sampling only the nearest corner causes visible AO steps on sub-block
    // geometry. Reconstruct that weighting directly in face-local coordinates.
    float u = 0.0F;
    float v = 0.0F;
    glm::vec3 c00 = corner, c10 = corner, c01 = corner, c11 = corner;
    if (face == Face::Up || face == Face::Down) {
        u = std::clamp(corner.x, 0.0F, 1.0F);
        v = std::clamp(corner.z, 0.0F, 1.0F);
        c00.x = 0; c00.z = 0; c10.x = 1; c10.z = 0;
        c01.x = 0; c01.z = 1; c11.x = 1; c11.z = 1;
    } else if (face == Face::North || face == Face::South) {
        u = std::clamp(corner.x, 0.0F, 1.0F);
        v = std::clamp(corner.y, 0.0F, 1.0F);
        c00.x = 0; c00.y = 0; c10.x = 1; c10.y = 0;
        c01.x = 0; c01.y = 1; c11.x = 1; c11.y = 1;
    } else {
        u = std::clamp(corner.z, 0.0F, 1.0F);
        v = std::clamp(corner.y, 0.0F, 1.0F);
        c00.z = 0; c00.y = 0; c10.z = 1; c10.y = 0;
        c01.z = 0; c01.y = 1; c11.z = 1; c11.y = 1;
    }

    const VertexLighting a = cornerLighting(snapshot, face, x, y, z, c00);
    const VertexLighting b = cornerLighting(snapshot, face, x, y, z, c10);
    const VertexLighting c = cornerLighting(snapshot, face, x, y, z, c01);
    const VertexLighting d = cornerLighting(snapshot, face, x, y, z, c11);
    const auto interpolate = [=](float av, float bv, float cv, float dv) {
        const float bottom = av + (bv - av) * u;
        const float top = cv + (dv - cv) * u;
        return bottom + (top - bottom) * v;
    };
    return {
        interpolate(a.ambientOcclusion, b.ambientOcclusion, c.ambientOcclusion, d.ambientOcclusion),
        interpolate(a.sky, b.sky, c.sky, d.sky),
        interpolate(a.block, b.block, c.block, d.block)
    };
}

std::array<float, 3> rgb(std::uint32_t color) {
    return {
        static_cast<float>((color >> 16U) & 255U) / 255.0F,
        static_cast<float>((color >> 8U) & 255U) / 255.0F,
        static_cast<float>(color & 255U) / 255.0F
    };
}

std::array<float, 3> tintFor(BlockState state, int tintIndex,
                             const SectionSnapshot& snapshot, int x, int y, int z) {
    if (tintIndex < 0) return {1.0F, 1.0F, 1.0F};
    const auto id = static_cast<BlockId>(blockId(state));
    const int biome = snapshot.biome(x, z);
    const int worldX = snapshot.worldX(x);
    const int worldY = snapshot.worldY(y);
    const int worldZ = snapshot.worldZ(z);

    switch (id) {
        case BlockId::Grass:
        case BlockId::TallGrass:
        case BlockId::Reeds:
            return rgb(BiomeColors::grass(biome, worldX, worldY, worldZ));
        case BlockId::Leaves:
            if ((blockMetadata(state) & 3U) == 1U) return rgb(6396257U); // pine
            if ((blockMetadata(state) & 3U) == 2U) return rgb(8431445U); // birch
            return rgb(BiomeColors::foliage(biome, worldX, worldY, worldZ));
        case BlockId::Leaves2:
        case BlockId::Vine:
            return rgb(BiomeColors::foliage(biome, worldX, worldY, worldZ));
        case BlockId::Waterlily:
            return rgb(2129968U);
        case BlockId::Water:
        case BlockId::FlowingWater:
            return rgb(BiomeProvider::definition(biome).waterColor);
        case BlockId::RedstoneWire: {
            const float power = static_cast<float>(blockMetadata(state) & 15U) / 15.0F;
            float red = power * 0.6F + 0.4F;
            if ((blockMetadata(state) & 15U) == 0U) red = 0.3F;
            const float green = std::max(0.0F, power * power * 0.7F - 0.5F);
            const float blue = std::max(0.0F, power * power * 0.6F - 0.7F);
            return {red, green, blue};
        }
        default:
            return {1.0F, 1.0F, 1.0F};
    }
}

void appendQuadIndices(MeshData& mesh, std::uint32_t first, bool reverse = false) {
    if (!reverse)
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    else
        mesh.indices.insert(mesh.indices.end(), {first + 2, first + 1, first, first + 3, first + 2, first});
}

void addBakedQuad(MeshData& mesh, const SectionSnapshot& snapshot, BlockState state,
                  const BakedBlockModel& model, const BakedModelQuad& quad,
                  int x, int y, int z, const BakedModelQuad* overlayQuad = nullptr) {
    if (quad.cullFace) {
        const Direction offset = direction(*quad.cullFace);
        if (!shouldRenderFace(state, snapshot.get(x + offset.x, y + offset.y, z + offset.z))) return;
    }

    // Vanilla grass uses two exactly coplanar model elements on each side:
    // grass_side plus a biome-tinted grass_side_overlay. Keeping them as two
    // independent depth-writing quads makes the overlay dependent on equal-
    // depth rasterization. Collapse that specific layered model into the
    // vertex format's base+overlay pair so the result is deterministic and the
    // tint applies only to the overlay, matching the 1.12.2 resource model.
    const int tintIndex = overlayQuad != nullptr ? overlayQuad->tintIndex : quad.tintIndex;
    const std::array<float, 3> tint = tintFor(state, tintIndex, snapshot, x, y, z);
    const bool smoothAo = model.ambientOcclusion && BlockRegistry::get(state).lightValue == 0;
    const float directionalShade = quad.shade ? faceBrightness[static_cast<std::size_t>(quad.face)] : 1.0F;
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t index = 0; index < 4; ++index) {
        const VertexLighting lighting = vertexLighting(snapshot, quad.face, x, y, z,
                                                        quad.positions[index], smoothAo);
        const glm::vec2 overlayUv = overlayQuad != nullptr
            ? overlayQuad->uvs[index] : glm::vec2(0.0F);
        mesh.vertices.push_back({
            x + quad.positions[index].x,
            y + quad.positions[index].y,
            z + quad.positions[index].z,
            quad.uvs[index].x, quad.uvs[index].y,
            tint[0], tint[1], tint[2],
            overlayUv.x, overlayUv.y, overlayQuad != nullptr ? 1.0F : 0.0F,
            directionalShade * lighting.ambientOcclusion,
            lighting.sky, lighting.block
        });
    }
    appendQuadIndices(mesh, first);
}

bool sameBakedQuadSurface(const BakedModelQuad& first, const BakedModelQuad& second) {
    if (first.face != second.face || first.cullFace != second.cullFace) return false;
    constexpr float epsilon = 1.0E-6F;
    for (std::size_t index = 0; index < first.positions.size(); ++index) {
        const glm::vec3 delta = first.positions[index] - second.positions[index];
        if (std::abs(delta.x) > epsilon || std::abs(delta.y) > epsilon ||
            std::abs(delta.z) > epsilon) return false;
    }
    return true;
}

std::array<glm::vec3, 4> legacyFaceCorners(Face face) {
    switch (face) {
        case Face::Down: return {{{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}}};
        case Face::Up: return {{{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}}};
        case Face::North: return {{{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}}};
        case Face::South: return {{{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}}};
        case Face::West: return {{{0,0,0}, {0,0,1}, {0,1,1}, {0,1,0}}};
        case Face::East: return {{{1,0,1}, {1,0,0}, {1,1,0}, {1,1,1}}};
    }
    return {};
}

void addLegacyQuad(MeshData& mesh, const SectionSnapshot& snapshot,
                   const BlockRenderResources& resources, BlockState state,
                   int x, int y, int z, const std::array<glm::vec3, 4>& corners,
                   Face face, bool doubleSided = false) {
    const AtlasBounds uv = resources.atlas().sprite(BlockRegistry::texture(state, face)).bounds;
    const std::array<glm::vec2, 4> uvs = {{{uv.u0, uv.v1}, {uv.u1, uv.v1},
                                            {uv.u1, uv.v0}, {uv.u0, uv.v0}}};
    const bool tinted = BlockRegistry::isTinted(state, face);
    const auto tint = tinted ? tintFor(state, 0, snapshot, x, y, z)
                             : std::array<float, 3>{1.0F, 1.0F, 1.0F};
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t index = 0; index < 4; ++index) {
        const VertexLighting lighting = vertexLighting(snapshot, face, x, y, z, corners[index],
                                                        BlockRegistry::get(state).lightValue == 0);
        mesh.vertices.push_back({
            x + corners[index].x, y + corners[index].y, z + corners[index].z,
            uvs[index].x, uvs[index].y,
            tint[0], tint[1], tint[2], 0.0F, 0.0F, 0.0F,
            faceBrightness[static_cast<std::size_t>(face)] * lighting.ambientOcclusion,
            lighting.sky, lighting.block
        });
    }
    appendQuadIndices(mesh, first);
    if (doubleSided) appendQuadIndices(mesh, first, true);
}

void addLegacyCube(MeshData& mesh, const SectionSnapshot& snapshot,
                   const BlockRenderResources& resources, BlockState state,
                   int x, int y, int z) {
    for (std::size_t faceIndex = 0; faceIndex < directions.size(); ++faceIndex) {
        const Direction offset = directions[faceIndex];
        if (!shouldRenderFace(state, snapshot.get(x + offset.x, y + offset.y, z + offset.z))) continue;
        const Face face = static_cast<Face>(faceIndex);
        addLegacyQuad(mesh, snapshot, resources, state, x, y, z,
                      legacyFaceCorners(face), face);
    }
}

void addLegacyNonCube(MeshData& mesh, const SectionSnapshot& snapshot,
                      const BlockRenderResources& resources, BlockState state,
                      int x, int y, int z) {
    const BlockShape shape = BlockRegistry::get(state).shape;
    if (shape == BlockShape::Flat) {
        addLegacyQuad(mesh, snapshot, resources, state, x, y, z,
            {{{0,0.015625F,1}, {1,0.015625F,1}, {1,0.015625F,0}, {0,0.015625F,0}}}, Face::Up, true);
        return;
    }
    if (shape == BlockShape::Cross) {
        addLegacyQuad(mesh, snapshot, resources, state, x, y, z,
            {{{0.14645F,0,0.14645F}, {0.85355F,0,0.85355F},
              {0.85355F,1,0.85355F}, {0.14645F,1,0.14645F}}}, Face::Up, true);
        addLegacyQuad(mesh, snapshot, resources, state, x, y, z,
            {{{0.14645F,0,0.85355F}, {0.85355F,0,0.14645F},
              {0.85355F,1,0.14645F}, {0.14645F,1,0.85355F}}}, Face::Up, true);
        return;
    }

    // This path exists only for built-ins or resources that have no JSON block
    // model. Stage 2 model-backed blocks use their vanilla elements instead.
    addLegacyCube(mesh, snapshot, resources, state, x, y, z);
}

bool sameFluid(BlockState state, bool lava) {
    const auto id = static_cast<BlockId>(blockId(state));
    return lava ? isLava(id) : isWater(id);
}

float liquidHeightPercent(int level) {
    if (level >= 8) level = 0;
    return static_cast<float>(level + 1) / 9.0F;
}

int renderedFluidDepth(BlockState state, bool lava) {
    if (!sameFluid(state, lava)) return -1;
    const int level = blockMetadata(state) & 15U;
    return level >= 8 ? 0 : level;
}

float fluidCornerHeight(const SectionSnapshot& snapshot, int x, int y, int z, bool lava) {
    int samples = 0;
    float accumulated = 0.0F;
    for (int index = 0; index < 4; ++index) {
        const int sx = x - (index & 1);
        const int sz = z - ((index >> 1) & 1);
        if (sameFluid(snapshot.get(sx, y + 1, sz), lava)) return 1.0F;
        const BlockState state = snapshot.get(sx, y, sz);
        if (!sameFluid(state, lava)) {
            if (!normalCube(state)) {
                accumulated += 1.0F;
                ++samples;
            }
            continue;
        }
        const int level = blockMetadata(state) & 15U;
        if (level >= 8 || level == 0) {
            accumulated += liquidHeightPercent(level) * 10.0F;
            samples += 10;
        }
        accumulated += liquidHeightPercent(level);
        ++samples;
    }
    return samples == 0 ? 1.0F : 1.0F - accumulated / static_cast<float>(samples);
}

float fluidSlopeAngle(const SectionSnapshot& snapshot, BlockState state,
                      int x, int y, int z, bool lava) {
    const int currentDepth = renderedFluidDepth(state, lava);
    double flowX = 0.0;
    double flowZ = 0.0;
    constexpr std::array<Direction, 4> horizontal = {{{0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}}};
    for (const Direction offset : horizontal) {
        const BlockState neighbor = snapshot.get(x + offset.x, y, z + offset.z);
        int depth = renderedFluidDepth(neighbor, lava);
        if (depth < 0) {
            if (!normalCube(neighbor)) {
                depth = renderedFluidDepth(snapshot.get(x + offset.x, y - 1, z + offset.z), lava);
                if (depth >= 0) {
                    const int delta = depth - (currentDepth - 8);
                    flowX += static_cast<double>(offset.x * delta);
                    flowZ += static_cast<double>(offset.z * delta);
                }
            }
        } else {
            const int delta = depth - currentDepth;
            flowX += static_cast<double>(offset.x * delta);
            flowZ += static_cast<double>(offset.z * delta);
        }
    }
    if (flowX == 0.0 && flowZ == 0.0) return -1000.0F;
    return static_cast<float>(std::atan2(flowZ, flowX) - 1.5707963267948966);
}

VertexLighting fluidLighting(const SectionSnapshot& snapshot, int x, int y, int z) {
    return {1.0F,
        static_cast<float>(std::max(snapshot.sky(x, y, z), snapshot.sky(x, y + 1, z))),
        static_cast<float>(std::max(snapshot.block(x, y, z), snapshot.block(x, y + 1, z)))};
}

void addFluidQuad(MeshData& mesh, const SectionSnapshot& snapshot,
                  const AtlasBounds& bounds, const std::array<glm::vec3, 4>& positions,
                  const std::array<glm::vec2, 4>& modelUvs,
                  const std::array<float, 3>& tint, float shade,
                  int x, int y, int z, bool reverse = false) {
    const VertexLighting lighting = fluidLighting(snapshot, x, y, z);
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t index = 0; index < 4; ++index) {
        mesh.vertices.push_back({
            x + positions[index].x, y + positions[index].y, z + positions[index].z,
            bounds.u(modelUvs[index].x), bounds.v(modelUvs[index].y),
            tint[0], tint[1], tint[2], 0.0F, 0.0F, 0.0F,
            shade, lighting.sky, lighting.block
        });
    }
    appendQuadIndices(mesh, first, reverse);
}

bool shouldRenderFluidFace(const SectionSnapshot& snapshot, int x, int y, int z,
                           Direction offset, bool lava) {
    const BlockState neighbor = snapshot.get(x + offset.x, y + offset.y, z + offset.z);
    if (sameFluid(neighbor, lava)) return false;
    if (offset.y > 0) return true;
    return !BlockRegistry::get(neighbor).opaque;
}

bool renderFluidBackFace(const SectionSnapshot& snapshot, int x, int y, int z, bool lava) {
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const BlockState state = snapshot.get(x + dx, y, z + dz);
            if (!sameFluid(state, lava) && !BlockRegistry::get(state).fullCube) return true;
        }
    }
    return false;
}

bool addFluid(SectionMeshData& result, const SectionSnapshot& snapshot,
              const BlockRenderResources& resources, BlockState state,
              int x, int y, int z) {
    const auto id = static_cast<BlockId>(blockId(state));
    if (!isWater(id) && !isLava(id)) return false;
    const bool lava = isLava(id);
    MeshData& mesh = result[static_cast<std::size_t>(lava ? RenderLayer::Solid : RenderLayer::Translucent)];
    const auto tint = lava ? std::array<float, 3>{1.0F, 1.0F, 1.0F}
                           : rgb(BiomeProvider::definition(snapshot.biome(x, z)).waterColor);
    const AtlasBounds still = resources.atlas().sprite(
        lava ? "minecraft:blocks/lava_still" : "minecraft:blocks/water_still").bounds;
    const AtlasBounds flow = resources.atlas().sprite(
        lava ? "minecraft:blocks/lava_flow" : "minecraft:blocks/water_flow").bounds;
    const AtlasBounds overlay = resources.atlas().contains("minecraft:blocks/water_overlay")
        ? resources.atlas().sprite("minecraft:blocks/water_overlay").bounds : flow;

    float hNW = fluidCornerHeight(snapshot, x, y, z, lava);
    float hSW = fluidCornerHeight(snapshot, x, y, z + 1, lava);
    float hSE = fluidCornerHeight(snapshot, x + 1, y, z + 1, lava);
    float hNE = fluidCornerHeight(snapshot, x + 1, y, z, lava);

    if (shouldRenderFluidFace(snapshot, x, y, z, {0,1,0}, lava)) {
        constexpr float epsilon = 0.001F;
        hNW -= epsilon; hSW -= epsilon; hSE -= epsilon; hNE -= epsilon;
        const float slope = fluidSlopeAngle(snapshot, state, x, y, z, lava);
        const AtlasBounds& topTexture = slope > -999.0F ? flow : still;
        std::array<glm::vec2, 4> uvs{};
        if (slope < -999.0F) {
            uvs = {{{0,0}, {0,16}, {16,16}, {16,0}}};
        } else {
            const float sinSlope = std::sin(slope) * 0.25F;
            const float cosSlope = std::cos(slope) * 0.25F;
            uvs = {{{8.0F + (-cosSlope - sinSlope) * 16.0F, 8.0F + (-cosSlope + sinSlope) * 16.0F},
                    {8.0F + (-cosSlope + sinSlope) * 16.0F, 8.0F + ( cosSlope + sinSlope) * 16.0F},
                    {8.0F + ( cosSlope + sinSlope) * 16.0F, 8.0F + ( cosSlope - sinSlope) * 16.0F},
                    {8.0F + ( cosSlope - sinSlope) * 16.0F, 8.0F + (-cosSlope - sinSlope) * 16.0F}}};
        }
        const std::array<glm::vec3, 4> top = {{{0,hNW,0}, {0,hSW,1}, {1,hSE,1}, {1,hNE,0}}};
        addFluidQuad(mesh, snapshot, topTexture, top, uvs, tint, 1.0F, x, y, z);
        if (renderFluidBackFace(snapshot, x, y + 1, z, lava))
            addFluidQuad(mesh, snapshot, topTexture, top, uvs, tint, 1.0F, x, y, z, true);
    }

    if (shouldRenderFluidFace(snapshot, x, y, z, {0,-1,0}, lava)) {
        addFluidQuad(mesh, snapshot, still,
            {{{0,0,1}, {0,0,0}, {1,0,0}, {1,0,1}}},
            {{{0,16}, {0,0}, {16,0}, {16,16}}},
            tint, 0.5F, x, y, z);
    }

    struct Side { Direction direction; float first; float second; };
    constexpr std::array<Side, 4> sides = {{
        {{0,0,-1}, 0.0F, 1.0F}, {{0,0,1}, 2.0F, 3.0F},
        {{-1,0,0}, 1.0F, 0.0F}, {{1,0,0}, 3.0F, 2.0F}
    }};
    const std::array<float, 4> heights = {hNW, hSW, hSE, hNE};
    for (std::size_t sideIndex = 0; sideIndex < sides.size(); ++sideIndex) {
        const Direction offset = sides[sideIndex].direction;
        if (!shouldRenderFluidFace(snapshot, x, y, z, offset, lava)) continue;
        const int firstIndex = static_cast<int>(sides[sideIndex].first);
        const int secondIndex = static_cast<int>(sides[sideIndex].second);
        const float firstHeight = heights[static_cast<std::size_t>(firstIndex)];
        const float secondHeight = heights[static_cast<std::size_t>(secondIndex)];
        const BlockState neighbor = snapshot.get(x + offset.x, y, z + offset.z);
        const bool waterOverlay = !lava && (static_cast<BlockId>(blockId(neighbor)) == BlockId::Glass ||
            static_cast<BlockId>(blockId(neighbor)) == BlockId::StainedGlass);
        const AtlasBounds& sideTexture = waterOverlay ? overlay : flow;
        std::array<glm::vec3, 4> positions{};
        if (sideIndex == 0) positions = {{{0,firstHeight,0.001F}, {1,secondHeight,0.001F}, {1,0,0.001F}, {0,0,0.001F}}};
        if (sideIndex == 1) positions = {{{1,firstHeight,0.999F}, {0,secondHeight,0.999F}, {0,0,0.999F}, {1,0,0.999F}}};
        if (sideIndex == 2) positions = {{{0.001F,firstHeight,1}, {0.001F,secondHeight,0}, {0.001F,0,0}, {0.001F,0,1}}};
        if (sideIndex == 3) positions = {{{0.999F,firstHeight,0}, {0.999F,secondHeight,1}, {0.999F,0,1}, {0.999F,0,0}}};
        const std::array<glm::vec2, 4> uvs = {{{0,(1.0F-firstHeight)*8.0F}, {8,(1.0F-secondHeight)*8.0F}, {8,8}, {0,8}}};
        const float shade = sideIndex < 2 ? 0.8F : 0.6F;
        addFluidQuad(mesh, snapshot, sideTexture, positions, uvs, tint, shade, x, y, z);
        if (!waterOverlay) addFluidQuad(mesh, snapshot, sideTexture, positions, uvs, tint, shade, x, y, z, true);
    }
    return true;
}

bool addJsonModel(SectionMeshData& result, const SectionSnapshot& snapshot,
                  const BlockRenderResources& resources, BlockState state,
                  int x, int y, int z) {
    const RelativeBlockLookup lookup = [&](int dx, int dy, int dz) {
        return snapshot.get(x + dx, y + dy, z + dz);
    };
    const BlockModelState modelState = resolveBlockModelState(state, lookup);
    if (!resources.models().hasBlockState(modelState.resourceName)) return false;

    const std::int64_t random = blockModelPositionRandom(
        snapshot.worldX(x), snapshot.worldY(y), snapshot.worldZ(z));
    const std::vector<const BakedBlockModel*> models = resources.models().select(modelState, random);
    if (models.empty()) return false;

    MeshData& mesh = result[static_cast<std::size_t>(BlockRegistry::get(state).layer)];
    bool emitted = false;
    for (const BakedBlockModel* model : models) {
        if (model == nullptr) continue;
        const bool grass = static_cast<BlockId>(blockId(state)) == BlockId::Grass;
        std::vector<bool> consumed(model->quads.size(), false);
        for (std::size_t quadIndex = 0; quadIndex < model->quads.size(); ++quadIndex) {
            if (consumed[quadIndex]) continue;
            const BakedModelQuad& quad = model->quads[quadIndex];
            const BakedModelQuad* overlay = nullptr;

            // assets/minecraft/models/block/grass.json defines the four side
            // overlays as later coplanar, tinted quads. Pair only this vanilla
            // grass pattern; arbitrary resource-pack layered models remain
            // independent quads.
            if (grass && quad.tintIndex < 0) {
                for (std::size_t candidateIndex = quadIndex + 1;
                     candidateIndex < model->quads.size(); ++candidateIndex) {
                    if (consumed[candidateIndex]) continue;
                    const BakedModelQuad& candidate = model->quads[candidateIndex];
                    if (candidate.tintIndex < 0 || !sameBakedQuadSurface(quad, candidate)) continue;
                    overlay = &candidate;
                    consumed[candidateIndex] = true;
                    break;
                }
            }

            const std::size_t before = mesh.indices.size();
            addBakedQuad(mesh, snapshot, state, *model, quad, x, y, z, overlay);
            emitted = emitted || mesh.indices.size() != before;
        }
    }
    return emitted || !models.empty();
}

} // namespace

SectionSnapshot ChunkMesher::capture(const World& world, int chunkX, int sectionY, int chunkZ) {
    SectionSnapshot snapshot;
    snapshot.chunkX = chunkX;
    snapshot.sectionY = sectionY;
    snapshot.chunkZ = chunkZ;
    const int originY = sectionY * sectionSize;
    const Chunk* center = world.findChunk(chunkX, chunkZ);
    const ChunkSection* section = center == nullptr ? nullptr : center->section(sectionY);
    if (section == nullptr) return snapshot;

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
            snapshot.biomes[SectionSnapshot::columnIndex(x, z)] = source ? source->biome(localX, localZ) : 1;
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

SectionMeshData ChunkMesher::build(const SectionSnapshot& snapshot,
                                   const BlockRenderResources& resources) {
    SectionMeshData result;
    for (int y = 0; y < sectionSize; ++y) {
        for (int z = 0; z < sectionSize; ++z) {
            for (int x = 0; x < sectionSize; ++x) {
                const BlockState state = snapshot.get(x, y, z);
                if (blockId(state) == 0) continue;
                if (addFluid(result, snapshot, resources, state, x, y, z)) continue;
                if (addJsonModel(result, snapshot, resources, state, x, y, z)) continue;

                const BlockDefinition& definition = BlockRegistry::get(state);
                MeshData& mesh = result[static_cast<std::size_t>(definition.layer)];
                if (definition.fullCube) addLegacyCube(mesh, snapshot, resources, state, x, y, z);
                else addLegacyNonCube(mesh, snapshot, resources, state, x, y, z);
            }
        }
    }
    return result;
}

SectionMeshData ChunkMesher::build(const World& world, int chunkX, int sectionY, int chunkZ,
                                   const BlockRenderResources& resources) {
    return build(capture(world, chunkX, sectionY, chunkZ), resources);
}
