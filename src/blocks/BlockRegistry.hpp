#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "blocks/BlockState.hpp"

enum class BlockId : std::uint16_t {
    Air=0, Stone=1, Grass=2, Dirt=3, Cobblestone=4, Planks=5, Sapling=6, Bedrock=7,
    FlowingWater=8, Water=9, FlowingLava=10, Lava=11, Sand=12, Gravel=13, GoldOre=14,
    IronOre=15, CoalOre=16, Log=17, Leaves=18, Sponge=19, Glass=20, LapisOre=21,
    LapisBlock=22, Dispenser=23, Sandstone=24, NoteBlock=25, Bed=26, GoldenRail=27,
    DetectorRail=28, StickyPiston=29, Web=30, TallGrass=31, DeadBush=32, Piston=33,
    PistonHead=34, Wool=35, PistonExtension=36, YellowFlower=37, RedFlower=38,
    BrownMushroom=39, RedMushroom=40, GoldBlock=41, IronBlock=42, DoubleStoneSlab=43,
    StoneSlab=44, BrickBlock=45, TNT=46, Bookshelf=47, MossyCobblestone=48, Obsidian=49,
    Torch=50, Fire=51, MobSpawner=52, OakStairs=53, Chest=54, RedstoneWire=55,
    DiamondOre=56, DiamondBlock=57, CraftingTable=58, Wheat=59, Farmland=60, Furnace=61,
    LitFurnace=62, StandingSign=63, WoodenDoor=64, Ladder=65, Rail=66, StoneStairs=67,
    WallSign=68, Lever=69, StonePressurePlate=70, IronDoor=71, WoodenPressurePlate=72,
    RedstoneOre=73, LitRedstoneOre=74, UnlitRedstoneTorch=75, RedstoneTorch=76,
    StoneButton=77, SnowLayer=78, Ice=79, Snow=80, Cactus=81, Clay=82, Reeds=83,
    Jukebox=84, Fence=85, Pumpkin=86, Netherrack=87, SoulSand=88, Glowstone=89, Portal=90,
    LitPumpkin=91, Cake=92, UnpoweredRepeater=93, PoweredRepeater=94, StainedGlass=95,
    Trapdoor=96, MonsterEgg=97, StoneBrick=98, BrownMushroomBlock=99, RedMushroomBlock=100,
    IronBars=101, GlassPane=102, MelonBlock=103, PumpkinStem=104, MelonStem=105, Vine=106,
    FenceGate=107, BrickStairs=108, StoneBrickStairs=109, Mycelium=110, Waterlily=111,
    NetherBrick=112, NetherBrickFence=113, NetherBrickStairs=114, NetherWart=115,
    EnchantingTable=116, BrewingStand=117, Cauldron=118, EndPortal=119, EndPortalFrame=120,
    EndStone=121, DragonEgg=122, RedstoneLamp=123, LitRedstoneLamp=124, DoubleWoodenSlab=125,
    WoodenSlab=126, Cocoa=127, SandstoneStairs=128, EmeraldOre=129, EnderChest=130,
    TripwireHook=131, Tripwire=132, EmeraldBlock=133, SpruceStairs=134, BirchStairs=135,
    JungleStairs=136, CommandBlock=137, Beacon=138, CobblestoneWall=139, FlowerPot=140,
    Carrots=141, Potatoes=142, WoodenButton=143, Skull=144, Anvil=145, TrappedChest=146,
    LightWeightedPressurePlate=147, HeavyWeightedPressurePlate=148, UnpoweredComparator=149,
    PoweredComparator=150, DaylightDetector=151, RedstoneBlock=152, QuartzOre=153, Hopper=154,
    QuartzBlock=155, QuartzStairs=156, ActivatorRail=157, Dropper=158, StainedHardenedClay=159,
    StainedGlassPane=160, Leaves2=161, Log2=162, AcaciaStairs=163, DarkOakStairs=164, Slime=165,
    Barrier=166, IronTrapdoor=167, Prismarine=168, SeaLantern=169, HayBlock=170, Carpet=171,
    HardenedClay=172, CoalBlock=173, PackedIce=174, DoublePlant=175, StandingBanner=176,
    WallBanner=177, DaylightDetectorInverted=178, RedSandstone=179, RedSandstoneStairs=180,
    DoubleStoneSlab2=181, StoneSlab2=182, SpruceFenceGate=183, BirchFenceGate=184,
    JungleFenceGate=185, DarkOakFenceGate=186, AcaciaFenceGate=187, SpruceFence=188,
    BirchFence=189, JungleFence=190, DarkOakFence=191, AcaciaFence=192, SpruceDoor=193,
    BirchDoor=194, JungleDoor=195, AcaciaDoor=196, DarkOakDoor=197, EndRod=198,
    ChorusPlant=199, ChorusFlower=200, PurpurBlock=201, PurpurPillar=202, PurpurStairs=203,
    PurpurDoubleSlab=204, PurpurSlab=205, EndBricks=206, Beetroots=207, GrassPath=208,
    EndGateway=209, RepeatingCommandBlock=210, ChainCommandBlock=211, FrostedIce=212,
    Magma=213, NetherWartBlock=214, RedNetherBrick=215, BoneBlock=216, StructureVoid=217,
    Observer=218, WhiteShulkerBox=219, OrangeShulkerBox=220, MagentaShulkerBox=221,
    LightBlueShulkerBox=222, YellowShulkerBox=223, LimeShulkerBox=224, PinkShulkerBox=225,
    GrayShulkerBox=226, SilverShulkerBox=227, CyanShulkerBox=228, PurpleShulkerBox=229,
    BlueShulkerBox=230, BrownShulkerBox=231, GreenShulkerBox=232, RedShulkerBox=233,
    BlackShulkerBox=234, WhiteGlazedTerracotta=235, OrangeGlazedTerracotta=236,
    MagentaGlazedTerracotta=237, LightBlueGlazedTerracotta=238, YellowGlazedTerracotta=239,
    LimeGlazedTerracotta=240, PinkGlazedTerracotta=241, GrayGlazedTerracotta=242,
    SilverGlazedTerracotta=243, CyanGlazedTerracotta=244, PurpleGlazedTerracotta=245,
    BlueGlazedTerracotta=246, BrownGlazedTerracotta=247, GreenGlazedTerracotta=248,
    RedGlazedTerracotta=249, BlackGlazedTerracotta=250, Concrete=251, ConcretePowder=252,
    StructureBlock=255,
    CobblestoneStairs=StoneStairs, OakFence=Fence, OakFenceGate=FenceGate, Melon=MelonBlock,
};

