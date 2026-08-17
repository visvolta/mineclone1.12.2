#include "survival/MiningRules.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <cmath>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemRegistry.hpp"

namespace {
ToolStats tool(ToolClass type, int level, float speed, int uses) { return {type, level, speed, uses}; }

bool oneOf(BlockId id, std::initializer_list<BlockId> ids) {
    for (BlockId v : ids) if (id == v) return true;
    return false;
}

bool pickaxeEffective(BlockId id) {
    return oneOf(id, {BlockId::ActivatorRail,BlockId::CoalOre,BlockId::Cobblestone,BlockId::DetectorRail,
        BlockId::DiamondBlock,BlockId::DiamondOre,BlockId::DoubleStoneSlab,BlockId::GoldenRail,
        BlockId::GoldBlock,BlockId::GoldOre,BlockId::Ice,BlockId::IronBlock,BlockId::IronOre,
        BlockId::LapisBlock,BlockId::LapisOre,BlockId::LitRedstoneOre,BlockId::MossyCobblestone,
        BlockId::Netherrack,BlockId::PackedIce,BlockId::Rail,BlockId::RedstoneOre,BlockId::Sandstone,
        BlockId::RedSandstone,BlockId::Stone,BlockId::StoneSlab,BlockId::StoneButton,BlockId::StonePressurePlate});
}

bool axeEffective(BlockId id) {
    return oneOf(id, {BlockId::Planks,BlockId::Bookshelf,BlockId::Log,BlockId::Log2,BlockId::Chest,
        BlockId::TrappedChest,BlockId::CraftingTable,BlockId::Fence,BlockId::FenceGate,
        BlockId::SpruceFence,BlockId::BirchFence,BlockId::JungleFence,BlockId::DarkOakFence,
        BlockId::AcaciaFence,BlockId::SpruceFenceGate,BlockId::BirchFenceGate,BlockId::JungleFenceGate,
        BlockId::DarkOakFenceGate,BlockId::AcaciaFenceGate,BlockId::WoodenDoor,BlockId::SpruceDoor,
        BlockId::BirchDoor,BlockId::JungleDoor,BlockId::AcaciaDoor,BlockId::DarkOakDoor,BlockId::WoodenSlab,
        BlockId::DoubleWoodenSlab,BlockId::WoodenPressurePlate,BlockId::WoodenButton,BlockId::OakStairs,BlockId::SpruceStairs,BlockId::BirchStairs,BlockId::JungleStairs,BlockId::AcaciaStairs,BlockId::DarkOakStairs,BlockId::NoteBlock,BlockId::Jukebox});
}

bool shovelEffective(BlockId id) {
    return oneOf(id, {BlockId::Clay,BlockId::Dirt,BlockId::Farmland,BlockId::Grass,BlockId::Gravel,
        BlockId::Mycelium,BlockId::Sand,BlockId::Snow,BlockId::SnowLayer,BlockId::SoulSand,BlockId::GrassPath,
        BlockId::ConcretePowder});
}
}

ToolStats SurvivalRules::toolStats(std::uint16_t id) {
    switch (id) {
        case 269:return tool(ToolClass::Shovel,0,2,59); case 270:return tool(ToolClass::Pickaxe,0,2,59); case 271:return tool(ToolClass::Axe,0,2,59);
        case 273:return tool(ToolClass::Shovel,1,4,131); case 274:return tool(ToolClass::Pickaxe,1,4,131); case 275:return tool(ToolClass::Axe,1,4,131);
        case 256:return tool(ToolClass::Shovel,2,6,250); case 257:return tool(ToolClass::Pickaxe,2,6,250); case 258:return tool(ToolClass::Axe,2,6,250);
        case 277:return tool(ToolClass::Shovel,3,8,1561); case 278:return tool(ToolClass::Pickaxe,3,8,1561); case 279:return tool(ToolClass::Axe,3,8,1561);
        case 284:return tool(ToolClass::Shovel,0,12,32); case 285:return tool(ToolClass::Pickaxe,0,12,32); case 286:return tool(ToolClass::Axe,0,12,32);
        case 268:return tool(ToolClass::Sword,0,1.5F,59); case 272:return tool(ToolClass::Sword,1,1.5F,131); case 267:return tool(ToolClass::Sword,2,1.5F,250); case 276:return tool(ToolClass::Sword,3,1.5F,1561); case 283:return tool(ToolClass::Sword,0,1.5F,32);
        case 290:return tool(ToolClass::Hoe,0,1,59); case 291:return tool(ToolClass::Hoe,1,1,131); case 292:return tool(ToolClass::Hoe,2,1,250); case 293:return tool(ToolClass::Hoe,3,1,1561); case 294:return tool(ToolClass::Hoe,0,1,32);
        case 359:return tool(ToolClass::Shears,0,1,238);
        default:return {};
    }
}

