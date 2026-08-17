#include <cassert>
#include <cmath>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemStack.hpp"
#include "player/PlayerInventory.hpp"
#include "survival/MiningRules.hpp"

int main(){
    const auto wood=SurvivalRules::toolStats(270), iron=SurvivalRules::toolStats(257), diamond=SurvivalRules::toolStats(278), gold=SurvivalRules::toolStats(285);
    assert(wood.toolClass==ToolClass::Pickaxe&&wood.harvestLevel==0&&wood.efficiency==2.0F&&wood.maxUses==59);
    assert(iron.harvestLevel==2&&iron.efficiency==6.0F&&iron.maxUses==250);
    assert(diamond.harvestLevel==3&&diamond.efficiency==8.0F&&diamond.maxUses==1561);
    assert(gold.harvestLevel==0&&gold.efficiency==12.0F&&gold.maxUses==32);
    const BlockState stone=makeBlockState(static_cast<std::uint16_t>(BlockId::Stone));
    const BlockState ironOre=makeBlockState(static_cast<std::uint16_t>(BlockId::IronOre));
    const auto hand=SurvivalRules::mining(stone,{},1.5F);
    const auto pick=SurvivalRules::mining(stone,{257,1,0,{}},1.5F);
    assert(!hand.canHarvest);
    assert(std::abs(hand.relativeHardness-(1.0F/1.5F/100.0F))<1e-6F);
    assert(std::abs(pick.relativeHardness-(6.0F/1.5F/30.0F))<1e-6F);
    assert(!SurvivalRules::canHarvest(ironOre,{270,1,0,{}}));
    assert(SurvivalRules::canHarvest(ironOre,{274,1,0,{}}));
    ItemStack tool{270,1,58,{}}; assert(SurvivalRules::damageTool(tool)); assert(tool.empty());
    assert(SurvivalRules::armorPoints(311)==8); assert(SurvivalRules::armorToughness(311)==2.0F);
    assert(SurvivalRules::isArmorForSlot(313,0)); assert(SurvivalRules::isArmorForSlot(310,3));
    PlayerInventory inventory; ItemStack logs{17,70,0,{}}; inventory.addStack(logs); assert(inventory.slot(0).count==64); assert(inventory.slot(1).count==6); assert(logs.empty());
    std::cout<<"Stage 9 survival/mining tests passed.\n";
}
