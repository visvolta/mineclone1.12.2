#include "world/BlockEntitySystem.hpp"

#include <algorithm>
#include <stdexcept>

#include "blocks/BlockRegistry.hpp"
#include "world/Chunk.hpp"
#include "world/BlockEntityLogic.hpp"
#include "world/Raycast.hpp"
#include "save/Nbt.hpp"
#include "survival/FurnaceRules.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {

bool isShulker(BlockId id) {
    const auto value = static_cast<std::uint16_t>(id);
    return value >= static_cast<std::uint16_t>(BlockId::WhiteShulkerBox) &&
           value <= static_cast<std::uint16_t>(BlockId::BlackShulkerBox);
}

bool isChest(BlockId id) {
    return id == BlockId::Chest || id == BlockId::TrappedChest;
}

glm::ivec3 facingOffset(std::uint8_t metadata) {
    // EnumFacing#getIndex: DOWN, UP, NORTH, SOUTH, WEST, EAST.
    switch (metadata & 7U) {
        case 0: return {0, -1, 0};
        case 1: return {0, 1, 0};
        case 2: return {0, 0, -1};
        case 3: return {0, 0, 1};
        case 4: return {-1, 0, 0};
        case 5: return {1, 0, 0};
        default: return {0, 1, 0};
    }
}

bool normalCube(BlockState state) {
    if (blockId(state) == 0) return false;
    const BlockDefinition& definition = BlockRegistry::get(state);
    return definition.opaque && definition.fullCube;
}


} // namespace

std::uint64_t BlockEntitySystem::key(const glm::ivec3& p) {
    // Minecraft coordinates used by this project are far inside these signed
    // ranges; bias and pack to keep lookups allocation-free.
    const std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.x) + 0x2000000LL) & 0x3FFFFFFULL;
    const std::uint64_t z = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.z) + 0x2000000LL) & 0x3FFFFFFULL;
    const std::uint64_t y = static_cast<std::uint64_t>(p.y) & 0xFFFULL;
    return (x << 38U) | (z << 12U) | y;
}

std::optional<RuntimeBlockEntityType> BlockEntitySystem::typeFor(BlockState state) {
    const BlockId id = static_cast<BlockId>(blockId(state));
    if (id == BlockId::Chest) return RuntimeBlockEntityType::Chest;
    if (id == BlockId::TrappedChest) return RuntimeBlockEntityType::TrappedChest;
    if (id == BlockId::StandingSign || id == BlockId::WallSign) return RuntimeBlockEntityType::Sign;
    if (id == BlockId::Bed) return RuntimeBlockEntityType::Bed;
    if (isShulker(id)) return RuntimeBlockEntityType::ShulkerBox;
    if (id == BlockId::Furnace || id == BlockId::LitFurnace) return RuntimeBlockEntityType::Furnace;
    if (id == BlockId::Hopper) return RuntimeBlockEntityType::Hopper;
    if (id == BlockId::BrewingStand) return RuntimeBlockEntityType::BrewingStand;
    if (id == BlockId::EnchantingTable) return RuntimeBlockEntityType::EnchantingTable;
    if (id == BlockId::Beacon) return RuntimeBlockEntityType::Beacon;
    if (id == BlockId::Jukebox) return RuntimeBlockEntityType::Jukebox;
    if (id == BlockId::FlowerPot) return RuntimeBlockEntityType::FlowerPot;
    if (id == BlockId::MobSpawner) return RuntimeBlockEntityType::MobSpawner;
    if (id == BlockId::EnderChest) return RuntimeBlockEntityType::EnderChest;
    if (id == BlockId::StandingBanner || id == BlockId::WallBanner) return RuntimeBlockEntityType::Banner;
    if (id == BlockId::Dispenser) return RuntimeBlockEntityType::Dispenser;
    if (id == BlockId::Dropper) return RuntimeBlockEntityType::Dropper;
    return std::nullopt;
}

