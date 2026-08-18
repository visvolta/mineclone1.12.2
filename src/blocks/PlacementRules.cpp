#include "blocks/PlacementRules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_set>

#include "blocks/BlockShape.hpp"
#include "items/ItemRegistry.hpp"
#include "items/ItemStack.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {

enum class Horizontal : std::uint8_t { South = 0, West = 1, North = 2, East = 3 };

constexpr BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

Horizontal horizontalFacing(const glm::vec3& look) {
    if (std::abs(look.x) > std::abs(look.z))
        return look.x > 0.0F ? Horizontal::East : Horizontal::West;
    return look.z > 0.0F ? Horizontal::South : Horizontal::North;
}

Horizontal opposite(Horizontal value) {
    return static_cast<Horizontal>((static_cast<unsigned>(value) + 2U) & 3U);
}

Horizontal rotateY(Horizontal value) {
    // EnumFacing#rotateY: NORTH->EAST->SOUTH->WEST. Our legacy horizontal
    // metadata order is SOUTH(0), WEST(1), NORTH(2), EAST(3), therefore -1.
    return static_cast<Horizontal>((static_cast<unsigned>(value) + 3U) & 3U);
}

Horizontal rotateYCCW(Horizontal value) {
    return static_cast<Horizontal>((static_cast<unsigned>(value) + 1U) & 3U);
}

std::pair<int, int> offset(Horizontal value) {
    switch (value) {
        case Horizontal::South: return {0, 1};
        case Horizontal::West: return {-1, 0};
        case Horizontal::North: return {0, -1};
        case Horizontal::East: return {1, 0};
    }
    return {0, 0};
}

std::uint8_t horizontalMeta(Horizontal value) {
    return static_cast<std::uint8_t>(value);
}

std::uint8_t faceIndex(Face face) {
    switch (face) {
        case Face::Down: return 0;
        case Face::Up: return 1;
        case Face::North: return 2;
        case Face::South: return 3;
        case Face::West: return 4;
        case Face::East: return 5;
    }
    return 0;
}

bool horizontalFace(Face face) {
    return face == Face::North || face == Face::South || face == Face::West || face == Face::East;
}

Horizontal horizontalFromFace(Face face) {
    switch (face) {
        case Face::South: return Horizontal::South;
        case Face::West: return Horizontal::West;
        case Face::North: return Horizontal::North;
        case Face::East: return Horizontal::East;
        default: return Horizontal::North;
    }
}

std::uint8_t stairMeta(Horizontal facing, bool top) {
    std::uint8_t meta = 3;
    switch (facing) {
        case Horizontal::East: meta = 0; break;
        case Horizontal::West: meta = 1; break;
        case Horizontal::South: meta = 2; break;
        case Horizontal::North: meta = 3; break;
    }
    return static_cast<std::uint8_t>(meta | (top ? 4U : 0U));
}

std::uint8_t doorMeta(Horizontal facing, bool open = false) {
    std::uint8_t meta = 3;
    switch (facing) {
        case Horizontal::East: meta = 0; break;
        case Horizontal::South: meta = 1; break;
        case Horizontal::West: meta = 2; break;
        case Horizontal::North: meta = 3; break;
    }
    return static_cast<std::uint8_t>(meta | (open ? 4U : 0U));
}

std::uint8_t cardinalMeta(Horizontal facing) {
    switch (facing) {
        case Horizontal::North: return 2;
        case Horizontal::South: return 3;
        case Horizontal::West: return 4;
        case Horizontal::East: return 5;
    }
    return 2;
}

bool isStair(BlockId id) {
    switch (id) {
        case BlockId::OakStairs: case BlockId::StoneStairs: case BlockId::BrickStairs:
        case BlockId::StoneBrickStairs: case BlockId::NetherBrickStairs: case BlockId::SandstoneStairs:
        case BlockId::SpruceStairs: case BlockId::BirchStairs: case BlockId::JungleStairs:
        case BlockId::QuartzStairs: case BlockId::AcaciaStairs: case BlockId::DarkOakStairs:
        case BlockId::RedSandstoneStairs: case BlockId::PurpurStairs:
            return true;
        default: return false;
    }
}

bool isSingleSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::WoodenSlab ||
           id == BlockId::StoneSlab2 || id == BlockId::PurpurSlab;
}

BlockId doubleSlabFor(BlockId id) {
    switch (id) {
        case BlockId::StoneSlab: return BlockId::DoubleStoneSlab;
        case BlockId::WoodenSlab: return BlockId::DoubleWoodenSlab;
        case BlockId::StoneSlab2: return BlockId::DoubleStoneSlab2;
        case BlockId::PurpurSlab: return BlockId::PurpurDoubleSlab;
        default: return id;
    }
}

std::uint8_t slabTypeMask(BlockId id) {
    if (id == BlockId::StoneSlab || id == BlockId::WoodenSlab) return 7U;
    return 0U;
}

bool matchingSlab(BlockState state, BlockId selected, std::uint8_t damage) {
    if (static_cast<BlockId>(blockId(state)) != selected) return false;
    const std::uint8_t mask = slabTypeMask(selected);
    return (blockMetadata(state) & mask) == (damage & mask);
}

bool isDoor(BlockId id) {
    switch (id) {
        case BlockId::WoodenDoor: case BlockId::IronDoor: case BlockId::SpruceDoor:
        case BlockId::BirchDoor: case BlockId::JungleDoor: case BlockId::AcaciaDoor:
        case BlockId::DarkOakDoor: return true;
        default: return false;
    }
}

