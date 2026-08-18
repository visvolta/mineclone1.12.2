#include "save/WorldSave.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "environment/Environment.hpp"
#include "entity/EntityManager.hpp"
#include "items/ItemRegistry.hpp"
#include "save/RegionFile.hpp"
#include "save/SaveMigration.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {
constexpr std::int32_t dataVersion1122 = 1343;
constexpr std::int32_t anvilVersion = 19133;

int floorDiv32(int value) {
    int quotient = value / 32;
    if (value < 0 && value % 32 != 0) --quotient;
    return quotient;
}

int floorMod32(int value) {
    const int remainder = value % 32;
    return remainder < 0 ? remainder + 32 : remainder;
}

std::int64_t nowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string generatorName(WorldType type) {
    switch (type) {
        case WorldType::Default: return "default";
        case WorldType::Flat: return "flat";
        case WorldType::LargeBiomes: return "largeBiomes";
        case WorldType::Amplified: return "amplified";
        case WorldType::Customized: return "customized";
        case WorldType::DebugAllBlockStates: return "debug_all_block_states";
        case WorldType::Default11: return "default_1_1";
    }
    return "default";
}

WorldType parseGenerator(std::string_view name) {
    if (name == "flat") return WorldType::Flat;
    if (name == "largeBiomes") return WorldType::LargeBiomes;
    if (name == "amplified") return WorldType::Amplified;
    if (name == "customized") return WorldType::Customized;
    if (name == "debug_all_block_states") return WorldType::DebugAllBlockStates;
    if (name == "default_1_1") return WorldType::Default11;
    return WorldType::Default;
}

