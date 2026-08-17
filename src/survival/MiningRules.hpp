#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "blocks/BlockState.hpp"
#include "items/ItemStack.hpp"

class ItemRegistry;

enum class ToolClass : std::uint8_t { None, Pickaxe, Axe, Shovel, Sword, Hoe, Shears };
enum class BlockMaterial : std::uint8_t { Air, Rock, Iron, Anvil, Wood, Ground, Grass, Sand, Plant, Leaves, Web, Glass, Other };

struct ToolStats {
    ToolClass toolClass = ToolClass::None;
    int harvestLevel = -1;
    float efficiency = 1.0F;
    int maxUses = 0;
};

struct MiningResult {
    float relativeHardness = 0.0F;
    float destroySpeed = 1.0F;
    bool canHarvest = true;
};

namespace SurvivalRules {
[[nodiscard]] ToolStats toolStats(std::uint16_t itemId);
[[nodiscard]] bool damageTool(ItemStack& stack, int amount = 1);
[[nodiscard]] BlockMaterial material(BlockState state);
[[nodiscard]] bool canHarvest(BlockState state, const ItemStack& held);
[[nodiscard]] float destroySpeed(BlockState state, const ItemStack& held);
[[nodiscard]] MiningResult mining(BlockState state, const ItemStack& held, float hardness);
[[nodiscard]] std::optional<ItemStack> primaryDrop(BlockState state, const ItemRegistry& items,
                                                   bool canHarvest, bool silkTouch = false,
                                                   int fortuneLevel = 0);
[[nodiscard]] int armorPoints(std::uint16_t itemId);
[[nodiscard]] float armorToughness(std::uint16_t itemId);
[[nodiscard]] bool isArmorForSlot(std::uint16_t itemId, int armorSlot);
}
