#include "rendering/BiomeColors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

#include <stb_image.h>

#include "worldgen/BiomeProvider.hpp"

extern "C" {
#include "blockcraft_bridge.h"
}

namespace {

std::array<std::uint32_t, 65536> grassMap{};
std::array<std::uint32_t, 65536> foliageMap{};
bool mapsLoaded = false;

struct VanillaColorNoise {
    CbColorNoiseHandle handle = cbCreateColorNoise();
    ~VanillaColorNoise() { cbDestroyColorNoise(handle); }
};

const VanillaColorNoise& colorNoise() {
    static const VanillaColorNoise value;
    return value;
}

void readMap(const std::filesystem::path& path,
             std::array<std::uint32_t, 65536>& destination) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels,
                                      STBI_rgb_alpha);
    if (pixels == nullptr || width != 256 || height != 256) {
        if (pixels != nullptr) stbi_image_free(pixels);
        throw std::runtime_error("Could not load Minecraft 1.12.2 biome colormap: " +
                                 path.string());
    }
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const std::size_t source = index * 4;
        destination[index] = (static_cast<std::uint32_t>(pixels[source]) << 16U) |
            (static_cast<std::uint32_t>(pixels[source + 1]) << 8U) |
            static_cast<std::uint32_t>(pixels[source + 2]);
    }
    stbi_image_free(pixels);
}

float temperatureAt(int biomeId, int x, int y, int z) {
    float temperature = BiomeProvider::definition(biomeId).temperature;
    if (y > 64) {
        const double noise = cbSampleTemperatureNoise(colorNoise().handle,
            static_cast<double>(x) / 8.0, static_cast<double>(z) / 8.0) * 4.0;
        temperature -= static_cast<float>((noise + y - 64.0) * 0.05 / 30.0);
    }
    return std::clamp(temperature, 0.0F, 1.0F);
}

std::uint32_t mapColor(const std::array<std::uint32_t, 65536>& map, int biomeId,
                       int x, int y, int z) {
    const float temperature = temperatureAt(biomeId, x, y, z);
    const float rainfall = std::clamp(BiomeProvider::definition(biomeId).rainfall,
                                      0.0F, 1.0F) * temperature;
    const int mapX = static_cast<int>((1.0F - temperature) * 255.0F);
    const int mapY = static_cast<int>((1.0F - rainfall) * 255.0F);
    return map[static_cast<std::size_t>((mapY << 8) | mapX)];
}

bool isSwamp(int biomeId) { return biomeId == 6 || biomeId == 134; }
bool isMesa(int biomeId) {
    return biomeId == 37 || biomeId == 38 || biomeId == 39 ||
        biomeId == 165 || biomeId == 166 || biomeId == 167;
}
bool isRoofedForest(int biomeId) { return biomeId == 29 || biomeId == 157; }

} // namespace

void BiomeColors::load(const std::filesystem::path& assetRoot) {
    const std::filesystem::path root = assetRoot / "assets/minecraft/textures/colormap";
    readMap(root / "grass.png", grassMap);
    readMap(root / "foliage.png", foliageMap);
    mapsLoaded = true;
}

std::uint32_t BiomeColors::grass(int biomeId, int x, int y, int z) {
    if (isSwamp(biomeId)) {
        const double noise = cbSampleGrassColorNoise(colorNoise().handle,
                                                     x * 0.0225, z * 0.0225);
        return noise < -0.1 ? 5011004U : 6975545U;
    }
    if (isMesa(biomeId)) return 9470285U;
    std::uint32_t color = mapsLoaded ? mapColor(grassMap, biomeId, x, y, z) : 0x7FB238U;
    if (isRoofedForest(biomeId)) color = ((color & 0xFEFEFEU) + 0x28340AU) >> 1U;
    return color;
}

std::uint32_t BiomeColors::foliage(int biomeId, int x, int y, int z) {
    if (isSwamp(biomeId)) return 6975545U;
    if (isMesa(biomeId)) return 10387789U;
    return mapsLoaded ? mapColor(foliageMap, biomeId, x, y, z) : 0x48B518U;
}

double BiomeColors::decorationNoise(double x, double z) {
    // Biome::GRASS_COLOR_NOISE is a Perlin generator initialized with Java's
    // Random(2345). The same field drives swamp colors, flower forests, and
    // the tulip patches in plains.
    return cbSampleGrassColorNoise(colorNoise().handle, x, z);
}
