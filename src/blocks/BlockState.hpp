#pragma once

#include <cstdint>

using BlockState = std::uint16_t;

// Minecraft 1.12.2's legacy state ID is (numeric block ID << 4) | metadata.
constexpr BlockState makeBlockState(std::uint16_t blockId, std::uint8_t metadata = 0) {
    return static_cast<BlockState>((blockId << 4U) | (metadata & 0x0FU));
}

constexpr std::uint16_t blockId(BlockState state) {
    return static_cast<std::uint16_t>(state >> 4U);
}

constexpr std::uint8_t blockMetadata(BlockState state) {
    return static_cast<std::uint8_t>(state & 0x0FU);
}