std::string cleanFolderName(std::string name) {
    for (char& value : name) {
        if (value == '/' || value == '\\' || value == ':' || value == '*' || value == '?' ||
            value == '"' || value == '<' || value == '>' || value == '|') value = '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.empty()) name = "New World";
    if (name.size() > 64) name.resize(64);
    return name;
}

std::size_t chunkCellIndex(int x, int y, int z) {
    return static_cast<std::size_t>((y * 16 + z) * 16 + x);
}

std::uint8_t nibble(const nbt::ByteArray& array, std::size_t index) {
    if ((index >> 1U) >= array.size()) return 0;
    const std::uint8_t packed = static_cast<std::uint8_t>(array[index >> 1U]);
    return (index & 1U) != 0U ? static_cast<std::uint8_t>(packed >> 4U)
                              : static_cast<std::uint8_t>(packed & 15U);
}

void setNibble(nbt::ByteArray& array, std::size_t index, std::uint8_t value) {
    std::int8_t& raw = array[index >> 1U];
    std::uint8_t packed = static_cast<std::uint8_t>(raw);
    if ((index & 1U) != 0U)
        packed = static_cast<std::uint8_t>((packed & 0x0FU) | ((value & 15U) << 4U));
    else
        packed = static_cast<std::uint8_t>((packed & 0xF0U) | (value & 15U));
    raw = static_cast<std::int8_t>(packed);
}

nbt::Compound* dataCompound(nbt::Document& document) {
    auto& root = document.root.compound();
    nbt::Tag* data = nbt::find(root, "Data");
    return data != nullptr && data->type == nbt::Type::Compound ? &data->compound() : nullptr;
}

const nbt::Compound* dataCompound(const nbt::Document& document) {
    const auto& root = document.root.compound();
    const nbt::Tag* data = nbt::find(root, "Data");
    return data != nullptr && data->type == nbt::Type::Compound ? &data->compound() : nullptr;
}

std::string tileEntityId(RuntimeBlockEntityType type) {
    switch (type) {
        case RuntimeBlockEntityType::Chest:
        case RuntimeBlockEntityType::TrappedChest: return "minecraft:chest";
        case RuntimeBlockEntityType::Sign: return "minecraft:sign";
        case RuntimeBlockEntityType::Bed: return "minecraft:bed";
        case RuntimeBlockEntityType::ShulkerBox: return "minecraft:shulker_box";
        case RuntimeBlockEntityType::Furnace: return "minecraft:furnace";
        case RuntimeBlockEntityType::Hopper: return "minecraft:hopper";
        case RuntimeBlockEntityType::BrewingStand: return "minecraft:brewing_stand";
        case RuntimeBlockEntityType::EnchantingTable: return "minecraft:enchanting_table";
        case RuntimeBlockEntityType::Beacon: return "minecraft:beacon";
        case RuntimeBlockEntityType::Jukebox: return "minecraft:jukebox";
        case RuntimeBlockEntityType::FlowerPot: return "minecraft:flower_pot";
        case RuntimeBlockEntityType::MobSpawner: return "minecraft:mob_spawner";
        case RuntimeBlockEntityType::EnderChest: return "minecraft:ender_chest";
        case RuntimeBlockEntityType::Banner: return "minecraft:banner";
        case RuntimeBlockEntityType::Dispenser: return "minecraft:dispenser";
        case RuntimeBlockEntityType::Dropper: return "minecraft:dropper";
    }
    return "minecraft:chest";
}

std::uint64_t blockPositionKey(int x, int y, int z) {
    const std::uint64_t px = static_cast<std::uint64_t>(static_cast<std::int64_t>(x) + 0x2000000LL) & 0x3FFFFFFULL;
    const std::uint64_t pz = static_cast<std::uint64_t>(static_cast<std::int64_t>(z) + 0x2000000LL) & 0x3FFFFFFULL;
    return (px << 38U) | (pz << 12U) | (static_cast<std::uint64_t>(y) & 0xFFFULL);
}

void writeSessionLock(const std::filesystem::path& folder) {
    std::ofstream output(folder / "session.lock", std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create session.lock");
    const std::uint64_t value = static_cast<std::uint64_t>(nowMillis());
    for (int shift = 56; shift >= 0; shift -= 8)
        output.put(static_cast<char>((value >> static_cast<unsigned>(shift)) & 255U));
}

} // namespace

WorldSave::WorldSave(std::filesystem::path folder, const ItemRegistry& items)
    : folder_(std::move(folder)), items_(items) {}

std::filesystem::path WorldSave::regionPath(int chunkX, int chunkZ) const {
    return folder_ / "region" /
        ("r." + std::to_string(floorDiv32(chunkX)) + "." + std::to_string(floorDiv32(chunkZ)) + ".mca");
}

WorldConfig WorldSave::loadConfig(const WorldConfig& defaults) const {
    WorldConfig config = defaults;
    nbt::Document document = nbt::readGzipFile(folder_ / "level.dat");
    nbt::Compound* data = dataCompound(document);
    if (data == nullptr) throw std::runtime_error("level.dat has no Data compound");
    SaveMigration::migrateLevelData(*data);

    config.seed = nbt::integer(*data, "RandomSeed", config.seed);
    config.seedText = std::to_string(config.seed);
    config.worldType = parseGenerator(nbt::string(*data, "generatorName", "default"));
    config.generatorOptions = nbt::string(*data, "generatorOptions", "{}");
    config.generateStructures = nbt::boolean(*data, "MapFeatures", true);
    config.initialWorldTime = nbt::integer(*data, "DayTime", nbt::integer(*data, "Time", 0));
    return config;
}

LoadedPlayerState WorldSave::loadPlayer(Player& player, BlockEntitySystem* blockEntities) const {
    LoadedPlayerState state;
    nbt::Document document = nbt::readGzipFile(folder_ / "level.dat");
    nbt::Compound* data = dataCompound(document);
    if (data == nullptr) return state;
    SaveMigration::migrateLevelData(*data);

    state.gameMode = nbt::integer(*data, "GameType", 0) == 1 ? GameMode::Creative : GameMode::Survival;
    state.spawnPosition = {static_cast<double>(nbt::integer(*data,"SpawnX",0))+0.5, static_cast<double>(nbt::integer(*data,"SpawnY",80)), static_cast<double>(nbt::integer(*data,"SpawnZ",0))+0.5};
    state.position = state.spawnPosition;

    const nbt::Tag* playerTag = nbt::find(*data, "Player");
    if (playerTag == nullptr || playerTag->type != nbt::Type::Compound) return state;
    state.hasPlayer = true;
    const nbt::Compound& stored = playerTag->compound();
    state.gameMode = nbt::integer(stored, "playerGameType",
        state.gameMode == GameMode::Creative ? 1 : 0) == 1 ? GameMode::Creative : GameMode::Survival;

    if (const nbt::Tag* position = nbt::find(stored, "Pos");
        position != nullptr && position->type == nbt::Type::List && position->list().size() >= 3) {
        state.position.x = std::get<double>(position->list()[0].value);
        state.position.y = std::get<double>(position->list()[1].value);
        state.position.z = std::get<double>(position->list()[2].value);
    }
    if (const nbt::Tag* motion = nbt::find(stored, "Motion");
        motion != nullptr && motion->type == nbt::Type::List && motion->list().size() >= 3) {
        state.velocity.x = std::get<double>(motion->list()[0].value);
        state.velocity.y = std::get<double>(motion->list()[1].value);
        state.velocity.z = std::get<double>(motion->list()[2].value);
    }
    if (const nbt::Tag* rotation = nbt::find(stored, "Rotation");
        rotation != nullptr && rotation->type == nbt::Type::List && rotation->list().size() >= 2) {
        state.yaw = std::get<float>(rotation->list()[0].value);
        state.pitch = std::get<float>(rotation->list()[1].value);
    }
    state.selectedHotbar = static_cast<std::size_t>(std::clamp<std::int64_t>(nbt::integer(stored,"SelectedItemSlot",0),0,8));
    state.health = static_cast<float>(nbt::number(stored,"Health",20.0));
    state.air = static_cast<int>(nbt::integer(stored,"Air",300));
    state.fireTicks = static_cast<int>(nbt::integer(stored,"Fire",0));
    state.foodLevel = static_cast<int>(nbt::integer(stored,"foodLevel",20));
    state.foodSaturationLevel = static_cast<float>(nbt::number(stored,"foodSaturationLevel",5.0));
    state.foodExhaustionLevel = static_cast<float>(nbt::number(stored,"foodExhaustionLevel",0.0));
    state.foodTickTimer = static_cast<int>(nbt::integer(stored,"foodTickTimer",0));
    state.experienceTotal = static_cast<int>(nbt::integer(stored,"XpTotal",0));
    state.experienceLevel = static_cast<int>(nbt::integer(stored,"XpLevel",0));
    state.experienceProgress = static_cast<float>(nbt::number(stored,"XpP",0.0));

    if (const nbt::Tag* inventory = nbt::find(stored, "Inventory");
        inventory != nullptr && inventory->type == nbt::Type::List) {
        for (const nbt::Tag& item : inventory->list()) {
            if (item.type != nbt::Type::Compound) continue;
            const nbt::Compound& itemData = item.compound();
            const int slot = static_cast<int>(nbt::integer(itemData, "Slot", -1));
            if (slot >= 0 && slot < static_cast<int>(PlayerInventory::mainSize)) player.inventory().slot(static_cast<std::size_t>(slot)) = itemStackFromTag(itemData);
            else if (slot >= 100 && slot < 104) player.inventory().armor(static_cast<std::size_t>(slot-100)) = itemStackFromTag(itemData);
        }
    }
    if (blockEntities != nullptr) {
        std::array<ItemStack,27> ender{};
        if (const nbt::Tag* enderItems = nbt::find(stored, "EnderItems");
            enderItems != nullptr && enderItems->type == nbt::Type::List) {
            for (const nbt::Tag& item : enderItems->list()) {
                if (item.type != nbt::Type::Compound) continue;
                const nbt::Compound& itemData = item.compound();
                const int slot = static_cast<int>(nbt::integer(itemData, "Slot", -1));
                if (slot >= 0 && slot < 27) ender[static_cast<std::size_t>(slot)] = itemStackFromTag(itemData);
            }
        }
        blockEntities->setEnderChestInventory(std::move(ender));
    }

    player.restoreState(state.position, state.velocity, state.gameMode, state.selectedHotbar);
    player.setRespawnPosition(state.spawnPosition);
    player.restoreSurvival(state.health, state.air, state.fireTicks, false,
                           state.foodLevel, state.foodSaturationLevel, state.foodExhaustionLevel,
                           state.experienceTotal, state.experienceLevel, state.experienceProgress);
    player.foodStats().restore(state.foodLevel, state.foodSaturationLevel, state.foodExhaustionLevel, state.foodTickTimer);
    return state;
}

void WorldSave::loadEnvironment(Environment& environment) const {
    nbt::Document document = nbt::readGzipFile(folder_ / "level.dat");
    nbt::Compound* data = dataCompound(document);
    if (data == nullptr) return;
    SaveMigration::migrateLevelData(*data);
    EnvironmentSaveState state;
    state.totalWorldTime = static_cast<double>(nbt::integer(*data, "Time", 0));
    state.dayTime = static_cast<double>(nbt::integer(*data, "DayTime", nbt::integer(*data, "Time", 0)));
    state.raining = nbt::boolean(*data, "raining", false);
    state.thundering = nbt::boolean(*data, "thundering", false);
    state.rainTime = static_cast<int>(nbt::integer(*data, "rainTime", 0));
    state.thunderTime = static_cast<int>(nbt::integer(*data, "thunderTime", 0));
    state.rainStrength = state.raining ? 1.0F : 0.0F;
    state.thunderStrength = state.thundering ? 1.0F : 0.0F;
    environment.restoreSaveState(state);
}

nbt::Tag WorldSave::itemStackTag(const ItemStack& stack, int slot) const {
    nbt::Compound data;
    data["Slot"] = nbt::Tag(static_cast<std::int8_t>(slot));
    const ItemDefinition& definition = items_.get(stack.itemId);
    const std::string resourceName = definition.name.rfind("minecraft:", 0) == 0
        ? definition.name : "minecraft:" + definition.name;
    data["id"] = nbt::Tag(resourceName);
    // Keep a private numeric hint so Stage 8 can restore inventories exactly
    // even for entries whose registry alias differs from vanilla's display key.
    data["BlockcraftItemId"] = nbt::Tag(static_cast<std::int16_t>(stack.itemId));
    data["Count"] = nbt::Tag(static_cast<std::int8_t>(std::clamp(stack.count, 0, 127)));
    data["Damage"] = nbt::Tag(static_cast<std::int16_t>(stack.damage));
    if (!stack.nbt.empty()) {
        try {
            nbt::Document storedTag = nbt::decode(stack.nbt);
            if (storedTag.root.type == nbt::Type::Compound) data["tag"] = std::move(storedTag.root);
        } catch (...) {
            nbt::ByteArray raw;
            raw.reserve(stack.nbt.size());
            for (std::uint8_t value : stack.nbt) raw.push_back(static_cast<std::int8_t>(value));
            data["BlockcraftRawNbt"] = nbt::Tag(std::move(raw));
        }
    }
    return nbt::Tag(std::move(data));
}

ItemStack WorldSave::itemStackFromTag(const nbt::Compound& data) const {
    ItemStack result;
    const std::int64_t numericHint = nbt::integer(data, "BlockcraftItemId", -1);
    const ItemDefinition* definition = nullptr;
    if (numericHint >= 0 && numericHint <= 65535) {
        const ItemDefinition& candidate = items_.get(static_cast<std::uint16_t>(numericHint));
        if (candidate.id == static_cast<std::uint16_t>(numericHint)) definition = &candidate;
    }
    if (definition == nullptr) {
        std::string id = nbt::string(data, "id", "");
        definition = items_.find(id);
        if (definition == nullptr && id.rfind("minecraft:", 0) == 0)
            definition = items_.find(id.substr(10));
    }
    if (definition == nullptr) return result;

    result.itemId = definition->id;
    result.count = static_cast<int>(nbt::integer(data, "Count", 0));
    result.damage = static_cast<std::uint16_t>(nbt::integer(data, "Damage", 0));
    if (const nbt::Tag* vanillaTag = nbt::find(data, "tag");
        vanillaTag != nullptr && vanillaTag->type == nbt::Type::Compound) {
        nbt::Document storedTag;
        storedTag.root = *vanillaTag;
        result.nbt = nbt::encode(storedTag);
    } else if (const nbt::Tag* raw = nbt::find(data, "BlockcraftRawNbt");
               raw != nullptr && raw->type == nbt::Type::ByteArray) {
        for (std::int8_t value : std::get<nbt::ByteArray>(raw->value))
            result.nbt.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

void WorldSave::saveLevel(const WorldConfig& config, const Player& player,
                          const Environment& environment, const BlockEntitySystem* blockEntities) {
    std::string levelName = folder_.filename().string();
    std::int32_t spawnX = 0;
    std::int32_t spawnY = 64;
    std::int32_t spawnZ = 0;
    try {
        const nbt::Document oldDocument = nbt::readGzipFile(folder_ / "level.dat");
        if (const nbt::Compound* old = dataCompound(oldDocument)) {
            levelName = nbt::string(*old, "LevelName", levelName);
            spawnX = static_cast<std::int32_t>(nbt::integer(*old, "SpawnX", spawnX));
            spawnY = static_cast<std::int32_t>(nbt::integer(*old, "SpawnY", spawnY));
            spawnZ = static_cast<std::int32_t>(nbt::integer(*old, "SpawnZ", spawnZ));
        }
    } catch (...) {
        // A missing old file is valid while creating the first level.dat.
    }

    nbt::Compound data;
    data["DataVersion"] = nbt::Tag(dataVersion1122);
    data["BlockcraftSaveVersion"] = nbt::Tag(SaveMigration::currentVersion);
    data["version"] = nbt::Tag(anvilVersion);
    data["LevelName"] = nbt::Tag(levelName);
    data["RandomSeed"] = nbt::Tag(config.seed);
    data["generatorName"] = nbt::Tag(generatorName(config.worldType));
    data["generatorVersion"] = nbt::Tag(static_cast<std::int32_t>(config.worldType == WorldType::Default11 ? 0 : 1));
    data["generatorOptions"] = nbt::Tag(config.generatorOptions);
    data["MapFeatures"] = nbt::Tag(static_cast<std::int8_t>(config.generateStructures));
    data["GameType"] = nbt::Tag(static_cast<std::int32_t>(player.gameMode() == GameMode::Creative ? 1 : 0));
    data["hardcore"] = nbt::Tag(static_cast<std::int8_t>(0));
    data["allowCommands"] = nbt::Tag(static_cast<std::int8_t>(player.gameMode() == GameMode::Creative));
    data["initialized"] = nbt::Tag(static_cast<std::int8_t>(1));
    data["LastPlayed"] = nbt::Tag(nowMillis());
    data["SpawnX"] = nbt::Tag(spawnX);
    data["SpawnY"] = nbt::Tag(spawnY);
    data["SpawnZ"] = nbt::Tag(spawnZ);

    const EnvironmentSaveState environmentState = environment.saveState();
    data["Time"] = nbt::Tag(static_cast<std::int64_t>(environmentState.totalWorldTime));
    data["DayTime"] = nbt::Tag(static_cast<std::int64_t>(environmentState.dayTime));
    data["raining"] = nbt::Tag(static_cast<std::int8_t>(environmentState.raining));
    data["rainTime"] = nbt::Tag(static_cast<std::int32_t>(environmentState.rainTime));
    data["thundering"] = nbt::Tag(static_cast<std::int8_t>(environmentState.thundering));
    data["thunderTime"] = nbt::Tag(static_cast<std::int32_t>(environmentState.thunderTime));

    nbt::Compound storedPlayer;
    storedPlayer["Dimension"] = nbt::Tag(static_cast<std::int32_t>(0));
    storedPlayer["playerGameType"] = nbt::Tag(static_cast<std::int32_t>(player.gameMode() == GameMode::Creative ? 1 : 0));
    storedPlayer["SelectedItemSlot"] = nbt::Tag(static_cast<std::int32_t>(player.inventory().selectedHotbar()));
    storedPlayer["OnGround"] = nbt::Tag(static_cast<std::int8_t>(player.onGround()));
    storedPlayer["Health"] = nbt::Tag(player.health());
    storedPlayer["Air"] = nbt::Tag(static_cast<std::int16_t>(player.air()));
    storedPlayer["Fire"] = nbt::Tag(static_cast<std::int16_t>(player.fireTicks()));
    storedPlayer["foodLevel"] = nbt::Tag(static_cast<std::int32_t>(player.foodStats().foodLevel()));
    storedPlayer["foodTickTimer"] = nbt::Tag(static_cast<std::int32_t>(player.foodStats().foodTimer()));
    storedPlayer["foodSaturationLevel"] = nbt::Tag(player.foodStats().saturationLevel());
    storedPlayer["foodExhaustionLevel"] = nbt::Tag(player.foodStats().exhaustionLevel());
    storedPlayer["XpTotal"] = nbt::Tag(static_cast<std::int32_t>(player.experienceTotal()));
    storedPlayer["XpLevel"] = nbt::Tag(static_cast<std::int32_t>(player.experienceLevel()));
    storedPlayer["XpP"] = nbt::Tag(player.experienceProgress());
    storedPlayer["Pos"] = nbt::Tag(nbt::Type::Double, nbt::List{
        nbt::Tag(player.feetPosition().x), nbt::Tag(player.feetPosition().y), nbt::Tag(player.feetPosition().z)});
    storedPlayer["Motion"] = nbt::Tag(nbt::Type::Double, nbt::List{
        nbt::Tag(player.velocity().x), nbt::Tag(player.velocity().y), nbt::Tag(player.velocity().z)});
    storedPlayer["Rotation"] = nbt::Tag(nbt::Type::Float, nbt::List{nbt::Tag(0.0F), nbt::Tag(0.0F)});

    nbt::List inventory;
    for (std::size_t index=0;index<PlayerInventory::mainSize;++index){const ItemStack& stack=player.inventory().slot(index);if(!stack.empty())inventory.push_back(itemStackTag(stack,static_cast<int>(index)));}
    for (std::size_t index=0;index<4;++index){const ItemStack& stack=player.inventory().armor(index);if(!stack.empty())inventory.push_back(itemStackTag(stack,100+static_cast<int>(index)));}
    storedPlayer["Inventory"] = nbt::Tag(nbt::Type::Compound, std::move(inventory));
    if (blockEntities != nullptr) {
        nbt::List enderItems;
        const auto& ender = blockEntities->enderChestInventory();
        for (std::size_t index=0; index<ender.size(); ++index) if (!ender[index].empty())
            enderItems.push_back(itemStackTag(ender[index], static_cast<int>(index)));
        storedPlayer["EnderItems"] = nbt::Tag(nbt::Type::Compound, std::move(enderItems));
    }
    data["Player"] = nbt::Tag(std::move(storedPlayer));

    nbt::Document document;
    document.root = nbt::Tag(nbt::Compound{{"Data", nbt::Tag(std::move(data))}});
    nbt::writeGzipFile(folder_ / "level.dat", document);
}

nbt::Document WorldSave::chunkDocument(const Chunk& chunk,
                                       const BlockEntitySystem& blockEntities,
                                       const EntityManager* entities) const {
    nbt::Compound level;
    level["xPos"] = nbt::Tag(static_cast<std::int32_t>(chunk.x()));
    level["zPos"] = nbt::Tag(static_cast<std::int32_t>(chunk.z()));
    level["LastUpdate"] = nbt::Tag(chunk.lastUpdate());
    level["TerrainPopulated"] = nbt::Tag(static_cast<std::int8_t>(chunk.terrainPopulated()));
    level["LightPopulated"] = nbt::Tag(static_cast<std::int8_t>(chunk.lightPopulated()));
    level["V"] = nbt::Tag(static_cast<std::int8_t>(1));
    level["InhabitedTime"] = nbt::Tag(chunk.inhabitedTime());

    nbt::ByteArray biomes(256);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            biomes[static_cast<std::size_t>((z << 4) | x)] = static_cast<std::int8_t>(chunk.biome(x, z));
    level["Biomes"] = nbt::Tag(std::move(biomes));

    nbt::IntArray heightMap(256, 0);
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 255; y >= 0; --y) {
                if (blockId(chunk.get(x, y, z)) != 0) {
                    heightMap[static_cast<std::size_t>((z << 4) | x)] = y + 1;
                    break;
                }
            }
        }
    }
    level["HeightMap"] = nbt::Tag(std::move(heightMap));

    nbt::List sections;
    for (int sectionY = 0; sectionY < sectionCount; ++sectionY) {
        if (chunk.section(sectionY) == nullptr) continue;
        nbt::ByteArray blocks(4096);
        nbt::ByteArray metadata(2048);
        nbt::ByteArray skyLight(2048);
        nbt::ByteArray blockLight(2048);
        nbt::ByteArray add(2048);
        bool addNeeded = false;
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const std::size_t index = static_cast<std::size_t>((y << 8) | (z << 4) | x);
                    const BlockState state = chunk.get(x, sectionY * 16 + y, z);
                    blocks[index] = static_cast<std::int8_t>(blockId(state) & 255U);
                    if (blockId(state) > 255U) {
                        setNibble(add, index, static_cast<std::uint8_t>(blockId(state) >> 8U));
                        addNeeded = true;
                    }
                    setNibble(metadata, index, blockMetadata(state));
                    setNibble(skyLight, index, chunk.skyLight(x, sectionY * 16 + y, z));
                    setNibble(blockLight, index, chunk.blockLight(x, sectionY * 16 + y, z));
                }
            }
        }
        nbt::Compound section;
        section["Y"] = nbt::Tag(static_cast<std::int8_t>(sectionY));
        section["Blocks"] = nbt::Tag(std::move(blocks));
        section["Data"] = nbt::Tag(std::move(metadata));
        section["SkyLight"] = nbt::Tag(std::move(skyLight));
        section["BlockLight"] = nbt::Tag(std::move(blockLight));
        if (addNeeded) section["Add"] = nbt::Tag(std::move(add));
        sections.emplace_back(std::move(section));
    }
    level["Sections"] = nbt::Tag(nbt::Type::Compound, std::move(sections));
    nbt::List entityList;
    for (const auto& encoded : chunk.entityNbt()) {
        try { auto doc = nbt::decode(encoded); if (doc.root.type == nbt::Type::Compound) entityList.push_back(std::move(doc.root)); } catch (...) {}
    }
    if (entities != nullptr) {
        for (const auto& encoded : entities->serializeChunk(chunk.x(), chunk.z())) {
            try { auto doc = nbt::decode(encoded); if (doc.root.type == nbt::Type::Compound) entityList.push_back(std::move(doc.root)); } catch (...) {}
        }
    }
    level["Entities"] = nbt::Tag(nbt::Type::Compound, std::move(entityList));

    const int minimumX = chunk.x() * 16;
    const int minimumZ = chunk.z() * 16;
    nbt::List tileEntities;
    std::unordered_set<std::uint64_t> runtimePositions;
    std::map<std::uint64_t, nbt::Tag> originalTiles;
    for (const GeneratedBlockEntity& generated : chunk.blockEntities()) {
        try {
            nbt::Document document = nbt::decode(generated.nbt);
            if (document.root.type == nbt::Type::Compound)
                originalTiles.insert_or_assign(blockPositionKey(generated.x, generated.y, generated.z),
                                               std::move(document.root));
        } catch (...) {
        }
    }

    for (const auto& [key, entity] : blockEntities.entities()) {
        static_cast<void>(key);
        if (entity.position.x < minimumX || entity.position.x >= minimumX + 16 ||
            entity.position.z < minimumZ || entity.position.z >= minimumZ + 16) continue;
        const std::uint64_t positionKey = blockPositionKey(entity.position.x, entity.position.y, entity.position.z);
        runtimePositions.insert(positionKey);

        nbt::Compound tile;
        if (const auto old = originalTiles.find(positionKey);
            old != originalTiles.end() && old->second.type == nbt::Type::Compound)
            tile = old->second.compound();
        tile["id"] = nbt::Tag(tileEntityId(entity.type));
        tile["x"] = nbt::Tag(static_cast<std::int32_t>(entity.position.x));
        tile["y"] = nbt::Tag(static_cast<std::int32_t>(entity.position.y));
        tile["z"] = nbt::Tag(static_cast<std::int32_t>(entity.position.z));
        if (entity.type == RuntimeBlockEntityType::Sign) {
            for (int line = 0; line < 4; ++line)
                tile["Text" + std::to_string(line + 1)] =
                    nbt::Tag(std::string("{\"text\":\"") + entity.signText[static_cast<std::size_t>(line)] + "\"}");
        }
        if (entity.type == RuntimeBlockEntityType::Bed)
            tile["color"] = nbt::Tag(static_cast<std::int32_t>(entity.color));
        if (entity.type == RuntimeBlockEntityType::ShulkerBox)
            tile["Color"] = nbt::Tag(static_cast<std::int8_t>(entity.color));
        if (entity.type == RuntimeBlockEntityType::Chest || entity.type == RuntimeBlockEntityType::TrappedChest ||
            entity.type == RuntimeBlockEntityType::ShulkerBox || entity.type == RuntimeBlockEntityType::Furnace ||
            entity.type == RuntimeBlockEntityType::Hopper || entity.type == RuntimeBlockEntityType::BrewingStand ||
            entity.type == RuntimeBlockEntityType::EnchantingTable || entity.type == RuntimeBlockEntityType::Beacon ||
            entity.type == RuntimeBlockEntityType::Jukebox || entity.type == RuntimeBlockEntityType::FlowerPot) {
            nbt::List items;
            const int slots = entity.type == RuntimeBlockEntityType::Furnace ? 3 :
                entity.type == RuntimeBlockEntityType::Hopper ? 5 : entity.type == RuntimeBlockEntityType::BrewingStand ? 5 :
                entity.type == RuntimeBlockEntityType::EnchantingTable ? 2 :
                (entity.type == RuntimeBlockEntityType::Beacon || entity.type == RuntimeBlockEntityType::Jukebox || entity.type == RuntimeBlockEntityType::FlowerPot) ? 1 : 27;
            for (int slot = 0; slot < slots; ++slot) {
                const ItemStack& stack = entity.inventory[static_cast<std::size_t>(slot)];
                if (!stack.empty()) items.push_back(itemStackTag(stack, slot));
            }
            tile["Items"] = nbt::Tag(nbt::Type::Compound, std::move(items));
            if (entity.type == RuntimeBlockEntityType::Furnace) {
                tile["BurnTime"] = nbt::Tag(static_cast<std::int16_t>(std::clamp(entity.furnaceBurnTime, 0, 32767)));
                tile["CookTime"] = nbt::Tag(static_cast<std::int16_t>(std::clamp(entity.furnaceCookTime, 0, 32767)));
                tile["CookTimeTotal"] = nbt::Tag(static_cast<std::int16_t>(std::clamp(entity.furnaceCookTimeTotal, 0, 32767)));
            }
            if (entity.type == RuntimeBlockEntityType::Hopper)
                tile["TransferCooldown"] = nbt::Tag(static_cast<std::int32_t>(entity.transferCooldown));
            if (entity.type == RuntimeBlockEntityType::BrewingStand) {
                tile["BrewTime"] = nbt::Tag(static_cast<std::int16_t>(std::clamp(entity.brewTime, 0, 32767)));
                tile["Fuel"] = nbt::Tag(static_cast<std::int8_t>(std::clamp(entity.brewingFuel, 0, 127)));
            }
            if (entity.type == RuntimeBlockEntityType::Beacon) {
                tile["Levels"] = nbt::Tag(static_cast<std::int32_t>(entity.beaconLevels));
                tile["Primary"] = nbt::Tag(static_cast<std::int32_t>(entity.beaconPrimary));
                tile["Secondary"] = nbt::Tag(static_cast<std::int32_t>(entity.beaconSecondary));
            }
            // Stage 7 does not yet materialize vanilla loot tables. Preserve
            // LootTable/LootTableSeed from generated data until that gameplay
            // path exists instead of silently destroying the pending loot.
        }
        if (entity.type == RuntimeBlockEntityType::Jukebox) tile["Record"] = nbt::Tag(static_cast<std::int32_t>(entity.recordItem));
        if (entity.type == RuntimeBlockEntityType::FlowerPot) { tile["Item"] = nbt::Tag(static_cast<std::int32_t>(entity.flowerItem)); tile["Data"] = nbt::Tag(static_cast<std::int32_t>(entity.flowerData)); }
        if (entity.type == RuntimeBlockEntityType::MobSpawner) {
            tile["Delay"] = nbt::Tag(static_cast<std::int16_t>(std::clamp(entity.spawnerDelay,0,32767)));
            tile["SpawnData"] = nbt::Tag(nbt::Compound{{"id", nbt::Tag(entity.spawnerEntityId)}});
        }
        if (entity.type == RuntimeBlockEntityType::Banner) tile["Base"] = nbt::Tag(static_cast<std::int32_t>(entity.color));
                tileEntities.emplace_back(std::move(tile));
    }

    for (auto& [positionKey, tile] : originalTiles) {
        if (!runtimePositions.contains(positionKey)) tileEntities.push_back(std::move(tile));
    }
    level["TileEntities"] = nbt::Tag(nbt::Type::Compound, std::move(tileEntities));

    nbt::List tileTicks;
    for (const std::vector<std::uint8_t>& encoded : chunk.scheduledTicks()) {
        try {
            nbt::Document tick = nbt::decode(encoded);
            if (tick.root.type == nbt::Type::Compound) tileTicks.push_back(std::move(tick.root));
        } catch (...) {
        }
    }
    level["TileTicks"] = nbt::Tag(nbt::Type::Compound, std::move(tileTicks));

    nbt::Document document;
    document.root = nbt::Tag(nbt::Compound{{"Level", nbt::Tag(std::move(level))}});
    return document;
}