bool isWoodDoor(BlockId id) { return isDoor(id) && id != BlockId::IronDoor; }

bool isFenceGate(BlockId id) {
    return id == BlockId::FenceGate || id == BlockId::SpruceFenceGate || id == BlockId::BirchFenceGate ||
           id == BlockId::JungleFenceGate || id == BlockId::DarkOakFenceGate || id == BlockId::AcaciaFenceGate;
}

bool isButton(BlockId id) { return id == BlockId::StoneButton || id == BlockId::WoodenButton; }


bool canPlaceChestAt(const World& world, BlockId id, const glm::ivec3& pos) {
    constexpr std::array<glm::ivec3, 4> dirs = {
        glm::ivec3{-1,0,0}, glm::ivec3{1,0,0}, glm::ivec3{0,0,-1}, glm::ivec3{0,0,1}
    };
    int adjacent = 0;
    for (const glm::ivec3& d : dirs) {
        const glm::ivec3 n = pos + d;
        if (static_cast<BlockId>(blockId(world.getBlock(n.x,n.y,n.z))) != id) continue;
        ++adjacent;
        // BlockChest#canPlaceBlockAt rejects attaching to an already-double chest.
        for (const glm::ivec3& d2 : dirs) {
            const glm::ivec3 nn = n + d2;
            if (nn.x == pos.x && nn.y == pos.y && nn.z == pos.z) continue;
            if (static_cast<BlockId>(blockId(world.getBlock(nn.x,nn.y,nn.z))) == id) return false;
        }
    }
    return adjacent <= 1;
}
bool isPressurePlate(BlockId id) {
    return id == BlockId::StonePressurePlate || id == BlockId::WoodenPressurePlate ||
           id == BlockId::LightWeightedPressurePlate || id == BlockId::HeavyWeightedPressurePlate;
}

bool isRail(BlockId id) {
    return id == BlockId::Rail || id == BlockId::GoldenRail || id == BlockId::DetectorRail || id == BlockId::ActivatorRail;
}


bool railAt(const World& world, int x, int y, int z) {
    if (y < 0 || y >= chunkHeight) return false;
    return isRail(static_cast<BlockId>(blockId(world.getBlock(x, y, z))));
}

bool railDirectionPresent(const World& world, const glm::ivec3& pos, int dx, int dz) {
    return railAt(world, pos.x + dx, pos.y, pos.z + dz) ||
           railAt(world, pos.x + dx, pos.y + 1, pos.z + dz) ||
           railAt(world, pos.x + dx, pos.y - 1, pos.z + dz);
}

std::uint8_t connectedRailMeta(const World& world, const glm::ivec3& pos, BlockState state) {
    const BlockId id = static_cast<BlockId>(blockId(state));
    const bool north = railDirectionPresent(world, pos, 0, -1);
    const bool south = railDirectionPresent(world, pos, 0, 1);
    const bool west = railDirectionPresent(world, pos, -1, 0);
    const bool east = railDirectionPresent(world, pos, 1, 0);
    const bool curves = id == BlockId::Rail;
    std::uint8_t shape = blockMetadata(state) & 7U;

    if ((north || south) && !(west || east)) shape = 0;          // NORTH_SOUTH
    else if ((west || east) && !(north || south)) shape = 1;     // EAST_WEST
    else if (curves) {
        if (south && east && !north && !west) shape = 6;
        else if (south && west && !north && !east) shape = 7;
        else if (north && west && !south && !east) shape = 8;
        else if (north && east && !south && !west) shape = 9;
        else if (north || south) shape = 0;
        else if (west || east) shape = 1;
    } else if (north || south) shape = 0;
    else if (west || east) shape = 1;

    // BlockRailBase promotes straight rails to ascending variants when a rail
    // exists one block higher in the selected axis.
    if (shape == 0) {
        if (railAt(world, pos.x, pos.y + 1, pos.z - 1)) shape = 4;
        else if (railAt(world, pos.x, pos.y + 1, pos.z + 1)) shape = 5;
    } else if (shape == 1) {
        if (railAt(world, pos.x + 1, pos.y + 1, pos.z)) shape = 2;
        else if (railAt(world, pos.x - 1, pos.y + 1, pos.z)) shape = 3;
    }

    // Powered/detector/activator rails keep their powered flag in bit 3.
    return static_cast<std::uint8_t>(shape | (blockMetadata(state) & 8U));
}

bool isRepeater(BlockId id) { return id == BlockId::UnpoweredRepeater || id == BlockId::PoweredRepeater; }
bool isComparator(BlockId id) { return id == BlockId::UnpoweredComparator || id == BlockId::PoweredComparator; }

bool isGlazed(BlockId id) {
    const auto value = static_cast<std::uint16_t>(id);
    return value >= static_cast<std::uint16_t>(BlockId::WhiteGlazedTerracotta) &&
           value <= static_cast<std::uint16_t>(BlockId::BlackGlazedTerracotta);
}

bool isShulker(BlockId id) {
    const auto value = static_cast<std::uint16_t>(id);
    return value >= static_cast<std::uint16_t>(BlockId::WhiteShulkerBox) &&
           value <= static_cast<std::uint16_t>(BlockId::BlackShulkerBox);
}

bool isPlant(BlockId id) {
    switch (id) {
        case BlockId::Sapling: case BlockId::TallGrass: case BlockId::DeadBush:
        case BlockId::YellowFlower: case BlockId::RedFlower: case BlockId::BrownMushroom:
        case BlockId::RedMushroom: case BlockId::DoublePlant: case BlockId::Waterlily:
            return true;
        default: return false;
    }
}

