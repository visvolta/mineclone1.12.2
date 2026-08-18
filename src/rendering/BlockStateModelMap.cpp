#include "rendering/BlockStateModelMap.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr std::array<std::string_view, 6> woodTypes = {
    "oak", "spruce", "birch", "jungle", "acacia", "dark_oak"
};
constexpr std::array<std::string_view, 16> colors = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
    "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"
};
constexpr std::array<std::string_view, 7> stoneTypes = {
    "stone", "granite", "smooth_granite", "diorite", "smooth_diorite", "andesite", "smooth_andesite"
};
constexpr std::array<std::string_view, 9> redFlowers = {
    "poppy", "blue_orchid", "allium", "houstonia", "red_tulip", "orange_tulip",
    "white_tulip", "pink_tulip", "oxeye_daisy"
};

std::string boolString(bool value) { return value ? "true" : "false"; }

std::string horizontalFacing(std::uint8_t value) {
    // EnumFacing.getHorizontal: 0 south, 1 west, 2 north, 3 east.
    constexpr std::array<std::string_view, 4> values = {"south", "west", "north", "east"};
    return std::string(values[value & 3U]);
}

std::string facingByIndex(std::uint8_t value) {
    constexpr std::array<std::string_view, 6> values = {"down", "up", "north", "south", "west", "east"};
    return std::string(values[value < 6 ? value : 2]);
}

bool normalCube(BlockState state) {
    const BlockDefinition& definition = BlockRegistry::get(state);
    return definition.opaque && definition.fullCube;
}

bool isFence(BlockId id) {
    return id == BlockId::Fence || id == BlockId::SpruceFence || id == BlockId::BirchFence ||
           id == BlockId::JungleFence || id == BlockId::DarkOakFence || id == BlockId::AcaciaFence ||
           id == BlockId::NetherBrickFence;
}

bool isFenceGate(BlockId id) {
    return id == BlockId::FenceGate || id == BlockId::SpruceFenceGate || id == BlockId::BirchFenceGate ||
           id == BlockId::JungleFenceGate || id == BlockId::DarkOakFenceGate || id == BlockId::AcaciaFenceGate;
}

bool fenceConnects(BlockState neighbor) {
    const auto id = static_cast<BlockId>(blockId(neighbor));
    return isFence(id) || isFenceGate(id) || normalCube(neighbor);
}

bool paneConnects(BlockState neighbor) {
    const auto id = static_cast<BlockId>(blockId(neighbor));
    return id == BlockId::GlassPane || id == BlockId::StainedGlassPane || id == BlockId::IronBars ||
           id == BlockId::Glass || id == BlockId::StainedGlass || normalCube(neighbor);
}

std::string logAxis(std::uint8_t metadata) {
    switch (metadata & 0x0CU) {
        case 0x04: return "x";
        case 0x08: return "z";
        case 0x0C: return "none";
        default: return "y";
    }
}

void setConnections(BlockModelState& result, const RelativeBlockLookup& lookup,
                    bool (*connects)(BlockState)) {
    result.properties["north"] = boolString(connects(lookup(0, 0, -1)));
    result.properties["east"] = boolString(connects(lookup(1, 0, 0)));
    result.properties["south"] = boolString(connects(lookup(0, 0, 1)));
    result.properties["west"] = boolString(connects(lookup(-1, 0, 0)));
}

struct HorizontalDirection {
    std::string_view name;
    int x;
    int z;
};

constexpr std::array<HorizontalDirection, 4> horizontalDirections = {{
    {"south", 0, 1}, {"west", -1, 0}, {"north", 0, -1}, {"east", 1, 0}
}};

HorizontalDirection horizontalDirection(std::string_view name) {
    for (const auto& direction : horizontalDirections)
        if (direction.name == name) return direction;
    return horizontalDirections[2];
}

std::string oppositeHorizontal(std::string_view name) {
    if (name == "north") return "south";
    if (name == "south") return "north";
    if (name == "west") return "east";
    return "west";
}

std::string rotateY(std::string_view name) {
    if (name == "north") return "east";
    if (name == "east") return "south";
    if (name == "south") return "west";
    return "north";
}

std::string rotateYCCW(std::string_view name) {
    if (name == "north") return "west";
    if (name == "west") return "south";
    if (name == "south") return "east";
    return "north";
}

bool horizontalAxisX(std::string_view name) { return name == "east" || name == "west"; }

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

struct StairState { std::string facing; bool top = false; };

