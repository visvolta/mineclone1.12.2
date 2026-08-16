#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "world/Chunk.hpp"

class World {
public:
    [[nodiscard]] BlockState getBlock(int x, int y, int z) const;
    [[nodiscard]] std::uint8_t getSkyLight(int x, int y, int z) const;
    [[nodiscard]] std::uint8_t getBlockLight(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockState state);
    // Generation builds an isolated chunk and does not need to invalidate
    // render/lighting caches for every one of its tens of thousands of writes.
    void setGeneratedBlock(int x, int y, int z, BlockState state);
    void addGeneratedBlockEntity(GeneratedBlockEntity entity);
    [[nodiscard]] Chunk& ensureChunk(int chunkX, int chunkZ);
    void insertChunk(std::unique_ptr<Chunk> chunk);
    [[nodiscard]] std::unique_ptr<Chunk> extractChunk(int chunkX, int chunkZ);
    bool removeChunk(int chunkX, int chunkZ);
    [[nodiscard]] Chunk* findChunk(int chunkX, int chunkZ);
    [[nodiscard]] const Chunk* findChunk(int chunkX, int chunkZ) const;
    [[nodiscard]] std::size_t chunkCount() const { return chunks_.size(); }
    [[nodiscard]] const std::unordered_map<std::uint64_t, std::unique_ptr<Chunk>>& chunks() const { return chunks_; }
    [[nodiscard]] std::uint64_t chunkEpoch(int chunkX, int chunkZ) const;
    [[nodiscard]] static int floorDiv16(int value);
    [[nodiscard]] static int floorMod16(int value);

private:
    [[nodiscard]] static std::uint64_t key(int chunkX, int chunkZ);
    void touchChunk(int chunkX, int chunkZ);
    std::unordered_map<std::uint64_t, std::unique_ptr<Chunk>> chunks_;
    std::unordered_map<std::uint64_t, std::uint64_t> chunkEpochs_;
    std::uint64_t nextChunkEpoch_ = 0;
};