bool topSupport(const World& world, const glm::ivec3& pos) {
    return pos.y > 0 && BlockShapes::isTopSolid(world, pos.x, pos.y - 1, pos.z);
}

bool canAttach(const World& world, const glm::ivec3& target, Face face) {
    switch (face) {
        case Face::Up: return BlockShapes::hasSolidFace(world, target.x, target.y - 1, target.z, Face::Up);
        case Face::Down: return BlockShapes::hasSolidFace(world, target.x, target.y + 1, target.z, Face::Down);
        case Face::North: return BlockShapes::hasSolidFace(world, target.x, target.y, target.z + 1, Face::North);
        case Face::South: return BlockShapes::hasSolidFace(world, target.x, target.y, target.z - 1, Face::South);
        case Face::West: return BlockShapes::hasSolidFace(world, target.x + 1, target.y, target.z, Face::West);
        case Face::East: return BlockShapes::hasSolidFace(world, target.x - 1, target.y, target.z, Face::East);
    }
    return false;
}

bool canTorchStand(const World& world, const glm::ivec3& target) {
    if (target.y <= 0) return false;
    const BlockId below = static_cast<BlockId>(blockId(world.getBlock(target.x, target.y - 1, target.z)));
    if (BlockShapes::isTopSolid(world, target.x, target.y - 1, target.z)) return true;
    switch (below) {
        case BlockId::Fence: case BlockId::SpruceFence: case BlockId::BirchFence:
        case BlockId::JungleFence: case BlockId::DarkOakFence: case BlockId::AcaciaFence:
        case BlockId::NetherBrickFence: case BlockId::Glass: case BlockId::StainedGlass:
        case BlockId::CobblestoneWall: return true;
        default: return false;
    }
}

std::optional<std::uint8_t> torchMeta(const World& world, const glm::ivec3& target, Face clicked) {
    const auto meta = [](Face face) -> std::uint8_t {
        switch (face) {
            case Face::East: return 1; case Face::West: return 2;
            case Face::South: return 3; case Face::North: return 4;
            default: return 5;
        }
    };
    if (horizontalFace(clicked) && canAttach(world, target, clicked)) return meta(clicked);
    if (canTorchStand(world, target)) return 5;
    for (Face face : {Face::North, Face::South, Face::West, Face::East})
        if (canAttach(world, target, face)) return meta(face);
    return std::nullopt;
}

bool intersectsPlayer(const World& world, const Player& player, const glm::ivec3& pos, BlockState state) {
    const Aabb playerBox = player.bounds();
    for (const BlockBox& box : BlockShapes::collision(world, state, pos.x, pos.y, pos.z)) {
        Aabb placed{{pos.x + box.minX, pos.y + box.minY, pos.z + box.minZ},
                    {pos.x + box.maxX, pos.y + box.maxY, pos.z + box.maxZ}};
        if (playerBox.intersects(placed)) return true;
    }
    return false;
}

bool normalAt(const World& world, int x, int y, int z) {
    return BlockShapes::isNormalCube(world.getBlock(x, y, z));
}

bool doorAt(const World& world, BlockId door, int x, int y, int z) {
    return static_cast<BlockId>(blockId(world.getBlock(x, y, z))) == door;
}

bool rightDoorHinge(const World& world, BlockId door, const glm::ivec3& position,
                    Horizontal facing, const glm::vec3& localHit) {
    const auto [fx, fz] = offset(facing);
    const bool hitRight = (fx < 0 && localHit.z < 0.5F) || (fx > 0 && localHit.z > 0.5F) ||
                          (fz < 0 && localHit.x > 0.5F) || (fz > 0 && localHit.x < 0.5F);
    const auto [rx, rz] = offset(rotateY(facing));
    const auto [lx, lz] = offset(rotateYCCW(facing));
    const int leftSolid = (normalAt(world, position.x + lx, position.y, position.z + lz) ? 1 : 0) +
                          (normalAt(world, position.x + lx, position.y + 1, position.z + lz) ? 1 : 0);
    const int rightSolid = (normalAt(world, position.x + rx, position.y, position.z + rz) ? 1 : 0) +
                           (normalAt(world, position.x + rx, position.y + 1, position.z + rz) ? 1 : 0);
    const bool leftDoor = doorAt(world, door, position.x + lx, position.y, position.z + lz) ||
                          doorAt(world, door, position.x + lx, position.y + 1, position.z + lz);
    const bool rightDoor = doorAt(world, door, position.x + rx, position.y, position.z + rz) ||
                           doorAt(world, door, position.x + rx, position.y + 1, position.z + rz);
    if ((!leftDoor || rightDoor) && rightSolid <= leftSolid) {
        if ((rightDoor && !leftDoor) || rightSolid < leftSolid) return false;
        return hitRight;
    }
    return true;
}

bool basicPlantSupport(const World& world, const glm::ivec3& target, BlockId plant) {
    if (target.y <= 0) return false;
    const BlockState belowState = world.getBlock(target.x, target.y - 1, target.z);
    const BlockId below = static_cast<BlockId>(blockId(belowState));
    if (plant == BlockId::DeadBush)
        return below == BlockId::Sand || below == BlockId::HardenedClay || below == BlockId::StainedHardenedClay || below == BlockId::Dirt;
    if (plant == BlockId::BrownMushroom || plant == BlockId::RedMushroom)
        return below == BlockId::Mycelium || (below == BlockId::Dirt && blockMetadata(belowState) == 2U) || BlockShapes::isTopSolid(world, target.x, target.y - 1, target.z);
    if (plant == BlockId::Waterlily)
        return below == BlockId::Water || below == BlockId::FlowingWater;
    return below == BlockId::Grass || below == BlockId::Dirt || below == BlockId::Farmland;
}