std::unique_ptr<Chunk> WorldSave::chunkFromDocument(const nbt::Document& document) const {
    const nbt::Compound& root = document.root.compound();
    const nbt::Tag* levelTag = nbt::find(root, "Level");
    if (levelTag == nullptr || levelTag->type != nbt::Type::Compound)
        throw std::runtime_error("Anvil chunk has no Level compound");
    const nbt::Compound& level = levelTag->compound();
    const int chunkX = static_cast<int>(nbt::integer(level, "xPos", 0));
    const int chunkZ = static_cast<int>(nbt::integer(level, "zPos", 0));
    auto chunk = std::make_unique<Chunk>(chunkX, chunkZ);
    chunk->setSaveMetadata(
        nbt::integer(level, "LastUpdate", 0),
        nbt::integer(level, "InhabitedTime", 0),
        nbt::boolean(level, "TerrainPopulated", true),
        nbt::boolean(level, "LightPopulated", true));

    std::vector<std::uint8_t> sky(16 * 256 * 16, 0);
    std::vector<std::uint8_t> block(16 * 256 * 16, 0);
    if (const nbt::Tag* sections = nbt::find(level, "Sections");
        sections != nullptr && sections->type == nbt::Type::List) {
        for (const nbt::Tag& sectionTag : sections->list()) {
            if (sectionTag.type != nbt::Type::Compound) continue;
            const nbt::Compound& section = sectionTag.compound();
            const int sectionY = static_cast<int>(nbt::integer(section, "Y", -1));
            if (sectionY < 0 || sectionY >= sectionCount) continue;
            const nbt::Tag* blocksTag = nbt::find(section, "Blocks");
            if (blocksTag == nullptr || blocksTag->type != nbt::Type::ByteArray) continue;
            const nbt::ByteArray& blocks = std::get<nbt::ByteArray>(blocksTag->value);
            const nbt::ByteArray emptyNibble(2048);
            const nbt::Tag* dataTag = nbt::find(section, "Data");
            const nbt::Tag* addTag = nbt::find(section, "Add");
            const nbt::Tag* skyTag = nbt::find(section, "SkyLight");
            const nbt::Tag* blockTag = nbt::find(section, "BlockLight");
            const nbt::ByteArray& metadata = dataTag != nullptr && dataTag->type == nbt::Type::ByteArray
                ? std::get<nbt::ByteArray>(dataTag->value) : emptyNibble;
            const nbt::ByteArray& add = addTag != nullptr && addTag->type == nbt::Type::ByteArray
                ? std::get<nbt::ByteArray>(addTag->value) : emptyNibble;
            const nbt::ByteArray& skyLight = skyTag != nullptr && skyTag->type == nbt::Type::ByteArray
                ? std::get<nbt::ByteArray>(skyTag->value) : emptyNibble;
            const nbt::ByteArray& blockLight = blockTag != nullptr && blockTag->type == nbt::Type::ByteArray
                ? std::get<nbt::ByteArray>(blockTag->value) : emptyNibble;

            for (int y = 0; y < 16; ++y) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        const std::size_t index = static_cast<std::size_t>((y << 8) | (z << 4) | x);
                        if (index >= blocks.size()) continue;
                        const std::uint16_t id = static_cast<std::uint8_t>(blocks[index]) |
                            (static_cast<std::uint16_t>(nibble(add, index)) << 8U);
                        const std::uint8_t meta = nibble(metadata, index);
                        if (id != 0) static_cast<void>(chunk->set(x, sectionY * 16 + y, z, makeBlockState(id, meta)));
                        const std::size_t cell = chunkCellIndex(x, sectionY * 16 + y, z);
                        sky[cell] = nibble(skyLight, index);
                        block[cell] = nibble(blockLight, index);
                    }
                }
            }
        }
    }

    if (const nbt::Tag* biomes = nbt::find(level, "Biomes");
        biomes != nullptr && biomes->type == nbt::Type::ByteArray) {
        const nbt::ByteArray& values = std::get<nbt::ByteArray>(biomes->value);
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const std::size_t index = static_cast<std::size_t>((z << 4) | x);
                if (index < values.size()) chunk->setBiome(x, z, static_cast<std::uint8_t>(values[index]));
            }
        }
    }
    chunk->applyLighting(sky, block);

    if (const nbt::Tag* entityList = nbt::find(level, "Entities");
        entityList != nullptr && entityList->type == nbt::Type::List) {
        for (const nbt::Tag& entity : entityList->list()) {
            if (entity.type != nbt::Type::Compound) continue;
            nbt::Document single; single.root = entity;
            chunk->addEntityNbt(nbt::encode(single));
        }
    }

    if (const nbt::Tag* tileEntities = nbt::find(level, "TileEntities");
        tileEntities != nullptr && tileEntities->type == nbt::Type::List) {
        for (const nbt::Tag& tile : tileEntities->list()) {
            if (tile.type != nbt::Type::Compound) continue;
            nbt::Tag normalizedTile = tile;
            nbt::Compound& compound = normalizedTile.compound();
            if (nbt::Tag* items = nbt::find(compound, "Items");
                items != nullptr && items->type == nbt::Type::List) {
                for (nbt::Tag& item : items->list()) {
                    if (item.type != nbt::Type::Compound) continue;
                    nbt::Compound& itemData = item.compound();
                    if (nbt::find(itemData, "BlockcraftItemId") == nullptr) {
                        std::string id = nbt::string(itemData, "id", "");
                        const ItemDefinition* definition = items_.find(id);
                        if (definition == nullptr && id.rfind("minecraft:", 0) == 0)
                            definition = items_.find(id.substr(10));
                        if (definition != nullptr)
                            itemData["BlockcraftItemId"] = nbt::Tag(static_cast<std::int16_t>(definition->id));
                    }
                    if (const nbt::Tag* vanillaTag = nbt::find(itemData, "tag");
                        vanillaTag != nullptr && vanillaTag->type == nbt::Type::Compound &&
                        nbt::find(itemData, "BlockcraftRawNbt") == nullptr) {
                        nbt::Document itemDocument;
                        itemDocument.root = *vanillaTag;
                        const std::vector<std::uint8_t> bytes = nbt::encode(itemDocument);
                        nbt::ByteArray raw;
                        raw.reserve(bytes.size());
                        for (std::uint8_t value : bytes) raw.push_back(static_cast<std::int8_t>(value));
                        itemData["BlockcraftRawNbt"] = nbt::Tag(std::move(raw));
                    }
                }
            }
            GeneratedBlockEntity generated;
            generated.x = static_cast<int>(nbt::integer(compound, "x", 0));
            generated.y = static_cast<int>(nbt::integer(compound, "y", 0));
            generated.z = static_cast<int>(nbt::integer(compound, "z", 0));
            generated.id = nbt::string(compound, "id", "");
            nbt::Document single;
            single.root = std::move(normalizedTile);
            generated.nbt = nbt::encode(single);
            chunk->addBlockEntity(std::move(generated));
        }
    }

    if (const nbt::Tag* tileTicks = nbt::find(level, "TileTicks");
        tileTicks != nullptr && tileTicks->type == nbt::Type::List) {
        for (const nbt::Tag& tick : tileTicks->list()) {
            if (tick.type != nbt::Type::Compound) continue;
            nbt::Document single;
            single.root = tick;
            chunk->addScheduledTick(nbt::encode(single));
        }
    }
    return chunk;
}

