#include <cassert>
#include <filesystem>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemRegistry.hpp"
#include "player/PlayerInventory.hpp"

int main() {
    const ItemRegistry items{std::filesystem::path(BLOCKCRAFT_ASSET_ROOT)};

    assert(items.get(256).name == "iron_shovel");
    assert(items.get(264).name == "diamond");
    assert(items.get(324).name == "wooden_door");
    assert(items.get(453).name == "knowledge_book");
    assert(items.get(2256).name == "record_13");

    const ItemDefinition* stone = items.find("stone");
    assert(stone != nullptr);
    assert(stone->id == static_cast<std::uint16_t>(BlockId::Stone));
    assert(stone->placedBlock && *stone->placedBlock == BlockId::Stone);

    const ItemStack pickedStone = items.stackForBlock(
        makeBlockState(static_cast<std::uint16_t>(BlockId::Stone), 0), 64);
    assert(!pickedStone.empty());
    assert(pickedStone.itemId == static_cast<std::uint16_t>(BlockId::Stone));

    const ItemStack pickedDoor = items.stackForBlock(
        makeBlockState(static_cast<std::uint16_t>(BlockId::WoodenDoor), 0), 1);
    assert(pickedDoor.itemId == 324);

    PlayerInventory inventory;
    inventory.slot(0) = pickedStone;
    inventory.selectHotbar(0);
    assert(inventory.selected().itemId == pickedStone.itemId);
    inventory.scroll(-1);
    assert(inventory.selectedHotbar() == 1);
    inventory.pickCreative(pickedStone);
    assert(inventory.selected().sameItem(pickedStone));


    assert(static_cast<int>(CreativeTab::BuildingBlocks) == 0);
    assert(static_cast<int>(CreativeTab::Hotbar) == 4);
    assert(static_cast<int>(CreativeTab::Search) == 5);
    assert(static_cast<int>(CreativeTab::Misc) == 6);
    assert(static_cast<int>(CreativeTab::Inventory) == 11);
    const ItemDefinition& whiteShulker = items.get(static_cast<std::uint16_t>(BlockId::WhiteShulkerBox));
    assert(whiteShulker.iconResource == "minecraft:blocks/shulker_top_white");

    const ItemStack cobbleSlab{static_cast<std::uint16_t>(BlockId::StoneSlab), 64, 3, {}};
    const ItemStack brickSlab{static_cast<std::uint16_t>(BlockId::StoneSlab), 64, 4, {}};
    const ItemStack quartzSlab{static_cast<std::uint16_t>(BlockId::StoneSlab), 64, 7, {}};
    assert(items.stackDisplayName(cobbleSlab) == "Cobblestone Slab");
    assert(items.stackDisplayName(brickSlab) == "Bricks Slab");
    assert(items.stackDisplayName(quartzSlab) == "Quartz Slab");
    assert(items.stackDisplayName(ItemStack{355,1,14,{}}) == "Red Bed");
    assert(items.stackDisplayName(ItemStack{397,1,5,{}}) == "Dragon Head");
    assert(items.stackDisplayName(ItemStack{425,1,15,{}}) == "White Banner");
    assert(items.stackDisplayName(ItemStack{static_cast<std::uint16_t>(BlockId::Anvil),64,2,{}}) == "Very Damaged Anvil");
    const auto slabSearch = items.searchStacks("cobblestone slab");
    assert(!slabSearch.empty());
    assert(items.stackDisplayName(slabSearch.front()) == "Cobblestone Slab");

    assert(!items.itemsForTab(CreativeTab::BuildingBlocks).empty());
    assert(!items.search("diamond").empty());
    return 0;
}