void BlockEntitySystem::ensure(const glm::ivec3& position, BlockState state) {
    const auto type = typeFor(state);
    if (!type) return;
    const auto k = key(position);
    const auto found = entities_.find(k);
    if (found != entities_.end()) {
        found->second.state = state;
        return;
    }
    RuntimeBlockEntity entity;
    entity.type = *type;
    entity.position = position;
    entity.state = state;
    if (entity.type == RuntimeBlockEntityType::ShulkerBox) {
        const int numeric = static_cast<int>(blockId(state));
        entity.color = static_cast<std::uint8_t>(std::clamp(numeric - static_cast<int>(BlockId::WhiteShulkerBox), 0, 15));
    }
    entities_.emplace(k, std::move(entity));
}

void BlockEntitySystem::scanChunk(const World& world, int chunkX, int chunkZ) {
    const Chunk* chunk = world.findChunk(chunkX, chunkZ);
    if (chunk == nullptr) return;
    for (int y = 0; y < chunkHeight; ++y) {
        for (int z = 0; z < chunkSize; ++z) {
            for (int x = 0; x < chunkSize; ++x) {
                const BlockState state = chunk->get(x, y, z);
                if (!typeFor(state)) continue;
                ensure({chunkX * 16 + x, y, chunkZ * 16 + z}, state);
            }
        }
    }

    // Anvil-loaded TileEntities are retained by Chunk as uncompressed NBT.
    // Restore the Stage 7 runtime state after the block scan created the
    // matching runtime object. Generated structure tile entities remain
    // available even when they are not one of the currently interactive types.
    for (const GeneratedBlockEntity& generated : chunk->blockEntities()) {
        try {
            const nbt::Document document = nbt::decode(generated.nbt);
            const nbt::Compound& c = document.root.compound();
            const glm::ivec3 position{
                static_cast<int>(nbt::integer(c, "x", generated.x)),
                static_cast<int>(nbt::integer(c, "y", generated.y)),
                static_cast<int>(nbt::integer(c, "z", generated.z))};
            RuntimeBlockEntity* entity = find(position);
            if (entity == nullptr) continue;
            if (entity->type == RuntimeBlockEntityType::Sign) {
                for (int line = 0; line < 4; ++line) {
                    std::string text = nbt::string(c, "Text" + std::to_string(line + 1), "");
                    const std::string marker = "\"text\":\"";
                    const std::size_t begin = text.find(marker);
                    if (begin != std::string::npos) {
                        const std::size_t first = begin + marker.size();
                        const std::size_t end = text.find('\"', first);
                        if (end != std::string::npos) text = text.substr(first, end - first);
                    }
                    entity->signText[static_cast<std::size_t>(line)] = text;
                }
            }
            if (entity->type == RuntimeBlockEntityType::Bed)
                entity->color = static_cast<std::uint8_t>(nbt::integer(c, "color", entity->color) & 15);
            if (entity->type == RuntimeBlockEntityType::ShulkerBox)
                entity->color = static_cast<std::uint8_t>(nbt::integer(c, "Color", entity->color) & 15);
            if (entity->type == RuntimeBlockEntityType::Furnace) {
                entity->furnaceBurnTime = static_cast<int>(nbt::integer(c, "BurnTime", 0));
                entity->furnaceCookTime = static_cast<int>(nbt::integer(c, "CookTime", 0));
                entity->furnaceCookTimeTotal = static_cast<int>(nbt::integer(c, "CookTimeTotal", 200));
            }
            if (entity->type == RuntimeBlockEntityType::Hopper)
                entity->transferCooldown = static_cast<int>(nbt::integer(c, "TransferCooldown", 0));
            if (entity->type == RuntimeBlockEntityType::BrewingStand) {
                entity->brewTime = static_cast<int>(nbt::integer(c, "BrewTime", 0));
                entity->brewingFuel = static_cast<int>(nbt::integer(c, "Fuel", 0));
            }
            if (entity->type == RuntimeBlockEntityType::Beacon) {
                entity->beaconLevels = static_cast<int>(nbt::integer(c, "Levels", 0));
                entity->beaconPrimary = static_cast<int>(nbt::integer(c, "Primary", 0));
                entity->beaconSecondary = static_cast<int>(nbt::integer(c, "Secondary", 0));
            }
            if (entity->type == RuntimeBlockEntityType::Jukebox)
                entity->recordItem = static_cast<int>(nbt::integer(c, "RecordItem", nbt::integer(c, "Record", 0)));
            if (entity->type == RuntimeBlockEntityType::FlowerPot) {
                entity->flowerItem = static_cast<int>(nbt::integer(c, "Item", 0));
                entity->flowerData = static_cast<int>(nbt::integer(c, "Data", 0));
            }
            if (entity->type == RuntimeBlockEntityType::MobSpawner) {
                entity->spawnerDelay = static_cast<int>(nbt::integer(c, "Delay", 20));
                if (const nbt::Tag* spawnData = nbt::find(c, "SpawnData"); spawnData && spawnData->type == nbt::Type::Compound)
                    entity->spawnerEntityId = nbt::string(spawnData->compound(), "id", "Pig");
            }
            if (entity->type == RuntimeBlockEntityType::Banner)
                entity->color = static_cast<std::uint8_t>(nbt::integer(c, "Base", entity->color) & 15);
            if (const nbt::Tag* items = nbt::find(c, "Items"); items && items->type == nbt::Type::List) {
                for (const nbt::Tag& item : items->list()) {
                    if (item.type != nbt::Type::Compound) continue;
                    const nbt::Compound& itemData = item.compound();
                    const int slot = static_cast<int>(nbt::integer(itemData, "Slot", -1));
                    if (slot < 0 || slot >= 27) continue;
                    ItemStack stack;
                    stack.itemId = static_cast<std::uint16_t>(nbt::integer(itemData, "BlockcraftItemId", 0));
                    stack.count = static_cast<int>(nbt::integer(itemData, "Count", 0));
                    stack.damage = static_cast<std::uint16_t>(nbt::integer(itemData, "Damage", 0));
                    if (const nbt::Tag* raw = nbt::find(itemData, "BlockcraftRawNbt"); raw && raw->type == nbt::Type::ByteArray)
                        for (std::int8_t value : std::get<nbt::ByteArray>(raw->value)) stack.nbt.push_back(static_cast<std::uint8_t>(value));
                    entity->inventory[static_cast<std::size_t>(slot)] = std::move(stack);
                }
            }
            if (entity->type == RuntimeBlockEntityType::Furnace)
                entity->currentItemBurnTime = std::max(entity->furnaceBurnTime, FurnaceRules::fuelBurnTime(entity->inventory[1]));
        } catch (...) {
            // Structure-template payloads from earlier stages may not have a
            // named NBT root. They remain preserved by Chunk and are ignored
            // here until their block-entity class is implemented.
        }
    }
}