bool SurvivalRules::damageTool(ItemStack& stack, int amount) {
    const ToolStats stats=toolStats(stack.itemId);
    if (stack.empty() || stats.maxUses<=0 || amount<=0) return false;
    stack.damage=static_cast<std::uint16_t>(stack.damage+amount);
    if (stack.damage>=stats.maxUses) { stack.clear(); return true; }
    return false;
}

BlockMaterial SurvivalRules::material(BlockState state) {
    const BlockId id=static_cast<BlockId>(blockId(state));
    if (id==BlockId::Air) return BlockMaterial::Air;
    if (oneOf(id,{BlockId::Stone,BlockId::Cobblestone,BlockId::Sandstone,BlockId::RedSandstone,BlockId::StoneBrick,
        BlockId::BrickBlock,BlockId::Netherrack,BlockId::NetherBrick,BlockId::QuartzOre,BlockId::QuartzBlock,
        BlockId::EndStone,BlockId::EndBricks,BlockId::Prismarine,BlockId::HardenedClay,BlockId::StainedHardenedClay,
        BlockId::Concrete,BlockId::CoalOre,BlockId::IronOre,BlockId::GoldOre,BlockId::LapisOre,BlockId::DiamondOre,
        BlockId::EmeraldOre,BlockId::RedstoneOre,BlockId::LitRedstoneOre,BlockId::Obsidian,BlockId::MossyCobblestone,
        BlockId::StoneSlab,BlockId::DoubleStoneSlab,BlockId::StoneSlab2,BlockId::DoubleStoneSlab2,BlockId::PurpurBlock,
        BlockId::PurpurPillar,BlockId::PurpurSlab,BlockId::PurpurDoubleSlab,
        BlockId::StoneStairs,BlockId::BrickStairs,BlockId::StoneBrickStairs,BlockId::NetherBrickStairs,
        BlockId::SandstoneStairs,BlockId::QuartzStairs,BlockId::RedSandstoneStairs,BlockId::PurpurStairs,
        BlockId::CobblestoneWall,BlockId::Furnace,BlockId::LitFurnace,BlockId::Dispenser,BlockId::Dropper,
        BlockId::Observer,BlockId::EnchantingTable,BlockId::EndPortalFrame})) return BlockMaterial::Rock;
    if (oneOf(id,{BlockId::IronBlock,BlockId::GoldBlock,BlockId::DiamondBlock,BlockId::EmeraldBlock,BlockId::LapisBlock,
        BlockId::IronBars,BlockId::IronDoor,BlockId::Hopper,BlockId::Cauldron,BlockId::Rail,BlockId::GoldenRail,
        BlockId::DetectorRail,BlockId::ActivatorRail})) return BlockMaterial::Iron;
    if (id==BlockId::Anvil) return BlockMaterial::Anvil;
    if (axeEffective(id)) return BlockMaterial::Wood;
    if (oneOf(id,{BlockId::Dirt,BlockId::Farmland,BlockId::Mycelium,BlockId::GrassPath,BlockId::Clay})) return BlockMaterial::Ground;
    if (id==BlockId::Grass) return BlockMaterial::Grass;
    if (oneOf(id,{BlockId::Sand,BlockId::Gravel,BlockId::SoulSand,BlockId::ConcretePowder})) return BlockMaterial::Sand;
    if (oneOf(id,{BlockId::Leaves,BlockId::Leaves2})) return BlockMaterial::Leaves;
    if (id==BlockId::Web) return BlockMaterial::Web;
    if (oneOf(id,{BlockId::Glass,BlockId::StainedGlass,BlockId::GlassPane,BlockId::StainedGlassPane})) return BlockMaterial::Glass;
    if (!BlockRegistry::get(state).fullCube) return BlockMaterial::Plant;
    return BlockMaterial::Other;
}

bool SurvivalRules::canHarvest(BlockState state,const ItemStack& held) {
    const BlockId id=static_cast<BlockId>(blockId(state));
    const ToolStats t=toolStats(held.itemId);
    const BlockMaterial m=material(state);
    if (m!=BlockMaterial::Rock && m!=BlockMaterial::Iron && m!=BlockMaterial::Anvil) return true;
    if (t.toolClass!=ToolClass::Pickaxe) return false;
    if (id==BlockId::Obsidian) return t.harvestLevel==3;
    if (oneOf(id,{BlockId::DiamondBlock,BlockId::DiamondOre,BlockId::EmeraldOre,BlockId::EmeraldBlock,BlockId::GoldBlock,BlockId::GoldOre,BlockId::RedstoneOre,BlockId::LitRedstoneOre})) return t.harvestLevel>=2;
    if (oneOf(id,{BlockId::IronBlock,BlockId::IronOre,BlockId::LapisBlock,BlockId::LapisOre})) return t.harvestLevel>=1;
    return true;
}