enum class Face : std::uint8_t { Down, Up, North, South, West, East };
enum class RenderLayer : std::uint8_t { Solid, CutoutMipped, Cutout, Translucent, Count };
enum class BlockShape : std::uint8_t { Cube, Cross, Vine, Flat, SnowLayer, Cactus };

enum class TextureId : std::uint8_t {
    Stone, GrassTop, GrassSide, GrassSideOverlay, GrassSideSnowed, Dirt, Cobblestone, PlanksOak,
    Bedrock, Sand, Gravel, LogOak, LogOakTop, LeavesOak, Glass,
    WaterStill, LavaStill, SandstoneSide, SandstoneTop, SandstoneBottom,
    CoalOre, IronOre, GoldOre, LapisOre, DiamondOre, RedstoneOre, EmeraldOre,
    Snow, Ice, Clay, MyceliumSide, MyceliumTop, HardenedClay,
    StainedWhite, StainedOrange, StainedMagenta, StainedLightBlue,
    StainedYellow, StainedLime, StainedPink, StainedGray, StainedSilver,
    StainedCyan, StainedPurple, StainedBlue, StainedBrown, StainedGreen,
    StainedRed, StainedBlack, RedSandstoneSide, RedSandstoneTop,
    RedSandstoneBottom, Prismarine, SeaLantern,
    StoneGranite, StoneGraniteSmooth, StoneDiorite, StoneDioriteSmooth,
    StoneAndesite, StoneAndesiteSmooth, DirtCoarse, DirtPodzolSide, DirtPodzolTop,
    RedSand, PlanksSpruce, PlanksBirch, PlanksJungle, PlanksAcacia, PlanksDarkOak,
    LogSpruce, LogSpruceTop, LogBirch, LogBirchTop, LogJungle, LogJungleTop,
    LogAcacia, LogAcaciaTop, LogDarkOak, LogDarkOakTop,
    LeavesSpruce, LeavesBirch, LeavesJungle, LeavesAcacia, LeavesDarkOak,
    TallGrass, Fern, DeadBush, FlowerDandelion, FlowerPoppy, FlowerBlueOrchid,
    FlowerAllium, FlowerHoustonia, FlowerTulipRed, FlowerTulipOrange,
    FlowerTulipWhite, FlowerTulipPink, FlowerOxeyeDaisy, MushroomBrown,
    MushroomRed, CactusSide, CactusTop, CactusBottom, Reeds, Vine, Waterlily,
    PumpkinSide, PumpkinTop, PumpkinFace, MelonSide, MelonTop,
    CobblestoneMossy, BoneSide, BoneTop, Web, WoolWhite, Brick, Bookshelf,
    CraftingTableSide, CraftingTableTop, CraftingTableFront, FurnaceSide,
    FurnaceTop, Chest, PlanksTextureFallback, SpongeWet, Obsidian, TNTSide,
    TNTTop, TNTBottom, StoneBrick, StoneBrickMossy, StoneBrickCracked,
    StoneBrickCarved, FinalCount
};

