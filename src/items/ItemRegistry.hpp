#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemStack.hpp"

enum class CreativeTab : std::uint8_t {
    BuildingBlocks = 0,
    Decorations = 1,
    Redstone = 2,
    Transportation = 3,
    Hotbar = 4,
    Search = 5,
    Misc = 6,
    Food = 7,
    Tools = 8,
    Combat = 9,
    Brewing = 10,
    Inventory = 11
};

struct ItemDefinition {
    std::uint16_t id = 0;
    std::string name;
    std::string displayName;
    std::string iconResource;
    std::vector<std::string> iconLayers;
    CreativeTab tab = CreativeTab::Misc;
    int maxStackSize = 64;
    bool hasSubtypes = false;
    std::optional<BlockId> placedBlock;
};

class ItemRegistry {
public:
    explicit ItemRegistry(const std::filesystem::path& assetRoot);

    [[nodiscard]] const ItemDefinition& get(std::uint16_t id) const;
    [[nodiscard]] const ItemDefinition* find(std::string_view name) const;
    [[nodiscard]] ItemStack stackForBlock(BlockState state, int count = 1) const;
    [[nodiscard]] const std::vector<std::uint16_t>& itemsForTab(CreativeTab tab) const;
    [[nodiscard]] std::vector<std::uint16_t> search(std::string_view text) const;
    [[nodiscard]] std::vector<ItemStack> creativeStacks(CreativeTab tab) const;
    [[nodiscard]] std::vector<ItemStack> searchStacks(std::string_view text) const;
    [[nodiscard]] const std::vector<ItemDefinition>& all() const { return items_; }
    [[nodiscard]] std::string stackDisplayName(const ItemStack& stack) const;

    [[nodiscard]] static std::string_view tabName(CreativeTab tab);

private:
    void registerBlockItems(const std::filesystem::path& assetRoot);
    void registerStandaloneItems(const std::filesystem::path& assetRoot);
    void add(ItemDefinition item);
    [[nodiscard]] std::string displayNameFor(std::string_view resourceName, bool block) const;
    [[nodiscard]] std::string resolveItemTexture(const std::filesystem::path& assetRoot,
                                                 std::string_view resourceName) const;
    [[nodiscard]] std::vector<std::string> resolveItemTextures(const std::filesystem::path& assetRoot,
                                                              std::string_view resourceName) const;
    [[nodiscard]] static CreativeTab inferTab(std::string_view name, std::optional<BlockId> block);
    [[nodiscard]] static int inferMaxStack(std::string_view name);
    [[nodiscard]] static std::optional<BlockId> specialPlacedBlock(std::uint16_t itemId);

    std::unordered_map<std::string, std::string> language_;
    std::vector<ItemDefinition> items_;
    std::unordered_map<std::uint16_t, std::size_t> byId_;
    std::unordered_map<std::string, std::size_t> byName_;
    std::unordered_map<std::uint32_t, std::uint16_t> blockStateToItem_;
    std::array<std::vector<std::uint16_t>, 12> creativeTabs_{};
    ItemDefinition air_{};
};
