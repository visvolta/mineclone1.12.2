#include "blocks/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace {
using T = TextureId;

constexpr std::array<T, 6> all(T texture) {
    return {texture, texture, texture, texture, texture, texture};
}

constexpr std::array<std::string_view, 256> legacyNames = {
    "air",
    "stone",
    "grass",
    "dirt",
    "cobblestone",
    "planks",
    "sapling",
    "bedrock",
    "flowing_water",
    "water",
    "flowing_lava",
    "lava",
    "sand",
    "gravel",
    "gold_ore",
    "iron_ore",
    "coal_ore",
    "log",
    "leaves",
    "sponge",
    "glass",
    "lapis_ore",
    "lapis_block",
    "dispenser",
    "sandstone",
    "noteblock",
    "bed",
    "golden_rail",
    "detector_rail",
    "sticky_piston",
    "web",
    "tallgrass",
    "deadbush",
    "piston",
    "piston_head",
    "wool",
    "piston_extension",
    "yellow_flower",
    "red_flower",
    "brown_mushroom",
    "red_mushroom",
    "gold_block",
    "iron_block",
    "double_stone_slab",
    "stone_slab",
    "brick_block",
    "tnt",
    "bookshelf",
    "mossy_cobblestone",
    "obsidian",
    "torch",
    "fire",
    "mob_spawner",
    "oak_stairs",
    "chest",
    "redstone_wire",
    "diamond_ore",
    "diamond_block",
    "crafting_table",
    "wheat",
    "farmland",
    "furnace",
    "lit_furnace",
    "standing_sign",
    "wooden_door",
    "ladder",
    "rail",
    "stone_stairs",
    "wall_sign",
    "lever",
    "stone_pressure_plate",
    "iron_door",
    "wooden_pressure_plate",
    "redstone_ore",
    "lit_redstone_ore",
    "unlit_redstone_torch",
    "redstone_torch",
    "stone_button",
    "snow_layer",
    "ice",
    "snow",
    "cactus",
    "clay",
    "reeds",
    "jukebox",
    "fence",
    "pumpkin",
    "netherrack",
    "soul_sand",
    "glowstone",
    "portal",
    "lit_pumpkin",
    "cake",
    "unpowered_repeater",
    "powered_repeater",
    "stained_glass",
    "trapdoor",
    "monster_egg",
    "stonebrick",
    "brown_mushroom_block",
    "red_mushroom_block",
    "iron_bars",
    "glass_pane",
    "melon_block",
    "pumpkin_stem",
    "melon_stem",
    "vine",
    "fence_gate",
    "brick_stairs",
    "stone_brick_stairs",
    "mycelium",
    "waterlily",
    "nether_brick",
    "nether_brick_fence",
    "nether_brick_stairs",
    "nether_wart",
    "enchanting_table",
    "brewing_stand",
    "cauldron",
    "end_portal",
    "end_portal_frame",
    "end_stone",
    "dragon_egg",
    "redstone_lamp",
    "lit_redstone_lamp",
    "double_wooden_slab",
    "wooden_slab",
    "cocoa",
    "sandstone_stairs",
    "emerald_ore",
    "ender_chest",
    "tripwire_hook",
    "tripwire",
    "emerald_block",
    "spruce_stairs",
    "birch_stairs",
    "jungle_stairs",
    "command_block",
    "beacon",
    "cobblestone_wall",
    "flower_pot",
    "carrots",
    "potatoes",
    "wooden_button",
    "skull",
    "anvil",
    "trapped_chest",
    "light_weighted_pressure_plate",
    "heavy_weighted_pressure_plate",
    "unpowered_comparator",
    "powered_comparator",
    "daylight_detector",
    "redstone_block",
    "quartz_ore",
    "hopper",
    "quartz_block",
    "quartz_stairs",
    "activator_rail",
    "dropper",
    "stained_hardened_clay",
    "stained_glass_pane",
    "leaves2",
    "log2",
    "acacia_stairs",
    "dark_oak_stairs",
    "slime",
    "barrier",
    "iron_trapdoor",
    "prismarine",
    "sea_lantern",
    "hay_block",
    "carpet",
    "hardened_clay",
    "coal_block",
    "packed_ice",
    "double_plant",
    "standing_banner",
    "wall_banner",
    "daylight_detector_inverted",
    "red_sandstone",
    "red_sandstone_stairs",
    "double_stone_slab2",
    "stone_slab2",
    "spruce_fence_gate",
    "birch_fence_gate",
    "jungle_fence_gate",
    "dark_oak_fence_gate",
    "acacia_fence_gate",
    "spruce_fence",
    "birch_fence",
    "jungle_fence",
    "dark_oak_fence",
    "acacia_fence",
    "spruce_door",
    "birch_door",
    "jungle_door",
    "acacia_door",
    "dark_oak_door",
    "end_rod",
    "chorus_plant",
    "chorus_flower",
    "purpur_block",
    "purpur_pillar",
    "purpur_stairs",
    "purpur_double_slab",
    "purpur_slab",
    "end_bricks",
    "beetroots",
    "grass_path",
    "end_gateway",
    "repeating_command_block",
    "chain_command_block",
    "frosted_ice",
    "magma",
    "nether_wart_block",
    "red_nether_brick",
    "bone_block",
    "structure_void",
    "observer",
    "white_shulker_box",
    "orange_shulker_box",
    "magenta_shulker_box",
    "light_blue_shulker_box",
    "yellow_shulker_box",
    "lime_shulker_box",
    "pink_shulker_box",
    "gray_shulker_box",
    "silver_shulker_box",
    "cyan_shulker_box",
    "purple_shulker_box",
    "blue_shulker_box",
    "brown_shulker_box",
    "green_shulker_box",
    "red_shulker_box",
    "black_shulker_box",
    "white_glazed_terracotta",
    "orange_glazed_terracotta",
    "magenta_glazed_terracotta",
    "light_blue_glazed_terracotta",
    "yellow_glazed_terracotta",
    "lime_glazed_terracotta",
    "pink_glazed_terracotta",
    "gray_glazed_terracotta",
    "silver_glazed_terracotta",
    "cyan_glazed_terracotta",
    "purple_glazed_terracotta",
    "blue_glazed_terracotta",
    "brown_glazed_terracotta",
    "green_glazed_terracotta",
    "red_glazed_terracotta",
    "black_glazed_terracotta",
    "concrete",
    "concrete_powder",
    "",
    "",
    "structure_block"
};