[[nodiscard]] constexpr bool vanillaNonOpaqueCube(std::string_view name) noexcept {
    return name == "sapling" || name == "flowing_water" || name == "water" ||
        name == "flowing_lava" || name == "lava" || name == "leaves" || name == "leaves2" ||
        name == "glass" || name == "ice" || name == "stained_glass" || name == "slime" || name == "bed" || name == "golden_rail" || name == "detector_rail" ||
        name == "rail" || name == "activator_rail" || name == "web" || name == "tallgrass" ||
        name == "deadbush" || name == "piston" || name == "sticky_piston" || name == "piston_head" ||
        name == "piston_extension" || name == "yellow_flower" || name == "red_flower" ||
        name == "brown_mushroom" || name == "red_mushroom" || name == "torch" || name == "mob_spawner" || name == "chest" ||
        name == "redstone_wire" || name == "wheat" || name == "farmland" || name == "standing_sign" ||
        name == "wooden_door" || name == "ladder" || name == "wall_sign" || name == "lever" ||
        name == "stone_pressure_plate" || name == "iron_door" || name == "wooden_pressure_plate" ||
        name == "unlit_redstone_torch" || name == "redstone_torch" || name == "stone_button" ||
        name == "snow_layer" || name == "cactus" || name == "reeds" || name == "fence" ||
        name == "portal" || name == "cake" || name == "unpowered_repeater" || name == "powered_repeater" ||
        name == "stained_glass" || name == "trapdoor" || name == "iron_bars" || name == "glass_pane" ||
        name == "pumpkin_stem" || name == "melon_stem" || name == "vine" || name == "fence_gate" ||
        name == "waterlily" || name == "nether_brick_fence" || name == "nether_wart" ||
        name == "enchanting_table" || name == "brewing_stand" || name == "beacon" || name == "cauldron" ||
        name == "end_portal" || name == "end_portal_frame" || name == "dragon_egg" || name == "cocoa" ||
        name == "ender_chest" || name == "tripwire_hook" || name == "tripwire" || name == "cobblestone_wall" ||
        name == "flower_pot" || name == "wooden_button" || name == "skull" || name == "anvil" ||
        name == "trapped_chest" || name == "light_weighted_pressure_plate" ||
        name == "heavy_weighted_pressure_plate" || name == "unpowered_comparator" ||
        name == "powered_comparator" || name == "daylight_detector" || name == "hopper" ||
        name == "stained_glass_pane" || name == "carpet" || name == "double_plant" ||
        name == "standing_banner" || name == "wall_banner" || name == "daylight_detector_inverted" ||
        name == "iron_trapdoor" || name == "end_rod" || name == "chorus_plant" || name == "chorus_flower" ||
        name == "stone_slab" || name == "wooden_slab" || name == "stone_slab2" || name == "purpur_slab" || name == "beetroots" || name == "grass_path" || name == "end_gateway" ||
        name == "frosted_ice" || name == "structure_void" ||
        (name.size() >= 12 && name.substr(name.size()-12) == "_shulker_box") ||
        (name.size() >= 7 && name.substr(name.size()-7) == "_stairs") ||
        (name.size() >= 6 && name.substr(name.size()-6) == "_fence") ||
        (name.size() >= 11 && name.substr(name.size()-11) == "_fence_gate") ||
        (name.size() >= 5 && name.substr(name.size()-5) == "_door");
}

