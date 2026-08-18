#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "items/ItemStack.hpp"

class ItemRegistry;

struct CraftingMatch {
    ItemStack output{};
    std::vector<ItemStack> remainders;
    std::string recipeName;
};

class CraftingSystem {
public:
    CraftingSystem(const std::filesystem::path& assetRoot, const ItemRegistry& items);

    [[nodiscard]] CraftingMatch match(const std::vector<ItemStack>& grid, int width, int height) const;
    bool takeResult(std::vector<ItemStack>& grid, int width, int height, ItemStack& destination,
                    std::vector<ItemStack>* overflowRemainders = nullptr) const;
    [[nodiscard]] std::size_t recipeCount() const noexcept { return recipes_.size(); }

private:
    struct IngredientOption { std::uint16_t itemId=0; int data=-1; };
    using Ingredient = std::vector<IngredientOption>;
    struct Recipe {
        std::string name;
        bool shaped=false;
        int width=0, height=0;
        std::vector<Ingredient> ingredients;
        ItemStack result{};
    };

    [[nodiscard]] Ingredient parseIngredient(const class JsonValue& value) const;
    [[nodiscard]] bool ingredientMatches(const Ingredient& ingredient, const ItemStack& stack) const;
    [[nodiscard]] bool shapedMatches(const Recipe& recipe, const std::vector<ItemStack>& grid,
                                     int width, int height, int offsetX, int offsetY, bool mirror) const;
    [[nodiscard]] bool shapelessMatches(const Recipe& recipe, const std::vector<ItemStack>& grid) const;
    [[nodiscard]] ItemStack remainderFor(const ItemStack& consumed) const;

    const ItemRegistry& items_;
    std::vector<Recipe> recipes_;
};