StairState stairState(BlockState state) {
    const std::uint8_t metadata = blockMetadata(state);
    constexpr std::array<std::string_view, 4> facings = {"east", "west", "south", "north"};
    return {std::string(facings[metadata & 3U]), (metadata & 4U) != 0U};
}

bool differentStairs(const StairState& current, const RelativeBlockLookup& lookup,
                     std::string_view directionName) {
    const auto offset = horizontalDirection(directionName);
    const BlockState other = lookup(offset.x, 0, offset.z);
    const auto otherId = static_cast<BlockId>(blockId(other));
    if (!isStair(otherId)) return true;
    const StairState neighbor = stairState(other);
    return neighbor.facing != current.facing || neighbor.top != current.top;
}

std::string stairShape(BlockState state, const RelativeBlockLookup& lookup) {
    const StairState current = stairState(state);
    const auto frontOffset = horizontalDirection(current.facing);
    const BlockState frontState = lookup(frontOffset.x, 0, frontOffset.z);
    if (isStair(static_cast<BlockId>(blockId(frontState)))) {
        const StairState front = stairState(frontState);
        if (front.top == current.top && horizontalAxisX(front.facing) != horizontalAxisX(current.facing) &&
            differentStairs(current, lookup, oppositeHorizontal(front.facing))) {
            return front.facing == rotateYCCW(current.facing) ? "outer_left" : "outer_right";
        }
    }
    const auto backOffset = horizontalDirection(oppositeHorizontal(current.facing));
    const BlockState backState = lookup(backOffset.x, 0, backOffset.z);
    if (isStair(static_cast<BlockId>(blockId(backState)))) {
        const StairState back = stairState(backState);
        if (back.top == current.top && horizontalAxisX(back.facing) != horizontalAxisX(current.facing) &&
            differentStairs(current, lookup, back.facing)) {
            return back.facing == rotateYCCW(current.facing) ? "inner_left" : "inner_right";
        }
    }
    return "straight";
}

bool isRedstoneDiode(BlockId id) {
    return id == BlockId::UnpoweredRepeater || id == BlockId::PoweredRepeater ||
           id == BlockId::UnpoweredComparator || id == BlockId::PoweredComparator;
}

bool providesRedstonePower(BlockId id) {
    switch (id) {
        case BlockId::RedstoneWire: case BlockId::RedstoneBlock: case BlockId::Lever:
        case BlockId::StonePressurePlate: case BlockId::WoodenPressurePlate:
        case BlockId::LightWeightedPressurePlate: case BlockId::HeavyWeightedPressurePlate:
        case BlockId::StoneButton: case BlockId::WoodenButton: case BlockId::TripwireHook:
        case BlockId::DaylightDetector: case BlockId::DaylightDetectorInverted:
        case BlockId::DetectorRail: case BlockId::Observer:
        case BlockId::UnpoweredRepeater: case BlockId::PoweredRepeater:
        case BlockId::UnpoweredComparator: case BlockId::PoweredComparator:
            return true;
        default: return false;
    }
}

std::string diodeFacing(BlockState state) { return horizontalFacing(blockMetadata(state)); }

bool redstoneCanConnectTo(BlockState state, std::string_view side) {
    const auto id = static_cast<BlockId>(blockId(state));
    if (id == BlockId::RedstoneWire) return true;
    if (isRedstoneDiode(id)) {
        const std::string facing = diodeFacing(state);
        return facing == side || oppositeHorizontal(facing) == side;
    }
    if (id == BlockId::Observer) return facingByIndex(blockMetadata(state) & 7U) == side;
    return providesRedstonePower(id);
}

std::string redstoneAttach(const RelativeBlockLookup& lookup, std::string_view directionName) {
    const auto offset = horizontalDirection(directionName);
    const BlockState neighbor = lookup(offset.x, 0, offset.z);
    const bool solid = normalCube(neighbor);
    const bool aboveCurrentSolid = normalCube(lookup(0, 1, 0));
    if (!redstoneCanConnectTo(neighbor, directionName) &&
        (solid || !redstoneCanConnectTo(lookup(offset.x, -1, offset.z), directionName))) {
        if (!aboveCurrentSolid) {
            if (solid && redstoneCanConnectTo(lookup(offset.x, 1, offset.z), directionName)) return "up";
        }
        return "none";
    }
    return "side";
}

bool tripwireConnected(const RelativeBlockLookup& lookup, std::string_view directionName) {
    const auto offset = horizontalDirection(directionName);
    const BlockState neighbor = lookup(offset.x, 0, offset.z);
    const auto id = static_cast<BlockId>(blockId(neighbor));
    if (id == BlockId::Tripwire) return true;
    if (id != BlockId::TripwireHook) return false;
    return diodeFacing(neighbor) == oppositeHorizontal(directionName);
}

