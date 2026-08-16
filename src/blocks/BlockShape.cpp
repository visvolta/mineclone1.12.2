#include "blocks/BlockShape.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "world/World.hpp"

namespace {

constexpr BlockBox full{0.0, 0.0, 0.0, 1.0, 1.0, 1.0};

enum class Horizontal { South = 0, West = 1, North = 2, East = 3 };

bool isStair(BlockId id) {
    switch (id) {
        case BlockId::OakStairs: case BlockId::StoneStairs: case BlockId::BrickStairs:
        case BlockId::StoneBrickStairs: case BlockId::NetherBrickStairs:
        case BlockId::SandstoneStairs: case BlockId::SpruceStairs: case BlockId::BirchStairs:
        case BlockId::JungleStairs: case BlockId::QuartzStairs: case BlockId::AcaciaStairs:
        case BlockId::DarkOakStairs: case BlockId::RedSandstoneStairs: case BlockId::PurpurStairs:
            return true;
        default: return false;
    }
}

bool isHalfSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::WoodenSlab ||
           id == BlockId::StoneSlab2 || id == BlockId::PurpurSlab;
}

bool isDoubleSlab(BlockId id) {
    return id == BlockId::DoubleStoneSlab || id == BlockId::DoubleWoodenSlab ||
           id == BlockId::DoubleStoneSlab2 || id == BlockId::PurpurDoubleSlab;
}

bool isFence(BlockId id) {
    return id == BlockId::Fence || id == BlockId::SpruceFence || id == BlockId::BirchFence ||
           id == BlockId::JungleFence || id == BlockId::DarkOakFence || id == BlockId::AcaciaFence ||
           id == BlockId::NetherBrickFence;
}

bool isWoodFence(BlockId id) {
    return id == BlockId::Fence || id == BlockId::SpruceFence || id == BlockId::BirchFence ||
           id == BlockId::JungleFence || id == BlockId::DarkOakFence || id == BlockId::AcaciaFence;
}

bool isFenceGate(BlockId id) {
    return id == BlockId::FenceGate || id == BlockId::SpruceFenceGate || id == BlockId::BirchFenceGate ||
           id == BlockId::JungleFenceGate || id == BlockId::DarkOakFenceGate || id == BlockId::AcaciaFenceGate;
}

bool isPane(BlockId id) {
    return id == BlockId::IronBars || id == BlockId::GlassPane || id == BlockId::StainedGlassPane;
}

bool isDoor(BlockId id) {
    return id == BlockId::WoodenDoor || id == BlockId::IronDoor || id == BlockId::SpruceDoor ||
           id == BlockId::BirchDoor || id == BlockId::JungleDoor || id == BlockId::AcaciaDoor ||
           id == BlockId::DarkOakDoor;
}

Horizontal stairFacing(BlockState state) {
    constexpr std::array<Horizontal, 4> mapping = {
        Horizontal::East, Horizontal::West, Horizontal::South, Horizontal::North
    };
    return mapping[blockMetadata(state) & 3U];
}

bool stairTop(BlockState state) { return (blockMetadata(state) & 4U) != 0U; }

std::pair<int, int> offset(Horizontal direction) {
    switch (direction) {
        case Horizontal::South: return {0, 1};
        case Horizontal::West: return {-1, 0};
        case Horizontal::North: return {0, -1};
        case Horizontal::East: return {1, 0};
    }
    return {0, 0};
}

Horizontal opposite(Horizontal direction) {
    switch (direction) {
        case Horizontal::South: return Horizontal::North;
        case Horizontal::West: return Horizontal::East;
        case Horizontal::North: return Horizontal::South;
        case Horizontal::East: return Horizontal::West;
    }
    return Horizontal::North;
}

Horizontal rotateY(Horizontal direction) {
    switch (direction) {
        case Horizontal::North: return Horizontal::East;
        case Horizontal::East: return Horizontal::South;
        case Horizontal::South: return Horizontal::West;
        case Horizontal::West: return Horizontal::North;
    }
    return Horizontal::North;
}

