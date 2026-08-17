#include "environment/Environment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/BiomeProvider.hpp"

namespace {

constexpr float pi = 3.14159265358979323846F;

glm::vec3 hsvToRgb(float hue, float saturation, float value) {
    const float scaled = hue * 6.0F;
    const int sector = static_cast<int>(std::floor(scaled)) % 6;
    const float fraction = scaled - std::floor(scaled);
    const float p = value * (1.0F - saturation);
    const float q = value * (1.0F - fraction * saturation);
    const float t = value * (1.0F - (1.0F - fraction) * saturation);
    switch (sector) {
        case 0: return {value, t, p};
        case 1: return {q, value, p};
        case 2: return {p, value, t};
        case 3: return {p, q, value};
        case 4: return {t, p, value};
        default: return {value, p, q};
    }
}

int highestPrecipitationY(const World& world, int x, int z) {
    for (int y = chunkHeight - 1; y >= 0; --y) {
        const BlockDefinition& block = BlockRegistry::get(world.getBlock(x, y, z));
        if (block.opaque || block.fullCube) return y + 1;
    }
    return 0;
}

// Biome.TEMPERATURE_NOISE uses one NoiseGeneratorSimplex seeded with 1234.
class TemperatureNoise {
public:
    TemperatureNoise() {
        JavaRandom random(1234);
        static_cast<void>(random.nextDouble());
        static_cast<void>(random.nextDouble());
        static_cast<void>(random.nextDouble());
        for (int i = 0; i < 256; ++i) permutation_[static_cast<std::size_t>(i)] = i;
        for (int i = 0; i < 256; ++i) {
            const int selected = random.nextInt(256 - i) + i;
            std::swap(permutation_[static_cast<std::size_t>(i)], permutation_[static_cast<std::size_t>(selected)]);
            permutation_[static_cast<std::size_t>(i + 256)] = permutation_[static_cast<std::size_t>(i)];
        }
    }

    [[nodiscard]] double value(double x, double z) const {
        constexpr double root3 = 1.7320508075688772935;
        constexpr double f2 = 0.5 * (root3 - 1.0);
        constexpr double g2 = (3.0 - root3) / 6.0;
        const double skew = (x + z) * f2;
        const int i = fastFloor(x + skew);
        const int j = fastFloor(z + skew);
        const double unskew = static_cast<double>(i + j) * g2;
        const double x0 = x - (static_cast<double>(i) - unskew);
        const double z0 = z - (static_cast<double>(j) - unskew);
        const int i1 = x0 > z0 ? 1 : 0;
        const int j1 = x0 > z0 ? 0 : 1;
        const double x1 = x0 - i1 + g2;
        const double z1 = z0 - j1 + g2;
        const double x2 = x0 - 1.0 + 2.0 * g2;
        const double z2 = z0 - 1.0 + 2.0 * g2;
        const int ii = i & 255;
        const int jj = j & 255;
        const int g0 = permutation_[static_cast<std::size_t>(ii + permutation_[static_cast<std::size_t>(jj)])] % 12;
        const int g1 = permutation_[static_cast<std::size_t>(ii + i1 + permutation_[static_cast<std::size_t>(jj + j1)])] % 12;
        const int g2i = permutation_[static_cast<std::size_t>(ii + 1 + permutation_[static_cast<std::size_t>(jj + 1)])] % 12;
        return 70.0 * (corner(g0, x0, z0) + corner(g1, x1, z1) + corner(g2i, x2, z2));
    }

private:
    static int fastFloor(double value) { return value > 0.0 ? static_cast<int>(value) : static_cast<int>(value) - 1; }
    static double corner(int gradient, double x, double z) {
        static constexpr std::array<std::array<int, 2>, 12> gradients{{
            {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}}, {{1, 0}}, {{-1, 0}},
            {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}, {{0, 1}}, {{0, -1}}
        }};
        double attenuation = 0.5 - x * x - z * z;
        if (attenuation < 0.0) return 0.0;
        attenuation *= attenuation;
        return attenuation * attenuation *
            (gradients[static_cast<std::size_t>(gradient)][0] * x +
             gradients[static_cast<std::size_t>(gradient)][1] * z);
    }
    std::array<int, 512> permutation_{};
};

