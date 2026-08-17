#include "player/PlayerInventory.hpp"
#include <algorithm>

PlayerInventory::PlayerInventory() = default;

void PlayerInventory::selectHotbar(std::size_t slot) {
    if (slot < hotbarSize) selectedHotbar_ = slot;
}

void PlayerInventory::scroll(int steps) {
    if (steps == 0) return;
    int next = static_cast<int>(selectedHotbar_) - steps;
    next %= static_cast<int>(hotbarSize);
    if (next < 0) next += static_cast<int>(hotbarSize);
    selectedHotbar_ = static_cast<std::size_t>(next);
}

int PlayerInventory::findHotbarMatch(const ItemStack& stack) const {
    if (stack.empty()) return -1;
    for (std::size_t i = 0; i < hotbarSize; ++i)
        if (!main_[i].empty() && main_[i].sameItem(stack)) return static_cast<int>(i);
    return -1;
}

void PlayerInventory::pickCreative(const ItemStack& stack) {
    if (stack.empty()) return;
    const int existing = findHotbarMatch(stack);
    if (existing >= 0) {
        selectedHotbar_ = static_cast<std::size_t>(existing);
        return;
    }
    main_[selectedHotbar_] = stack;
}

void PlayerInventory::addStack(ItemStack& stack) {
    if (stack.empty()) return;
    const int maxStack = 64;
    for (ItemStack& slot : main_) {
        if (stack.empty()) return;
        if (!slot.empty() && slot.sameItem(stack) && slot.count < maxStack) {
            const int moved = std::min(maxStack - slot.count, stack.count);
            slot.count += moved;
            stack.shrink(moved);
        }
    }
    for (ItemStack& slot : main_) {
        if (stack.empty()) return;
        if (slot.empty()) {
            const int moved = std::min(maxStack, stack.count);
            slot = stack; slot.count = moved; stack.shrink(moved);
        }
    }
}
