#pragma once

#include <array>
#include <cstddef>

#include "items/ItemStack.hpp"

class PlayerInventory {
public:
    static constexpr std::size_t mainSize = 36;
    static constexpr std::size_t hotbarSize = 9;

    PlayerInventory();

    [[nodiscard]] ItemStack& slot(std::size_t index) { return main_[index < mainSize ? index : 0]; }
    [[nodiscard]] const ItemStack& slot(std::size_t index) const { return main_[index < mainSize ? index : 0]; }
    [[nodiscard]] ItemStack& selected() { return main_[selectedHotbar_]; }
    [[nodiscard]] ItemStack& armor(std::size_t index) { return armor_[index < armor_.size() ? index : 0]; }
    [[nodiscard]] const ItemStack& armor(std::size_t index) const { return armor_[index < armor_.size() ? index : 0]; }
    [[nodiscard]] const ItemStack& selected() const { return main_[selectedHotbar_]; }
    [[nodiscard]] std::size_t selectedHotbar() const { return selectedHotbar_; }
    void selectHotbar(std::size_t slot);
    void scroll(int steps);
    void pickCreative(const ItemStack& stack);
    void addStack(ItemStack& stack);
    [[nodiscard]] int findHotbarMatch(const ItemStack& stack) const;

private:
    std::array<ItemStack, mainSize> main_{};
    std::array<ItemStack, 4> armor_{}; // boots, leggings, chestplate, helmet
    std::size_t selectedHotbar_ = 0;
};
