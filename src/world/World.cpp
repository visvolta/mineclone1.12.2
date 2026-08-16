#include "world/World.hpp"

#include <cstdint>

std::uint64_t World::key(int chunkX, int chunkZ) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32U) |
           static_cast<std::uint32_t>(chunkZ);
}

int World::floorDiv16(int value) {
    int quotient = value / 16;
    if (value < 0 && value % 16 != 0) --quotient;
    return quotient;
}

int World::floorMod16(int value) {
    const int remainder = value % 16;
    return remainder < 0 ? remainder + 16 : remainder;
}

BlockState World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= chunkHeight) return makeBlockState(0);
    const Chunk* chunk = findChunk(floorDiv16(x), floorDiv16(z));
    return chunk ? chunk->get(floorMod16(x), y, floorMod16(z)) : makeBlockState(0);
}

std::uint8_t World::getSkyLight(int x, int y, int z) const {
    if (y >= chunkHeight) return 15;
    if (y < 0) return 0;
    const Chunk* chunk = findChunk(floorDiv16(x), floorDiv16(z));
    return chunk ? chunk->skyLight(floorMod16(x), y, floorMod16(z)) : 15;
}

std::uint8_t World::getBlockLight(int x, int y, int z) const {
    if (y < 0 || y >= chunkHeight) return 0;
    const Chunk* chunk = findChunk(floorDiv16(x), floorDiv16(z));
    return chunk ? chunk->blockLight(floorMod16(x), y, floorMod16(z)) : 0;
}

void World::setBlock(int x, int y, int z, BlockState state) {
    if (y < 0 || y >= chunkHeight) return;
    const int chunkX = floorDiv16(x);
    const int chunkZ = floorDiv16(z);
    if (ensureChunk(chunkX, chunkZ).set(floorMod16(x), y, floorMod16(z), state))
        touchChunk(chunkX, chunkZ);
}

void World::setGeneratedBlock(int x, int y, int z, BlockState state) {
    if (y < 0 || y >= chunkHeight) return;
    const int chunkX = floorDiv16(x);
    const int chunkZ = floorDiv16(z);
    static_cast<void>(ensureChunk(chunkX, chunkZ).set(floorMod16(x), y, floorMod16(z), state));
}

void World::addGeneratedBlockEntity(GeneratedBlockEntity entity) {
    const int chunkX = floorDiv16(entity.x);
    const int chunkZ = floorDiv16(entity.z);
    ensureChunk(chunkX, chunkZ).addBlockEntity(std::move(entity));
}

Chunk& World::ensureChunk(int chunkX, int chunkZ) {
    const std::uint64_t chunkKey = key(chunkX, chunkZ);
    auto [iterator, inserted] = chunks_.try_emplace(chunkKey);
    if (inserted) {
        iterator->second = std::make_unique<Chunk>(chunkX, chunkZ);
        touchChunk(chunkX, chunkZ);
    }
    return *iterator->second;
}

void World::insertChunk(std::unique_ptr<Chunk> chunk) {
    if (!chunk) return;
    // A cached chunk may return beside a different set of neighbors. Preserve
    // its packed light bytes for storage, but do not expose them for rendering
    // until a versioned relight has committed against the current neighborhood.
    chunk->invalidateLighting();
    const std::uint64_t chunkKey = key(chunk->x(), chunk->z());
    const int chunkX = chunk->x();
    const int chunkZ = chunk->z();
    chunks_.insert_or_assign(chunkKey, std::move(chunk));
    touchChunk(chunkX, chunkZ);
}

std::unique_ptr<Chunk> World::extractChunk(int chunkX, int chunkZ) {
    const auto iterator = chunks_.find(key(chunkX, chunkZ));
    if (iterator == chunks_.end()) return nullptr;
    std::unique_ptr<Chunk> chunk = std::move(iterator->second);
    chunks_.erase(iterator);
    touchChunk(chunkX, chunkZ);
    return chunk;
}

bool World::removeChunk(int chunkX, int chunkZ) {
    const bool removed = chunks_.erase(key(chunkX, chunkZ)) != 0;
    if (removed) touchChunk(chunkX, chunkZ);
    return removed;
}

const Chunk* World::findChunk(int chunkX, int chunkZ) const {
    const auto iterator = chunks_.find(key(chunkX, chunkZ));
    return iterator == chunks_.end() ? nullptr : iterator->second.get();
}

Chunk* World::findChunk(int chunkX, int chunkZ) {
    const auto iterator = chunks_.find(key(chunkX, chunkZ));
    return iterator == chunks_.end() ? nullptr : iterator->second.get();
}

void World::touchChunk(int chunkX, int chunkZ) {
    chunkEpochs_.insert_or_assign(key(chunkX, chunkZ), ++nextChunkEpoch_);
}

std::uint64_t World::chunkEpoch(int chunkX, int chunkZ) const {
    const auto iterator = chunkEpochs_.find(key(chunkX, chunkZ));
    return iterator == chunkEpochs_.end() ? 0 : iterator->second;
}