Horizontal rotateYCCW(Horizontal direction) {
    switch (direction) {
        case Horizontal::North: return Horizontal::West;
        case Horizontal::West: return Horizontal::South;
        case Horizontal::South: return Horizontal::East;
        case Horizontal::East: return Horizontal::North;
    }
    return Horizontal::North;
}

bool axisX(Horizontal direction) {
    return direction == Horizontal::East || direction == Horizontal::West;
}

struct StairActualState {
    Horizontal facing = Horizontal::North;
    bool top = false;
    enum class Shape { Straight, InnerLeft, InnerRight, OuterLeft, OuterRight } shape = Shape::Straight;
};

bool differentStairs(const World& world, int x, int y, int z,
                     const StairActualState& current, Horizontal direction) {
    const auto [dx, dz] = offset(direction);
    const BlockState state = world.getBlock(x + dx, y, z + dz);
    if (!isStair(static_cast<BlockId>(blockId(state)))) return true;
    return stairFacing(state) != current.facing || stairTop(state) != current.top;
}

StairActualState actualStairState(const World& world, BlockState state, int x, int y, int z) {
    StairActualState result;
    result.facing = stairFacing(state);
    result.top = stairTop(state);

    const auto [frontX, frontZ] = offset(result.facing);
    const BlockState frontState = world.getBlock(x + frontX, y, z + frontZ);
    if (isStair(static_cast<BlockId>(blockId(frontState)))) {
        const Horizontal frontFacing = stairFacing(frontState);
        if (stairTop(frontState) == result.top && axisX(frontFacing) != axisX(result.facing) &&
            differentStairs(world, x, y, z, result, opposite(frontFacing))) {
            result.shape = frontFacing == rotateYCCW(result.facing)
                ? StairActualState::Shape::OuterLeft : StairActualState::Shape::OuterRight;
            return result;
        }
    }

    const auto [backX, backZ] = offset(opposite(result.facing));
    const BlockState backState = world.getBlock(x + backX, y, z + backZ);
    if (isStair(static_cast<BlockId>(blockId(backState)))) {
        const Horizontal backFacing = stairFacing(backState);
        if (stairTop(backState) == result.top && axisX(backFacing) != axisX(result.facing) &&
            differentStairs(world, x, y, z, result, backFacing)) {
            result.shape = backFacing == rotateYCCW(result.facing)
                ? StairActualState::Shape::InnerLeft : StairActualState::Shape::InnerRight;
        }
    }
    return result;
}

BlockBox stairQuarter(Horizontal facing, bool top) {
    const double y0 = top ? 0.0 : 0.5;
    const double y1 = top ? 0.5 : 1.0;
    switch (facing) {
        case Horizontal::North: return {0.0, y0, 0.0, 1.0, y1, 0.5};
        case Horizontal::South: return {0.0, y0, 0.5, 1.0, y1, 1.0};
        case Horizontal::West: return {0.0, y0, 0.0, 0.5, y1, 1.0};
        case Horizontal::East: return {0.5, y0, 0.0, 1.0, y1, 1.0};
    }
    return full;
}

BlockBox stairEighth(Horizontal facing, StairActualState::Shape shape, bool top) {
    Horizontal selected = facing;
    switch (shape) {
        case StairActualState::Shape::OuterLeft: selected = facing; break;
        case StairActualState::Shape::OuterRight: selected = rotateY(facing); break;
        case StairActualState::Shape::InnerRight: selected = opposite(facing); break;
        case StairActualState::Shape::InnerLeft: selected = rotateYCCW(facing); break;
        case StairActualState::Shape::Straight: break;
    }
    const double y0 = top ? 0.0 : 0.5;
    const double y1 = top ? 0.5 : 1.0;
    switch (selected) {
        case Horizontal::North: return {0.0, y0, 0.0, 0.5, y1, 0.5};
        case Horizontal::South: return {0.5, y0, 0.5, 1.0, y1, 1.0};
        case Horizontal::West: return {0.0, y0, 0.5, 0.5, y1, 1.0};
        case Horizontal::East: return {0.5, y0, 0.0, 1.0, y1, 0.5};
    }
    return full;
}