bool reedSupport(const World& world, const glm::ivec3& target) {
    if (target.y <= 0) return false;
    const BlockId below = static_cast<BlockId>(blockId(world.getBlock(target.x, target.y - 1, target.z)));
    if (below == BlockId::Reeds) return true;
    if (below != BlockId::Grass && below != BlockId::Dirt && below != BlockId::Sand) return false;
    for (const auto [dx, dz] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
        const BlockId adjacent = static_cast<BlockId>(blockId(world.getBlock(target.x + dx, target.y - 1, target.z + dz)));
        if (adjacent == BlockId::Water || adjacent == BlockId::FlowingWater || adjacent == BlockId::FrostedIce) return true;
    }
    return false;
}

bool cactusSupport(const World& world, const glm::ivec3& target) {
    if (target.y <= 0) return false;
    const BlockId below = static_cast<BlockId>(blockId(world.getBlock(target.x, target.y - 1, target.z)));
    if (below != BlockId::Sand && below != BlockId::Cactus) return false;
    for (const auto [dx, dz] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
        const BlockState state = world.getBlock(target.x + dx, target.y, target.z + dz);
        if (BlockRegistry::get(state).opaque) return false;
        const BlockId id = static_cast<BlockId>(blockId(state));
        if (id == BlockId::FlowingLava || id == BlockId::Lava) return false;
    }
    return true;
}

bool cocoaSupport(const World& world, const glm::ivec3& target, Face face) {
    if (!horizontalFace(face)) return false;
    glm::ivec3 support = target;
    switch (face) {
        case Face::North: ++support.z; break;
        case Face::South: --support.z; break;
        case Face::West: ++support.x; break;
        case Face::East: --support.x; break;
        default: break;
    }
    const BlockState state = world.getBlock(support.x, support.y, support.z);
    return static_cast<BlockId>(blockId(state)) == BlockId::Log && (blockMetadata(state) & 3U) == 3U;
}

bool unsupported(const World& world, const glm::ivec3& pos, BlockState state) {
    const BlockId id = static_cast<BlockId>(blockId(state));
    const std::uint8_t meta = blockMetadata(state);
    if (blockId(state) == 0) return false;
    if (id == BlockId::Torch || id == BlockId::RedstoneTorch || id == BlockId::UnlitRedstoneTorch) {
        if (meta == 5U) return !canTorchStand(world, pos);
        Face face = meta == 1 ? Face::East : meta == 2 ? Face::West : meta == 3 ? Face::South : Face::North;
        return !canAttach(world, pos, face);
    }
    if (id == BlockId::Ladder || isButton(id)) {
        Face face = meta == 2 ? Face::North : meta == 3 ? Face::South : meta == 4 ? Face::West : meta == 5 ? Face::East : Face::Down;
        return !canAttach(world, pos, face);
    }
    if (id == BlockId::Lever) {
        Face face = Face::Down;
        switch (meta & 7U) {
            case 1: face = Face::East; break; case 2: face = Face::West; break;
            case 3: face = Face::South; break; case 4: face = Face::North; break;
            case 5: case 6: face = Face::Up; break;
            default: face = Face::Down; break;
        }
        return !canAttach(world, pos, face);
    }
    if (id == BlockId::StandingSign || id == BlockId::StandingBanner) return !topSupport(world, pos);
    if (id == BlockId::WallSign || id == BlockId::WallBanner || id == BlockId::Skull) {
        Face face = meta == 2 ? Face::North : meta == 3 ? Face::South : meta == 4 ? Face::West : meta == 5 ? Face::East : Face::Down;
        return face == Face::Down || !canAttach(world, pos, face);
    }
    if (id == BlockId::Vine) {
        bool supported = pos.y + 1 < chunkHeight && static_cast<BlockId>(blockId(world.getBlock(pos.x, pos.y + 1, pos.z))) == BlockId::Vine;
        if ((meta & 1U) != 0U) supported = supported || canAttach(world, pos, Face::South);
        if ((meta & 2U) != 0U) supported = supported || canAttach(world, pos, Face::West);
        if ((meta & 4U) != 0U) supported = supported || canAttach(world, pos, Face::North);
        if ((meta & 8U) != 0U) supported = supported || canAttach(world, pos, Face::East);
        return !supported;
    }
    if (isPressurePlate(id) || isRail(id) || isRepeater(id) || isComparator(id) || id == BlockId::RedstoneWire)
        return !topSupport(world, pos);
    if (isPlant(id)) return !basicPlantSupport(world, pos, id == BlockId::DoublePlant && (meta & 8U) ? BlockId::DoublePlant : id);
    if (id == BlockId::Reeds) return !reedSupport(world, pos);
    if (id == BlockId::Cactus) return !cactusSupport(world, pos);
    if (id == BlockId::SnowLayer) return !topSupport(world, pos);
    if (id == BlockId::Cocoa) {
        const Horizontal h = static_cast<Horizontal>(meta & 3U);
        const Face face = h == Horizontal::South ? Face::South : h == Horizontal::West ? Face::West : h == Horizontal::North ? Face::North : Face::East;
        return !cocoaSupport(world, pos, face);
    }
    return false;
}

