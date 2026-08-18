#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "worldgen/JavaRandom.hpp"
#include "worldgen/WorldConfig.hpp"

class World;

enum class PrecipitationType { None, Rain, Snow };
enum class FogMode { Linear, Exponential };

struct EnvironmentSaveState {
    double totalWorldTime = 0.0;
    double dayTime = 0.0;
    bool raining = false;
    bool thundering = false;
    int rainTime = 0;
    int thunderTime = 0;
    float rainStrength = 0.0F;
    float thunderStrength = 0.0F;
};

struct EnvironmentFrame {
    double worldTime = 0.0;
    float celestialAngle = 0.0F;
    int moonPhase = 0;
    float rainStrength = 0.0F;
    float thunderStrength = 0.0F;
    float starBrightness = 0.0F;
    float sunBrightness = 1.0F;
    float skyLightSubtracted = 0.0F;
    float lightningFlash = 0.0F;
    glm::vec3 skyColor{0.72F, 0.86F, 1.0F};
    glm::vec3 fogColor{0.72F, 0.86F, 1.0F};
    glm::vec3 cloudColor{1.0F};
    FogMode fogMode = FogMode::Linear;
    float fogStart = 96.0F;
    float fogEnd = 128.0F;
    float fogDensity = 0.0F;
    bool underwater = false;
    bool inLava = false;
    bool lightningVisible = false;
    glm::vec3 lightningPosition{0.0F};
    std::int64_t lightningSeed = 0;
    std::uint64_t rendererTicks = 0;
    float partialTick = 0.0F;
};

class Environment {
public:
    explicit Environment(const WorldConfig& config);

    void tick(const World& world);
    [[nodiscard]] EnvironmentFrame sample(const World& world, const glm::vec3& cameraPosition,
                                          const glm::vec3& lookDirection, float partialTick) const;
    [[nodiscard]] PrecipitationType precipitationAt(const World& world, int x, int y, int z) const;
    // Vanilla keeps total world age (Time) separate from the daylight clock
    // (DayTime). Sleeping and /time affect DayTime; the total age keeps moving.
    [[nodiscard]] double totalWorldTime() const { return totalWorldTime_; }
    [[nodiscard]] double dayTime() const { return dayTime_; }
    [[nodiscard]] double worldTime() const { return dayTime_; } // compatibility alias
    void setDayTime(double value) { dayTime_ = value; }
    void setWorldTime(double value) { setDayTime(value); } // compatibility alias
    [[nodiscard]] EnvironmentSaveState saveState() const {
        return {totalWorldTime_, dayTime_, raining_, thundering_, rainTime_, thunderTime_, rainStrength_, thunderStrength_};
    }
    void restoreSaveState(const EnvironmentSaveState& state) {
        totalWorldTime_ = state.totalWorldTime;
        dayTime_ = state.dayTime;
        raining_ = state.raining;
        thundering_ = state.thundering;

        // WorldInfo may legitimately contain zero weather timers (notably a
        // newly-created world). Vanilla treats zero as "choose the next
        // duration for the current state", not "toggle weather immediately".
        // The Stage 8 implementation restored zero directly and its pre-
        // decrement update path could start a new world in rain/thunder on the
        // first tick.
        rainTime_ = state.rainTime > 0 ? state.rainTime :
            (raining_ ? random_.nextInt(12000) + 12000
                      : random_.nextInt(168000) + 12000);
        thunderTime_ = state.thunderTime > 0 ? state.thunderTime :
            (thundering_ ? random_.nextInt(12000) + 3600
                         : random_.nextInt(168000) + 12000);

        previousRainStrength_ = rainStrength_ = state.rainStrength;
        previousThunderStrength_ = thunderStrength_ = state.thunderStrength;
    }

    [[nodiscard]] static float celestialAngle(double worldTime);
    [[nodiscard]] static float starBrightness(float angle);

private:
    void updateWeather();
    void updateLightning(const World& world);
    [[nodiscard]] float temperatureAt(const World& world, int x, int y, int z) const;

    const WorldConfig& config_;
    JavaRandom random_;
    double totalWorldTime_ = 0.0;
    double dayTime_ = 0.0;
    double ticksPerGameTick_ = 1.0;
    std::uint64_t rendererTicks_ = 0;
    bool raining_ = false;
    bool thundering_ = false;
    int rainTime_ = 0;
    int thunderTime_ = 0;
    float previousRainStrength_ = 0.0F;
    float rainStrength_ = 0.0F;
    float previousThunderStrength_ = 0.0F;
    float thunderStrength_ = 0.0F;
    int lightningFlashTicks_ = 0;
    int lightningVisibleTicks_ = 0;
    glm::vec3 lightningPosition_{0.0F};
    std::int64_t lightningSeed_ = 0;
};
