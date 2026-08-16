#include "worldgen/BiomeProvider.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "worldgen/FlatGeneratorSettings.hpp"

extern "C" {
#include "blockcraft_bridge.h"
}

namespace {

constexpr BiomeDefinition biome(int id, float base, float variation, float temperature = 0.5F,
                                float rainfall = 0.5F, bool snowy = false,
                                std::uint32_t waterColor = 0xFFFFFFU) {
    return {id, base, variation, temperature, rainfall, snowy, waterColor};
}

const std::array<BiomeDefinition, 256>& biomeTable() {
    static const std::array<BiomeDefinition, 256> table = [] {
        std::array<BiomeDefinition, 256> result{};
        for (int i = 0; i < 256; ++i) result[static_cast<std::size_t>(i)] = biome(i, 0.1F, 0.2F);
        result[0] = biome(0, -1.0F, 0.1F); result[1] = biome(1, 0.125F, 0.05F, 0.8F, 0.4F);
        result[2] = biome(2, 0.125F, 0.05F, 2.0F, 0.0F); result[3] = biome(3, 1.0F, 0.5F, 0.2F, 0.3F);
        result[4] = biome(4, 0.1F, 0.2F, 0.7F, 0.8F); result[5] = biome(5, 0.2F, 0.2F, 0.25F, 0.8F);
        result[6] = biome(6, -0.2F, 0.1F, 0.8F, 0.9F, false, 14745518U); result[7] = biome(7, -0.5F, 0.0F);
        result[10] = biome(10, -1.0F, 0.1F, 0.0F, 0.5F, true); result[11] = biome(11, -0.5F, 0.0F, 0.0F, 0.5F, true);
        result[12] = biome(12, 0.125F, 0.05F, 0.0F, 0.5F, true); result[13] = biome(13, 0.45F, 0.3F, 0.0F, 0.5F, true);
        result[14] = biome(14, 0.2F, 0.3F, 0.9F, 1.0F); result[15] = biome(15, 0.0F, 0.025F, 0.9F, 1.0F);
        result[16] = biome(16, 0.0F, 0.025F, 0.8F, 0.4F); result[17] = biome(17, 0.45F, 0.3F, 2.0F, 0.0F);
        result[18] = biome(18, 0.45F, 0.3F, 0.7F, 0.8F); result[19] = biome(19, 0.45F, 0.3F, 0.25F, 0.8F);
        result[20] = biome(20, 0.8F, 0.3F, 0.2F, 0.3F); result[21] = biome(21, 0.1F, 0.2F, 0.95F, 0.9F);
        result[22] = biome(22, 0.45F, 0.3F, 0.95F, 0.9F); result[23] = biome(23, 0.1F, 0.2F, 0.95F, 0.8F);
        result[24] = biome(24, -1.8F, 0.1F); result[25] = biome(25, 0.1F, 0.8F, 0.2F, 0.3F);
        result[26] = biome(26, 0.0F, 0.025F, 0.05F, 0.3F, true); result[27] = biome(27, 0.1F, 0.2F, 0.6F, 0.6F);
        result[28] = biome(28, 0.45F, 0.3F, 0.6F, 0.6F); result[29] = biome(29, 0.1F, 0.2F, 0.7F, 0.8F);
        result[30] = biome(30, 0.2F, 0.2F, -0.5F, 0.4F, true); result[31] = biome(31, 0.45F, 0.3F, -0.5F, 0.4F, true);
        result[32] = biome(32, 0.2F, 0.2F, 0.3F, 0.8F); result[33] = biome(33, 0.45F, 0.3F, 0.3F, 0.8F);
        result[34] = biome(34, 1.0F, 0.5F, 0.2F, 0.3F); result[35] = biome(35, 0.125F, 0.05F, 1.2F, 0.0F);
        result[36] = biome(36, 1.5F, 0.025F, 1.0F, 0.0F); result[37] = biome(37, 0.1F, 0.2F, 2.0F, 0.0F);
        result[38] = biome(38, 1.5F, 0.025F, 2.0F, 0.0F); result[39] = biome(39, 1.5F, 0.025F, 2.0F, 0.0F);
        result[127] = biome(127, 0.1F, 0.2F);
        result[129] = biome(129, 0.125F, 0.05F, 0.8F, 0.4F); result[130] = biome(130, 0.225F, 0.25F, 2.0F, 0.0F);
        result[131] = biome(131, 1.0F, 0.5F, 0.2F, 0.3F); result[132] = biome(132, 0.1F, 0.4F, 0.7F, 0.8F);
        result[133] = biome(133, 0.3F, 0.4F, 0.25F, 0.8F); result[134] = biome(134, -0.1F, 0.3F, 0.8F, 0.9F, false, 14745518U);
        result[140] = biome(140, 0.425F, 0.45000002F, 0.0F, 0.5F, true);
        result[149] = biome(149, 0.2F, 0.4F, 0.95F, 0.9F); result[151] = biome(151, 0.2F, 0.4F, 0.95F, 0.8F);
        result[155] = biome(155, 0.2F, 0.4F, 0.6F, 0.6F); result[156] = biome(156, 0.55F, 0.5F, 0.6F, 0.6F);
        result[157] = biome(157, 0.2F, 0.4F, 0.7F, 0.8F); result[158] = biome(158, 0.3F, 0.4F, -0.5F, 0.4F, true);
        result[160] = biome(160, 0.2F, 0.2F, 0.25F, 0.8F); result[161] = biome(161, 0.2F, 0.2F, 0.25F, 0.8F);
        result[162] = biome(162, 1.0F, 0.5F, 0.2F, 0.3F); result[163] = biome(163, 0.3625F, 1.225F, 1.1F, 0.0F);
        result[164] = biome(164, 1.05F, 1.2125001F, 1.0F, 0.0F); result[165] = biome(165, 0.1F, 0.2F, 2.0F, 0.0F);
        result[166] = biome(166, 0.45F, 0.3F, 2.0F, 0.0F); result[167] = biome(167, 0.45F, 0.3F, 2.0F, 0.0F);
        return result;
    }();
    return table;
}

} // namespace