const TemperatureNoise& temperatureNoise() {
    static const TemperatureNoise value;
    return value;
}

} // namespace

Environment::Environment(const WorldConfig& config)
    : config_(config), random_(config.seed ^ 0x5DEECE66DLL),
      worldTime_(static_cast<double>(config.initialWorldTime)),
      ticksPerGameTick_(1200.0 / config.dayCycleSeconds),
      rainTime_(random_.nextInt(168000) + 12000),
      thunderTime_(random_.nextInt(168000) + 12000) {}

float Environment::celestialAngle(double worldTime) {
    double dayTick = std::fmod(worldTime, 24000.0);
    if (dayTick < 0.0) dayTick += 24000.0;
    float value = static_cast<float>(dayTick / 24000.0 - 0.25);
    if (value < 0.0F) value += 1.0F;
    if (value > 1.0F) value -= 1.0F;
    const float eased = 1.0F - static_cast<float>((std::cos(value * pi) + 1.0) / 2.0);
    return value + (eased - value) / 3.0F;
}

float Environment::starBrightness(float angle) {
    float value = 1.0F - (std::cos(angle * pi * 2.0F) * 2.0F + 0.25F);
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * 0.5F;
}

void Environment::updateWeather() {
    if (config_.weatherOverride == WeatherOverride::Vanilla) {
        if (config_.weatherCycle) {
            if (--thunderTime_ <= 0) {
                thundering_ = !thundering_;
                thunderTime_ = thundering_ ? random_.nextInt(12000) + 3600
                                            : random_.nextInt(168000) + 12000;
            }
            if (--rainTime_ <= 0) {
                raining_ = !raining_;
                rainTime_ = raining_ ? random_.nextInt(12000) + 12000
                                      : random_.nextInt(168000) + 12000;
            }
        }
    } else {
        raining_ = config_.weatherOverride == WeatherOverride::Rain ||
                   config_.weatherOverride == WeatherOverride::Thunder;
        thundering_ = config_.weatherOverride == WeatherOverride::Thunder;
    }

    previousRainStrength_ = rainStrength_;
    previousThunderStrength_ = thunderStrength_;
    rainStrength_ = std::clamp(static_cast<float>(static_cast<double>(rainStrength_) +
        (raining_ ? 0.01 : -0.01)), 0.0F, 1.0F);
    thunderStrength_ = std::clamp(static_cast<float>(static_cast<double>(thunderStrength_) +
        (thundering_ ? 0.01 : -0.01)), 0.0F, 1.0F);
}

void Environment::updateLightning(const World& world) {
    if (lightningFlashTicks_ > 0) --lightningFlashTicks_;
    if (lightningVisibleTicks_ > 0) --lightningVisibleTicks_;
    if (!(raining_ && thundering_)) return;

    // WorldServer checks once per active chunk with probability 1/100000.
    for (const auto& [key, chunk] : world.chunks()) {
        static_cast<void>(key);
        if (random_.nextInt(100000) != 0) continue;
        const int x = chunk->x() * chunkSize + random_.nextInt(chunkSize);
        const int z = chunk->z() * chunkSize + random_.nextInt(chunkSize);
        const int y = highestPrecipitationY(world, x, z);
        if (precipitationAt(world, x, y, z) != PrecipitationType::Rain) continue;
        lightningPosition_ = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        lightningSeed_ = random_.nextLong();
        lightningFlashTicks_ = 2;
        lightningVisibleTicks_ = 8;
        break;
    }
}

void Environment::tick(const World& world) {
    ++rendererTicks_;
    if (config_.daylightCycle) worldTime_ += ticksPerGameTick_;
    updateWeather();
    updateLightning(world);
}