bool chorusConnects(BlockState state) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::ChorusPlant || id == BlockId::ChorusFlower;
}

} // namespace

BlockModelState resolveBlockModelState(BlockState state, const RelativeBlockLookup& lookup) {
    const auto id = static_cast<BlockId>(blockId(state));
    const std::uint8_t meta = blockMetadata(state);
    BlockModelState result{std::string(BlockRegistry::get(state).name), {}, {}};

    switch (id) {
        case BlockId::Stone:
            result.resourceName = std::string(stoneTypes[meta < stoneTypes.size() ? meta : 0]);
            break;
        case BlockId::Grass:
            result.properties["snowy"] = boolString(blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::Snow) ||
                                                      blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::SnowLayer));
            break;
        case BlockId::Dirt:
            if ((meta & 3U) == 1U) result.resourceName = "coarse_dirt";
            else if ((meta & 3U) == 2U) {
                result.resourceName = "podzol";
                result.properties["snowy"] = boolString(blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::Snow) ||
                                                          blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::SnowLayer));
            }
            break;
        case BlockId::Planks:
            result.resourceName = std::string(woodTypes[std::min<std::size_t>(meta & 7U, woodTypes.size() - 1)]) + "_planks";
            break;
        case BlockId::Sapling:
            result.resourceName = std::string(woodTypes[std::min<std::size_t>(meta & 7U, woodTypes.size() - 1)]) + "_sapling";
            result.properties["stage"] = (meta & 8U) != 0U ? "1" : "0";
            break;
        case BlockId::Sand:
            result.resourceName = (meta & 1U) != 0U ? "red_sand" : "sand";
            break;
        case BlockId::Log: {
            const std::size_t species = std::min<std::size_t>(meta & 3U, 3U);
            result.resourceName = std::string(woodTypes[species]) + "_log";
            result.properties["axis"] = logAxis(meta);
            break;
        }
        case BlockId::Log2: {
            const std::size_t species = 4U + std::min<std::size_t>(meta & 1U, 1U);
            result.resourceName = std::string(woodTypes[species]) + "_log";
            result.properties["axis"] = logAxis(meta);
            break;
        }
        case BlockId::Leaves: {
            const std::size_t species = std::min<std::size_t>(meta & 3U, 3U);
            result.resourceName = std::string(woodTypes[species]) + "_leaves";
            break;
        }
        case BlockId::Leaves2: {
            const std::size_t species = 4U + std::min<std::size_t>(meta & 1U, 1U);
            result.resourceName = std::string(woodTypes[species]) + "_leaves";
            break;
        }
        case BlockId::Sponge:
            result.properties["wet"] = boolString((meta & 1U) != 0U);
            break;
        case BlockId::Sandstone: {
            constexpr std::array<std::string_view, 3> types = {"sandstone", "chiseled_sandstone", "smooth_sandstone"};
            result.resourceName = std::string(types[std::min<std::size_t>(meta, 2)]);
            break;
        }
        case BlockId::RedSandstone: {
            constexpr std::array<std::string_view, 3> types = {"red_sandstone", "chiseled_red_sandstone", "smooth_red_sandstone"};
            result.resourceName = std::string(types[std::min<std::size_t>(meta, 2)]);
            break;
        }
        case BlockId::TallGrass: {
            constexpr std::array<std::string_view, 3> types = {"dead_bush", "tall_grass", "fern"};
            result.resourceName = std::string(types[std::min<std::size_t>(meta & 3U, 2)]);
            break;
        }
        case BlockId::DeadBush:
            result.resourceName = "dead_bush";
            break;
        case BlockId::YellowFlower:
            result.resourceName = "dandelion";
            break;
        case BlockId::RedFlower:
            result.resourceName = std::string(redFlowers[std::min<std::size_t>(meta, redFlowers.size() - 1)]);
            break;
        case BlockId::Wool:
            result.resourceName = std::string(colors[meta & 15U]) + "_wool";
            break;
        case BlockId::StainedHardenedClay:
            result.resourceName = std::string(colors[meta & 15U]) + "_stained_hardened_clay";
            break;
        case BlockId::Carpet:
            result.resourceName = std::string(colors[meta & 15U]) + "_carpet";
            break;
        case BlockId::StainedGlass:
            result.resourceName = std::string(colors[meta & 15U]) + "_stained_glass";
            break;
        case BlockId::StainedGlassPane:
            result.resourceName = std::string(colors[meta & 15U]) + "_stained_glass_pane";
            setConnections(result, lookup, paneConnects);
            break;
        case BlockId::Concrete:
            result.resourceName = std::string(colors[meta & 15U]) + "_concrete";
            break;
        case BlockId::ConcretePowder:
            result.resourceName = std::string(colors[meta & 15U]) + "_concrete_powder";
            break;
        case BlockId::Prismarine: {
            constexpr std::array<std::string_view, 3> types = {"prismarine", "prismarine_bricks", "dark_prismarine"};
            result.resourceName = std::string(types[std::min<std::size_t>(meta & 3U, 2)]);
            break;
        }
        case BlockId::Torch:
        case BlockId::UnlitRedstoneTorch:
        case BlockId::RedstoneTorch:
            switch (meta) {
                case 1: result.properties["facing"] = "east"; break;
                case 2: result.properties["facing"] = "west"; break;
                case 3: result.properties["facing"] = "south"; break;
                case 4: result.properties["facing"] = "north"; break;
                default: result.properties["facing"] = "up"; break;
            }
            break;
        case BlockId::Pumpkin:
        case BlockId::LitPumpkin:
            result.properties["facing"] = horizontalFacing(meta);
            break;
        case BlockId::Furnace:
        case BlockId::LitFurnace:
        case BlockId::Ladder:
            result.properties["facing"] = facingByIndex(meta);
            if (result.properties["facing"] == "up" || result.properties["facing"] == "down")
                result.properties["facing"] = "north";
            break;
        case BlockId::Rail: {
            constexpr std::array<std::string_view, 10> shapes = {
                "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north",
                "ascending_south", "south_east", "south_west", "north_west", "north_east"
            };
            result.properties["shape"] = std::string(shapes[std::min<std::size_t>(meta, 9)]);
            break;
        }
        case BlockId::GoldenRail:
        case BlockId::DetectorRail:
        case BlockId::ActivatorRail: {
            constexpr std::array<std::string_view, 6> shapes = {
                "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south"
            };
            result.properties["shape"] = std::string(shapes[std::min<std::size_t>(meta & 7U, 5)]);
            result.properties["powered"] = boolString((meta & 8U) != 0U);
            break;
        }
        case BlockId::SnowLayer:
            result.properties["layers"] = std::to_string((meta & 7U) + 1U);
            break;
        case BlockId::Fence:
        case BlockId::SpruceFence:
        case BlockId::BirchFence:
        case BlockId::JungleFence:
        case BlockId::DarkOakFence:
        case BlockId::AcaciaFence:
        case BlockId::NetherBrickFence:
            setConnections(result, lookup, fenceConnects);
            break;
        case BlockId::IronBars:
        case BlockId::GlassPane:
            setConnections(result, lookup, paneConnects);
            break;
        case BlockId::Vine:
            result.properties["south"] = boolString((meta & 1U) != 0U);
            result.properties["west"] = boolString((meta & 2U) != 0U);
            result.properties["north"] = boolString((meta & 4U) != 0U);
            result.properties["east"] = boolString((meta & 8U) != 0U);
            result.properties["up"] = boolString(normalCube(lookup(0, 1, 0)));
            break;
        case BlockId::Mycelium:
            result.properties["snowy"] = boolString(blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::Snow) ||
                                                      blockId(lookup(0, 1, 0)) == static_cast<std::uint16_t>(BlockId::SnowLayer));
            break;
        case BlockId::CobblestoneWall: {
            result.resourceName = (meta & 1U) != 0U ? "mossy_cobblestone_wall" : "cobblestone_wall";
            setConnections(result, lookup, fenceConnects);
            const bool north = result.properties["north"] == "true";
            const bool east = result.properties["east"] == "true";
            const bool south = result.properties["south"] == "true";
            const bool west = result.properties["west"] == "true";
            const bool straightNS = north && south && !east && !west;
            const bool straightEW = east && west && !north && !south;
            result.properties["up"] = boolString(!(straightNS || straightEW) || blockId(lookup(0, 1, 0)) != 0);
            break;
        }
        case BlockId::StoneSlab: {
            constexpr std::array<std::string_view, 8> types = {
                "stone", "sandstone", "wood_old", "cobblestone", "brick", "stone_brick", "nether_brick", "quartz"
            };
            result.resourceName = std::string(types[meta & 7U]) + "_slab";
            result.properties["half"] = (meta & 8U) != 0U ? "top" : "bottom";
            break;
        }
        case BlockId::DoubleStoneSlab: {
            constexpr std::array<std::string_view, 8> types = {
                "stone", "sandstone", "wood_old", "cobblestone", "brick", "stone_brick", "nether_brick", "quartz"
            };
            result.resourceName = std::string(types[meta & 7U]) + "_double_slab";
            result.properties.clear();
            result.variantName = (meta & 8U) != 0U ? "all" : "";
            break;
        }
        case BlockId::WoodenSlab:
        case BlockId::DoubleWoodenSlab: {
            const std::string wood(woodTypes[std::min<std::size_t>(meta & 7U, woodTypes.size() - 1)]);
            result.resourceName = wood + (id == BlockId::WoodenSlab ? "_slab" : "_double_slab");
            if (id == BlockId::WoodenSlab) result.properties["half"] = (meta & 8U) != 0U ? "top" : "bottom";
            break;
        }
        case BlockId::StoneBrick: {
            constexpr std::array<std::string_view, 4> types = {
                "stonebrick", "mossy_stonebrick", "cracked_stonebrick", "chiseled_stonebrick"
            };
            result.resourceName = std::string(types[meta & 3U]);
            break;
        }
        case BlockId::BoneBlock:
        case BlockId::HayBlock:
        case BlockId::PurpurPillar:
            result.properties["axis"] = (meta & 0x0CU) == 4U ? "x" : ((meta & 0x0CU) == 8U ? "z" : "y");
            break;
        case BlockId::QuartzBlock:
            switch (meta) {
                case 1: result.resourceName = "chiseled_quartz_block"; break;
                case 2: result.resourceName = "quartz_column"; result.properties["axis"] = "y"; break;
                case 3: result.resourceName = "quartz_column"; result.properties["axis"] = "x"; break;
                case 4: result.resourceName = "quartz_column"; result.properties["axis"] = "z"; break;
                default: result.resourceName = "quartz_block"; break;
            }
            break;
        case BlockId::Wheat:
        case BlockId::Carrots:
        case BlockId::Potatoes:
            result.properties["age"] = std::to_string(meta & 7U);
            break;
        case BlockId::Beetroots:
            result.properties["age"] = std::to_string(meta & 3U);
            break;
        case BlockId::Cactus:
        case BlockId::Reeds:
            // Vanilla StateMap ignores age for rendering.
            break;
        case BlockId::Fire:
            result.properties["north"] = "false";
            result.properties["east"] = "false";
            result.properties["south"] = "false";
            result.properties["west"] = "false";
            result.properties["up"] = "false";
            break;
        case BlockId::Portal:
            result.properties["axis"] = (meta & 3U) == 2U ? "z" : "x";
            break;
        case BlockId::CommandBlock:
        case BlockId::RepeatingCommandBlock:
        case BlockId::ChainCommandBlock:
            result.properties["facing"] = facingByIndex(meta & 7U);
            result.properties["conditional"] = boolString((meta & 8U) != 0U);
            break;
        case BlockId::FlowerPot:
            result.properties["contents"] = "empty";
            break;
        case BlockId::FrostedIce:
            result.properties["age"] = std::to_string(std::min<std::uint8_t>(meta & 3U, 3U));
            break;
        case BlockId::Dispenser:
        case BlockId::Dropper:
            result.properties["facing"] = facingByIndex(meta & 7U);
            break;
        case BlockId::Piston:
        case BlockId::StickyPiston:
            result.properties["facing"] = facingByIndex(meta & 7U);
            result.properties["extended"] = boolString((meta & 8U) != 0U);
            break;
        case BlockId::PistonHead:
            result.properties["facing"] = facingByIndex(meta & 7U);
            result.properties["type"] = (meta & 8U) != 0U ? "sticky" : "normal";
            result.properties["short"] = "false";
            break;
        case BlockId::OakStairs: case BlockId::StoneStairs: case BlockId::BrickStairs:
        case BlockId::StoneBrickStairs: case BlockId::NetherBrickStairs:
        case BlockId::SandstoneStairs: case BlockId::SpruceStairs: case BlockId::BirchStairs:
        case BlockId::JungleStairs: case BlockId::QuartzStairs: case BlockId::AcaciaStairs:
        case BlockId::DarkOakStairs: case BlockId::RedSandstoneStairs: case BlockId::PurpurStairs: {
            const StairState stair = stairState(state);
            result.properties["facing"] = stair.facing;
            result.properties["half"] = stair.top ? "top" : "bottom";
            result.properties["shape"] = stairShape(state, lookup);
            break;
        }
        case BlockId::WoodenDoor: case BlockId::IronDoor: case BlockId::SpruceDoor:
        case BlockId::BirchDoor: case BlockId::JungleDoor: case BlockId::AcaciaDoor:
        case BlockId::DarkOakDoor: {
            constexpr std::array<std::string_view, 4> lowerFacings = {"east", "south", "west", "north"};
            const bool upper = (meta & 8U) != 0U;
            result.properties["half"] = upper ? "upper" : "lower";
            if (upper) {
                result.properties["hinge"] = (meta & 1U) != 0U ? "right" : "left";
                const BlockState lower = lookup(0, -1, 0);
                if (static_cast<BlockId>(blockId(lower)) == id && (blockMetadata(lower) & 8U) == 0U) {
                    result.properties["facing"] = std::string(lowerFacings[blockMetadata(lower) & 3U]);
                    result.properties["open"] = boolString((blockMetadata(lower) & 4U) != 0U);
                } else {
                    result.properties["facing"] = "north";
                    result.properties["open"] = "false";
                }
            } else {
                result.properties["facing"] = std::string(lowerFacings[meta & 3U]);
                result.properties["open"] = boolString((meta & 4U) != 0U);
                const BlockState upperState = lookup(0, 1, 0);
                result.properties["hinge"] =
                    static_cast<BlockId>(blockId(upperState)) == id && (blockMetadata(upperState) & 8U) != 0U &&
                    (blockMetadata(upperState) & 1U) != 0U ? "right" : "left";
            }
            break;
        }
        case BlockId::Lever: {
            constexpr std::array<std::string_view, 8> orientation = {
                "down_x", "east", "west", "south", "north", "up_z", "up_x", "down_z"
            };
            result.properties["facing"] = std::string(orientation[meta & 7U]);
            result.properties["powered"] = boolString((meta & 8U) != 0U);
            break;
        }
        case BlockId::StonePressurePlate:
        case BlockId::WoodenPressurePlate:
            result.properties["powered"] = boolString(meta != 0U);
            break;
        case BlockId::LightWeightedPressurePlate:
        case BlockId::HeavyWeightedPressurePlate:
        case BlockId::DaylightDetector:
        case BlockId::DaylightDetectorInverted:
            result.properties["power"] = std::to_string(meta & 15U);
            break;
        case BlockId::StoneButton:
        case BlockId::WoodenButton: {
            constexpr std::array<std::string_view, 6> buttonFacing = {
                "down", "east", "west", "south", "north", "up"
            };
            result.properties["facing"] = std::string(buttonFacing[std::min<std::size_t>(meta & 7U, 5U)]);
            result.properties["powered"] = boolString((meta & 8U) != 0U);
            break;
        }
        case BlockId::Trapdoor:
        case BlockId::IronTrapdoor: {
            constexpr std::array<std::string_view, 4> trapdoorFacing = {"north", "south", "west", "east"};
            result.properties["facing"] = std::string(trapdoorFacing[meta & 3U]);
            result.properties["open"] = boolString((meta & 4U) != 0U);
            result.properties["half"] = (meta & 8U) != 0U ? "top" : "bottom";
            break;
        }
        case BlockId::UnpoweredRepeater:
        case BlockId::PoweredRepeater: {
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["delay"] = std::to_string(1U + ((meta >> 2U) & 3U));
            const std::string left = rotateY(result.properties["facing"]);
            const std::string right = rotateYCCW(result.properties["facing"]);
            const auto leftOffset = horizontalDirection(left);
            const auto rightOffset = horizontalDirection(right);
            const BlockState leftState = lookup(leftOffset.x, 0, leftOffset.z);
            const BlockState rightState = lookup(rightOffset.x, 0, rightOffset.z);
            const bool locked = static_cast<BlockId>(blockId(leftState)) == BlockId::PoweredRepeater ||
                                static_cast<BlockId>(blockId(rightState)) == BlockId::PoweredRepeater ||
                                static_cast<BlockId>(blockId(leftState)) == BlockId::PoweredComparator ||
                                static_cast<BlockId>(blockId(rightState)) == BlockId::PoweredComparator;
            result.properties["locked"] = boolString(locked);
            break;
        }
        case BlockId::UnpoweredComparator:
        case BlockId::PoweredComparator:
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["mode"] = (meta & 4U) != 0U ? "subtract" : "compare";
            // In 1.12.2 comparator power is represented by the powered/unpowered
            // block ID, not by an extra legacy metadata bit.
            result.properties["powered"] = boolString(id == BlockId::PoweredComparator);
            break;
        case BlockId::RedstoneWire:
            result.properties["north"] = redstoneAttach(lookup, "north");
            result.properties["east"] = redstoneAttach(lookup, "east");
            result.properties["south"] = redstoneAttach(lookup, "south");
            result.properties["west"] = redstoneAttach(lookup, "west");
            break;
        case BlockId::Cake:
            result.properties["bites"] = std::to_string(std::min<std::uint8_t>(meta, 6U));
            break;
        case BlockId::MonsterEgg: {
            constexpr std::array<std::string_view, 6> eggs = {
                "stone_monster_egg", "cobblestone_monster_egg", "stone_brick_monster_egg",
                "mossy_brick_monster_egg", "cracked_brick_monster_egg", "chiseled_brick_monster_egg"
            };
            result.resourceName = std::string(eggs[std::min<std::size_t>(meta, eggs.size() - 1)]);
            break;
        }
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock: {
            constexpr std::array<std::string_view, 16> variants = {
                "all_inside", "north_west", "north", "north_east", "west", "center", "east",
                "south_west", "south", "south_east", "stem", "all_inside", "all_inside",
                "all_inside", "all_outside", "all_stem"
            };
            result.properties["variant"] = std::string(variants[meta & 15U]);
            break;
        }
        case BlockId::PumpkinStem:
        case BlockId::MelonStem: {
            const BlockId fruit = id == BlockId::PumpkinStem ? BlockId::Pumpkin : BlockId::MelonBlock;
            std::string facing = "up";
            for (const auto& direction : horizontalDirections) {
                if (static_cast<BlockId>(blockId(lookup(direction.x, 0, direction.z))) == fruit) {
                    facing = std::string(direction.name);
                    break;
                }
            }
            result.properties["facing"] = facing;
            if (facing == "up") result.properties["age"] = std::to_string(meta & 7U);
            break;
        }
        case BlockId::FenceGate: case BlockId::SpruceFenceGate: case BlockId::BirchFenceGate:
        case BlockId::JungleFenceGate: case BlockId::DarkOakFenceGate: case BlockId::AcaciaFenceGate: {
            const std::string facing = horizontalFacing(meta);
            result.properties["facing"] = facing;
            result.properties["open"] = boolString((meta & 4U) != 0U);
            const bool axisX = horizontalAxisX(facing);
            const bool inWall = axisX
                ? static_cast<BlockId>(blockId(lookup(0, 0, -1))) == BlockId::CobblestoneWall ||
                  static_cast<BlockId>(blockId(lookup(0, 0, 1))) == BlockId::CobblestoneWall
                : static_cast<BlockId>(blockId(lookup(-1, 0, 0))) == BlockId::CobblestoneWall ||
                  static_cast<BlockId>(blockId(lookup(1, 0, 0))) == BlockId::CobblestoneWall;
            result.properties["in_wall"] = boolString(inWall);
            break;
        }
        case BlockId::NetherWart:
            result.properties["age"] = std::to_string(std::min<std::uint8_t>(meta, 3U));
            break;
        case BlockId::BrewingStand:
            result.properties["has_bottle_0"] = boolString((meta & 1U) != 0U);
            result.properties["has_bottle_1"] = boolString((meta & 2U) != 0U);
            result.properties["has_bottle_2"] = boolString((meta & 4U) != 0U);
            break;
        case BlockId::Cauldron:
            result.properties["level"] = std::to_string(std::min<std::uint8_t>(meta, 3U));
            break;
        case BlockId::EndPortalFrame:
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["eye"] = boolString((meta & 4U) != 0U);
            break;
        case BlockId::Cocoa:
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["age"] = std::to_string(std::min<std::uint8_t>((meta & 15U) >> 2U, 2U));
            break;
        case BlockId::TripwireHook:
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["attached"] = boolString((meta & 4U) != 0U);
            result.properties["powered"] = boolString((meta & 8U) != 0U);
            break;
        case BlockId::Tripwire:
            result.properties["attached"] = boolString((meta & 4U) != 0U);
            result.properties["north"] = boolString(tripwireConnected(lookup, "north"));
            result.properties["east"] = boolString(tripwireConnected(lookup, "east"));
            result.properties["south"] = boolString(tripwireConnected(lookup, "south"));
            result.properties["west"] = boolString(tripwireConnected(lookup, "west"));
            break;
        case BlockId::Anvil:
            result.properties["facing"] = horizontalFacing(meta);
            result.properties["damage"] = std::to_string(std::min<std::uint8_t>((meta >> 2U) & 3U, 2U));
            break;
        case BlockId::Hopper: {
            std::string facing = facingByIndex(meta & 7U);
            if (facing == "up") facing = "down";
            result.properties["facing"] = facing;
            break;
        }
        case BlockId::StoneSlab2:
            result.resourceName = "red_sandstone_slab";
            result.properties["half"] = (meta & 8U) != 0U ? "top" : "bottom";
            break;
        case BlockId::DoubleStoneSlab2:
            result.resourceName = "red_sandstone_double_slab";
            result.properties.clear();
            result.variantName = (meta & 8U) != 0U ? "all" : "";
            break;
        case BlockId::PurpurSlab:
            result.properties["half"] = (meta & 8U) != 0U ? "top" : "bottom";
            result.properties["variant"] = "default";
            break;
        case BlockId::PurpurDoubleSlab:
            result.properties["variant"] = "default";
            break;
        case BlockId::EndRod:
            result.properties["facing"] = facingByIndex(meta & 7U);
            break;
        case BlockId::ChorusPlant:
            result.properties["north"] = boolString(chorusConnects(lookup(0, 0, -1)));
            result.properties["east"] = boolString(chorusConnects(lookup(1, 0, 0)));
            result.properties["south"] = boolString(chorusConnects(lookup(0, 0, 1)));
            result.properties["west"] = boolString(chorusConnects(lookup(-1, 0, 0)));
            result.properties["up"] = boolString(chorusConnects(lookup(0, 1, 0)));
            result.properties["down"] = boolString(chorusConnects(lookup(0, -1, 0)) ||
                static_cast<BlockId>(blockId(lookup(0, -1, 0))) == BlockId::EndStone);
            break;
        case BlockId::ChorusFlower:
            result.properties["age"] = std::to_string(std::min<std::uint8_t>(meta, 5U));
            break;
        case BlockId::DoublePlant: {
            constexpr std::array<std::string_view, 6> plantNames = {
                "sunflower", "syringa", "double_grass", "double_fern", "double_rose", "paeonia"
            };
            const bool upper = (meta & 8U) != 0U;
            std::uint8_t type = meta & 7U;
            if (upper) {
                const BlockState lower = lookup(0, -1, 0);
                if (static_cast<BlockId>(blockId(lower)) == BlockId::DoublePlant) type = blockMetadata(lower) & 7U;
            }
            type = std::min<std::uint8_t>(type, 5U);
            result.resourceName = std::string(plantNames[type]);
            result.properties["half"] = upper ? "upper" : "lower";
            break;
        }
        case BlockId::Observer:
            result.properties["facing"] = facingByIndex(meta & 7U);
            result.properties["powered"] = boolString((meta & 8U) != 0U);
            break;
        case BlockId::WhiteGlazedTerracotta: case BlockId::OrangeGlazedTerracotta:
        case BlockId::MagentaGlazedTerracotta: case BlockId::LightBlueGlazedTerracotta:
        case BlockId::YellowGlazedTerracotta: case BlockId::LimeGlazedTerracotta:
        case BlockId::PinkGlazedTerracotta: case BlockId::GrayGlazedTerracotta:
        case BlockId::SilverGlazedTerracotta: case BlockId::CyanGlazedTerracotta:
        case BlockId::PurpleGlazedTerracotta: case BlockId::BlueGlazedTerracotta:
        case BlockId::BrownGlazedTerracotta: case BlockId::GreenGlazedTerracotta:
        case BlockId::RedGlazedTerracotta: case BlockId::BlackGlazedTerracotta:
            result.properties["facing"] = horizontalFacing(meta);
            break;
        case BlockId::StructureBlock: {
            constexpr std::array<std::string_view, 4> modes = {"save", "load", "corner", "data"};
            result.properties["mode"] = std::string(modes[std::min<std::size_t>(meta & 3U, 3U)]);
            break;
        }
        case BlockId::Farmland:
            result.properties["moisture"] = std::to_string(meta & 7U);
            break;
        default:
            break;
    }
    return result;
}

std::int64_t blockModelPositionRandom(int worldX, int worldY, int worldZ) {
    // MathHelper#getPositionRandom(BlockPos) in 1.12.2, using defined
    // unsigned wraparound to reproduce Java long overflow.
    std::uint64_t value = static_cast<std::uint64_t>(
        (static_cast<std::int64_t>(worldX) * 3129871LL) ^
        (static_cast<std::int64_t>(worldZ) * 116129781LL) ^
        static_cast<std::int64_t>(worldY));
    value = value * value * 42317861ULL + value * 11ULL;
    return static_cast<std::int64_t>(value >> 16U);
}