BlockShapeSet stairs(const World& world, BlockState state, int x, int y, int z) {
    const StairActualState actual = actualStairState(world, state, x, y, z);
    BlockShapeSet result;
    result.add(actual.top ? BlockBox{0.0, 0.5, 0.0, 1.0, 1.0, 1.0}
                          : BlockBox{0.0, 0.0, 0.0, 1.0, 0.5, 1.0});
    if (actual.shape == StairActualState::Shape::Straight ||
        actual.shape == StairActualState::Shape::InnerLeft ||
        actual.shape == StairActualState::Shape::InnerRight)
        result.add(stairQuarter(actual.facing, actual.top));
    if (actual.shape != StairActualState::Shape::Straight)
        result.add(stairEighth(actual.facing, actual.shape, actual.top));
    return result;
}

bool normalCube(BlockState state) {
    const BlockDefinition& definition = BlockRegistry::get(state);
    return definition.opaque && definition.fullCube;
}

bool fenceException(BlockId id) {
    switch (id) {
        case BlockId::Barrier: case BlockId::MelonBlock: case BlockId::Pumpkin: case BlockId::LitPumpkin:
            return true;
        default: return false;
    }
}

bool fenceConnects(BlockId current, BlockState neighbor) {
    const auto other = static_cast<BlockId>(blockId(neighbor));
    if (isFenceGate(other)) return true;
    if (isFence(other)) {
        if (current == BlockId::NetherBrickFence) return other == BlockId::NetherBrickFence;
        return isWoodFence(current) && isWoodFence(other);
    }
    return normalCube(neighbor) && !fenceException(other);
}

bool paneConnects(BlockState neighbor) {
    const auto id = static_cast<BlockId>(blockId(neighbor));
    if (isPane(id)) return true;
    if (id == BlockId::Glass || id == BlockId::StainedGlass) return true;
    switch (id) {
        case BlockId::Barrier: case BlockId::MelonBlock: case BlockId::Pumpkin: case BlockId::LitPumpkin:
        case BlockId::Leaves: case BlockId::Leaves2: case BlockId::Beacon: case BlockId::Cauldron:
        case BlockId::Glowstone: case BlockId::Ice: case BlockId::SeaLantern:
        case BlockId::Piston: case BlockId::StickyPiston: case BlockId::PistonHead:
            return false;
        default: return normalCube(neighbor);
    }
}

bool wallConnects(BlockState neighbor) {
    const auto id = static_cast<BlockId>(blockId(neighbor));
    if (id == BlockId::CobblestoneWall || isFenceGate(id)) return true;
    return normalCube(neighbor) && !fenceException(id);
}

int connectionMask(const World& world, int x, int y, int z,
                   bool (*connects)(BlockState)) {
    int mask = 0;
    if (connects(world.getBlock(x, y, z - 1))) mask |= 1; // north horizontal index 2 -> bit 0 in vanilla arrays
    if (connects(world.getBlock(x + 1, y, z))) mask |= 8; // east
    if (connects(world.getBlock(x, y, z + 1))) mask |= 4; // south
    if (connects(world.getBlock(x - 1, y, z))) mask |= 2; // west
    return mask;
}

int fenceMask(const World& world, BlockId current, int x, int y, int z) {
    int mask = 0;
    if (fenceConnects(current, world.getBlock(x, y, z - 1))) mask |= 1;
    if (fenceConnects(current, world.getBlock(x + 1, y, z))) mask |= 8;
    if (fenceConnects(current, world.getBlock(x, y, z + 1))) mask |= 4;
    if (fenceConnects(current, world.getBlock(x - 1, y, z))) mask |= 2;
    return mask;
}

