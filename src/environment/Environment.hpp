#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include "worldgen/JavaRandom.hpp"
#include "worldgen/WorldConfig.hpp"

class World;

enum class PrecipitationType { None, Rain, Snow };
enum class FogMode { Linear, Exponential };

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
    [[nodiscard]] double worldTime() const { return worldTime_; }

    [[nodiscard]] static float celestialAngle(double worldTime);
    [[nodiscard]] static float starBrightness(float angle);

private:
    void updateWeather();
    void updateLightning(const World& world);
    [[nodiscard]] float temperatureAt(const World& world, int x, int y, int z) const;

    const WorldConfig& config_;
    JavaRandom random_;
    double worldTime_ = 0.0;
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