void BlockEntitySystem::scanLoadedWorld(const World& world) {
    for (const auto& [unused, chunk] : world.chunks()) {
        (void)unused;
        if (chunk) scanChunk(world, chunk->x(), chunk->z());
    }
}

void BlockEntitySystem::blockChanged(const World&, const glm::ivec3& position,
                                     BlockState oldState, BlockState newState) {
    const auto oldType = typeFor(oldState);
    const auto newType = typeFor(newState);
    if (!newType) {
        if (oldType) entities_.erase(key(position));
        return;
    }
    if (!oldType || *oldType != *newType) entities_.erase(key(position));
    ensure(position, newState);
}

void BlockEntitySystem::rescanPosition(const World& world, const glm::ivec3& position) {
    const BlockState state = world.getBlock(position.x, position.y, position.z);
    if (!typeFor(state)) { entities_.erase(key(position)); return; }
    ensure(position, state);
}

void BlockEntitySystem::placedFromItem(const glm::ivec3& position, BlockState state,
                                       const ItemStack& stack) {
    ensure(position, state);
    RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Bed || entity->type == RuntimeBlockEntityType::Banner)
        entity->color = static_cast<std::uint8_t>(stack.damage & 15U);
}

void BlockEntitySystem::restore(RuntimeBlockEntity entity) {
    entities_.insert_or_assign(key(entity.position), std::move(entity));
}

RuntimeBlockEntity* BlockEntitySystem::find(const glm::ivec3& position) {
    const auto it = entities_.find(key(position));
    return it == entities_.end() ? nullptr : &it->second;
}