BlockBox connectedBounds(int mask, double insetMin, double insetMax, double height) {
    double minX = insetMin;
    double minZ = insetMin;
    double maxX = insetMax;
    double maxZ = insetMax;
    if ((mask & 2) != 0) minX = 0.0;
    if ((mask & 8) != 0) maxX = 1.0;
    if ((mask & 1) != 0) minZ = 0.0;
    if ((mask & 4) != 0) maxZ = 1.0;
    return {minX, 0.0, minZ, maxX, height, maxZ};
}

BlockShapeSet fenceCollision(const World& world, BlockId current, int x, int y, int z) {
    const int mask = fenceMask(world, current, x, y, z);
    BlockShapeSet result;
    result.add({0.375, 0.0, 0.375, 0.625, 1.5, 0.625});
    if ((mask & 1) != 0) result.add({0.375, 0.0, 0.0, 0.625, 1.5, 0.375});
    if ((mask & 8) != 0) result.add({0.625, 0.0, 0.375, 1.0, 1.5, 0.625});
    if ((mask & 4) != 0) result.add({0.375, 0.0, 0.625, 0.625, 1.5, 1.0});
    if ((mask & 2) != 0) result.add({0.0, 0.0, 0.375, 0.375, 1.5, 0.625});
    return result;
}

struct DoorActualState {
    Horizontal facing = Horizontal::North;
    bool open = false;
    bool rightHinge = false;
};

DoorActualState doorActual(const World& world, BlockState state, int x, int y, int z) {
    const std::uint8_t meta = blockMetadata(state);
    const bool upper = (meta & 8U) != 0U;
    BlockState lower = upper ? world.getBlock(x, y - 1, z) : state;
    BlockState upperState = upper ? state : world.getBlock(x, y + 1, z);
    if (!isDoor(static_cast<BlockId>(blockId(lower)))) lower = state;
    if (!isDoor(static_cast<BlockId>(blockId(upperState)))) upperState = state;
    const std::uint8_t lowerMeta = blockMetadata(lower);
    const std::uint8_t upperMeta = blockMetadata(upperState);
    constexpr std::array<Horizontal, 4> mapping = {
        Horizontal::East, Horizontal::South, Horizontal::West, Horizontal::North
    };
    return {mapping[lowerMeta & 3U], (lowerMeta & 4U) != 0U, (upperMeta & 1U) != 0U};
}

BlockBox doorBox(const DoorActualState& state) {
    constexpr BlockBox south{0.0, 0.0, 0.0, 1.0, 1.0, 0.1875};
    constexpr BlockBox north{0.0, 0.0, 0.8125, 1.0, 1.0, 1.0};
    constexpr BlockBox west{0.8125, 0.0, 0.0, 1.0, 1.0, 1.0};
    constexpr BlockBox east{0.0, 0.0, 0.0, 0.1875, 1.0, 1.0};
    const bool closed = !state.open;
    switch (state.facing) {
        case Horizontal::East: return closed ? east : (state.rightHinge ? north : south);
        case Horizontal::South: return closed ? south : (state.rightHinge ? east : west);
        case Horizontal::West: return closed ? west : (state.rightHinge ? south : north);
        case Horizontal::North: return closed ? north : (state.rightHinge ? west : east);
    }
    return full;
}

Horizontal trapdoorFacing(std::uint8_t meta) {
    switch (meta & 3U) {
        case 0: return Horizontal::North;
        case 1: return Horizontal::South;
        case 2: return Horizontal::West;
        default: return Horizontal::East;
    }
}

