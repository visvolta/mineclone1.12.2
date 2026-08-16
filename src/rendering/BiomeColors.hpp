#pragma once

#include <cstdint>
#include <filesystem>

// CPU copy of Minecraft 1.12.2's two 256x256 biome colormaps. The renderer
// samples these before mesh jobs are queued, so worker threads never touch an
// image decoder or mutable global state.
class BiomeColors {
public:
    static void load(const std::filesystem::path& assetRoot);
    [[nodiscard]] static std::uint32_t grass(int biomeId, int x, int y, int z);
    [[nodiscard]] static std::uint32_t foliage(int biomeId, int x, int y, int z);
    [[nodiscard]] static double decorationNoise(double x, double z);
};
