#include "player/BlockInteraction.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "blocks/BlockShape.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/Player.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/World.hpp"

namespace {

enum class Horizontal { South = 0, West = 1, North = 2, East = 3 };

BlockState block(BlockId id, std::uint8_t metadata = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), metadata);
}

bool samePosition(const glm::ivec3& left, const glm::ivec3& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

Horizontal horizontalFacing(const glm::vec3& look) {
    if (std::abs(look.x) > std::abs(look.z))
        return look.x > 0.0F ? Horizontal::East : Horizontal::West;
    return look.z > 0.0F ? Horizontal::South : Horizontal::North;
}

std::pair<int, int> horizontalOffset(Horizontal direction) {
    switch (direction) {
        case Horizontal::South: return {0, 1};
        case Horizontal::West: return {-1, 0};
        case Horizontal::North: return {0, -1};
        case Horizontal::East: return {1, 0};
    }
    return {0, 0};
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

std::uint8_t stairMetadata(Horizontal facing, bool top) {
    std::uint8_t value = 3;
    switch (facing) {
        case Horizontal::East: value = 0; break;
        case Horizontal::West: value = 1; break;
        case Horizontal::South: value = 2; break;
        case Horizontal::North: value = 3; break;
    }
    return static_cast<std::uint8_t>(value | (top ? 4U : 0U));
}

std::uint8_t doorLowerMetadata(Horizontal facing, bool open = false) {
    std::uint8_t value = 3;
    switch (facing) {
        case Horizontal::East: value = 0; break;
        case Horizontal::South: value = 1; break;
        case Horizontal::West: value = 2; break;
        case Horizontal::North: value = 3; break;
    }
    return static_cast<std::uint8_t>(value | (open ? 4U : 0U));
}

std::uint8_t gateMetadata(Horizontal facing) {
    return static_cast<std::uint8_t>(facing);
}

bool sameSlabType(BlockState state, BlockId selected) {
    if (static_cast<BlockId>(blockId(state)) != selected) return false;
    const std::uint8_t typeMask = selected == BlockId::StoneSlab ? 7U :
                                  selected == BlockId::WoodenSlab ? 7U :
                                  selected == BlockId::StoneSlab2 ? 0U : 0U;
    return (blockMetadata(state) & typeMask) == 0U;
}

BlockId doubleSlabFor(BlockId single) {
    switch (single) {
        case BlockId::StoneSlab: return BlockId::DoubleStoneSlab;
        case BlockId::WoodenSlab: return BlockId::DoubleWoodenSlab;
        case BlockId::StoneSlab2: return BlockId::DoubleStoneSlab2;
        case BlockId::PurpurSlab: return BlockId::PurpurDoubleSlab;
        default: return single;
    }
}

bool isSingleSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::WoodenSlab ||
           id == BlockId::StoneSlab2 || id == BlockId::PurpurSlab;
}

bool proposedStateIntersectsPlayer(const World& world, const Player& player,
                                   const glm::ivec3& position, BlockState state) {
    const Aabb playerBox = player.bounds();
    const BlockShapeSet shape = BlockShapes::collision(world, state, position.x, position.y, position.z);
    for (const BlockBox& box : shape) {
        const Aabb worldBox{
            {position.x + box.minX, position.y + box.minY, position.z + box.minZ},
            {position.x + box.maxX, position.y + box.maxY, position.z + box.maxZ}
        };
        if (playerBox.intersects(worldBox)) return true;
    }
    return false;
}

bool canTorchStandOn(const World& world, int x, int y, int z) {
    const auto id = static_cast<BlockId>(blockId(world.getBlock(x, y, z)));
    if (id == BlockId::EndGateway || id == BlockId::LitPumpkin) return false;
    if (BlockShapes::isTopSolid(world, x, y, z)) return true;
    return id == BlockId::Fence || id == BlockId::SpruceFence || id == BlockId::BirchFence ||
           id == BlockId::JungleFence || id == BlockId::DarkOakFence || id == BlockId::AcaciaFence ||
           id == BlockId::NetherBrickFence || id == BlockId::Glass ||
           id == BlockId::StainedGlass || id == BlockId::CobblestoneWall;
}

std::optional<std::uint8_t> torchMetadata(const World& world, const glm::ivec3& target, Face clickedFace) {
    const auto supportFor = [&](Face facing) -> bool {
        switch (facing) {
            case Face::Up:
                return canTorchStandOn(world, target.x, target.y - 1, target.z);
            case Face::East:
                return BlockShapes::hasSolidFace(world, target.x - 1, target.y, target.z, Face::East);
            case Face::West:
                return BlockShapes::hasSolidFace(world, target.x + 1, target.y, target.z, Face::West);
            case Face::South:
                return BlockShapes::hasSolidFace(world, target.x, target.y, target.z - 1, Face::South);
            case Face::North:
                return BlockShapes::hasSolidFace(world, target.x, target.y, target.z + 1, Face::North);
            case Face::Down: return false;
        }
        return false;
    };
    const auto metadata = [](Face face) -> std::uint8_t {
        switch (face) {
            case Face::East: return 1;
            case Face::West: return 2;
            case Face::South: return 3;
            case Face::North: return 4;
            default: return 5;
        }
    };
    if (clickedFace != Face::Down && supportFor(clickedFace)) return metadata(clickedFace);
    for (Face face : {Face::North, Face::South, Face::West, Face::East})
        if (supportFor(face)) return metadata(face);
    if (supportFor(Face::Up)) return metadata(Face::Up);
    return std::nullopt;
}

bool normalAt(const World& world, int x, int y, int z) {
    return BlockShapes::isNormalCube(world.getBlock(x, y, z));
}

bool doorAt(const World& world, BlockId door, int x, int y, int z) {
    return static_cast<BlockId>(blockId(world.getBlock(x, y, z))) == door;
}

bool chooseRightDoorHinge(const World& world, BlockId door, const glm::ivec3& position,
                          Horizontal facing, const glm::vec3& localHit) {
    const auto [fx, fz] = horizontalOffset(facing);
    const bool initialRight = (fx < 0 && localHit.z < 0.5F) ||
                              (fx > 0 && localHit.z > 0.5F) ||
                              (fz < 0 && localHit.x > 0.5F) ||
                              (fz > 0 && localHit.x < 0.5F);
    const auto [rightX, rightZ] = horizontalOffset(rotateY(facing));
    const auto [leftX, leftZ] = horizontalOffset(rotateYCCW(facing));
    const int leftSolid = (normalAt(world, position.x + leftX, position.y, position.z + leftZ) ? 1 : 0) +
                          (normalAt(world, position.x + leftX, position.y + 1, position.z + leftZ) ? 1 : 0);
    const int rightSolid = (normalAt(world, position.x + rightX, position.y, position.z + rightZ) ? 1 : 0) +
                           (normalAt(world, position.x + rightX, position.y + 1, position.z + rightZ) ? 1 : 0);
    const bool doorLeft = doorAt(world, door, position.x + leftX, position.y, position.z + leftZ) ||
                          doorAt(world, door, position.x + leftX, position.y + 1, position.z + leftZ);
    const bool doorRight = doorAt(world, door, position.x + rightX, position.y, position.z + rightZ) ||
                           doorAt(world, door, position.x + rightX, position.y + 1, position.z + rightZ);

    bool rightHinge = initialRight;
    if ((!doorLeft || doorRight) && rightSolid <= leftSolid) {
        if ((doorRight && !doorLeft) || rightSolid < leftSolid) rightHinge = false;
    } else {
        rightHinge = true;
    }
    return rightHinge;
}

} // namespace