BlockBox trapdoorBox(BlockState state) {
    const std::uint8_t meta = blockMetadata(state);
    const bool open = (meta & 4U) != 0U;
    const bool top = (meta & 8U) != 0U;
    if (!open) return top ? BlockBox{0.0, 0.8125, 0.0, 1.0, 1.0, 1.0}
                          : BlockBox{0.0, 0.0, 0.0, 1.0, 0.1875, 1.0};
    switch (trapdoorFacing(meta)) {
        case Horizontal::North: return {0.0, 0.0, 0.8125, 1.0, 1.0, 1.0};
        case Horizontal::South: return {0.0, 0.0, 0.0, 1.0, 1.0, 0.1875};
        case Horizontal::West: return {0.8125, 0.0, 0.0, 1.0, 1.0, 1.0};
        case Horizontal::East: return {0.0, 0.0, 0.0, 0.1875, 1.0, 1.0};
    }
    return full;
}

Horizontal gateFacing(BlockState state) {
    return static_cast<Horizontal>(blockMetadata(state) & 3U);
}

bool gateInWall(const World& world, Horizontal facing, int x, int y, int z) {
    if (facing == Horizontal::North || facing == Horizontal::South)
        return static_cast<BlockId>(blockId(world.getBlock(x - 1, y, z))) == BlockId::CobblestoneWall ||
               static_cast<BlockId>(blockId(world.getBlock(x + 1, y, z))) == BlockId::CobblestoneWall;
    return static_cast<BlockId>(blockId(world.getBlock(x, y, z - 1))) == BlockId::CobblestoneWall ||
           static_cast<BlockId>(blockId(world.getBlock(x, y, z + 1))) == BlockId::CobblestoneWall;
}

BlockBox gateSelection(const World& world, BlockState state, int x, int y, int z) {
    const Horizontal facing = gateFacing(state);
    const bool axisX = facing == Horizontal::East || facing == Horizontal::West;
    const double height = gateInWall(world, facing, x, y, z) ? 0.8125 : 1.0;
    return axisX ? BlockBox{0.375, 0.0, 0.0, 0.625, height, 1.0}
                 : BlockBox{0.0, 0.0, 0.375, 1.0, height, 0.625};
}

BlockShapeSet gateCollision(BlockState state) {
    BlockShapeSet result;
    if ((blockMetadata(state) & 4U) != 0U) return result;
    const Horizontal facing = gateFacing(state);
    const bool axisX = facing == Horizontal::East || facing == Horizontal::West;
    result.add(axisX ? BlockBox{0.375, 0.0, 0.0, 0.625, 1.5, 1.0}
                     : BlockBox{0.0, 0.0, 0.375, 1.0, 1.5, 0.625});
    return result;
}

BlockBox unionBounds(const BlockShapeSet& shape) {
    BlockBox result = shape.boxes[0];
    for (std::size_t i = 1; i < shape.count; ++i) {
        result.minX = std::min(result.minX, shape.boxes[i].minX);
        result.minY = std::min(result.minY, shape.boxes[i].minY);
        result.minZ = std::min(result.minZ, shape.boxes[i].minZ);
        result.maxX = std::max(result.maxX, shape.boxes[i].maxX);
        result.maxY = std::max(result.maxY, shape.boxes[i].maxY);
        result.maxZ = std::max(result.maxZ, shape.boxes[i].maxZ);
    }
    return result;
}

bool noCollision(BlockId id) {
    switch (id) {
        case BlockId::Air: case BlockId::FlowingWater: case BlockId::Water:
        case BlockId::FlowingLava: case BlockId::Lava: case BlockId::Sapling:
        case BlockId::TallGrass: case BlockId::DeadBush: case BlockId::YellowFlower:
        case BlockId::RedFlower: case BlockId::BrownMushroom: case BlockId::RedMushroom:
        case BlockId::Torch: case BlockId::Fire: case BlockId::RedstoneWire:
        case BlockId::UnlitRedstoneTorch: case BlockId::RedstoneTorch:
        case BlockId::Reeds: case BlockId::Vine: case BlockId::StandingSign:
        case BlockId::WallSign: case BlockId::Rail: case BlockId::GoldenRail:
        case BlockId::DetectorRail: case BlockId::ActivatorRail: case BlockId::Tripwire:
            return true;
        default: return false;
    }
}