struct BlockDefinition {
    constexpr BlockDefinition() = default;
    constexpr BlockDefinition(std::string_view blockName, RenderLayer renderLayer,
                              bool renderOpaque, bool isFullCube, float blockHardness,
                              bool needsTool, std::array<TextureId, 6> faceTextures,
                              BlockShape blockShape = BlockShape::Cube)
        : name(blockName), layer(renderLayer),
          opaque(renderOpaque && !vanillaNonOpaqueCube(blockName)),
          fullCube(isFullCube),
          hardness(blockHardness), requiresTool(needsTool),
          lightOpacity(vanillaNonOpaqueCube(blockName) ? 0U : 255U),
          shape(blockShape), textures(faceTextures) {}
    constexpr BlockDefinition(std::string_view blockName, RenderLayer renderLayer,
                              bool renderOpaque, bool isFullCube, float blockHardness,
                              bool needsTool, std::uint8_t opacity, std::uint8_t emission,
                              std::array<TextureId, 6> faceTextures,
                              BlockShape blockShape = BlockShape::Cube)
        : name(blockName), layer(renderLayer),
          opaque(renderOpaque && !vanillaNonOpaqueCube(blockName)),
          fullCube(isFullCube),
          hardness(blockHardness), requiresTool(needsTool),
          lightOpacity(vanillaNonOpaqueCube(blockName) && opacity == 255U ? 0U : opacity),
          lightValue(emission), shape(blockShape), textures(faceTextures) {}

    std::string_view name;
    RenderLayer layer = RenderLayer::Solid;
    bool opaque = true;
    bool fullCube = true;
    float hardness = 0.0F;
    bool requiresTool = false;
    std::uint8_t lightOpacity = 255;
    std::uint8_t lightValue = 0;
    BlockShape shape = BlockShape::Cube;
    std::array<TextureId, 6> textures{};
};

class BlockRegistry {
public:
    static constexpr std::size_t vanillaRegisteredBlockCount = 254;
    [[nodiscard]] static const BlockDefinition& get(BlockState state);
    [[nodiscard]] static TextureId texture(BlockState state, Face face);
    [[nodiscard]] static bool hasGrassOverlay(BlockState state, Face face);
    [[nodiscard]] static bool isTinted(BlockState state, Face face);
    [[nodiscard]] static bool isRegisteredId(std::uint16_t numericId);
    [[nodiscard]] static std::string_view legacyName(std::uint16_t numericId);
    [[nodiscard]] static constexpr std::size_t registeredCount() { return vanillaRegisteredBlockCount; }
};