const RuntimeBlockEntity* BlockEntitySystem::find(const glm::ivec3& position) const {
    const auto it = entities_.find(key(position));
    return it == entities_.end() ? nullptr : &it->second;
}

std::optional<glm::ivec3> BlockEntitySystem::pairedChest(const World& world,
                                                         const glm::ivec3& position) const {
    const BlockId id = static_cast<BlockId>(blockId(world.getBlock(position.x, position.y, position.z)));
    if (!isChest(id)) return std::nullopt;
    constexpr std::array<glm::ivec3, 4> offsets = {
        glm::ivec3{-1,0,0}, glm::ivec3{1,0,0}, glm::ivec3{0,0,-1}, glm::ivec3{0,0,1}
    };
    for (const glm::ivec3& d : offsets) {
        const glm::ivec3 p = position + d;
        if (static_cast<BlockId>(blockId(world.getBlock(p.x, p.y, p.z))) == id) return p;
    }
    return std::nullopt;
}

std::optional<BlockEntityAction> BlockEntitySystem::activate(const World& world,
                                                             const RaycastHit& hit) const {
    const BlockId id = static_cast<BlockId>(blockId(hit.state));
    if (isChest(id)) {
        const auto blockedAbove = [&](const glm::ivec3& p) {
            return normalCube(world.getBlock(p.x, p.y + 1, p.z));
        };
        if (blockedAbove(hit.block)) return std::nullopt;
        if (const auto pair = pairedChest(world, hit.block); pair && blockedAbove(*pair))
            return std::nullopt;
        return BlockEntityAction{BlockEntityActionType::OpenChest, hit.block};
    }
    if (id == BlockId::Bed)
        return BlockEntityAction{BlockEntityActionType::Sleep, hit.block};
    if (isShulker(id)) {
        const glm::ivec3 d = facingOffset(blockMetadata(hit.state));
        const glm::ivec3 adjacent = hit.block + d;
        if (normalCube(world.getBlock(adjacent.x, adjacent.y, adjacent.z))) return std::nullopt;
        return BlockEntityAction{BlockEntityActionType::OpenShulker, hit.block};
    }
    if (id == BlockId::Furnace || id == BlockId::LitFurnace)
        return BlockEntityAction{BlockEntityActionType::OpenFurnace, hit.block};
    if (id == BlockId::CraftingTable)
        return BlockEntityAction{BlockEntityActionType::OpenCraftingTable, hit.block};
    if (id == BlockId::Hopper) return BlockEntityAction{BlockEntityActionType::OpenHopper, hit.block};
    if (id == BlockId::BrewingStand) return BlockEntityAction{BlockEntityActionType::OpenBrewingStand, hit.block};
    if (id == BlockId::EnchantingTable) return BlockEntityAction{BlockEntityActionType::OpenEnchantingTable, hit.block};
    if (id == BlockId::Beacon) return BlockEntityAction{BlockEntityActionType::OpenBeacon, hit.block};
    if (id == BlockId::EnderChest) return BlockEntityAction{BlockEntityActionType::OpenEnderChest, hit.block};
    if (id == BlockId::Jukebox) return BlockEntityAction{BlockEntityActionType::OpenJukebox, hit.block};
    if (id == BlockId::FlowerPot) return BlockEntityAction{BlockEntityActionType::OpenFlowerPot, hit.block};
    if (id == BlockId::Dispenser) return BlockEntityAction{BlockEntityActionType::OpenDispenser, hit.block};
    if (id == BlockId::Dropper) return BlockEntityAction{BlockEntityActionType::OpenDropper, hit.block};
    return std::nullopt;
}