std::unique_ptr<Chunk> WorldSave::loadChunk(int chunkX, int chunkZ) const {
    RegionFile region(regionPath(chunkX, chunkZ));
    const auto bytes = region.readChunk(floorMod32(chunkX), floorMod32(chunkZ));
    if (!bytes) return nullptr;
    return chunkFromDocument(nbt::decode(*bytes));
}

void WorldSave::saveChunk(const Chunk& chunk, const BlockEntitySystem& blockEntities, const EntityManager* entities) {
    RegionFile region(regionPath(chunk.x(), chunk.z()));
    region.writeChunk(floorMod32(chunk.x()), floorMod32(chunk.z()),
                      nbt::encode(chunkDocument(chunk, blockEntities, entities)));
}

void WorldSave::saveAll(const World& world, const BlockEntitySystem& blockEntities,
                        const WorldConfig& config, const Player& player,
                        const Environment& environment, const EntityManager* entities) {
    for (const auto& [key, chunk] : world.chunks()) {
        static_cast<void>(key);
        if (chunk) saveChunk(*chunk, blockEntities, entities);
    }
    saveLevel(config, player, environment, &blockEntities);
}

std::vector<WorldSummary> WorldSave::listWorlds(const std::filesystem::path& savesRoot) {
    std::vector<WorldSummary> worlds;
    if (!std::filesystem::exists(savesRoot)) return worlds;
    for (const auto& entry : std::filesystem::directory_iterator(savesRoot)) {
        if (!entry.is_directory() || !std::filesystem::exists(entry.path() / "level.dat")) continue;
        try {
            const nbt::Document document = nbt::readGzipFile(entry.path() / "level.dat");
            const nbt::Compound* data = dataCompound(document);
            if (data == nullptr) continue;
            WorldSummary summary;
            summary.folder = entry.path();
            summary.folderName = entry.path().filename().string();
            summary.levelName = nbt::string(*data, "LevelName", summary.folderName);
            summary.lastPlayed = nbt::integer(*data, "LastPlayed", 0);
            summary.seed = nbt::integer(*data, "RandomSeed", 0);
            summary.worldType = parseGenerator(nbt::string(*data, "generatorName", "default"));
            summary.gameMode = nbt::integer(*data, "GameType", 0) == 1 ? GameMode::Creative : GameMode::Survival;
            summary.hardcore = nbt::boolean(*data, "hardcore", false);
            summary.requiresConversion = nbt::integer(*data, "version", anvilVersion) != anvilVersion;
            worlds.push_back(std::move(summary));
        } catch (...) {
            // Vanilla's world list also tolerates individual broken saves.
        }
    }
    std::sort(worlds.begin(), worlds.end(), [](const WorldSummary& left, const WorldSummary& right) {
        return left.lastPlayed > right.lastPlayed;
    });
    return worlds;
}

