#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "crafting/CraftingSystem.hpp"
#include "items/ItemRegistry.hpp"
#include "player/PlayerInventory.hpp"
#include "survival/FoodStats.hpp"
#include "survival/FurnaceRules.hpp"

int main() {
    ItemRegistry items(BLOCKCRAFT_ASSET_ROOT);
    CraftingSystem crafting(BLOCKCRAFT_ASSET_ROOT, items);
    assert(crafting.recipeCount() > 300);

    std::vector<ItemStack> twoByTwo(4);
    for(ItemStack& s:twoByTwo) s={5,1,0,{}};
    const CraftingMatch table=crafting.match(twoByTwo,2,2);
    assert(table.output.itemId==58 && table.output.count==1);

    std::vector<ItemStack> pick(9);
    pick[0]={5,1,0,{}}; pick[1]={5,1,0,{}}; pick[2]={5,1,0,{}};
    pick[4]={280,1,0,{}}; pick[7]={280,1,0,{}};
    const CraftingMatch woodenPick=crafting.match(pick,3,3);
    assert(woodenPick.output.itemId==270);

    const auto iron=FurnaceRules::recipe(ItemStack{15,1,0,{}});
    assert(iron && iron->result.itemId==265 && std::abs(iron->experience-0.7F)<1e-6F);
    assert(FurnaceRules::fuelBurnTime(ItemStack{263,1,0,{}})==1600);
    assert(FurnaceRules::fuelBurnTime(ItemStack{327,1,0,{}})==20000);

    const ItemDefinition& chest=items.get(54);
    assert(chest.id==54 && chest.placedBlock && *chest.placedBlock==BlockId::Chest);

    // Inventory insertion must respect per-item stack limits. Tools are max-stack 1.
    PlayerInventory inventory;
    ItemStack twoPickaxes{257,2,0,{}};
    inventory.addStack(twoPickaxes, items.get(257).maxStackSize);
    assert(twoPickaxes.empty());
    assert(inventory.slot(0).itemId==257 && inventory.slot(0).count==1);
    assert(inventory.slot(1).itemId==257 && inventory.slot(1).count==1);
    assert(!items.stackDisplayName(ItemStack{44,1,3,{}}).empty());
    std::cout << "Stage 10 gameplay parity tests passed.\n";
}
