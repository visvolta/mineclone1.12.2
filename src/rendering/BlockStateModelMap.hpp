#pragma once

#include <functional>

#include "blocks/BlockRegistry.hpp"
#include "rendering/ModelLoader.hpp"

using RelativeBlockLookup = std::function<BlockState(int dx, int dy, int dz)>;

[[nodiscard]] BlockModelState resolveBlockModelState(BlockState state,
                                                      const RelativeBlockLookup& lookup);
[[nodiscard]] std::int64_t blockModelPositionRandom(int worldX, int worldY, int worldZ);
