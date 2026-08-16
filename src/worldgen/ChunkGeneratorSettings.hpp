#pragma once

#include "worldgen/WorldConfig.hpp"

// Native representation of ChunkGeneratorSettings.Factory from Java Edition
// 1.12.2. Keeping every field in one immutable value is important: vanilla
// consumes the same settings object in terrain, map features and decoration.
struct ChunkGeneratorSettings {
    float coordinateScale = 684.412F;
    float heightScale = 684.412F;
    float upperLimitScale = 512.0F;
    float lowerLimitScale = 512.0F;
    float depthNoiseScaleX = 200.0F;
    float depthNoiseScaleZ = 200.0F;
    float depthNoiseScaleExponent = 0.5F;
    float mainNoiseScaleX = 80.0F;
    float mainNoiseScaleY = 160.0F;
    float mainNoiseScaleZ = 80.0F;
    float baseSize = 8.5F;
    float stretchY = 12.0F;
    float biomeDepthWeight = 1.0F;
    float biomeDepthOffset = 0.0F;
    float biomeScaleWeight = 1.0F;
    float biomeScaleOffset = 0.0F;
    int seaLevel = 63;

    bool useCaves = true;
    bool useDungeons = true;
    int dungeonChance = 8;
    bool useStrongholds = true;
    bool useVillages = true;
    bool useMineShafts = true;
    bool useTemples = true;
    bool useMonuments = true;
    bool useMansions = true;
    bool useRavines = true;
    bool useWaterLakes = true;
    int waterLakeChance = 4;
    bool useLavaLakes = true;
    int lavaLakeChance = 80;
    bool useLavaOceans = false;
    int fixedBiome = -1;
    int biomeSize = 4;
    int riverSize = 4;

    int dirtSize = 33;
    int dirtCount = 10;
    int dirtMinHeight = 0;
    int dirtMaxHeight = 256;
    int gravelSize = 33;
    int gravelCount = 8;
    int gravelMinHeight = 0;
    int gravelMaxHeight = 256;
    int graniteSize = 33;
    int graniteCount = 10;
    int graniteMinHeight = 0;
    int graniteMaxHeight = 80;
    int dioriteSize = 33;
    int dioriteCount = 10;
    int dioriteMinHeight = 0;
    int dioriteMaxHeight = 80;
    int andesiteSize = 33;
    int andesiteCount = 10;
    int andesiteMinHeight = 0;
    int andesiteMaxHeight = 80;
    int coalSize = 17;
    int coalCount = 20;
    int coalMinHeight = 0;
    int coalMaxHeight = 128;
    int ironSize = 9;
    int ironCount = 20;
    int ironMinHeight = 0;
    int ironMaxHeight = 64;
    int goldSize = 9;
    int goldCount = 2;
    int goldMinHeight = 0;
    int goldMaxHeight = 32;
    int redstoneSize = 8;
    int redstoneCount = 8;
    int redstoneMinHeight = 0;
    int redstoneMaxHeight = 16;
    int diamondSize = 8;
    int diamondCount = 1;
    int diamondMinHeight = 0;
    int diamondMaxHeight = 16;
    int lapisSize = 7;
    int lapisCount = 1;
    int lapisCenterHeight = 16;
    int lapisSpread = 16;

    [[nodiscard]] static ChunkGeneratorSettings fromConfig(const WorldConfig& config);
};