BlockState BlockInteraction::selectedState() const {
    return block(placeableBlocks_[selectedIndex_]);
}

const BlockDefinition& BlockInteraction::selectedDefinition() const {
    return BlockRegistry::get(selectedState());
}

void BlockInteraction::selectNumber(int number) {
    if (number >= 1 && number <= static_cast<int>(placeableBlocks_.size()))
        selectedIndex_ = static_cast<std::size_t>(number - 1);
}

void BlockInteraction::scroll(int steps) {
    if (steps == 0) return;
    const int count = static_cast<int>(placeableBlocks_.size());
    int index = static_cast<int>(selectedIndex_) - steps;
    index %= count;
    if (index < 0) index += count;
    selectedIndex_ = static_cast<std::size_t>(index);
}

void BlockInteraction::commitEdit(World& world, LightingEngine& lighting,
                                  WorldRenderer& renderer, const glm::ivec3& position,
                                  BlockState state) {
    world.setBlock(position.x, position.y, position.z, state);
    const std::vector<LightingChange> changes =
        lighting.blockChangedSync(position.x, position.y, position.z);
    renderer.blockChangedSync(position.x, position.y, position.z, changes);
}

void BlockInteraction::removeBlock(World& world, LightingEngine& lighting,
                                   WorldRenderer& renderer, const glm::ivec3& position) {
    commitEdit(world, lighting, renderer, position, block(BlockId::Air));
}

