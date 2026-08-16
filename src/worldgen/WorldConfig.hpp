#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

enum class WorldType {
    Default,
    Flat,
    LargeBiomes,
    Amplified,
    Customized,
    DebugAllBlockStates,
    Default11
};

enum class WeatherOverride {
    Vanilla,
    Clear,
    Rain,
    Thunder
};

struct WorldConfig {
    std::string seedText = "Blockcraft";
    std::int64_t seed = 0;
    WorldType worldType = WorldType::Default;
    std::string generatorOptions = "{}";
    bool generateStructures = true;
    int viewDistance = 8;
    bool vsync = false;
    int targetFps = 120;
    int chunkCacheCapacity = 128;
    double streamMainThreadBudgetMs = 1.0;
    double lightMainThreadBudgetMs = 1.0;
    double meshMainThreadBudgetMs = 1.5;
    bool daylightCycle = true;
    double dayCycleSeconds = 1200.0;
    std::int64_t initialWorldTime = 0;
    bool weatherCycle = true;
    WeatherOverride weatherOverride = WeatherOverride::Vanilla;

    [[nodiscard]] static WorldConfig load(const std::filesystem::path& path);
};

[[nodiscard]] std::string_view worldTypeName(WorldType type);
[[nodiscard]] std::string_view weatherOverrideName(WeatherOverride value);
[[nodiscard]] int generatorOptionInt(const std::string& json, std::string_view key, int fallback);
[[nodiscard]] double generatorOptionDouble(const std::string& json, std::string_view key, double fallback);
[[nodiscard]] bool generatorOptionBool(const std::string& json, std::string_view key, bool fallback);