struct BiomeProvider::Implementation {
    CbGeneratorHandle generator = nullptr;
};

BiomeProvider::BiomeProvider(const WorldConfig& config)
    : implementation_(std::make_unique<Implementation>()) {
    const int fixedBiome = config.worldType == WorldType::Customized
        ? generatorOptionInt(config.generatorOptions, "fixedBiome", -1)
        : (config.worldType == WorldType::Flat
            ? FlatGeneratorSettings::parse(config.generatorOptions).biome : -1);
    implementation_->generator = cbCreateGenerator(config.seed,
        config.worldType == WorldType::LargeBiomes,
        config.worldType == WorldType::Default11,
        fixedBiome);
    if (implementation_->generator == nullptr) throw std::bad_alloc();
}

BiomeProvider::~BiomeProvider() { cbDestroyGenerator(implementation_->generator); }

std::vector<std::int32_t> BiomeProvider::getBiomes(int x, int z, int width, int height) const {
    std::vector<std::int32_t> output(static_cast<std::size_t>(width * height));
    if (cbGenerateBiomes(implementation_->generator, 1, x, z, width, height, 63, output.data()) != 0)
        throw std::runtime_error("1.12.2 block-scale biome generation failed");
    return output;
}

std::vector<std::int32_t> BiomeProvider::getBiomesForGeneration(int x, int z, int width, int height) const {
    std::vector<std::int32_t> output(static_cast<std::size_t>(width * height));
    if (cbGenerateBiomes(implementation_->generator, 4, x, z, width, height, 0, output.data()) != 0)
        throw std::runtime_error("1.12.2 generation-scale biome generation failed");
    return output;
}

const BiomeDefinition& BiomeProvider::definition(std::int32_t id) {
    if (id < 0 || id > 255) return biomeTable()[1];
    return biomeTable()[static_cast<std::size_t>(id)];
}