float SurvivalRules::destroySpeed(BlockState state,const ItemStack& held) {
    const ToolStats t=toolStats(held.itemId);
    const BlockId id=static_cast<BlockId>(blockId(state));
    const BlockMaterial m=material(state);
    if (t.toolClass==ToolClass::Pickaxe && (m==BlockMaterial::Rock||m==BlockMaterial::Iron||m==BlockMaterial::Anvil||pickaxeEffective(id))) return t.efficiency;
    if (t.toolClass==ToolClass::Axe && (m==BlockMaterial::Wood||axeEffective(id))) return t.efficiency;
    if (t.toolClass==ToolClass::Shovel && shovelEffective(id)) return t.efficiency;
    if (t.toolClass==ToolClass::Sword) { if(id==BlockId::Web) return 15.0F; if(m==BlockMaterial::Plant||m==BlockMaterial::Leaves||id==BlockId::Pumpkin||id==BlockId::MelonBlock||id==BlockId::Vine) return 1.5F; }
    if (t.toolClass==ToolClass::Shears) { if(id==BlockId::Web||m==BlockMaterial::Leaves) return 15.0F; if(id==BlockId::Wool||id==BlockId::Carpet) return 5.0F; }
    return 1.0F;
}

MiningResult SurvivalRules::mining(BlockState state,const ItemStack& held,float hardness) {
    MiningResult r; r.destroySpeed=destroySpeed(state,held); r.canHarvest=canHarvest(state,held);
    if (hardness<0) return r;
    r.relativeHardness=r.destroySpeed/std::max(hardness,1.0e-6F)/(r.canHarvest?30.0F:100.0F);
    return r;
}

std::optional<ItemStack> SurvivalRules::primaryDrop(BlockState state,const ItemRegistry& items,bool harvest,bool silk,int fortune) {
    if (!harvest) return std::nullopt;
    const BlockId id=static_cast<BlockId>(blockId(state));
    if (id==BlockId::Air||id==BlockId::Bedrock||id==BlockId::Barrier||id==BlockId::StructureVoid||id==BlockId::Fire) return std::nullopt;
    if (!silk) {
        if (id==BlockId::Leaves || id==BlockId::Leaves2 || id==BlockId::Glass || id==BlockId::StainedGlass || id==BlockId::GlassPane || id==BlockId::StainedGlassPane || id==BlockId::Cake) return std::nullopt;
        if (id==BlockId::Stone) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::Cobblestone)),1);
        if (id==BlockId::Farmland || id==BlockId::GrassPath) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::Dirt)),1);
        if (id==BlockId::DoubleStoneSlab) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::StoneSlab),blockMetadata(state)&7U),2);
        if (id==BlockId::DoubleStoneSlab2) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::StoneSlab2),0),2);
        if (id==BlockId::DoubleWoodenSlab) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::WoodenSlab),blockMetadata(state)&7U),2);
        if (id==BlockId::PurpurDoubleSlab) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::PurpurSlab),0),2);
        if (id==BlockId::Grass) return items.stackForBlock(makeBlockState(static_cast<std::uint16_t>(BlockId::Dirt)),1);
        if (id==BlockId::CoalOre) return ItemStack{263,1,0,{}};
        if (id==BlockId::DiamondOre) return ItemStack{264,1+std::max(0,fortune),0,{}};
        if (id==BlockId::EmeraldOre) return ItemStack{388,1+std::max(0,fortune),0,{}};
        if (id==BlockId::QuartzOre) return ItemStack{406,1+std::max(0,fortune),0,{}};
        if (id==BlockId::RedstoneOre||id==BlockId::LitRedstoneOre) return ItemStack{331,4+std::max(0,fortune),0,{}};
        if (id==BlockId::LapisOre) return ItemStack{351,4+std::max(0,fortune),4,{}};
        if (id==BlockId::Gravel) return items.stackForBlock(state,1); // flint chance waits for deterministic RNG/drop context
    }
    ItemStack stack=items.stackForBlock(state,1);
    if (stack.empty()) return std::nullopt;
    stack.count=1;
    return stack;
}

int SurvivalRules::armorPoints(std::uint16_t id) {
    switch(id){
        case 298:return 1;case 299:return 3;case 300:return 2;case 301:return 1;
        case 302:return 2;case 303:return 5;case 304:return 4;case 305:return 1;
        case 306:return 2;case 307:return 6;case 308:return 5;case 309:return 2;
        case 310:return 3;case 311:return 8;case 312:return 6;case 313:return 3;
        case 314:return 2;case 315:return 5;case 316:return 3;case 317:return 1;
        default:return 0;
    }
}
float SurvivalRules::armorToughness(std::uint16_t id){return id>=310&&id<=313?2.0F:0.0F;}
bool SurvivalRules::isArmorForSlot(std::uint16_t id,int slot){
    if(slot<0||slot>3)return false; // 0 boots, 1 legs, 2 chest, 3 head
    const int part=(id>=298&&id<=317)?static_cast<int>((id-298)%4):-1;
    const int expected=3-slot; return part==expected;
}