std::filesystem::path WorldSave::createWorld(const std::filesystem::path& savesRoot,
                                              const CreateWorldRequest& request,
                                              const WorldConfig& defaults) {
    std::filesystem::create_directories(savesRoot);
    const std::string base = cleanFolderName(request.levelName.empty() ? "New World" : request.levelName);
    std::string folderName = base;
    int suffix = 1;
    while (std::filesystem::exists(savesRoot / folderName))
        folderName = base + "-" + std::to_string(++suffix);

    const std::filesystem::path folder = savesRoot / folderName;
    std::filesystem::create_directories(folder / "region");
    std::filesystem::create_directories(folder / "data");
    std::filesystem::create_directories(folder / "playerdata");
    writeSessionLock(folder);

    WorldConfig config = defaults;
    config.seedText = request.seedText.empty() ? std::to_string(nowMillis()) : request.seedText;
    config.seed = parseMinecraftSeed(config.seedText);
    config.worldType = request.worldType;
    config.generatorOptions = request.generatorOptions;
    config.generateStructures = request.generateStructures;

    nbt::Compound data;
    data["DataVersion"] = nbt::Tag(dataVersion1122);
    data["BlockcraftSaveVersion"] = nbt::Tag(SaveMigration::currentVersion);
    data["version"] = nbt::Tag(anvilVersion);
    data["LevelName"] = nbt::Tag(request.levelName.empty() ? std::string("New World") : request.levelName);
    data["RandomSeed"] = nbt::Tag(config.seed);
    data["generatorName"] = nbt::Tag(generatorName(config.worldType));
    data["generatorVersion"] = nbt::Tag(static_cast<std::int32_t>(config.worldType == WorldType::Default11 ? 0 : 1));
    data["generatorOptions"] = nbt::Tag(config.generatorOptions);
    data["MapFeatures"] = nbt::Tag(static_cast<std::int8_t>(config.generateStructures));
    data["GameType"] = nbt::Tag(static_cast<std::int32_t>(request.gameMode == GameMode::Creative ? 1 : 0));
    data["hardcore"] = nbt::Tag(static_cast<std::int8_t>(0));
    data["allowCommands"] = nbt::Tag(static_cast<std::int8_t>(request.gameMode == GameMode::Creative));
    data["Time"] = nbt::Tag(static_cast<std::int64_t>(0));
    data["DayTime"] = nbt::Tag(static_cast<std::int64_t>(0));
    data["LastPlayed"] = nbt::Tag(nowMillis());
    data["SpawnX"] = nbt::Tag(static_cast<std::int32_t>(0));
    data["SpawnY"] = nbt::Tag(static_cast<std::int32_t>(64));
    data["SpawnZ"] = nbt::Tag(static_cast<std::int32_t>(0));
    data["raining"] = nbt::Tag(static_cast<std::int8_t>(0));
    data["rainTime"] = nbt::Tag(static_cast<std::int32_t>(0));
    data["thundering"] = nbt::Tag(static_cast<std::int8_t>(0));
    data["thunderTime"] = nbt::Tag(static_cast<std::int32_t>(0));
    data["initialized"] = nbt::Tag(static_cast<std::int8_t>(1));

    nbt::Document document;
    document.root = nbt::Tag(nbt::Compound{{"Data", nbt::Tag(std::move(data))}});
    nbt::writeGzipFile(folder / "level.dat", document);
    return folder;
}

void WorldSave::deleteWorld(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    if (error) throw std::runtime_error("Could not delete world: " + folder.string());
}