void addUnique(std::vector<glm::ivec3>& positions, glm::ivec3 pos) {
    const auto same = [&](const glm::ivec3& value) {
        return value.x == pos.x && value.y == pos.y && value.z == pos.z;
    };
    if (std::find_if(positions.begin(), positions.end(), same) == positions.end()) positions.push_back(pos);
}

} // namespace

std::optional<PlacementPlan> PlacementRules::placement(
    const World& world, const Player& player, const glm::vec3& lookDirection,
    const RaycastHit& hit, const ItemStack& held) const {
    if (held.empty()) return std::nullopt;
    const ItemDefinition& item = items_.get(held.itemId);
    std::optional<BlockId> placed = item.placedBlock;

    // ItemDye brown/cocoa has special ItemDye#onItemUse semantics rather than
    // being a direct ItemBlock.
    if (!placed && held.itemId == 351U && (held.damage & 15U) == 3U) placed = BlockId::Cocoa;
    if (!placed) return std::nullopt;

    const BlockId selected = *placed;
    const std::uint8_t damage = static_cast<std::uint8_t>(held.damage & 15U);
    const glm::vec3 localHit = hit.hitPoint - glm::vec3(hit.block);

    // ItemSlab first combines the slab that was clicked, before normal ItemBlock
    // offset/replaceability processing.
    if (isSingleSlab(selected) && matchingSlab(hit.state, selected, damage)) {
        const bool top = (blockMetadata(hit.state) & 8U) != 0U;
        if ((hit.face == Face::Up && !top) || (hit.face == Face::Down && top)) {
            const BlockState combined = block(doubleSlabFor(selected), damage & slabTypeMask(selected));
            if (!intersectsPlayer(world, player, hit.block, combined))
                return PlacementPlan{{{hit.block, combined}}, true};
        }
    }

    glm::ivec3 target = BlockShapes::isReplaceable(hit.state) ? hit.block : hit.adjacent;
    if (target.y < 0 || target.y >= chunkHeight) return std::nullopt;
    const BlockState existing = world.getBlock(target.x, target.y, target.z);

    if (isSingleSlab(selected) && matchingSlab(existing, selected, damage)) {
        const BlockState combined = block(doubleSlabFor(selected), damage & slabTypeMask(selected));
        if (intersectsPlayer(world, player, target, combined)) return std::nullopt;
        return PlacementPlan{{{target, combined}}, true};
    }
    if (!BlockShapes::isReplaceable(existing)) return std::nullopt;

    const Horizontal facing = horizontalFacing(lookDirection);
    BlockState state = block(selected, damage);

    if (isDoor(selected)) {
        if (hit.face != Face::Up || target.y >= chunkHeight - 1 || !topSupport(world, target) ||
            !BlockShapes::isReplaceable(world.getBlock(target.x, target.y + 1, target.z))) return std::nullopt;
        const bool right = rightDoorHinge(world, selected, target, facing, localHit);
        const BlockState lower = block(selected, doorMeta(facing));
        const BlockState upper = block(selected, static_cast<std::uint8_t>(8U | (right ? 1U : 0U)));
        if (intersectsPlayer(world, player, target, lower) ||
            intersectsPlayer(world, player, target + glm::ivec3(0,1,0), upper)) return std::nullopt;
        return PlacementPlan{{{target, lower}, {target + glm::ivec3(0,1,0), upper}}, true};
    }

    if (selected == BlockId::Bed) {
        if (hit.face != Face::Up || !topSupport(world, target)) return std::nullopt;
        const auto [dx, dz] = offset(facing);
        const glm::ivec3 head = target + glm::ivec3(dx, 0, dz);
        if (head.y < 0 || head.y >= chunkHeight || !BlockShapes::isReplaceable(world.getBlock(head.x, head.y, head.z)) ||
            !topSupport(world, head)) return std::nullopt;
        const BlockState footState = block(BlockId::Bed, horizontalMeta(facing));
        const BlockState headState = block(BlockId::Bed, static_cast<std::uint8_t>(horizontalMeta(facing) | 8U));
        return PlacementPlan{{{target, footState}, {head, headState}}, true};
    }

    if (selected == BlockId::StandingSign || selected == BlockId::StandingBanner) {
        if (hit.face == Face::Down) return std::nullopt;
        if (hit.face == Face::Up) {
            if (!topSupport(world, target)) return std::nullopt;
            const float yaw = static_cast<float>(std::atan2(-lookDirection.x, lookDirection.z));
            const float degrees = yaw * 180.0F / 3.14159265358979323846F;
            const int rotation = (static_cast<int>(std::floor((degrees + 180.0F) * 16.0F / 360.0F + 0.5F))) & 15;
            return PlacementPlan{{{target, block(selected, static_cast<std::uint8_t>(rotation))}}, true};
        }
        if (!horizontalFace(hit.face) || !canAttach(world, target, hit.face)) return std::nullopt;
        const BlockId wall = selected == BlockId::StandingSign ? BlockId::WallSign : BlockId::WallBanner;
        return PlacementPlan{{{target, block(wall, faceIndex(hit.face))}}, true};
    }

    if (selected == BlockId::Skull) {
        if (hit.face == Face::Down || !canAttach(world, target, hit.face)) return std::nullopt;
        return PlacementPlan{{{target, block(BlockId::Skull, faceIndex(hit.face))}}, true};
    }

    if (selected == BlockId::DoublePlant) {
        if (!basicPlantSupport(world, target, selected) || target.y >= chunkHeight - 1 ||
            !BlockShapes::isReplaceable(world.getBlock(target.x, target.y + 1, target.z))) return std::nullopt;
        return PlacementPlan{{{target, block(selected, damage & 7U)},
                              {target + glm::ivec3(0,1,0), block(selected, 8U)}}, true};
    }

    if (selected == BlockId::SnowLayer) {
        if (static_cast<BlockId>(blockId(hit.state)) == BlockId::SnowLayer && hit.face == Face::Up) {
            const std::uint8_t layer = blockMetadata(hit.state) & 7U;
            if (layer < 7U) return PlacementPlan{{{hit.block, block(BlockId::SnowLayer, static_cast<std::uint8_t>(layer + 1U))}}, true};
        }
        if (!topSupport(world, target)) return std::nullopt;
        state = block(selected, 0);
    } else if (selected == BlockId::Cocoa) {
        if (!cocoaSupport(world, target, hit.face)) return std::nullopt;
        state = block(selected, horizontalMeta(horizontalFromFace(hit.face)));
    } else if (selected == BlockId::Vine) {
        if (!horizontalFace(hit.face) || !canAttach(world, target, hit.face)) return std::nullopt;
        state = block(selected, static_cast<std::uint8_t>(1U << horizontalMeta(horizontalFromFace(hit.face))));
    } else if (selected == BlockId::Reeds) {
        if (!reedSupport(world, target)) return std::nullopt;
        state = block(selected, 0);
    } else if (selected == BlockId::Cactus) {
        if (!cactusSupport(world, target)) return std::nullopt;
        state = block(selected, 0);
    } else if (isPlant(selected)) {
        if (!basicPlantSupport(world, target, selected)) return std::nullopt;
        state = block(selected, damage);
    } else if (isPressurePlate(selected) || isRail(selected) || isRepeater(selected) ||
               isComparator(selected) || selected == BlockId::RedstoneWire) {
        if (!topSupport(world, target)) return std::nullopt;
        if (isRail(selected)) {
            const bool xAxis = facing == Horizontal::East || facing == Horizontal::West;
            state = block(selected, static_cast<std::uint8_t>(xAxis ? 1U : 0U));
        } else if (isRepeater(selected) || isComparator(selected)) {
            state = block(selected, horizontalMeta(opposite(facing)));
        } else state = block(selected, 0);
    } else if (selected == BlockId::Ladder) {
        if (!horizontalFace(hit.face) || !canAttach(world, target, hit.face)) return std::nullopt;
        state = block(selected, faceIndex(hit.face));
    } else if (isButton(selected)) {
        if (!canAttach(world, target, hit.face)) return std::nullopt;
        std::uint8_t meta = faceIndex(hit.face);
        // Button metadata uses DOWN=0,E=1,W=2,S=3,N=4,UP=5.
        if (hit.face == Face::Up) meta = 5;
        else if (hit.face == Face::Down) meta = 0;
        else if (hit.face == Face::East) meta = 1;
        else if (hit.face == Face::West) meta = 2;
        else if (hit.face == Face::South) meta = 3;
        else if (hit.face == Face::North) meta = 4;
        state = block(selected, meta);
    } else if (selected == BlockId::Lever) {
        if (!canAttach(world, target, hit.face)) return std::nullopt;
        std::uint8_t meta = 0;
        if (hit.face == Face::East) meta = 1;
        else if (hit.face == Face::West) meta = 2;
        else if (hit.face == Face::South) meta = 3;
        else if (hit.face == Face::North) meta = 4;
        else if (hit.face == Face::Up) meta = (facing == Horizontal::East || facing == Horizontal::West) ? 6 : 5;
        else meta = (facing == Horizontal::East || facing == Horizontal::West) ? 0 : 7;
        state = block(selected, meta);
    } else if (selected == BlockId::Torch || selected == BlockId::RedstoneTorch || selected == BlockId::UnlitRedstoneTorch) {
        const auto meta = torchMeta(world, target, hit.face);
        if (!meta) return std::nullopt;
        state = block(selected, *meta);
    } else if (selected == BlockId::Trapdoor || selected == BlockId::IronTrapdoor) {
        std::uint8_t meta = 0;
        if (hit.face == Face::North) meta = 0;
        else if (hit.face == Face::South) meta = 1;
        else if (hit.face == Face::West) meta = 2;
        else if (hit.face == Face::East) meta = 3;
        else {
            const Horizontal wall = opposite(facing);
            meta = wall == Horizontal::North ? 0 : wall == Horizontal::South ? 1 : wall == Horizontal::West ? 2 : 3;
        }
        const bool top = hit.face == Face::Down || (horizontalFace(hit.face) && localHit.y > 0.5F);
        state = block(selected, static_cast<std::uint8_t>(meta | (top ? 8U : 0U)));
    } else if (isFenceGate(selected)) {
        state = block(selected, horizontalMeta(facing));
    } else if (isStair(selected)) {
        const bool top = hit.face == Face::Down || (horizontalFace(hit.face) && localHit.y > 0.5F);
        state = block(selected, stairMeta(facing, top));
    } else if (isSingleSlab(selected)) {
        const bool top = hit.face == Face::Down || (horizontalFace(hit.face) && localHit.y > 0.5F);
        state = block(selected, static_cast<std::uint8_t>((damage & slabTypeMask(selected)) | (top ? 8U : 0U)));
    } else if (selected == BlockId::Log || selected == BlockId::Log2) {
        const std::uint8_t species = selected == BlockId::Log ? damage & 3U : damage & 1U;
        std::uint8_t axis = 0;
        if (hit.face == Face::East || hit.face == Face::West) axis = 4;
        else if (hit.face == Face::North || hit.face == Face::South) axis = 8;
        state = block(selected, static_cast<std::uint8_t>(species | axis));
    } else if (selected == BlockId::HayBlock) {
        state = block(selected, (hit.face == Face::East || hit.face == Face::West) ? 4U :
                               (hit.face == Face::North || hit.face == Face::South) ? 8U : 0U);
    } else if (selected == BlockId::BoneBlock) {
        state = block(selected, (hit.face == Face::East || hit.face == Face::West) ? 4U :
                               (hit.face == Face::North || hit.face == Face::South) ? 8U : 0U);
    } else if (selected == BlockId::QuartzBlock && damage == 2U) {
        state = block(selected, (hit.face == Face::East || hit.face == Face::West) ? 3U :
                               (hit.face == Face::North || hit.face == Face::South) ? 4U : 2U);
    } else if (selected == BlockId::Anvil) {
        const Horizontal anvilFacing = rotateY(facing);
        state = block(selected, static_cast<std::uint8_t>(horizontalMeta(anvilFacing) | ((damage & 3U) << 2U)));
    } else if (selected == BlockId::Hopper) {
        // BlockHopper#getStateForPlacement uses clickedFacing.getOpposite();
        // UP is forbidden, so a hopper that would point upward is forced DOWN.
        Face output = Face::Down;
        switch (hit.face) {
            case Face::Down: output = Face::Down; break; // opposite is UP -> forced DOWN
            case Face::Up: output = Face::Down; break;
            case Face::North: output = Face::South; break;
            case Face::South: output = Face::North; break;
            case Face::West: output = Face::East; break;
            case Face::East: output = Face::West; break;
        }
        state = block(selected, faceIndex(output));
    } else if (selected == BlockId::Observer) {
        // BlockObserver uses the direction from the placed position toward the player, then opposite.
        state = block(selected, cardinalMeta(opposite(facing)));
    } else if (selected == BlockId::Piston || selected == BlockId::StickyPiston || selected == BlockId::Dispenser || selected == BlockId::Dropper) {
        // The vertical branch of getFacingFromEntity is only selected when the player is very close above/below the block.
        const double dx = player.feetPosition().x - static_cast<double>(target.x);
        const double dz = player.feetPosition().z - static_cast<double>(target.z);
        std::uint8_t meta = cardinalMeta(opposite(facing));
        if (std::abs(dx) < 2.0 && std::abs(dz) < 2.0) {
            const double eyeDelta = player.eyePosition().y - static_cast<double>(target.y);
            if (eyeDelta > 2.0) meta = 1;       // UP
            else if (eyeDelta < 0.0) meta = 0;  // DOWN
        }
        state = block(selected, meta);
    } else if (selected == BlockId::Furnace || selected == BlockId::LitFurnace) {
        state = block(selected, cardinalMeta(opposite(facing)));
    } else if (selected == BlockId::Chest || selected == BlockId::TrappedChest) {
        if (!canPlaceChestAt(world, selected, target)) return std::nullopt;
        state = block(selected, cardinalMeta(opposite(facing)));
    } else if (isGlazed(selected)) {
        state = block(selected, horizontalMeta(opposite(facing)));
    } else if (isShulker(selected)) {
        // BlockShulkerBox faces the clicked placement side.
        state = block(selected, faceIndex(hit.face));
    }

    if (intersectsPlayer(world, player, target, state)) return std::nullopt;
    return PlacementPlan{{{target, state}}, true};
}

