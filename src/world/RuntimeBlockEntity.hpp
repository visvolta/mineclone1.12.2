#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"
#include "items/ItemStack.hpp"

// Compact runtime data shared by the block-entity manager, feature logic,
// renderer and persistence layer. Keeping this data model separate prevents
// BlockEntitySystem from becoming the owner of every feature's implementation.
enum class RuntimeBlockEntityType : std::uint8_t {
    Chest,
    TrappedChest,
    Sign,
    Bed,
    ShulkerBox,
    Furnace,
    Hopper,
    BrewingStand,
    EnchantingTable,
    Beacon,
    Jukebox,
    FlowerPot,
    MobSpawner,
    EnderChest,
    Banner
};

enum class BlockEntityActionType : std::uint8_t {
    OpenChest,
    EditSign,
    Sleep,
    OpenShulker,
    OpenFurnace,
    OpenCraftingTable,
    OpenHopper,
    OpenBrewingStand,
    OpenEnchantingTable,
    OpenBeacon,
    OpenEnderChest,
    OpenJukebox,
    OpenFlowerPot
};

struct BlockEntityAction {
    BlockEntityActionType type = BlockEntityActionType::OpenChest;
    glm::ivec3 position{};
};

struct RuntimeBlockEntity {
    RuntimeBlockEntityType type = RuntimeBlockEntityType::Chest;
    glm::ivec3 position{};
    BlockState state = 0;
    std::array<ItemStack, 27> inventory{};
    std::array<std::string, 4> signText{};
    std::uint8_t color = 14;
    float previousAnimation = 0.0F;
    float animation = 0.0F;
    int viewers = 0;

    // Furnace
    int furnaceBurnTime = 0;
    int currentItemBurnTime = 0;
    int furnaceCookTime = 0;
    int furnaceCookTimeTotal = 200;
    float furnaceStoredXp = 0.0F;

    // Hopper
    int transferCooldown = 0;

    // Brewing stand
    int brewTime = 0;
    int brewingFuel = 0;

    // Beacon
    int beaconLevels = 0;
    int beaconPrimary = 0;
    int beaconSecondary = 0;

    // Jukebox / flower pot / spawner
    int recordItem = 0;
    int flowerItem = 0;
    int flowerData = 0;
    std::string spawnerEntityId = "Pig";
    int spawnerDelay = 20;
};