bool BlockEntitySystem::useSpecial(World&, const RaycastHit& hit, Player& player, ItemStack& ejected) {
    RuntimeBlockEntity* entity = find(hit.block);
    if (entity == nullptr) return false;
    ItemStack& held = player.inventory().selected();
    if (entity->type == RuntimeBlockEntityType::Jukebox) {
        if (entity->recordItem != 0) {
            ejected = ItemStack{static_cast<std::uint16_t>(entity->recordItem),1,0,{}};
            entity->recordItem = 0;
            entity->inventory[0].clear();
            return true;
        }
        // Legacy record IDs are 2256..2267 in 1.12.2.
        if (!held.empty() && held.itemId >= 2256 && held.itemId <= 2267) {
            entity->recordItem = held.itemId;
            entity->inventory[0] = held;
            entity->inventory[0].count = 1;
            if (player.gameMode() != GameMode::Creative) held.shrink(1);
            return true;
        }
        return true;
    }
    if (entity->type == RuntimeBlockEntityType::FlowerPot) {
        if (entity->flowerItem != 0) {
            ejected = ItemStack{static_cast<std::uint16_t>(entity->flowerItem),1,static_cast<std::uint16_t>(entity->flowerData),{}};
            entity->flowerItem = entity->flowerData = 0;
            entity->inventory[0].clear();
            return true;
        }
        const BlockId id = static_cast<BlockId>(held.itemId);
        const bool allowed = id == BlockId::Sapling || id == BlockId::YellowFlower || id == BlockId::RedFlower ||
            id == BlockId::BrownMushroom || id == BlockId::RedMushroom || id == BlockId::Cactus || id == BlockId::DeadBush;
        if (allowed && !held.empty()) {
            entity->flowerItem = held.itemId; entity->flowerData = held.damage;
            entity->inventory[0] = held; entity->inventory[0].count = 1;
            if (player.gameMode() != GameMode::Creative) held.shrink(1);
        }
        return true;
    }
    return false;
}

void BlockEntitySystem::beginViewing(const BlockEntityAction& action) {
    RuntimeBlockEntity* entity = find(action.position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Chest || entity->type == RuntimeBlockEntityType::TrappedChest ||
        entity->type == RuntimeBlockEntityType::ShulkerBox || entity->type == RuntimeBlockEntityType::EnderChest)
        ++entity->viewers;
}

void BlockEntitySystem::endViewing(const BlockEntityAction& action) {
    RuntimeBlockEntity* entity = find(action.position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Chest || entity->type == RuntimeBlockEntityType::TrappedChest ||
        entity->type == RuntimeBlockEntityType::ShulkerBox || entity->type == RuntimeBlockEntityType::EnderChest)
        entity->viewers = std::max(0, entity->viewers - 1);
}

std::vector<glm::ivec3> BlockEntitySystem::tick(World& world) {
    std::vector<glm::ivec3> changedBlocks;
    for (auto& [unused, entity] : entities_) {
        (void)unused;
        entity.previousAnimation = entity.animation;
        if (entity.type == RuntimeBlockEntityType::Chest || entity.type == RuntimeBlockEntityType::TrappedChest ||
            entity.type == RuntimeBlockEntityType::ShulkerBox || entity.type == RuntimeBlockEntityType::EnderChest) {
            if (entity.viewers > 0 && entity.animation < 1.0F) entity.animation += 0.1F;
            else if (entity.viewers == 0 && entity.animation > 0.0F) entity.animation -= 0.1F;
            entity.animation = std::clamp(entity.animation, 0.0F, 1.0F);
            continue;
        }

        switch (entity.type) {
            case RuntimeBlockEntityType::Furnace:
                BlockEntityLogic::tickFurnace(world, entity, changedBlocks);
                break;
            case RuntimeBlockEntityType::Hopper:
                BlockEntityLogic::tickHopper(world, *this, entity);
                break;
            case RuntimeBlockEntityType::BrewingStand:
                BlockEntityLogic::tickBrewing(entity);
                break;
            case RuntimeBlockEntityType::Beacon:
                BlockEntityLogic::tickBeacon(world, entity);
                break;
            case RuntimeBlockEntityType::MobSpawner:
                BlockEntityLogic::tickSpawner(entity, key(entity.position));
                break;
            default:
                break;
        }
    }
    return changedBlocks;
}

