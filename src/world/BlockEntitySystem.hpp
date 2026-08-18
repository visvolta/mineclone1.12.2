#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"
#include "items/ItemStack.hpp"

class World;
class Player;
struct RaycastHit;

// Runtime subset of vanilla 1.12.2 TileEntity data used by the first visible
// block-entity milestone. Persistence is intentionally left to the save stage,
// but instances survive normal chunk streaming for the current process.
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
    std::uint8_t color = 14; // EnumDyeColor.RED metadata, vanilla bed default.
    float previousAnimation = 0.0F;
    float animation = 0.0F;
    int viewers = 0;
    int furnaceBurnTime = 0;
    int currentItemBurnTime = 0;
    int furnaceCookTime = 0;
    int furnaceCookTimeTotal = 200;
    float furnaceStoredXp = 0.0F;
    int transferCooldown = 0;
    int brewTime = 0;
    int brewingFuel = 0;
    int beaconLevels = 0;
    int beaconPrimary = 0;
    int beaconSecondary = 0;
    int recordItem = 0;
    int flowerItem = 0;
    int flowerData = 0;
    std::string spawnerEntityId = "Pig";
    int spawnerDelay = 20;
};

class BlockEntitySystem {
public:
    void scanLoadedWorld(const World& world);
    void scanChunk(const World& world, int chunkX, int chunkZ);
    void blockChanged(const World& world, const glm::ivec3& position,
                      BlockState oldState, BlockState newState);
    void rescanPosition(const World& world, const glm::ivec3& position);
    void placedFromItem(const glm::ivec3& position, BlockState state, const ItemStack& stack);
    [[nodiscard]] std::vector<glm::ivec3> tick(World& world);
    void restore(RuntimeBlockEntity entity);

    [[nodiscard]] RuntimeBlockEntity* find(const glm::ivec3& position);
    [[nodiscard]] const RuntimeBlockEntity* find(const glm::ivec3& position) const;
    [[nodiscard]] const std::unordered_map<std::uint64_t, RuntimeBlockEntity>& entities() const { return entities_; }

    [[nodiscard]] std::optional<BlockEntityAction> activate(const World& world,
                                                            const RaycastHit& hit) const;
    bool useSpecial(World& world, const RaycastHit& hit, Player& player, ItemStack& ejected);

    void beginViewing(const BlockEntityAction& action);
    void endViewing(const BlockEntityAction& action);

    [[nodiscard]] int containerSlotCount(const World& world, const glm::ivec3& position) const;
    [[nodiscard]] ItemStack& containerSlot(const World& world, const glm::ivec3& position, int index);
    [[nodiscard]] const ItemStack& containerSlot(const World& world, const glm::ivec3& position, int index) const;
    [[nodiscard]] std::string containerTitle(const World& world, const glm::ivec3& position) const;

    [[nodiscard]] std::array<std::string, 4>* signLines(const glm::ivec3& position);
    [[nodiscard]] const std::array<std::string, 4>* signLines(const glm::ivec3& position) const;

    [[nodiscard]] float animation(const glm::ivec3& position, float partialTick) const;
    [[nodiscard]] bool furnaceBurning(const glm::ivec3& position) const;
    [[nodiscard]] float furnaceCookProgress(const glm::ivec3& position) const;
    [[nodiscard]] float furnaceBurnProgress(const glm::ivec3& position) const;
    float takeFurnaceExperience(const glm::ivec3& position);
    [[nodiscard]] float brewingProgress(const glm::ivec3& position) const;
    [[nodiscard]] int brewingFuel(const glm::ivec3& position) const;
    [[nodiscard]] int beaconLevels(const glm::ivec3& position) const;
    [[nodiscard]] const std::array<ItemStack,27>& enderChestInventory() const { return enderChestInventory_; }
    void setEnderChestInventory(std::array<ItemStack,27> inventory) { enderChestInventory_ = std::move(inventory); }

private:
    [[nodiscard]] static std::uint64_t key(const glm::ivec3& position);
    [[nodiscard]] static std::optional<RuntimeBlockEntityType> typeFor(BlockState state);
    void ensure(const glm::ivec3& position, BlockState state);
    [[nodiscard]] std::optional<glm::ivec3> pairedChest(const World& world,
                                                       const glm::ivec3& position) const;

    bool moveOneItem(World& world, RuntimeBlockEntity& source, RuntimeBlockEntity& destination);
    void tickHopper(World& world, RuntimeBlockEntity& entity);
    void tickBrewing(RuntimeBlockEntity& entity);
    void tickBeacon(const World& world, RuntimeBlockEntity& entity);

    std::unordered_map<std::uint64_t, RuntimeBlockEntity> entities_;
    std::array<ItemStack, 27> enderChestInventory_{};
};
