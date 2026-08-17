#include "world/Chunk.hpp"

#include <cassert>

BlockState ChunkSection::get(int x, int y, int z) const {
    assert(x >= 0 && x < sectionSize && y >= 0 && y < sectionSize && z >= 0 && z < sectionSize);
    return blocks_[index(x, y, z)];
}

bool ChunkSection::set(int x, int y, int z, BlockState state) {
    assert(x >= 0 && x < sectionSize && y >= 0 && y < sectionSize && z >= 0 && z < sectionSize);
    BlockState& current = blocks_[index(x, y, z)];
    if (current == state) return false;
    if (blockId(current) == 0 && blockId(state) != 0) ++nonAirCount_;
    if (blockId(current) != 0 && blockId(state) == 0) --nonAirCount_;
    current = state;
    return true;
}

std::uint8_t NibbleArray::get(int x, int y, int z) const {
    assert(x >= 0 && x < sectionSize && y >= 0 && y < sectionSize && z >= 0 && z < sectionSize);
    const std::size_t cell = index(x, y, z);
    const std::uint8_t packed = bytes_[cell >> 1U];
    return static_cast<std::uint8_t>((cell & 1U) == 0U ? packed & 0x0FU : packed >> 4U);
}

void NibbleArray::set(int x, int y, int z, std::uint8_t value) {
    assert(x >= 0 && x < sectionSize && y >= 0 && y < sectionSize && z >= 0 && z < sectionSize);
    assert(value <= 15);
    const std::size_t cell = index(x, y, z);
    std::uint8_t& packed = bytes_[cell >> 1U];
    if ((cell & 1U) == 0U)
        packed = static_cast<std::uint8_t>((packed & 0xF0U) | value);
    else
        packed = static_cast<std::uint8_t>((packed & 0x0FU) | (value << 4U));
}

BlockState Chunk::get(int localX, int y, int localZ) const {
    if (localX < 0 || localX >= chunkSize || localZ < 0 || localZ >= chunkSize || y < 0 || y >= chunkHeight)
        return makeBlockState(0);
    const auto& storage = sections_[static_cast<std::size_t>(y >> 4)];
    return storage ? storage->get(localX, y & 15, localZ) : makeBlockState(0);
}

bool Chunk::set(int localX, int y, int localZ, BlockState state) {
    if (localX < 0 || localX >= chunkSize || localZ < 0 || localZ >= chunkSize || y < 0 || y >= chunkHeight)
        return false;
    auto& storage = sections_[static_cast<std::size_t>(y >> 4)];
    if (!storage && blockId(state) == 0) return false;
    if (!storage) storage = std::make_unique<ChunkSection>();
    const bool changed = storage->set(localX, y & 15, localZ, state);
    if (changed) invalidateLighting();
    return changed;
}

const ChunkSection* Chunk::section(int index) const {
    if (index < 0 || index >= sectionCount) return nullptr;
    return sections_[static_cast<std::size_t>(index)].get();
}

std::uint8_t Chunk::skyLight(int localX, int y, int localZ) const {
    if (localX < 0 || localX >= chunkSize || localZ < 0 || localZ >= chunkSize || y < 0 || y >= chunkHeight)
        return y >= chunkHeight ? 15 : 0;
    const auto& section = lightSections_[static_cast<std::size_t>(y >> 4)];
    if (!section) return lightingReady_ ? 0 : 15;
    return section->sky.get(localX, y & 15, localZ);
}

std::uint8_t Chunk::blockLight(int localX, int y, int localZ) const {
    if (localX < 0 || localX >= chunkSize || localZ < 0 || localZ >= chunkSize || y < 0 || y >= chunkHeight)
        return 0;
    const auto& section = lightSections_[static_cast<std::size_t>(y >> 4)];
    return section ? section->block.get(localX, y & 15, localZ) : 0;
}

std::uint16_t Chunk::applyLighting(const std::vector<std::uint8_t>& sky,
                                   const std::vector<std::uint8_t>& block,
                                   std::uint8_t* dirtyBorders) {
    constexpr std::size_t cellCount = static_cast<std::size_t>(chunkSize * chunkHeight * chunkSize);
    assert(sky.size() == cellCount && block.size() == cellCount);
    std::uint16_t dirtySections = 0;
    std::uint8_t borders = 0;
    for (int sectionY = 0; sectionY < sectionCount; ++sectionY) {
        const ChunkLightSection* old = lightSections_[static_cast<std::size_t>(sectionY)].get();
        const int baseY = sectionY * sectionSize;
        bool changed = false;
        for (int y = 0; y < sectionSize; ++y) {
            for (int z = 0; z < chunkSize; ++z) {
                const std::size_t base = static_cast<std::size_t>(((baseY + y) * chunkSize + z) * chunkSize);
                for (int x = 0; x < chunkSize; ++x) {
                    const std::size_t index = base + static_cast<std::size_t>(x);
                    const std::uint8_t oldSky = old ? old->sky.get(x, y, z) : 0;
                    const std::uint8_t oldBlock = old ? old->block.get(x, y, z) : 0;
                    if (oldSky != sky[index] || oldBlock != block[index]) {
                        changed = true;
                        if (x == 0) borders |= LightBorderWest;
                        if (x == chunkSize - 1) borders |= LightBorderEast;
                        if (z == 0) borders |= LightBorderNorth;
                        if (z == chunkSize - 1) borders |= LightBorderSouth;
                    }
                }
            }
        }
        if (changed) dirtySections |= static_cast<std::uint16_t>(1U << sectionY);
    }
    lightSections_ = {};
    for (int sectionY = 0; sectionY < sectionCount; ++sectionY) {
        const int baseY = sectionY * sectionSize;
        bool anyLight = false;
        for (int y = 0; y < sectionSize && !anyLight; ++y) {
            for (int z = 0; z < chunkSize && !anyLight; ++z) {
                const std::size_t base = static_cast<std::size_t>(((baseY + y) * chunkSize + z) * chunkSize);
                for (int x = 0; x < chunkSize; ++x) {
                    if (sky[base + static_cast<std::size_t>(x)] != 0 || block[base + static_cast<std::size_t>(x)] != 0) {
                        anyLight = true;
                        break;
                    }
                }
            }
        }
        if (!anyLight) continue;
        auto storage = std::make_unique<ChunkLightSection>();
        for (int y = 0; y < sectionSize; ++y) {
            for (int z = 0; z < chunkSize; ++z) {
                const std::size_t base = static_cast<std::size_t>(((baseY + y) * chunkSize + z) * chunkSize);
                for (int x = 0; x < chunkSize; ++x) {
                    const std::size_t index = base + static_cast<std::size_t>(x);
                    storage->sky.set(x, y, z, sky[index]);
                    storage->block.set(x, y, z, block[index]);
                }
            }
        }
        lightSections_[static_cast<std::size_t>(sectionY)] = std::move(storage);
    }
    lightingReady_ = true;
    ++lightingRevision_;
    if (dirtyBorders != nullptr) *dirtyBorders = borders;
    return dirtySections;
}