int BlockEntitySystem::containerSlotCount(const World& world, const glm::ivec3& position) const {
    const RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return 0;
    if (entity->type == RuntimeBlockEntityType::Furnace) return 3;
    if (entity->type == RuntimeBlockEntityType::Hopper) return 5;
    if (entity->type == RuntimeBlockEntityType::BrewingStand) return 5;
    if (entity->type == RuntimeBlockEntityType::EnchantingTable) return 2;
    if (entity->type == RuntimeBlockEntityType::Beacon) return 1;
    if (entity->type == RuntimeBlockEntityType::Jukebox) return 1;
    if (entity->type == RuntimeBlockEntityType::FlowerPot) return 1;
    if (entity->type == RuntimeBlockEntityType::EnderChest) return 27;
    if (entity->type == RuntimeBlockEntityType::Dispenser || entity->type == RuntimeBlockEntityType::Dropper) return 9;
    if (entity->type == RuntimeBlockEntityType::ShulkerBox) return 27;
    if (entity->type == RuntimeBlockEntityType::Chest || entity->type == RuntimeBlockEntityType::TrappedChest)
        return pairedChest(world, position) ? 54 : 27;
    return 0;
}

ItemStack& BlockEntitySystem::containerSlot(const World& world, const glm::ivec3& position, int index) {
    RuntimeBlockEntity* clicked = find(position);
    if (clicked == nullptr) throw std::out_of_range("Block entity container is missing");
    if (index < 0) throw std::out_of_range("Negative container slot");
    if (clicked->type == RuntimeBlockEntityType::Furnace) {
        if (index >= 3) throw std::out_of_range("Container slot outside furnace");
        return clicked->inventory[static_cast<std::size_t>(index)];
    }
    if (clicked->type == RuntimeBlockEntityType::EnderChest) {
        if (index >= 27) throw std::out_of_range("Container slot outside ender chest");
        return enderChestInventory_[static_cast<std::size_t>(index)];
    }
    if (clicked->type == RuntimeBlockEntityType::Dispenser || clicked->type == RuntimeBlockEntityType::Dropper) {
        if (index >= 9) throw std::out_of_range("Container slot outside dispenser/dropper");
        return clicked->inventory[static_cast<std::size_t>(index)];
    }
    if (clicked->type == RuntimeBlockEntityType::Hopper || clicked->type == RuntimeBlockEntityType::BrewingStand ||
        clicked->type == RuntimeBlockEntityType::EnchantingTable || clicked->type == RuntimeBlockEntityType::Beacon ||
        clicked->type == RuntimeBlockEntityType::Jukebox || clicked->type == RuntimeBlockEntityType::FlowerPot) {
        const int limit = clicked->type == RuntimeBlockEntityType::Hopper ? 5 : clicked->type == RuntimeBlockEntityType::BrewingStand ? 5 : clicked->type == RuntimeBlockEntityType::EnchantingTable ? 2 : 1;
        if (index >= limit) throw std::out_of_range("Container slot outside block entity");
        return clicked->inventory[static_cast<std::size_t>(index)];
    }
    if (clicked->type == RuntimeBlockEntityType::ShulkerBox) {
        if (index >= 27) throw std::out_of_range("Container slot outside shulker box");
        return clicked->inventory[static_cast<std::size_t>(index)];
    }

    const auto pair = pairedChest(world, position);
    if (!pair) {
        if (index >= 27) throw std::out_of_range("Container slot outside chest");
        return clicked->inventory[static_cast<std::size_t>(index)];
    }
    if (index >= 54) throw std::out_of_range("Container slot outside large chest");
    RuntimeBlockEntity* adjacent = find(*pair);
    if (adjacent == nullptr) throw std::out_of_range("Adjacent chest block entity is missing");

    // InventoryLargeChest places the west/north half first and east/south half
    // second. Keep slot order stable regardless of which half the player used.
    const bool pairFirst = pair->x < position.x || pair->z < position.z;
    RuntimeBlockEntity* first = pairFirst ? adjacent : clicked;
    RuntimeBlockEntity* second = pairFirst ? clicked : adjacent;
    return index < 27 ? first->inventory[static_cast<std::size_t>(index)]
                      : second->inventory[static_cast<std::size_t>(index - 27)];
}

const ItemStack& BlockEntitySystem::containerSlot(const World& world, const glm::ivec3& position, int index) const {
    return const_cast<BlockEntitySystem*>(this)->containerSlot(world, position, index);
}

