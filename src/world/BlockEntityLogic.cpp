#include "world/BlockEntityLogic.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "blocks/BlockRegistry.hpp"
#include "player/Player.hpp"
#include "save/Nbt.hpp"
#include "survival/FurnaceRules.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/World.hpp"

namespace {

glm::ivec3 facingOffset(std::uint8_t metadata) {
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

std::string potionType(const ItemStack& stack) {
    if (stack.itemId != 373 && stack.itemId != 438 && stack.itemId != 441) return {};
    if (stack.nbt.empty()) return "minecraft:water";
    try {
        const nbt::Document doc = nbt::decode(stack.nbt);
        if (doc.root.type == nbt::Type::Compound)
            return nbt::string(doc.root.compound(), "Potion", "minecraft:water");
    } catch (...) {
    }
    return "minecraft:water";
}

void setPotionType(ItemStack& stack, std::string type) {
    nbt::Compound compound;
    if (!stack.nbt.empty()) {
        try {
            const nbt::Document old = nbt::decode(stack.nbt);
            if (old.root.type == nbt::Type::Compound) compound = old.root.compound();
        } catch (...) {
        }
    }
    compound["Potion"] = nbt::Tag(std::move(type));
    nbt::Document doc;
    doc.root = nbt::Tag(std::move(compound));
    stack.nbt = nbt::encode(doc);
}

bool brewOne(ItemStack& bottle, const ItemStack& ingredient) {
    if (bottle.empty()) return false;
    if (ingredient.itemId == 289 && bottle.itemId == 373) {
        bottle.itemId = 438;
        return true;
    }
    if (ingredient.itemId == 437 && bottle.itemId == 438) {
        bottle.itemId = 441;
        return true;
    }

    const std::string current = potionType(bottle);
    if (current.empty()) return false;
    std::string next;
    if (ingredient.itemId == 372 && current == "minecraft:water") next = "minecraft:awkward";
    else if (current == "minecraft:awkward") {
        if (ingredient.itemId == 353) next = "minecraft:swiftness";
        else if (ingredient.itemId == 414) next = "minecraft:leaping";
        else if (ingredient.itemId == 378) next = "minecraft:fire_resistance";
        else if (ingredient.itemId == 382) next = "minecraft:healing";
        else if (ingredient.itemId == 375) next = "minecraft:poison";
        else if (ingredient.itemId == 370) next = "minecraft:regeneration";
        else if (ingredient.itemId == 396) next = "minecraft:night_vision";
        else if (ingredient.itemId == 349 && ingredient.damage == 3) next = "minecraft:water_breathing";
    } else if (ingredient.itemId == 376) {
        if (current == "minecraft:night_vision" || current == "minecraft:long_night_vision") next = "minecraft:invisibility";
        else if (current == "minecraft:healing" || current == "minecraft:strong_healing" || current == "minecraft:poison" || current == "minecraft:long_poison" || current == "minecraft:strong_poison") next = "minecraft:harming";
        else if (current == "minecraft:swiftness" || current == "minecraft:long_swiftness" || current == "minecraft:leaping" || current == "minecraft:long_leaping") next = "minecraft:slowness";
    } else if (ingredient.itemId == 331) {
        if (current == "minecraft:swiftness") next = "minecraft:long_swiftness";
        else if (current == "minecraft:leaping") next = "minecraft:long_leaping";
        else if (current == "minecraft:fire_resistance") next = "minecraft:long_fire_resistance";
        else if (current == "minecraft:poison") next = "minecraft:long_poison";
        else if (current == "minecraft:regeneration") next = "minecraft:long_regeneration";
        else if (current == "minecraft:night_vision") next = "minecraft:long_night_vision";
        else if (current == "minecraft:water_breathing") next = "minecraft:long_water_breathing";
        else if (current == "minecraft:invisibility") next = "minecraft:long_invisibility";
        else if (current == "minecraft:slowness") next = "minecraft:long_slowness";
    } else if (ingredient.itemId == 348) {
        if (current == "minecraft:swiftness") next = "minecraft:strong_swiftness";
        else if (current == "minecraft:leaping") next = "minecraft:strong_leaping";
        else if (current == "minecraft:healing") next = "minecraft:strong_healing";
        else if (current == "minecraft:poison") next = "minecraft:strong_poison";
        else if (current == "minecraft:regeneration") next = "minecraft:strong_regeneration";
        else if (current == "minecraft:harming") next = "minecraft:strong_harming";
    }
    if (next.empty()) return false;
    setPotionType(bottle, std::move(next));
    return true;
}

bool canBrew(const RuntimeBlockEntity& entity) {
    if (entity.inventory[3].empty()) return false;
    for (int i = 0; i < 3; ++i) {
        ItemStack copy = entity.inventory[static_cast<std::size_t>(i)];
        if (brewOne(copy, entity.inventory[3])) return true;
    }
    return false;
}

bool moveOneItem(RuntimeBlockEntity& source, RuntimeBlockEntity& destination) {
    const int sourceSlots = source.type == RuntimeBlockEntityType::Hopper ? 5 : 27;
    const int destinationSlots = destination.type == RuntimeBlockEntityType::Hopper ? 5 :
        destination.type == RuntimeBlockEntityType::Furnace ? 3 : 27;
    for (int sourceIndex = 0; sourceIndex < sourceSlots; ++sourceIndex) {
        ItemStack& from = source.inventory[static_cast<std::size_t>(sourceIndex)];
        if (from.empty()) continue;
        for (int destinationIndex = 0; destinationIndex < destinationSlots; ++destinationIndex) {
            ItemStack& to = destination.inventory[static_cast<std::size_t>(destinationIndex)];
            if (to.empty()) {
                to = from;
                to.count = 1;
                from.shrink(1);
                return true;
            }
            if (to.sameItem(from) && to.count < 64) {
                ++to.count;
                from.shrink(1);
                return true;
            }
        }
    }
    return false;
}

} // namespace

namespace BlockEntityLogic {

void tickFurnace(World& world, RuntimeBlockEntity& entity, std::vector<glm::ivec3>& changedBlocks) {
    const bool wasBurning = entity.furnaceBurnTime > 0;
    if (entity.furnaceBurnTime > 0) --entity.furnaceBurnTime;

    ItemStack& input = entity.inventory[0];
    ItemStack& fuel = entity.inventory[1];
    ItemStack& output = entity.inventory[2];
    const auto recipe = FurnaceRules::recipe(input);
    const bool canSmelt = recipe && (output.empty() || (output.sameItem(recipe->result) && output.count < 64));

    if (entity.furnaceBurnTime <= 0 && canSmelt && !fuel.empty()) {
        const int burn = FurnaceRules::fuelBurnTime(fuel);
        if (burn > 0) {
            entity.furnaceBurnTime = burn;
            entity.currentItemBurnTime = burn;
            const bool lavaBucket = fuel.itemId == 327;
            fuel.shrink(1);
            if (lavaBucket && fuel.empty()) fuel = ItemStack{325, 1, 0, {}};
        }
    }

    if (entity.furnaceBurnTime > 0 && canSmelt && recipe) {
        ++entity.furnaceCookTime;
        if (entity.furnaceCookTime >= entity.furnaceCookTimeTotal) {
            entity.furnaceCookTime = 0;
            if (output.empty()) output = recipe->result;
            else ++output.count;
            entity.furnaceStoredXp += recipe->experience;
            const bool wetSponge = input.itemId == 19 && input.damage == 1;
            input.shrink(1);
            if (wetSponge && fuel.itemId == 325 && fuel.count == 1) fuel = ItemStack{326, 1, 0, {}};
        }
    } else if (entity.furnaceBurnTime <= 0 && entity.furnaceCookTime > 0) {
        entity.furnaceCookTime = std::clamp(entity.furnaceCookTime - 2, 0, entity.furnaceCookTimeTotal);
    } else if (!canSmelt) {
        entity.furnaceCookTime = 0;
    }

    const bool burning = entity.furnaceBurnTime > 0;
    if (burning == wasBurning) return;
    const BlockState current = world.getBlock(entity.position.x, entity.position.y, entity.position.z);
    const std::uint8_t metadata = blockMetadata(current);
    const BlockId wanted = burning ? BlockId::LitFurnace : BlockId::Furnace;
    const BlockState replacement = makeBlockState(static_cast<std::uint16_t>(wanted), metadata);
    world.setBlock(entity.position.x, entity.position.y, entity.position.z, replacement);
    entity.state = replacement;
    changedBlocks.push_back(entity.position);
}

void tickHopper(World&, BlockEntitySystem& system, RuntimeBlockEntity& entity) {
    if (entity.transferCooldown > 0) {
        --entity.transferCooldown;
        return;
    }
    bool moved = false;
    if (RuntimeBlockEntity* above = system.find(entity.position + glm::ivec3(0, 1, 0)); above != nullptr)
        moved = moveOneItem(*above, entity);
    if (!moved) {
        const glm::ivec3 target = entity.position + facingOffset(blockMetadata(entity.state));
        if (RuntimeBlockEntity* destination = system.find(target); destination != nullptr)
            moved = moveOneItem(entity, *destination);
    }
    if (moved) entity.transferCooldown = 8;
}

void tickBrewing(RuntimeBlockEntity& entity) {
    ItemStack& ingredient = entity.inventory[3];
    ItemStack& fuel = entity.inventory[4];
    if (entity.brewingFuel <= 0 && fuel.itemId == 377 && !fuel.empty()) {
        fuel.shrink(1);
        entity.brewingFuel = 20;
    }
    const bool valid = entity.brewingFuel > 0 && canBrew(entity);
    if (!valid) {
        entity.brewTime = 0;
        return;
    }
    if (entity.brewTime <= 0) {
        entity.brewTime = 400;
        return;
    }
    if (--entity.brewTime > 0) return;

    bool changed = false;
    for (int index = 0; index < 3; ++index)
        changed = brewOne(entity.inventory[static_cast<std::size_t>(index)], ingredient) || changed;
    if (changed) {
        ingredient.shrink(1);
        --entity.brewingFuel;
    }
}

void tickBeacon(const World& world, RuntimeBlockEntity& entity) {
    int levels = 0;
    for (int layer = 1; layer <= 4; ++layer) {
        bool full = true;
        for (int x = -layer; x <= layer && full; ++x) {
            for (int z = -layer; z <= layer; ++z) {
                const BlockId id = static_cast<BlockId>(blockId(world.getBlock(
                    entity.position.x + x, entity.position.y - layer, entity.position.z + z)));
                if (id != BlockId::IronBlock && id != BlockId::GoldBlock &&
                    id != BlockId::DiamondBlock && id != BlockId::EmeraldBlock) {
                    full = false;
                    break;
                }
            }
        }
        if (!full) break;
        levels = layer;
    }
    entity.beaconLevels = levels;
}

void tickSpawner(RuntimeBlockEntity& entity, std::uint64_t stableKey) {
    if (entity.spawnerDelay > 0) --entity.spawnerDelay;
    else entity.spawnerDelay = 200 + static_cast<int>((stableKey + static_cast<std::uint64_t>(entity.position.y)) % 600U);
}

} // namespace BlockEntityLogic
