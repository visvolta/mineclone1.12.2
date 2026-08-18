#include <cassert>
#include <cmath>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemStack.hpp"
#include "player/Player.hpp"
#include "player/PlayerInventory.hpp"
#include "survival/FoodStats.hpp"
#include "survival/MiningRules.hpp"
#include "world/World.hpp"

int main() {
    const auto wood=SurvivalRules::toolStats(270), iron=SurvivalRules::toolStats(257);
    assert(wood.toolClass==ToolClass::Pickaxe && wood.harvestLevel==0 && wood.maxUses==59);
    assert(iron.harvestLevel==2 && iron.efficiency==6.0F && iron.maxUses==250);
    const BlockState stone=makeBlockState(static_cast<std::uint16_t>(BlockId::Stone));
    const auto hand=SurvivalRules::mining(stone,{},1.5F);
    assert(!hand.canHarvest);
    assert(std::abs(hand.relativeHardness-(1.0F/1.5F/100.0F))<1e-6F);

    FoodStats food;
    food.restore(20,5.0F,0.0F);
    for(int i=0;i<10;++i) food.tick(true,18.0F,true);
    assert(food.consumePendingHeal()>0.0F); // fast saturated regeneration
    food.restore(18,0.0F,0.0F);
    for(int i=0;i<80;++i) food.tick(true,18.0F,true);
    assert(std::abs(food.consumePendingHeal()-1.0F)<1e-6F);
    food.restore(0,0.0F,0.0F);
    for(int i=0;i<80;++i) food.tick(false,10.0F,true);
    assert(std::abs(food.consumePendingStarveDamage()-1.0F)<1e-6F);

    World world;
    Player player(glm::dvec3(0.5,80.0,0.5));
    assert(player.hurt(4.0F,DamageType::Generic));
    assert(player.health()<20.0F && player.hurtTime()==player.maxHurtTime());
    assert(player.hurtCameraStrength(0.0F)>=0.0F);
    player.respawn();
    assert(player.health()==20.0F && player.foodStats().foodLevel()==20);
    std::cout << "Player/survival parity tests passed.\n";
}
