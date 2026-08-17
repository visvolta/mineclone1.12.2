#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "blocks/BlockState.hpp"

inline constexpr int chunkSize = 16;
inline constexpr int sectionSize = 16;
inline constexpr int chunkHeight = 256;
inline constexpr int sectionCount = chunkHeight / sectionSize;

enum LightBorder : std::uint8_t {
    LightBorderWest = 1U << 0U,
    LightBorderEast = 1U << 1U,
    LightBorderNorth = 1U << 2U,
    LightBorderSouth = 1U << 3U
};

class ChunkSection {
public:
    [[nodiscard]] BlockState get(int x, int y, int z) const;
    [[nodiscard]] bool set(int x, int y, int z, BlockState state);
    [[nodiscard]] bool empty() const { return nonAirCount_ == 0; }
    [[nodiscard]] std::uint16_t nonAirCount() const { return nonAirCount_; }
    [[nodiscard]] const BlockState* data() const { return blocks_.data(); }

private:
    [[nodiscard]] static constexpr std::size_t index(int x, int y, int z) {
        return static_cast<std::size_t>((y << 8) | (z << 4) | x);
    }
    std::array<BlockState, 4096> blocks_{};
    std::uint16_t nonAirCount_ = 0;
};

class NibbleArray {
public:
    [[nodiscard]] std::uint8_t get(int x, int y, int z) const;
    void set(int x, int y, int z, std::uint8_t value);

private:
    [[nodiscard]] static constexpr std::size_t index(int x, int y, int z) {
        return static_cast<std::size_t>((y << 8) | (z << 4) | x);
    }
    std::array<std::uint8_t, 2048> bytes_{};
};

struct ChunkLightSection {
    NibbleArray sky;
    NibbleArray block;
};

struct GeneratedBlockEntity {
    int x = 0;
    int y = 0;
    int z = 0;
    std::string id;
    std::vector<std::uint8_t> nbt;
};

class Chunk {
public:
    Chunk(int chunkX, int chunkZ) : x_(chunkX), z_(chunkZ) {}
    [[nodiscard]] int x() const { return x_; }
    [[nodiscard]] int z() const { return z_; }
    [[nodiscard]] BlockState get(int localX, int y, int localZ) const;
    [[nodiscard]] bool set(int localX, int y, int localZ, BlockState state);
    [[nodiscard]] const ChunkSection* section(int index) const;
    [[nodiscard]] std::uint8_t skyLight(int localX, int y, int localZ) const;
    [[nodiscard]] std::uint8_t blockLight(int localX, int y, int localZ) const;
    std::uint16_t applyLighting(const std::vector<std::uint8_t>& sky,
                                const std::vector<std::uint8_t>& block,
                                std::uint8_t* dirtyBorders = nullptr);
    void invalidateLighting() { lightingReady_ = false; }
    [[nodiscard]] bool lightingReady() const { return lightingReady_; }
    [[nodiscard]] std::uint64_t lightingRevision() const { return lightingRevision_; }
    void setBiome(int localX, int localZ, std::uint8_t biome) {
        biomes_[static_cast<std::size_t>((localZ << 4) | localX)] = biome;
    }
    [[nodiscard]] std::uint8_t biome(int localX, int localZ) const {
        return biomes_[static_cast<std::size_t>((localZ << 4) | localX)];
    }
    void addBlockEntity(GeneratedBlockEntity entity) { blockEntities_.push_back(std::move(entity)); }
    [[nodiscard]] const std::vector<GeneratedBlockEntity>& blockEntities() const { return blockEntities_; }

    // Stage 8 preserves vanilla TileTicks even before the runtime scheduled-tick
    // engine is implemented. Each entry is a complete uncompressed compound NBT
    // document, so loading/saving a 1.12.2 world is lossless for this field.
    void addScheduledTick(std::vector<std::uint8_t> tickNbt) { scheduledTicks_.push_back(std::move(tickNbt)); }
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& scheduledTicks() const { return scheduledTicks_; }

    void setSaveMetadata(std::int64_t lastUpdate, std::int64_t inhabitedTime,
                         bool terrainPopulated, bool lightPopulated) {
        lastUpdate_ = lastUpdate;
        inhabitedTime_ = inhabitedTime;
        terrainPopulated_ = terrainPopulated;
        lightPopulated_ = lightPopulated;
    }
    [[nodiscard]] std::int64_t lastUpdate() const { return lastUpdate_; }
    [[nodiscard]] std::int64_t inhabitedTime() const { return inhabitedTime_; }
    [[nodiscard]] bool terrainPopulated() const { return terrainPopulated_; }
    [[nodiscard]] bool lightPopulated() const { return lightPopulated_; }

private:
    int x_;
    int z_;
    std::array<std::unique_ptr<ChunkSection>, sectionCount> sections_{};
    std::array<std::unique_ptr<ChunkLightSection>, sectionCount> lightSections_{};
    std::array<std::uint8_t, 256> biomes_{};
    std::vector<GeneratedBlockEntity> blockEntities_;
    std::vector<std::vector<std::uint8_t>> scheduledTicks_;
    bool lightingReady_ = false;
    std::uint64_t lightingRevision_ = 0;
    std::int64_t lastUpdate_ = 0;
    std::int64_t inhabitedTime_ = 0;
    bool terrainPopulated_ = true;
    bool lightPopulated_ = true;
};
