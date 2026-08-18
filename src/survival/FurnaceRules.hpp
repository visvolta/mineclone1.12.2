#pragma once

#include <optional>

#include "items/ItemStack.hpp"

struct SmeltingRecipe {
    ItemStack result{};
    float experience = 0.0F;
};

namespace FurnaceRules {
[[nodiscard]] std::optional<SmeltingRecipe> recipe(const ItemStack& input);
[[nodiscard]] int fuelBurnTime(const ItemStack& fuel);
[[nodiscard]] bool isFuel(const ItemStack& fuel);
}
