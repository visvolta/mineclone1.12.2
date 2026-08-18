#include "world/BlockEntitySystem.hpp"

#include <algorithm>
#include <stdexcept>

#include "blocks/BlockRegistry.hpp"
#include "world/Chunk.hpp"
#include "world/Raycast.hpp"
#include "save/Nbt.hpp"
#include "survival/FurnaceRules.hpp"
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

void BlockEntitySystem::placedFromItem(const glm::ivec3& position, BlockState state,
                                       const ItemStack& stack) {
    ensure(position, state);
    RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Bed)
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
    return std::nullopt;
}

void BlockEntitySystem::beginViewing(const BlockEntityAction& action) {
    RuntimeBlockEntity* entity = find(action.position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Chest || entity->type == RuntimeBlockEntityType::TrappedChest ||
        entity->type == RuntimeBlockEntityType::ShulkerBox)
        ++entity->viewers;
}

void BlockEntitySystem::endViewing(const BlockEntityAction& action) {
    RuntimeBlockEntity* entity = find(action.position);
    if (entity == nullptr) return;
    if (entity->type == RuntimeBlockEntityType::Chest || entity->type == RuntimeBlockEntityType::TrappedChest ||
        entity->type == RuntimeBlockEntityType::ShulkerBox)
        entity->viewers = std::max(0, entity->viewers - 1);
}

std::vector<glm::ivec3> BlockEntitySystem::tick(World& world) {
    std::vector<glm::ivec3> changedBlocks;
    for (auto& [unused, entity] : entities_) {
        (void)unused;
        entity.previousAnimation = entity.animation;
        if (entity.type == RuntimeBlockEntityType::Chest || entity.type == RuntimeBlockEntityType::TrappedChest ||
            entity.type == RuntimeBlockEntityType::ShulkerBox) {
            if (entity.viewers > 0 && entity.animation < 1.0F) entity.animation += 0.1F;
            else if (entity.viewers == 0 && entity.animation > 0.0F) entity.animation -= 0.1F;
            entity.animation = std::clamp(entity.animation, 0.0F, 1.0F);
            continue;
        }
        if (entity.type != RuntimeBlockEntityType::Furnace) continue;

        const bool wasBurning = entity.furnaceBurnTime > 0;
        if (entity.furnaceBurnTime > 0) --entity.furnaceBurnTime;
        ItemStack& input = entity.inventory[0];
        ItemStack& fuel = entity.inventory[1];
        ItemStack& output = entity.inventory[2];
        const auto recipe = FurnaceRules::recipe(input);
        bool canSmelt = false;
        if (recipe) {
            canSmelt = output.empty() || (output.sameItem(recipe->result) && output.count < 64);
        }
        if (entity.furnaceBurnTime <= 0 && canSmelt && !fuel.empty()) {
            const int burn = FurnaceRules::fuelBurnTime(fuel);
            if (burn > 0) {
                entity.furnaceBurnTime = burn; entity.currentItemBurnTime = burn;
                const bool lavaBucket = fuel.itemId == 327;
                fuel.shrink(1);
                if (lavaBucket && fuel.empty()) fuel = ItemStack{325,1,0,{}};
            }
        }
        if (entity.furnaceBurnTime > 0 && canSmelt && recipe) {
            ++entity.furnaceCookTime;
            if (entity.furnaceCookTime >= entity.furnaceCookTimeTotal) {
                entity.furnaceCookTime = 0;
                if (output.empty()) output = recipe->result; else ++output.count;
                entity.furnaceStoredXp += recipe->experience;
                const bool wetSponge = input.itemId == 19 && input.damage == 1;
                input.shrink(1);
                if (wetSponge && fuel.itemId == 325 && fuel.count == 1) fuel = ItemStack{326,1,0,{}};
            }
        } else if (entity.furnaceBurnTime <= 0 && entity.furnaceCookTime > 0) {
            entity.furnaceCookTime = std::clamp(entity.furnaceCookTime - 2, 0, entity.furnaceCookTimeTotal);
        } else if (!canSmelt) {
            entity.furnaceCookTime = 0;
        }

        const bool burning = entity.furnaceBurnTime > 0;
        if (burning != wasBurning) {
            const BlockState current = world.getBlock(entity.position.x,entity.position.y,entity.position.z);
            const std::uint8_t meta = blockMetadata(current);
            const BlockId wanted = burning ? BlockId::LitFurnace : BlockId::Furnace;
            world.setBlock(entity.position.x,entity.position.y,entity.position.z,
                           makeBlockState(static_cast<std::uint16_t>(wanted),meta));
            entity.state = makeBlockState(static_cast<std::uint16_t>(wanted),meta);
            changedBlocks.push_back(entity.position);
        }
    }
    return changedBlocks;
}

int BlockEntitySystem::containerSlotCount(const World& world, const glm::ivec3& position) const {
    const RuntimeBlockEntity* entity = find(position);
    if (entity == nullptr) return 0;
    if (entity->type == RuntimeBlockEntityType::Furnace) return 3;
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