std::optional<PlacementPlan> PlacementRules::activation(
    const World& world, const Player&, const glm::vec3& lookDirection,
    const RaycastHit& hit) const {
    const BlockId id = static_cast<BlockId>(blockId(hit.state));
    const std::uint8_t meta = blockMetadata(hit.state);

    if (isWoodDoor(id)) {
        glm::ivec3 lower = hit.block;
        BlockState lowerState = hit.state;
        if ((meta & 8U) != 0U) {
            --lower.y;
            lowerState = world.getBlock(lower.x, lower.y, lower.z);
            if (static_cast<BlockId>(blockId(lowerState)) != id) return PlacementPlan{};
        }
        return PlacementPlan{{{lower, block(id, static_cast<std::uint8_t>(blockMetadata(lowerState) ^ 4U))}}, false};
    }
    if (id == BlockId::Trapdoor) {
        return PlacementPlan{{{hit.block, block(id, static_cast<std::uint8_t>(meta ^ 4U))}}, false};
    }
    if (isFenceGate(id)) {
        std::uint8_t next = meta;
        const Horizontal playerFacing = horizontalFacing(lookDirection);
        if ((next & 4U) != 0U) next = static_cast<std::uint8_t>(next & ~4U);
        else {
            const Horizontal gateFacing = static_cast<Horizontal>(next & 3U);
            if (gateFacing == opposite(playerFacing))
                next = static_cast<std::uint8_t>((next & ~3U) | horizontalMeta(playerFacing));
            next = static_cast<std::uint8_t>(next | 4U);
        }
        return PlacementPlan{{{hit.block, block(id, next)}}, false};
    }
    if (id == BlockId::Lever) {
        return PlacementPlan{{{hit.block, block(id, static_cast<std::uint8_t>(meta ^ 8U))}}, false};
    }
    if (isButton(id)) {
        if ((meta & 8U) != 0U) return PlacementPlan{};
        return PlacementPlan{{{hit.block, block(id, static_cast<std::uint8_t>(meta | 8U))}}, false};
    }
    if (isRepeater(id)) {
        const std::uint8_t delay = static_cast<std::uint8_t>(((meta >> 2U) + 1U) & 3U);
        return PlacementPlan{{{hit.block, block(id, static_cast<std::uint8_t>((meta & 3U) | (delay << 2U)))}}, false};
    }
    if (isComparator(id)) {
        return PlacementPlan{{{hit.block, block(id, static_cast<std::uint8_t>(meta ^ 4U))}}, false};
    }
    if (id == BlockId::DaylightDetector || id == BlockId::DaylightDetectorInverted) {
        const BlockId toggled = id == BlockId::DaylightDetector
            ? BlockId::DaylightDetectorInverted : BlockId::DaylightDetector;
        return PlacementPlan{{{hit.block, block(toggled, meta)}}, false};
    }
    return std::nullopt;
}