bool BlockInteraction::placeBlock(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                                  const Player& player, const glm::vec3& lookDirection,
                                  const RaycastHit& hit) {
    const BlockId selected = placeableBlocks_[selectedIndex_];
    const glm::vec3 localHit = hit.hitPoint - glm::vec3(hit.block);

    // ItemSlab first attempts to merge the slab that was actually clicked.
    if (isSingleSlab(selected) && sameSlabType(hit.state, selected)) {
        const bool top = (blockMetadata(hit.state) & 8U) != 0U;
        if ((hit.face == Face::Up && !top) || (hit.face == Face::Down && top)) {
            const BlockState combined = block(doubleSlabFor(selected), blockMetadata(hit.state) & 7U);
            if (!proposedStateIntersectsPlayer(world, player, hit.block, combined)) {
                commitEdit(world, lighting, renderer, hit.block, combined);
                return true;
            }
        }
    }

    glm::ivec3 target = BlockShapes::isReplaceable(hit.state) ? hit.block : hit.adjacent;
    if (target.y < 0 || target.y >= chunkHeight) return false;
    const BlockState existing = world.getBlock(target.x, target.y, target.z);

    // ItemSlab also merges a compatible slab in the adjacent target cell.
    if (isSingleSlab(selected) && sameSlabType(existing, selected)) {
        const BlockState combined = block(doubleSlabFor(selected), blockMetadata(existing) & 7U);
        if (proposedStateIntersectsPlayer(world, player, target, combined)) return false;
        commitEdit(world, lighting, renderer, target, combined);
        return true;
    }
    if (!BlockShapes::isReplaceable(existing)) return false;

    BlockState state = block(selected);
    if (selected == BlockId::Log || selected == BlockId::Log2) {
        if (hit.face == Face::East || hit.face == Face::West) state = block(selected, 4);
        else if (hit.face == Face::North || hit.face == Face::South) state = block(selected, 8);
    } else if (isSingleSlab(selected)) {
        const bool top = hit.face == Face::Down ||
            (hit.face != Face::Up && hit.face != Face::Down && localHit.y > 0.5F);
        state = block(selected, top ? 8U : 0U);
    } else if (selected == BlockId::OakStairs || selected == BlockId::StoneStairs ||
               selected == BlockId::BrickStairs || selected == BlockId::StoneBrickStairs ||
               selected == BlockId::NetherBrickStairs || selected == BlockId::SandstoneStairs ||
               selected == BlockId::SpruceStairs || selected == BlockId::BirchStairs ||
               selected == BlockId::JungleStairs || selected == BlockId::QuartzStairs ||
               selected == BlockId::AcaciaStairs || selected == BlockId::DarkOakStairs ||
               selected == BlockId::RedSandstoneStairs || selected == BlockId::PurpurStairs) {
        const bool top = hit.face == Face::Down ||
            (hit.face != Face::Up && hit.face != Face::Down && localHit.y > 0.5F);
        state = block(selected, stairMetadata(horizontalFacing(lookDirection), top));
    } else if (selected == BlockId::Trapdoor || selected == BlockId::IronTrapdoor) {
        const Horizontal playerFacing = horizontalFacing(lookDirection);
        std::uint8_t metadata = 0;
        if (hit.face == Face::North) metadata = 0;
        else if (hit.face == Face::South) metadata = 1;
        else if (hit.face == Face::West) metadata = 2;
        else if (hit.face == Face::East) metadata = 3;
        else {
            // Vertical placement uses placer horizontal facing opposite.
            const Horizontal opposite = static_cast<Horizontal>((static_cast<int>(playerFacing) + 2) & 3);
            switch (opposite) {
                case Horizontal::North: metadata = 0; break;
                case Horizontal::South: metadata = 1; break;
                case Horizontal::West: metadata = 2; break;
                case Horizontal::East: metadata = 3; break;
            }
        }
        const bool top = (hit.face == Face::Down) ||
                         ((hit.face != Face::Up && hit.face != Face::Down) && localHit.y > 0.5F);
        if (top) metadata |= 8U;
        state = block(selected, metadata);
    } else if (selected == BlockId::FenceGate || selected == BlockId::SpruceFenceGate ||
               selected == BlockId::BirchFenceGate || selected == BlockId::JungleFenceGate ||
               selected == BlockId::DarkOakFenceGate || selected == BlockId::AcaciaFenceGate) {
        state = block(selected, gateMetadata(horizontalFacing(lookDirection)));
    } else if (selected == BlockId::Torch) {
        const auto metadata = torchMetadata(world, target, hit.face);
        if (!metadata) return false;
        state = block(selected, *metadata);
    } else if (selected == BlockId::WoodenDoor || selected == BlockId::IronDoor ||
               selected == BlockId::SpruceDoor || selected == BlockId::BirchDoor ||
               selected == BlockId::JungleDoor || selected == BlockId::AcaciaDoor ||
               selected == BlockId::DarkOakDoor) {
        // ItemDoor only succeeds on the top face and requires solid support plus
        // two replaceable cells.
        if (hit.face != Face::Up || target.y >= chunkHeight - 1 ||
            !BlockShapes::isTopSolid(world, target.x, target.y - 1, target.z) ||
            !BlockShapes::isReplaceable(world.getBlock(target.x, target.y + 1, target.z)))
            return false;
        const Horizontal facing = horizontalFacing(lookDirection);
        const bool rightHinge = chooseRightDoorHinge(world, selected, target, facing, localHit);
        const BlockState lower = block(selected, doorLowerMetadata(facing));
        const BlockState upper = block(selected, static_cast<std::uint8_t>(8U | (rightHinge ? 1U : 0U)));
        // Door placement in vanilla is rejected if either resulting collision
        // box intersects the player. Conservatively test the occupied cells here.
        if (proposedStateIntersectsPlayer(world, player, target, lower) ||
            proposedStateIntersectsPlayer(world, player, target + glm::ivec3(0, 1, 0), upper))
            return false;
        commitEdit(world, lighting, renderer, target, lower);
        commitEdit(world, lighting, renderer, target + glm::ivec3(0, 1, 0), upper);
        return true;
    }

    if (proposedStateIntersectsPlayer(world, player, target, state)) return false;
    commitEdit(world, lighting, renderer, target, state);
    return true;
}