BlockShapeSet shapeFor(const World& world, BlockState state, int x, int y, int z, bool collision) {
    BlockShapeSet result;
    const auto id = static_cast<BlockId>(blockId(state));
    if (noCollision(id) && collision) return result;

    if (isDoubleSlab(id)) { result.add(full); return result; }
    if (isHalfSlab(id)) {
        const bool top = (blockMetadata(state) & 8U) != 0U;
        result.add(top ? BlockBox{0.0, 0.5, 0.0, 1.0, 1.0, 1.0}
                       : BlockBox{0.0, 0.0, 0.0, 1.0, 0.5, 1.0});
        return result;
    }
    if (isStair(id)) return stairs(world, state, x, y, z);
    if (isFence(id)) {
        if (collision) return fenceCollision(world, id, x, y, z);
        result.add(connectedBounds(fenceMask(world, id, x, y, z), 0.375, 0.625, 1.0));
        return result;
    }
    if (id == BlockId::CobblestoneWall) {
        const int mask = connectionMask(world, x, y, z, wallConnects);
        if (mask == (1 | 4)) {
            result.add({0.3125, 0.0, 0.0, 0.6875, collision ? 1.5 : 0.875, 1.0});
        } else if (mask == (2 | 8)) {
            result.add({0.0, 0.0, 0.3125, 1.0, collision ? 1.5 : 0.875, 0.6875});
        } else {
            result.add(connectedBounds(mask, 0.25, 0.75, collision ? 1.5 : 1.0));
        }
        return result;
    }
    if (isPane(id)) {
        const int mask = connectionMask(world, x, y, z, paneConnects);
        result.add(connectedBounds(mask, 0.4375, 0.5625, 1.0));
        return result;
    }
    if (id == BlockId::SnowLayer) {
        const int layers = (blockMetadata(state) & 7U) + 1;
        const double height = collision ? static_cast<double>(layers - 1) * 0.125
                                        : static_cast<double>(layers) * 0.125;
        if (height > 0.0) result.add({0.0, 0.0, 0.0, 1.0, height, 1.0});
        return result;
    }
    if (id == BlockId::Cactus) {
        result.add(collision ? BlockBox{0.0625, 0.0, 0.0625, 0.9375, 0.9375, 0.9375}
                             : BlockBox{0.0625, 0.0, 0.0625, 0.9375, 1.0, 0.9375});
        return result;
    }
    if (id == BlockId::Farmland || id == BlockId::GrassPath) {
        result.add({0.0, 0.0, 0.0, 1.0, 0.9375, 1.0});
        return result;
    }
    if (isDoor(id)) {
        result.add(doorBox(doorActual(world, state, x, y, z)));
        return result;
    }
    if (id == BlockId::Trapdoor || id == BlockId::IronTrapdoor) {
        result.add(trapdoorBox(state));
        return result;
    }
    if (isFenceGate(id)) {
        if (collision) return gateCollision(state);
        result.add(gateSelection(world, state, x, y, z));
        return result;
    }
    if (id == BlockId::Ladder) {
        if (collision) return result;
        switch (blockMetadata(state)) {
            case 2: result.add({0.0, 0.0, 0.8125, 1.0, 1.0, 1.0}); break; // north
            case 3: result.add({0.0, 0.0, 0.0, 1.0, 1.0, 0.1875}); break; // south
            case 4: result.add({0.8125, 0.0, 0.0, 1.0, 1.0, 1.0}); break; // west
            case 5: default: result.add({0.0, 0.0, 0.0, 0.1875, 1.0, 1.0}); break; // east
        }
        return result;
    }
    if (id == BlockId::Torch || id == BlockId::UnlitRedstoneTorch || id == BlockId::RedstoneTorch) {
        if (collision) return result;
        switch (blockMetadata(state)) {
            case 1: result.add({0.0, 0.2, 0.35, 0.3, 0.8, 0.65}); break;
            case 2: result.add({0.7, 0.2, 0.35, 1.0, 0.8, 0.65}); break;
            case 3: result.add({0.35, 0.2, 0.0, 0.65, 0.8, 0.3}); break;
            case 4: result.add({0.35, 0.2, 0.7, 0.65, 0.8, 1.0}); break;
            default: result.add({0.4, 0.0, 0.4, 0.6, 0.6, 0.6}); break;
        }
        return result;
    }

    if (noCollision(id)) {
        // Ray tracing still uses a finite selection box for most non-colliding
        // vegetation/circuit blocks. Keep the vanilla-style bush footprint for
        // the common cross-model blocks instead of treating the entire voxel as hit.
        switch (id) {
            case BlockId::Sapling: case BlockId::TallGrass: case BlockId::DeadBush:
            case BlockId::YellowFlower: case BlockId::RedFlower: case BlockId::BrownMushroom:
            case BlockId::RedMushroom:
                result.add({0.1, 0.0, 0.1, 0.9, 0.8, 0.9});
                break;
            default: break;
        }
        return result;
    }

    if (BlockRegistry::get(state).fullCube) result.add(full);
    return result;
}

} // namespace

