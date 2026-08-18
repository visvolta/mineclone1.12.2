#pragma once

#include <cstdint>

#include "save/Nbt.hpp"

namespace SaveMigration {

inline constexpr std::int32_t currentVersion = 1;

// Applies Blockcraft-private schema migrations in memory while leaving vanilla
// DataVersion untouched at 1.12.2's value. Version 0 is every save made before
// Stage 12.5.
void migrateLevelData(nbt::Compound& data);

} // namespace SaveMigration