void BlockInteraction::tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                            const Player& player, const glm::vec3& lookDirection,
                            bool attacking, bool usingBlock) {
    if (useDelay_ > 0) --useDelay_;
    const float reach = player.gameMode() == GameMode::Creative ? 5.0F : 4.5F;
    const auto hit = raycastBlocks(world, player.eyePosition(), lookDirection, reach);

    if (usingBlock && useDelay_ == 0 && hit) {
        if (placeBlock(world, lighting, renderer, player, lookDirection, *hit)) useDelay_ = 4;
    }

    if (!attacking || !hit) {
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (attackDelay_ > 0) {
        --attackDelay_;
        return;
    }

    if (player.gameMode() == GameMode::Creative) {
        removeBlock(world, lighting, renderer, hit->block);
        attackDelay_ = 5;
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (!breakingBlock_ || !samePosition(*breakingBlock_, hit->block)) {
        breakingBlock_ = hit->block;
        breakProgress_ = 0.0F;
    }

    const BlockDefinition& definition = BlockRegistry::get(hit->state);
    if (definition.hardness < 0.0F) return;
    // PlayerControllerMP#getIsHittingBlock uses Block#getPlayerRelativeBlockHardness:
    // bare-hand speed is 1.0; harvestable blocks divide by 30, otherwise 100.
    const float divisor = definition.requiresTool ? 100.0F : 30.0F;
    breakProgress_ += 1.0F / definition.hardness / divisor;
    if (breakProgress_ >= 1.0F) {
        removeBlock(world, lighting, renderer, hit->block);
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        attackDelay_ = 5;
    }
}
