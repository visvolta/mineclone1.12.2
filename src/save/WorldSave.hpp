#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "player/Player.hpp"
#include "save/Nbt.hpp"
#include "worldgen/WorldConfig.hpp"

class BlockEntitySystem;
class Chunk;
class Environment;
class ItemRegistry;
class World;

struct WorldSummary {
    std::filesystem::path folder;
    std::string folderName;
    std::string levelName;
    std::int64_t lastPlayed = 0;
    std::int64_t seed = 0;
    WorldType worldType = WorldType::Default;
    GameMode gameMode = GameMode::Survival;
    bool hardcore = false;
    bool requiresConversion = false;
};

struct CreateWorldRequest {
    std::string levelName = "New World";
    std::string seedText;
    WorldType worldType = WorldType::Default;
    std::string generatorOptions = "{}";
    GameMode gameMode = GameMode::Survival;
    bool generateStructures = true;
};

struct LoadedPlayerState {
    bool hasPlayer = false;
    glm::dvec3 position{0.5, 80.0, 0.5};
    glm::dvec3 velocity{0.0};
    float yaw = 0.0F;
    float pitch = 0.0F;
    GameMode gameMode = GameMode::Survival;
    std::size_t selectedHotbar = 0;
    glm::dvec3 spawnPosition{0.5,80.0,0.5};
    float health = 20.0F;
    int air = 300;
    int fireTicks = 0;
};

class WorldSave {
public:
    WorldSave(std::filesystem::path folder, const ItemRegistry& items);

    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }
    [[nodiscard]] WorldConfig loadConfig(const WorldConfig& clientDefaults) const;
    [[nodiscard]] LoadedPlayerState loadPlayer(Player& player) const;
    void loadEnvironment(Environment& environment) const;
    void saveLevel(const WorldConfig& config, const Player& player, const Environment& environment);

    [[nodiscard]] std::unique_ptr<Chunk> loadChunk(int chunkX, int chunkZ) const;
    void saveChunk(const Chunk& chunk, const BlockEntitySystem& blockEntities);
    void saveAll(const World& world, const BlockEntitySystem& blockEntities,
                 const WorldConfig& config, const Player& player, const Environment& environment);

    [[nodiscard]] static std::vector<WorldSummary> listWorlds(const std::filesystem::path& savesRoot);
    [[nodiscard]] static std::filesystem::path createWorld(const std::filesystem::path& savesRoot,
                                                           const CreateWorldRequest& request,
                                                           const WorldConfig& clientDefaults);
    static void deleteWorld(const std::filesystem::path& folder);

private:
    [[nodiscard]] std::filesystem::path regionPath(int chunkX, int chunkZ) const;
    [[nodiscard]] nbt::Document chunkDocument(const Chunk& chunk,
                                              const BlockEntitySystem& blockEntities) const;
    [[nodiscard]] std::unique_ptr<Chunk> chunkFromDocument(const nbt::Document& document) const;
    [[nodiscard]] nbt::Tag itemStackTag(const ItemStack& stack, int slot) const;
    [[nodiscard]] ItemStack itemStackFromTag(const nbt::Compound& compound) const;

    std::filesystem::path folder_;
    const ItemRegistry& items_;
};