float Environment::temperatureAt(const World& world, int x, int y, int z) const {
    const Chunk* chunk = world.findChunk(World::floorDiv16(x), World::floorDiv16(z));
    const int biomeId = chunk ? chunk->biome(World::floorMod16(x), World::floorMod16(z)) : 1;
    float temperature = BiomeProvider::definition(biomeId).temperature;
    if (y > 64) {
        const float noise = static_cast<float>(temperatureNoise().value(x / 8.0, z / 8.0) * 4.0);
        temperature -= (noise + static_cast<float>(y) - 64.0F) * 0.05F / 30.0F;
    }
    return temperature;
}

PrecipitationType Environment::precipitationAt(const World& world, int x, int y, int z) const {
    const Chunk* chunk = world.findChunk(World::floorDiv16(x), World::floorDiv16(z));
    if (chunk == nullptr) return PrecipitationType::None;
    const BiomeDefinition& biome = BiomeProvider::definition(
        chunk->biome(World::floorMod16(x), World::floorMod16(z)));
    if (biome.rainfall <= 0.0F) return PrecipitationType::None;
    return temperatureAt(world, x, y, z) < 0.15F ? PrecipitationType::Snow : PrecipitationType::Rain;
}

EnvironmentFrame Environment::sample(const World& world, const glm::vec3& cameraPosition,
                                     const glm::vec3& lookDirection, float partialTick) const {
    EnvironmentFrame result;
    result.worldTime = worldTime_ + (config_.daylightCycle ? ticksPerGameTick_ * partialTick : 0.0);
    result.celestialAngle = celestialAngle(result.worldTime);
    const std::int64_t day = static_cast<std::int64_t>(std::floor(result.worldTime / 24000.0));
    result.moonPhase = static_cast<int>((day % 8 + 8) % 8);
    result.rainStrength = previousRainStrength_ + (rainStrength_ - previousRainStrength_) * partialTick;
    const float rawThunder = previousThunderStrength_ +
        (thunderStrength_ - previousThunderStrength_) * partialTick;
    result.thunderStrength = rawThunder * result.rainStrength;
    result.starBrightness = starBrightness(result.celestialAngle) * (1.0F - result.rainStrength);
    result.rendererTicks = rendererTicks_;
    result.partialTick = partialTick;

    const float daylight = std::clamp(std::cos(result.celestialAngle * pi * 2.0F) * 2.0F + 0.5F, 0.0F, 1.0F);
    const int cameraX = static_cast<int>(std::floor(cameraPosition.x));
    const int cameraY = static_cast<int>(std::floor(cameraPosition.y));
    const int cameraZ = static_cast<int>(std::floor(cameraPosition.z));
    const float temperature = temperatureAt(world, cameraX, cameraY, cameraZ);
    result.skyColor = hsvToRgb(0.62222224F - std::clamp(temperature / 3.0F, -1.0F, 1.0F) * 0.05F,
                               0.5F + std::clamp(temperature / 3.0F, -1.0F, 1.0F) * 0.1F, 1.0F) * daylight;
    if (result.rainStrength > 0.0F) {
        const float gray = glm::dot(result.skyColor, glm::vec3(0.3F, 0.59F, 0.11F)) * 0.6F;
        result.skyColor = glm::mix(glm::vec3(gray), result.skyColor, 1.0F - result.rainStrength * 0.75F);
    }
    if (result.thunderStrength > 0.0F) {
        const float gray = glm::dot(result.skyColor, glm::vec3(0.3F, 0.59F, 0.11F)) * 0.2F;
        result.skyColor = glm::mix(glm::vec3(gray), result.skyColor, 1.0F - result.thunderStrength * 0.75F);
    }
    result.lightningFlash = lightningFlashTicks_ > 0 ?
        std::min(1.0F, static_cast<float>(lightningFlashTicks_) - partialTick) * 0.45F : 0.0F;
    result.skyColor = glm::mix(result.skyColor, glm::vec3(0.8F, 0.8F, 1.0F), result.lightningFlash);

    result.cloudColor = glm::vec3(daylight * 0.9F + 0.1F, daylight * 0.9F + 0.1F,
                                  daylight * 0.85F + 0.15F);
    if (result.rainStrength > 0.0F) {
        const float gray = glm::dot(result.cloudColor, glm::vec3(0.3F, 0.59F, 0.11F)) * 0.6F;
        result.cloudColor = glm::mix(glm::vec3(gray), result.cloudColor, 1.0F - result.rainStrength * 0.95F);
    }
    if (result.thunderStrength > 0.0F) {
        const float gray = glm::dot(result.cloudColor, glm::vec3(0.3F, 0.59F, 0.11F)) * 0.2F;
        result.cloudColor = glm::mix(glm::vec3(gray), result.cloudColor, 1.0F - result.thunderStrength * 0.95F);
    }

    result.fogColor = glm::vec3(0.7529412F * (daylight * 0.94F + 0.06F),
                                0.84705883F * (daylight * 0.94F + 0.06F),
                                daylight * 0.91F + 0.09F);
    const float horizonCosine = std::cos(result.celestialAngle * pi * 2.0F);
    if (horizonCosine >= -0.4F && horizonCosine <= 0.4F && config_.viewDistance >= 4) {
        const float phase = horizonCosine / 0.4F * 0.5F + 0.5F;
        float alpha = 1.0F - (1.0F - std::sin(phase * pi)) * 0.99F;
        alpha *= alpha;
        const glm::vec3 sunrise{phase * 0.3F + 0.7F, phase * phase * 0.7F + 0.2F, 0.2F};
        const glm::vec3 horizonDirection{
            std::sin(result.celestialAngle * pi * 2.0F) > 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F};
        const float facing = std::max(0.0F, glm::dot(lookDirection, horizonDirection)) * alpha;
        result.fogColor = glm::mix(result.fogColor, sunrise, facing);
    }
    const float distanceBlend = 1.0F - std::pow(0.25F + 0.75F * config_.viewDistance / 32.0F, 0.25F);
    result.fogColor = glm::mix(result.fogColor, result.skyColor, distanceBlend);
    result.fogColor *= glm::vec3(1.0F - result.rainStrength * 0.5F,
                                 1.0F - result.rainStrength * 0.5F,
                                 1.0F - result.rainStrength * 0.4F);
    result.fogColor *= 1.0F - result.thunderStrength * 0.5F;

    const BlockId cameraBlock = static_cast<BlockId>(blockId(world.getBlock(cameraX, cameraY, cameraZ)));
    result.underwater = cameraBlock == BlockId::Water;
    result.inLava = cameraBlock == BlockId::Lava;
    const float farPlane = static_cast<float>(config_.viewDistance * chunkSize);
    if (result.underwater) {
        result.fogColor = glm::vec3(0.02F, 0.02F, 0.2F);
        result.fogMode = FogMode::Exponential;
        result.fogDensity = 0.1F;
    } else if (result.inLava) {
        result.fogColor = glm::vec3(0.6F, 0.1F, 0.0F);
        result.fogMode = FogMode::Exponential;
        result.fogDensity = 2.0F;
    } else {
        result.fogStart = farPlane * 0.75F;
        result.fogEnd = farPlane;
    }
    // EntityRenderer#updateFogColor applies the void multiplier after choosing
    // the air, water, or lava color. Keeping it outside the material branch is
    // observable when the camera is submerged near y=0.
    const double voidFactor = config_.worldType == WorldType::Flat ? 1.0 : 0.03125;
    const float voidBrightness = static_cast<float>(std::clamp(cameraPosition.y * voidFactor, 0.0, 1.0));
    result.fogColor *= voidBrightness * voidBrightness;

    float skyFactor = daylight * (1.0F - result.rainStrength * 5.0F / 16.0F) *
                      (1.0F - result.thunderStrength * 5.0F / 16.0F);
    result.sunBrightness = daylight * (1.0F - result.rainStrength * 5.0F / 16.0F) *
                           (1.0F - result.thunderStrength * 5.0F / 16.0F) * 0.8F + 0.2F;
    result.skyLightSubtracted = (1.0F - std::clamp(skyFactor, 0.0F, 1.0F)) * 11.0F;
    result.lightningVisible = lightningVisibleTicks_ > 0;
    result.lightningPosition = lightningPosition_;
    result.lightningSeed = lightningSeed_;
    return result;
}