std::string BlockEntitySystem::containerTitle(const World& world, const glm::ivec3& position) const {
    const RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return "Container";
    if (entity->type == RuntimeBlockEntityType::Furnace) return "Furnace";
    if (entity->type == RuntimeBlockEntityType::Hopper) return "Item Hopper";
    if (entity->type == RuntimeBlockEntityType::BrewingStand) return "Brewing Stand";
    if (entity->type == RuntimeBlockEntityType::EnchantingTable) return "Enchant";
    if (entity->type == RuntimeBlockEntityType::Beacon) return "Beacon";
    if (entity->type == RuntimeBlockEntityType::Jukebox) return "Jukebox";
    if (entity->type == RuntimeBlockEntityType::FlowerPot) return "Flower Pot";
    if (entity->type == RuntimeBlockEntityType::EnderChest) return "Ender Chest";
    if (entity->type == RuntimeBlockEntityType::Dispenser) return "Dispenser";
    if (entity->type == RuntimeBlockEntityType::Dropper) return "Dropper";
    if (entity->type == RuntimeBlockEntityType::ShulkerBox) return "Shulker Box";
    if (entity->type == RuntimeBlockEntityType::TrappedChest)
        return pairedChest(world, position) ? "Large Trapped Chest" : "Trapped Chest";
    if (entity->type == RuntimeBlockEntityType::Chest)
        return pairedChest(world, position) ? "Large Chest" : "Chest";
    return "Container";
}

std::array<std::string, 4>* BlockEntitySystem::signLines(const glm::ivec3& position) {
    RuntimeBlockEntity* entity = find(position);
    return entity != nullptr && entity->type == RuntimeBlockEntityType::Sign ? &entity->signText : nullptr;
}

const std::array<std::string, 4>* BlockEntitySystem::signLines(const glm::ivec3& position) const {
    const RuntimeBlockEntity* entity = find(position);
    return entity != nullptr && entity->type == RuntimeBlockEntityType::Sign ? &entity->signText : nullptr;
}

float BlockEntitySystem::animation(const glm::ivec3& position, float partialTick) const {
    const RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return 0.0F;
    return entity->previousAnimation + (entity->animation - entity->previousAnimation) * partialTick;
}


bool BlockEntitySystem::furnaceBurning(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e=find(position); return e && e->type==RuntimeBlockEntityType::Furnace && e->furnaceBurnTime>0;
}
float BlockEntitySystem::furnaceCookProgress(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e=find(position); if(!e||e->type!=RuntimeBlockEntityType::Furnace||e->furnaceCookTimeTotal<=0)return 0.0F;
    return std::clamp(static_cast<float>(e->furnaceCookTime)/static_cast<float>(e->furnaceCookTimeTotal),0.0F,1.0F);
}
float BlockEntitySystem::furnaceBurnProgress(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e=find(position); if(!e||e->type!=RuntimeBlockEntityType::Furnace||e->currentItemBurnTime<=0)return 0.0F;
    return std::clamp(static_cast<float>(e->furnaceBurnTime)/static_cast<float>(e->currentItemBurnTime),0.0F,1.0F);
}
float BlockEntitySystem::takeFurnaceExperience(const glm::ivec3& position) {
    RuntimeBlockEntity* e=find(position); if(!e||e->type!=RuntimeBlockEntityType::Furnace)return 0.0F;
    const float xp=e->furnaceStoredXp; e->furnaceStoredXp=0.0F; return xp;
}


float BlockEntitySystem::brewingProgress(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e = find(position); if (!e || e->type != RuntimeBlockEntityType::BrewingStand || e->brewTime <= 0) return 0.0F;
    return std::clamp(1.0F - static_cast<float>(e->brewTime) / 400.0F, 0.0F, 1.0F);
}
int BlockEntitySystem::brewingFuel(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e = find(position); return e && e->type == RuntimeBlockEntityType::BrewingStand ? e->brewingFuel : 0;
}
int BlockEntitySystem::beaconLevels(const glm::ivec3& position) const {
    const RuntimeBlockEntity* e = find(position); return e && e->type == RuntimeBlockEntityType::Beacon ? e->beaconLevels : 0;
}