constexpr std::size_t registeredNameCount() {
    std::size_t count = 0;
    for (std::string_view name : legacyNames)
        if (!name.empty()) ++count;
    return count;
}

static_assert(registeredNameCount() == BlockRegistry::vanillaRegisteredBlockCount);
static_assert(legacyNames[253].empty() && legacyNames[254].empty());
static_assert(legacyNames[255] == "structure_block");

constexpr std::array<T, 6> grassTextures = {
    T::Dirt, T::GrassTop, T::GrassSide, T::GrassSide, T::GrassSide, T::GrassSide
};
constexpr std::array<T, 6> verticalLogTextures = {
    T::LogOakTop, T::LogOakTop, T::LogOak, T::LogOak, T::LogOak, T::LogOak
};
constexpr std::array<T, 6> sandstoneTextures = {
    T::SandstoneBottom, T::SandstoneTop, T::SandstoneSide,
    T::SandstoneSide, T::SandstoneSide, T::SandstoneSide
};
constexpr std::array<T, 6> redSandstoneTextures = {
    T::RedSandstoneBottom, T::RedSandstoneTop, T::RedSandstoneSide,
    T::RedSandstoneSide, T::RedSandstoneSide, T::RedSandstoneSide
};
constexpr std::array<T, 6> myceliumTextures = {
    T::Dirt, T::MyceliumTop, T::MyceliumSide,
    T::MyceliumSide, T::MyceliumSide, T::MyceliumSide
};
constexpr std::array<T, 6> cactusTextures = {
    T::CactusBottom, T::CactusTop, T::CactusSide,
    T::CactusSide, T::CactusSide, T::CactusSide
};
constexpr std::array<T, 6> pumpkinTextures = {
    T::PumpkinTop, T::PumpkinTop, T::PumpkinSide,
    T::PumpkinSide, T::PumpkinSide, T::PumpkinFace
};
constexpr std::array<T, 6> melonTextures = {
    T::MelonTop, T::MelonTop, T::MelonSide,
    T::MelonSide, T::MelonSide, T::MelonSide
};
constexpr std::array<T, 6> boneTextures = {
    T::BoneTop, T::BoneTop, T::BoneSide, T::BoneSide, T::BoneSide, T::BoneSide
};
constexpr std::array<T, 6> craftingTextures = {
    T::PlanksOak, T::CraftingTableTop, T::CraftingTableFront,
    T::CraftingTableFront, T::CraftingTableSide, T::CraftingTableSide
};
constexpr std::array<T, 6> furnaceTextures = {
    T::FurnaceTop, T::FurnaceTop, T::FurnaceSide,
    T::FurnaceSide, T::FurnaceSide, T::FurnaceSide
};
constexpr std::array<T, 6> tntTextures = {
    T::TNTBottom, T::TNTTop, T::TNTSide, T::TNTSide, T::TNTSide, T::TNTSide
};