BlockShapeSet BlockShapes::collision(const World& world, BlockState state, int x, int y, int z) {
    return shapeFor(world, state, x, y, z, true);
}

BlockShapeSet BlockShapes::rayTrace(const World& world, BlockState state, int x, int y, int z) {
    return shapeFor(world, state, x, y, z, false);
}

std::optional<BlockBox> BlockShapes::selectionBounds(const World& world, BlockState state,
                                                      int x, int y, int z) {
    const BlockShapeSet shape = rayTrace(world, state, x, y, z);
    if (shape.empty()) return std::nullopt;
    return unionBounds(shape);
}

bool BlockShapes::isReplaceable(BlockState state) {
    switch (static_cast<BlockId>(blockId(state))) {
        case BlockId::Air: case BlockId::TallGrass: case BlockId::DeadBush:
        case BlockId::YellowFlower: case BlockId::RedFlower: case BlockId::BrownMushroom:
        case BlockId::RedMushroom: case BlockId::Fire: case BlockId::Vine:
            return true;
        case BlockId::SnowLayer:
            return (blockMetadata(state) & 7U) == 0U;
        default: return false;
    }
}

bool BlockShapes::isNormalCube(BlockState state) { return normalCube(state); }

bool BlockShapes::isTopSolid(const World& world, int x, int y, int z) {
    const BlockState state = world.getBlock(x, y, z);
    const auto id = static_cast<BlockId>(blockId(state));
    if (BlockRegistry::get(state).fullCube || isDoubleSlab(id)) return true;
    if (isHalfSlab(id)) return (blockMetadata(state) & 8U) != 0U;
    if (isStair(id)) return stairTop(state);
    if (id == BlockId::SnowLayer) return (blockMetadata(state) & 7U) == 7U;
    return false;
}

bool BlockShapes::hasSolidFace(const World& world, int x, int y, int z, Face face) {
    const BlockState state = world.getBlock(x, y, z);
    const auto id = static_cast<BlockId>(blockId(state));
    if (BlockRegistry::get(state).fullCube || isDoubleSlab(id)) return true;
    if (isHalfSlab(id)) {
        const bool top = (blockMetadata(state) & 8U) != 0U;
        return (face == Face::Up && top) || (face == Face::Down && !top);
    }
    if (isStair(id)) {
        const StairActualState actual = actualStairState(world, state, x, y, z);
        if (face == Face::Up) return actual.top;
        if (face == Face::Down) return !actual.top;
        switch (face) {
            case Face::North: return actual.facing == Horizontal::North;
            case Face::South: return actual.facing == Horizontal::South;
            case Face::West: return actual.facing == Horizontal::West;
            case Face::East: return actual.facing == Horizontal::East;
            default: return false;
        }
    }
    return false;
}