std::vector<PlannedBlockChange> PlacementRules::onBlockAdded(
    const World& world, const std::vector<glm::ivec3>& placedPositions) const {
    std::vector<PlannedBlockChange> result;
    for (const glm::ivec3& pos : placedPositions) {
        if (pos.y < 0 || pos.y >= chunkHeight) continue;
        const BlockState state = world.getBlock(pos.x, pos.y, pos.z);
        const BlockId id = static_cast<BlockId>(blockId(state));
        if (isRail(id) && topSupport(world, pos)) {
            const std::uint8_t updated = connectedRailMeta(world, pos, state);
            if (updated != blockMetadata(state)) result.push_back({pos, block(id, updated)});
        }
        // Observer/piston/dispenser/hopper/redstone onBlockAdded callbacks need
        // scheduled ticks or power queries; their dispatch intentionally lands
        // here but those effects are deferred until those systems exist.
    }
    return result;
}

std::vector<PlannedBlockChange> PlacementRules::neighborReactions(
    const World& world, const std::vector<glm::ivec3>& changedPositions) const {
    std::vector<glm::ivec3> candidates;
    const std::array<glm::ivec3, 7> offsets = {
        glm::ivec3{0,0,0}, glm::ivec3{1,0,0}, glm::ivec3{-1,0,0}, glm::ivec3{0,1,0},
        glm::ivec3{0,-1,0}, glm::ivec3{0,0,1}, glm::ivec3{0,0,-1}
    };
    for (const glm::ivec3& changed : changedPositions)
        for (const glm::ivec3& delta : offsets) addUnique(candidates, changed + delta);

    std::vector<PlannedBlockChange> reactions;
    for (const glm::ivec3& pos : candidates) {
        if (pos.y < 0 || pos.y >= chunkHeight) continue;
        const BlockState state = world.getBlock(pos.x, pos.y, pos.z);
        if (blockId(state) == 0) continue;

        const BlockId id = static_cast<BlockId>(blockId(state));
        if (id == BlockId::DoublePlant) {
            if ((blockMetadata(state) & 8U) != 0U) {
                const BlockState below = world.getBlock(pos.x, pos.y - 1, pos.z);
                if (static_cast<BlockId>(blockId(below)) != BlockId::DoublePlant)
                    reactions.push_back({pos, block(BlockId::Air)});
            } else if (!basicPlantSupport(world, pos, BlockId::DoublePlant)) {
                reactions.push_back({pos, block(BlockId::Air)});
                if (pos.y + 1 < chunkHeight && static_cast<BlockId>(blockId(world.getBlock(pos.x, pos.y + 1, pos.z))) == BlockId::DoublePlant)
                    reactions.push_back({pos + glm::ivec3(0,1,0), block(BlockId::Air)});
            }
            continue;
        }
        if (isDoor(id)) {
            if ((blockMetadata(state) & 8U) != 0U) {
                if (static_cast<BlockId>(blockId(world.getBlock(pos.x, pos.y - 1, pos.z))) != id)
                    reactions.push_back({pos, block(BlockId::Air)});
            } else if (pos.y + 1 >= chunkHeight || static_cast<BlockId>(blockId(world.getBlock(pos.x, pos.y + 1, pos.z))) != id || !topSupport(world, pos)) {
                reactions.push_back({pos, block(BlockId::Air)});
                if (pos.y + 1 < chunkHeight && static_cast<BlockId>(blockId(world.getBlock(pos.x, pos.y + 1, pos.z))) == id)
                    reactions.push_back({pos + glm::ivec3(0,1,0), block(BlockId::Air)});
            }
            continue;
        }
        if (id == BlockId::Bed && !topSupport(world, pos)) {
            reactions.push_back({pos, block(BlockId::Air)});
            const Horizontal bedFacing = static_cast<Horizontal>(blockMetadata(state) & 3U);
            const auto [dx, dz] = offset(bedFacing);
            const bool head = (blockMetadata(state) & 8U) != 0U;
            const glm::ivec3 mate = pos + glm::ivec3(head ? -dx : dx, 0, head ? -dz : dz);
            if (static_cast<BlockId>(blockId(world.getBlock(mate.x, mate.y, mate.z))) == BlockId::Bed)
                reactions.push_back({mate, block(BlockId::Air)});
            continue;
        }
        if (isRail(id) && topSupport(world, pos)) {
            const std::uint8_t updated = connectedRailMeta(world, pos, state);
            if (updated != blockMetadata(state)) reactions.push_back({pos, block(id, updated)});
            continue;
        }
        if (unsupported(world, pos, state)) reactions.push_back({pos, block(BlockId::Air)});
    }
    return reactions;
}