BlockDefinition named(BlockDefinition definition, std::string_view name) {
    definition.name = name;
    return definition;
}

std::array<BlockDefinition, 256> makeDefinitions() {
    std::array<BlockDefinition, 256> definitions{};

    // Stage 1 owns legacy identity/state parity. Unsupported gameplay/model
    // families deliberately retain the engine's pre-Stage-1 generic runtime
    // behaviour so adding their IDs cannot destabilize terrain, lighting, or
    // meshing before those systems implement the real vanilla block classes.
    for (std::size_t id = 0; id < definitions.size(); ++id) {
        if (!legacyNames[id].empty()) {
            definitions[id] = BlockDefinition{
                legacyNames[id], RenderLayer::Solid, true, true,
                1.0F, false, 255, 0, all(T::Stone)
            };
        }
    }

    const auto set = [&](BlockId id, BlockDefinition definition) {
        const auto index = static_cast<std::size_t>(id);
        definitions[index] = named(definition, legacyNames[index]);
    };
    const auto setGeneratedCube = [&](BlockId id) {
        set(id, {legacyNames[static_cast<std::size_t>(id)], RenderLayer::Solid,
                 true, true, 2.0F, false, 255, 0, all(T::PlanksTextureFallback)});
    };
    const auto setGeneratedCross = [&](BlockId id, std::uint8_t light = 0) {
        set(id, {legacyNames[static_cast<std::size_t>(id)], RenderLayer::Cutout,
                 false, false, 0.0F, false, 0, light,
                 all(T::PlanksTextureFallback), BlockShape::Cross});
    };

    // Definitions that were already supported by the renderer/game before the
    // full registry was added. These preserve the old stable runtime behaviour.
    set(BlockId::Air, {"air", RenderLayer::Solid, false, false, 0.0F, false, 0, 0, all(T::Stone)});
    set(BlockId::Stone, {"stone", RenderLayer::Solid, true, true, 1.5F, true, 255, 0, all(T::Stone)});
    set(BlockId::Grass, {"grass", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, grassTextures});
    set(BlockId::Dirt, {"dirt", RenderLayer::Solid, true, true, 0.5F, false, 255, 0, all(T::Dirt)});
    set(BlockId::Cobblestone, {"cobblestone", RenderLayer::Solid, true, true, 2.0F, true, 255, 0, all(T::Cobblestone)});
    set(BlockId::Planks, {"planks", RenderLayer::Solid, true, true, 2.0F, false, 255, 0, all(T::PlanksOak)});
    set(BlockId::Bedrock, {"bedrock", RenderLayer::Solid, true, true, -1.0F, true, 255, 0, all(T::Bedrock)});
    set(BlockId::FlowingWater, {"flowing_water", RenderLayer::Translucent, false, true, 100.0F, false, 3, 0, all(T::WaterStill)});
    set(BlockId::Water, {"water", RenderLayer::Translucent, false, true, 100.0F, false, 3, 0, all(T::WaterStill)});
    set(BlockId::FlowingLava, {"flowing_lava", RenderLayer::Translucent, false, true, 100.0F, false, 0, 15, all(T::LavaStill)});
    set(BlockId::Lava, {"lava", RenderLayer::Translucent, false, true, 100.0F, false, 0, 15, all(T::LavaStill)});
    set(BlockId::Sand, {"sand", RenderLayer::Solid, true, true, 0.5F, false, 255, 0, all(T::Sand)});
    set(BlockId::Gravel, {"gravel", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, all(T::Gravel)});
    set(BlockId::GoldOre, {"gold_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::GoldOre)});
    set(BlockId::IronOre, {"iron_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::IronOre)});
    set(BlockId::CoalOre, {"coal_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::CoalOre)});
    set(BlockId::Log, {"log", RenderLayer::Solid, true, true, 2.0F, false, 255, 0, verticalLogTextures});
    set(BlockId::Leaves, {"leaves", RenderLayer::Cutout, false, true, 0.2F, false, 1, 0, all(T::LeavesOak)});
    set(BlockId::Sponge, {"sponge", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, all(T::SpongeWet)});
    set(BlockId::Glass, {"glass", RenderLayer::Translucent, false, true, 0.3F, false, 0, 0, all(T::Glass)});
    set(BlockId::LapisOre, {"lapis_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::LapisOre)});
    set(BlockId::Sandstone, {"sandstone", RenderLayer::Solid, true, true, 0.8F, true, 255, 0, sandstoneTextures});
    set(BlockId::Web, {"web", RenderLayer::Cutout, false, false, 4.0F, false, 1, 0, all(T::Web), BlockShape::Cross});
    set(BlockId::TallGrass, {"tallgrass", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::TallGrass), BlockShape::Cross});
    set(BlockId::DeadBush, {"deadbush", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::DeadBush), BlockShape::Cross});
    set(BlockId::Wool, {"wool", RenderLayer::Solid, true, true, 0.8F, false, 255, 0, all(T::WoolWhite)});
    set(BlockId::YellowFlower, {"yellow_flower", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::FlowerDandelion), BlockShape::Cross});
    set(BlockId::RedFlower, {"red_flower", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::FlowerPoppy), BlockShape::Cross});
    set(BlockId::BrownMushroom, {"brown_mushroom", RenderLayer::Cutout, false, false, 0.0F, false, 0, 1, all(T::MushroomBrown), BlockShape::Cross});
    set(BlockId::RedMushroom, {"red_mushroom", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::MushroomRed), BlockShape::Cross});
    set(BlockId::BrickBlock, {"brick_block", RenderLayer::Solid, true, true, 2.0F, true, 255, 0, all(T::Brick)});
    set(BlockId::TNT, {"tnt", RenderLayer::Solid, true, true, 0.0F, false, 255, 0, tntTextures});
    set(BlockId::Bookshelf, {"bookshelf", RenderLayer::Solid, true, true, 1.5F, false, 255, 0, all(T::Bookshelf)});
    set(BlockId::MossyCobblestone, {"mossy_cobblestone", RenderLayer::Solid, true, true, 2.0F, true, 255, 0, all(T::CobblestoneMossy)});
    set(BlockId::Obsidian, {"obsidian", RenderLayer::Solid, true, true, 50.0F, true, 255, 0, all(T::Obsidian)});
    set(BlockId::DiamondOre, {"diamond_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::DiamondOre)});
    set(BlockId::CraftingTable, {"crafting_table", RenderLayer::Solid, true, true, 2.5F, false, 255, 0, craftingTextures});
    set(BlockId::Farmland, {"farmland", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, all(T::Dirt)});
    set(BlockId::Furnace, {"furnace", RenderLayer::Solid, true, true, 3.5F, true, 255, 0, furnaceTextures});
    // Chests use a special model in vanilla. Until that renderer exists, keep a stable
    // cube placeholder but preserve their non-opaque/light-transparent behavior.
    set(BlockId::Chest, {"chest", RenderLayer::Solid, false, true, 2.5F, false, 0, 0, all(T::PlanksOak)});
    set(BlockId::TrappedChest, {"trapped_chest", RenderLayer::Solid, false, true, 2.5F, false, 0, 0, all(T::PlanksOak)});
    set(BlockId::RedstoneOre, {"redstone_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::RedstoneOre)});
    set(BlockId::SnowLayer, {"snow_layer", RenderLayer::Cutout, false, false, 0.1F, false, 0, 0, all(T::Snow), BlockShape::SnowLayer});
    set(BlockId::Ice, {"ice", RenderLayer::Translucent, false, true, 0.5F, false, 3, 0, all(T::Ice)});
    set(BlockId::Snow, {"snow", RenderLayer::Solid, true, true, 0.2F, false, 255, 0, all(T::Snow)});
    set(BlockId::Cactus, {"cactus", RenderLayer::Cutout, false, false, 0.4F, false, 0, 0, cactusTextures, BlockShape::Cactus});
    set(BlockId::Clay, {"clay", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, all(T::Clay)});
    set(BlockId::Reeds, {"reeds", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::Reeds), BlockShape::Cross});
    set(BlockId::Pumpkin, {"pumpkin", RenderLayer::Solid, true, true, 1.0F, false, 255, 0, pumpkinTextures});
    set(BlockId::StoneBrick, {"stonebrick", RenderLayer::Solid, true, true, 1.5F, true, 255, 0, all(T::StoneBrick)});
    set(BlockId::MelonBlock, {"melon_block", RenderLayer::Solid, true, true, 1.0F, false, 255, 0, melonTextures});
    set(BlockId::Vine, {"vine", RenderLayer::Cutout, false, false, 0.2F, false, 0, 0, all(T::Vine), BlockShape::Vine});
    set(BlockId::Mycelium, {"mycelium", RenderLayer::Solid, true, true, 0.6F, false, 255, 0, myceliumTextures});
    set(BlockId::Waterlily, {"waterlily", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::Waterlily), BlockShape::Flat});
    set(BlockId::EmeraldOre, {"emerald_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 0, all(T::EmeraldOre)});
    set(BlockId::StainedHardenedClay, {"stained_hardened_clay", RenderLayer::Solid, true, true, 1.25F, true, 255, 0, all(T::StainedWhite)});
    set(BlockId::Leaves2, {"leaves2", RenderLayer::Cutout, false, true, 0.2F, false, 1, 0, all(T::LeavesAcacia)});
    set(BlockId::Log2, {"log2", RenderLayer::Solid, true, true, 2.0F, false, 255, 0, all(T::LogAcacia)});
    set(BlockId::Barrier, {"barrier", RenderLayer::Solid, false, false, -1.0F, false, 0, 0, all(T::Stone)});
    set(BlockId::Prismarine, {"prismarine", RenderLayer::Solid, true, true, 1.5F, true, 255, 0, all(T::Prismarine)});
    set(BlockId::SeaLantern, {"sea_lantern", RenderLayer::Solid, true, true, 0.3F, false, 255, 15, all(T::SeaLantern)});
    set(BlockId::Carpet, {"carpet", RenderLayer::Solid, true, true, 2.0F, false, 255, 0, all(T::PlanksTextureFallback)});
    set(BlockId::HardenedClay, {"hardened_clay", RenderLayer::Solid, true, true, 1.25F, true, 255, 0, all(T::HardenedClay)});
    set(BlockId::PackedIce, {"packed_ice", RenderLayer::Translucent, false, true, 0.5F, false, 3, 0, all(T::Ice)});
    set(BlockId::DoublePlant, {"double_plant", RenderLayer::Cutout, false, false, 0.0F, false, 0, 0, all(T::TallGrass), BlockShape::Cross});
    set(BlockId::RedSandstone, {"red_sandstone", RenderLayer::Solid, true, true, 0.8F, true, 255, 0, redSandstoneTextures});
    set(BlockId::BoneBlock, {"bone_block", RenderLayer::Solid, true, true, 2.0F, true, 255, 0, boneTextures});
    set(BlockId::StructureVoid, {"structure_void", RenderLayer::Solid, false, false, 0.0F, false, 0, 0, all(T::Stone)});

    // Old fallback families. Keeping these exactly where the renderer already
    // knows how to handle them avoids changing mesh topology merely because
    // Stage 1 learned their canonical registry names.
    for (BlockId id : {
        BlockId::Torch, BlockId::Fire, BlockId::RedstoneWire, BlockId::Wheat,
        BlockId::Carrots, BlockId::Potatoes, BlockId::Beetroots, BlockId::Ladder,
        BlockId::Rail, BlockId::Lever, BlockId::StonePressurePlate,
        BlockId::WoodenPressurePlate, BlockId::StoneButton, BlockId::IronBars,
        BlockId::GlassPane, BlockId::TripwireHook, BlockId::Tripwire,
        BlockId::FlowerPot, BlockId::WoodenButton, BlockId::Cocoa
    }) setGeneratedCross(id);

    for (BlockId id : {
        BlockId::StoneSlab, BlockId::DoubleStoneSlab, BlockId::Dispenser,
        BlockId::StickyPiston, BlockId::MonsterEgg, BlockId::BrownMushroomBlock,
        BlockId::RedMushroomBlock, BlockId::OakStairs, BlockId::StoneStairs,
        BlockId::Fence, BlockId::FenceGate, BlockId::BrickStairs,
        BlockId::StoneBrickStairs, BlockId::EndPortalFrame, BlockId::WoodenSlab,
        BlockId::SandstoneStairs, BlockId::SpruceStairs, BlockId::BirchStairs,
        BlockId::JungleStairs, BlockId::CobblestoneWall, BlockId::AcaciaStairs,
        BlockId::DarkOakStairs, BlockId::RedSandstoneStairs, BlockId::WoodenDoor,
        BlockId::IronDoor, BlockId::SpruceDoor, BlockId::BirchDoor,
        BlockId::JungleDoor, BlockId::AcaciaDoor, BlockId::DarkOakDoor,
        BlockId::SpruceFence, BlockId::BirchFence, BlockId::JungleFence,
        BlockId::DarkOakFence, BlockId::AcaciaFence, BlockId::Cauldron,
        BlockId::UnpoweredRepeater, BlockId::GrassPath, BlockId::StructureBlock
    }) setGeneratedCube(id);

    // Source-backed light values can be represented safely without changing
    // model topology. These are the values Block#setLightLevel produces in
    // Minecraft 1.12.2 (floor(15 * level)).
    definitions[static_cast<std::size_t>(BlockId::Torch)].lightValue = 14;
    definitions[static_cast<std::size_t>(BlockId::Fire)].lightValue = 15;
    set(BlockId::Glowstone, {"glowstone", RenderLayer::Solid, true, true, 0.3F, false, 255, 15, all(T::Stone)});
    set(BlockId::LitFurnace, {"lit_furnace", RenderLayer::Solid, true, true, 3.5F, true, 255, 13, furnaceTextures});
    set(BlockId::LitRedstoneOre, {"lit_redstone_ore", RenderLayer::Solid, true, true, 3.0F, true, 255, 9, all(T::RedstoneOre)});
    setGeneratedCross(BlockId::UnlitRedstoneTorch, 0);
    setGeneratedCross(BlockId::RedstoneTorch, 7);
    set(BlockId::LitPumpkin, {"lit_pumpkin", RenderLayer::Solid, true, true, 1.0F, false, 255, 15, pumpkinTextures});
    set(BlockId::LitRedstoneLamp, {"lit_redstone_lamp", RenderLayer::Solid, true, true, 0.3F, false, 255, 15, all(T::Stone)});
    set(BlockId::SeaLantern, {"sea_lantern", RenderLayer::Solid, true, true, 0.3F, false, 255, 15, all(T::SeaLantern)});
    set(BlockId::EndRod, {"end_rod", RenderLayer::Solid, true, true, 0.0F, false, 255, 14, all(T::Stone)});
    set(BlockId::Magma, {"magma", RenderLayer::Solid, true, true, 0.5F, false, 255, 3, all(T::Stone)});
    set(BlockId::Concrete, {"concrete", RenderLayer::Solid, true, true, 1.8F, true, 255, 0, all(T::Stone)});
    set(BlockId::ConcretePowder, {"concrete_powder", RenderLayer::Solid, true, true, 0.5F, false, 255, 0, all(T::Stone)});

    return definitions;
}

const std::array<BlockDefinition, 256>& definitions() {
    static const std::array<BlockDefinition, 256> value = makeDefinitions();
    return value;
}

const BlockDefinition unregisteredDefinition{
    "unregistered", RenderLayer::Solid, false, false, 0.0F, false, 0, 0, all(T::Stone)
};

} // namespace

bool BlockRegistry::isRegisteredId(std::uint16_t numericId) {
    return numericId <= 252U || numericId == 255U;
}

std::string_view BlockRegistry::legacyName(std::uint16_t numericId) {
    if (numericId >= legacyNames.size()) return {};
    return legacyNames[numericId];
}

const BlockDefinition& BlockRegistry::get(BlockState state) {
    const std::uint16_t numericId = blockId(state);
    if (!isRegisteredId(numericId) || numericId >= definitions().size())
        return unregisteredDefinition;
    return definitions()[numericId];
}

TextureId BlockRegistry::texture(BlockState state, Face face) {
    const auto id = static_cast<BlockId>(blockId(state));
    if (id == BlockId::Stone) {
        constexpr std::array variants{T::Stone, T::StoneGranite, T::StoneGraniteSmooth,
            T::StoneDiorite, T::StoneDioriteSmooth, T::StoneAndesite, T::StoneAndesiteSmooth};
        const std::uint8_t metadata = blockMetadata(state);
        return metadata < variants.size() ? variants[metadata] : T::Stone;
    }
    if (id == BlockId::Dirt) {
        const std::uint8_t metadata = blockMetadata(state);
        if (metadata == 1) return T::DirtCoarse;
        if (metadata == 2) return face == Face::Up ? T::DirtPodzolTop :
            (face == Face::Down ? T::Dirt : T::DirtPodzolSide);
    }
    if (id == BlockId::Sand && (blockMetadata(state) & 1U) != 0U) return T::RedSand;
    if (id == BlockId::Planks) {
        constexpr std::array variants{T::PlanksOak, T::PlanksSpruce, T::PlanksBirch,
            T::PlanksJungle, T::PlanksAcacia, T::PlanksDarkOak};
        return variants[std::min<std::size_t>(blockMetadata(state) & 7U, variants.size() - 1)];
    }
    if (id == BlockId::Log) {
        const std::uint8_t axis = blockMetadata(state) & 0x0CU;
        constexpr std::array sides{T::LogOak, T::LogSpruce, T::LogBirch, T::LogJungle};
        constexpr std::array ends{T::LogOakTop, T::LogSpruceTop, T::LogBirchTop, T::LogJungleTop};
        const std::size_t species = blockMetadata(state) & 3U;
        const bool endFace =
            (axis == 0U && (face == Face::Down || face == Face::Up)) ||
            (axis == 4U && (face == Face::West || face == Face::East)) ||
            (axis == 8U && (face == Face::North || face == Face::South));
        return endFace ? ends[species] : sides[species];
    }
    if (id == BlockId::Log2) {
        const bool darkOak = (blockMetadata(state) & 1U) != 0U;
        const std::uint8_t axis = blockMetadata(state) & 0x0CU;
        const bool endFace = (axis == 0U && (face == Face::Down || face == Face::Up)) ||
            (axis == 4U && (face == Face::West || face == Face::East)) ||
            (axis == 8U && (face == Face::North || face == Face::South));
        return darkOak ? (endFace ? T::LogDarkOakTop : T::LogDarkOak) :
            (endFace ? T::LogAcaciaTop : T::LogAcacia);
    }
    if (id == BlockId::Leaves) {
        constexpr std::array variants{T::LeavesOak, T::LeavesSpruce, T::LeavesBirch, T::LeavesJungle};
        return variants[blockMetadata(state) & 3U];
    }
    if (id == BlockId::Leaves2)
        return (blockMetadata(state) & 1U) != 0U ? T::LeavesDarkOak : T::LeavesAcacia;
    if (id == BlockId::TallGrass)
        return (blockMetadata(state) & 3U) == 2U ? T::Fern : T::TallGrass;
    if (id == BlockId::RedFlower) {
        constexpr std::array variants{T::FlowerPoppy, T::FlowerBlueOrchid, T::FlowerAllium,
            T::FlowerHoustonia, T::FlowerTulipRed, T::FlowerTulipOrange,
            T::FlowerTulipWhite, T::FlowerTulipPink, T::FlowerOxeyeDaisy};
        return variants[std::min<std::size_t>(blockMetadata(state), variants.size() - 1)];
    }
    if (id == BlockId::StainedHardenedClay) {
        return static_cast<T>(static_cast<std::uint8_t>(T::StainedWhite) +
                              (blockMetadata(state) & 0x0FU));
    }
    if (id == BlockId::StoneBrick) {
        constexpr std::array variants{T::StoneBrick, T::StoneBrickMossy,
            T::StoneBrickCracked, T::StoneBrickCarved};
        return variants[blockMetadata(state) & 3U];
    }
    return get(state).textures[static_cast<std::size_t>(face)];
}

bool BlockRegistry::hasGrassOverlay(BlockState state, Face face) {
    const bool side = face == Face::North || face == Face::South ||
        face == Face::West || face == Face::East;
    return blockId(state) == static_cast<std::uint16_t>(BlockId::Grass) && side;
}

bool BlockRegistry::isTinted(BlockState state, Face face) {
    const auto id = static_cast<BlockId>(blockId(state));
    return id == BlockId::Leaves || id == BlockId::Leaves2 || id == BlockId::TallGrass ||
        id == BlockId::Vine || id == BlockId::Waterlily ||
        (id == BlockId::Grass && face == Face::Up);
}
