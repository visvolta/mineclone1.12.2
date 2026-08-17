#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

struct ItemStack {
    std::uint16_t itemId = 0;
    int count = 0;
    std::uint16_t damage = 0;
    std::vector<std::uint8_t> nbt;

    [[nodiscard]] bool empty() const noexcept { return itemId == 0 || count <= 0; }
    explicit operator bool() const noexcept { return !empty(); }

    void clear() {
        itemId = 0;
        count = 0;
        damage = 0;
        nbt.clear();
    }

    void shrink(int amount) {
        count = std::max(0, count - std::max(0, amount));
        if (count == 0) clear();
    }

    [[nodiscard]] bool sameItem(const ItemStack& other) const noexcept {
        return itemId == other.itemId && damage == other.damage && nbt == other.nbt;
    }
};
