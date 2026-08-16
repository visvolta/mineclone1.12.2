#include "worldgen/WorldConfig.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "worldgen/JavaRandom.hpp"

namespace {

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

WorldType parseWorldType(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "default") return WorldType::Default;
    if (value == "flat") return WorldType::Flat;
    if (value == "largebiomes" || value == "large_biomes") return WorldType::LargeBiomes;
    if (value == "amplified") return WorldType::Amplified;
    if (value == "customized") return WorldType::Customized;
    if (value == "debug_all_block_states" || value == "debug") return WorldType::DebugAllBlockStates;
    if (value == "default_1_1") return WorldType::Default11;
    throw std::runtime_error("Unknown 1.12.2 world_type: " + value);
}

bool parseBoolean(const std::string& value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("Expected true or false, received: " + value);
}

WeatherOverride parseWeatherOverride(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "vanilla") return WeatherOverride::Vanilla;
    if (value == "clear") return WeatherOverride::Clear;
    if (value == "rain") return WeatherOverride::Rain;
    if (value == "thunder") return WeatherOverride::Thunder;
    throw std::runtime_error("Unknown weather_override: " + value);
}

std::string_view optionValue(const std::string& json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    std::size_t position = json.find(quoted);
    if (position == std::string::npos) return {};
    position = json.find(':', position + quoted.size());
    if (position == std::string::npos) return {};
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    std::size_t end = position;
    while (end < json.size() && json[end] != ',' && json[end] != '}' &&
           !std::isspace(static_cast<unsigned char>(json[end]))) ++end;
    return std::string_view(json).substr(position, end - position);
}

} // namespace

WorldConfig WorldConfig::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Could not open world configuration: " + path.string());

    std::unordered_map<std::string, std::string> values;
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
            throw std::runtime_error("Invalid world.cfg line " + std::to_string(lineNumber));
        values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
    }

    WorldConfig config;
    if (values.contains("seed")) config.seedText = values.at("seed");
    if (values.contains("world_type")) config.worldType = parseWorldType(values.at("world_type"));
    if (values.contains("generator_options")) config.generatorOptions = values.at("generator_options");
    if (values.contains("generate_structures")) config.generateStructures = parseBoolean(values.at("generate_structures"));
    if (values.contains("view_distance")) config.viewDistance = std::stoi(values.at("view_distance"));
    if (values.contains("vsync")) config.vsync = parseBoolean(values.at("vsync"));
    if (values.contains("target_fps")) config.targetFps = std::stoi(values.at("target_fps"));
    if (values.contains("gui_scale")) config.guiScale = std::stoi(values.at("gui_scale"));
    if (values.contains("chunk_cache_capacity"))
        config.chunkCacheCapacity = std::stoi(values.at("chunk_cache_capacity"));
    if (values.contains("stream_main_thread_budget_ms"))
        config.streamMainThreadBudgetMs = std::stod(values.at("stream_main_thread_budget_ms"));
    if (values.contains("light_main_thread_budget_ms"))
        config.lightMainThreadBudgetMs = std::stod(values.at("light_main_thread_budget_ms"));
    if (values.contains("mesh_main_thread_budget_ms"))
        config.meshMainThreadBudgetMs = std::stod(values.at("mesh_main_thread_budget_ms"));
    if (values.contains("daylight_cycle")) config.daylightCycle = parseBoolean(values.at("daylight_cycle"));
    if (values.contains("day_cycle_seconds")) config.dayCycleSeconds = std::stod(values.at("day_cycle_seconds"));
    if (values.contains("initial_world_time")) config.initialWorldTime = std::stoll(values.at("initial_world_time"));
    if (values.contains("weather_cycle")) config.weatherCycle = parseBoolean(values.at("weather_cycle"));
    if (values.contains("weather_override")) config.weatherOverride = parseWeatherOverride(values.at("weather_override"));
    if (config.viewDistance < 2 || config.viewDistance > 32)
        throw std::runtime_error("view_distance must be between 2 and 32");
    if (config.targetFps < 0 || config.targetFps > 1000)
        throw std::runtime_error("target_fps must be between 0 and 1000");
    if (config.guiScale < 0 || config.guiScale > 3)
        throw std::runtime_error("gui_scale must be 0 (Auto), 1 (Small), 2 (Normal), or 3 (Large)");
    if (config.chunkCacheCapacity < 0 || config.chunkCacheCapacity > 4096)
        throw std::runtime_error("chunk_cache_capacity must be between 0 and 4096");
    if (config.streamMainThreadBudgetMs <= 0.0 || config.streamMainThreadBudgetMs > 16.0)
        throw std::runtime_error("stream_main_thread_budget_ms must be greater than 0 and at most 16");
    if (config.lightMainThreadBudgetMs <= 0.0 || config.lightMainThreadBudgetMs > 16.0)
        throw std::runtime_error("light_main_thread_budget_ms must be greater than 0 and at most 16");
    if (config.meshMainThreadBudgetMs <= 0.0 || config.meshMainThreadBudgetMs > 16.0)
        throw std::runtime_error("mesh_main_thread_budget_ms must be greater than 0 and at most 16");
    if (config.dayCycleSeconds < 10.0 || config.dayCycleSeconds > 86400.0)
        throw std::runtime_error("day_cycle_seconds must be between 10 and 86400");
    config.seed = parseMinecraftSeed(config.seedText);
    return config;
}

std::string_view weatherOverrideName(WeatherOverride value) {
    switch (value) {
        case WeatherOverride::Vanilla: return "vanilla";
        case WeatherOverride::Clear: return "clear";
        case WeatherOverride::Rain: return "rain";
        case WeatherOverride::Thunder: return "thunder";
    }
    return "vanilla";
}

int generatorOptionInt(const std::string& json, std::string_view key, int fallback) {
    const std::string_view value = optionValue(json, key);
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? parsed : fallback;
}

double generatorOptionDouble(const std::string& json, std::string_view key, double fallback) {
    const std::string_view value = optionValue(json, key);
    if (value.empty()) return fallback;
    try {
        std::size_t used = 0;
        const double parsed = std::stod(std::string(value), &used);
        return used == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

bool generatorOptionBool(const std::string& json, std::string_view key, bool fallback) {
    const std::string_view value = optionValue(json, key);
    if (value == "true") return true;
    if (value == "false") return false;
    return fallback;
}

std::string_view worldTypeName(WorldType type) {
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
