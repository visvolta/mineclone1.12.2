#include "worldgen/ChunkGeneratorSettings.hpp"

namespace {

float optionFloat(const std::string& json, std::string_view key, float fallback) {
    return static_cast<float>(generatorOptionDouble(json, key, fallback));
}

} // namespace

ChunkGeneratorSettings ChunkGeneratorSettings::fromConfig(const WorldConfig& config) {
    ChunkGeneratorSettings value;
    const std::string& json = config.generatorOptions;

    value.coordinateScale = optionFloat(json, "coordinateScale", value.coordinateScale);
    value.heightScale = optionFloat(json, "heightScale", value.heightScale);
    value.upperLimitScale = optionFloat(json, "upperLimitScale", value.upperLimitScale);
    value.lowerLimitScale = optionFloat(json, "lowerLimitScale", value.lowerLimitScale);
    value.depthNoiseScaleX = optionFloat(json, "depthNoiseScaleX", value.depthNoiseScaleX);
    value.depthNoiseScaleZ = optionFloat(json, "depthNoiseScaleZ", value.depthNoiseScaleZ);
    value.depthNoiseScaleExponent = optionFloat(json, "depthNoiseScaleExponent", value.depthNoiseScaleExponent);
    value.mainNoiseScaleX = optionFloat(json, "mainNoiseScaleX", value.mainNoiseScaleX);
    value.mainNoiseScaleY = optionFloat(json, "mainNoiseScaleY", value.mainNoiseScaleY);
    value.mainNoiseScaleZ = optionFloat(json, "mainNoiseScaleZ", value.mainNoiseScaleZ);
    value.baseSize = optionFloat(json, "baseSize", value.baseSize);
    value.stretchY = optionFloat(json, "stretchY", value.stretchY);
    value.biomeDepthWeight = optionFloat(json, "biomeDepthWeight", value.biomeDepthWeight);
    value.biomeDepthOffset = optionFloat(json, "biomeDepthOffset", value.biomeDepthOffset);
    value.biomeScaleWeight = optionFloat(json, "biomeScaleWeight", value.biomeScaleWeight);
    value.biomeScaleOffset = optionFloat(json, "biomeScaleOffset", value.biomeScaleOffset);
    value.seaLevel = generatorOptionInt(json, "seaLevel", value.seaLevel);

    value.useCaves = generatorOptionBool(json, "useCaves", value.useCaves);
    value.useDungeons = generatorOptionBool(json, "useDungeons", value.useDungeons);
    value.dungeonChance = generatorOptionInt(json, "dungeonChance", value.dungeonChance);
    value.useStrongholds = generatorOptionBool(json, "useStrongholds", value.useStrongholds);
    value.useVillages = generatorOptionBool(json, "useVillages", value.useVillages);
    value.useMineShafts = generatorOptionBool(json, "useMineShafts", value.useMineShafts);
    value.useTemples = generatorOptionBool(json, "useTemples", value.useTemples);
    value.useMonuments = generatorOptionBool(json, "useMonuments", value.useMonuments);
    value.useMansions = generatorOptionBool(json, "useMansions", value.useMansions);
    value.useRavines = generatorOptionBool(json, "useRavines", value.useRavines);
    value.useWaterLakes = generatorOptionBool(json, "useWaterLakes", value.useWaterLakes);
    value.waterLakeChance = generatorOptionInt(json, "waterLakeChance", value.waterLakeChance);
    value.useLavaLakes = generatorOptionBool(json, "useLavaLakes", value.useLavaLakes);
    value.lavaLakeChance = generatorOptionInt(json, "lavaLakeChance", value.lavaLakeChance);
    value.useLavaOceans = generatorOptionBool(json, "useLavaOceans", value.useLavaOceans);
    value.fixedBiome = generatorOptionInt(json, "fixedBiome", value.fixedBiome);
    value.biomeSize = generatorOptionInt(json, "biomeSize", value.biomeSize);
    value.riverSize = generatorOptionInt(json, "riverSize", value.riverSize);

#define READ_ORE(name) \
    value.name##Size = generatorOptionInt(json, #name "Size", value.name##Size); \
    value.name##Count = generatorOptionInt(json, #name "Count", value.name##Count); \
    value.name##MinHeight = generatorOptionInt(json, #name "MinHeight", value.name##MinHeight); \
    value.name##MaxHeight = generatorOptionInt(json, #name "MaxHeight", value.name##MaxHeight)
    READ_ORE(dirt);
    READ_ORE(gravel);
    READ_ORE(granite);
    READ_ORE(diorite);
    READ_ORE(andesite);
    READ_ORE(coal);
    READ_ORE(iron);
    READ_ORE(gold);
    READ_ORE(redstone);
    READ_ORE(diamond);
#undef READ_ORE
    value.lapisSize = generatorOptionInt(json, "lapisSize", value.lapisSize);
    value.lapisCount = generatorOptionInt(json, "lapisCount", value.lapisCount);
    value.lapisCenterHeight = generatorOptionInt(json, "lapisCenterHeight", value.lapisCenterHeight);
    value.lapisSpread = generatorOptionInt(json, "lapisSpread", value.lapisSpread);
    return value;
}
