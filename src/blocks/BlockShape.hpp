#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "blocks/BlockRegistry.hpp"

class World;

struct BlockBox {
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 1.0;
    double maxY = 1.0;
    double maxZ = 1.0;
};

struct BlockShapeSet {
    std::array<BlockBox, 8> boxes{};
    std::size_t count = 0;

    void add(BlockBox box) {
        if (count < boxes.size()) boxes[count++] = box;
    }

    [[nodiscard]] bool empty() const { return count == 0; }
    [[nodiscard]] const BlockBox* begin() const { return boxes.data(); }
    [[nodiscard]] const BlockBox* end() const { return boxes.data() + count; }
};

class BlockShapes {
public:
    [[nodiscard]] static BlockShapeSet collision(const World& world, BlockState state,
                                                  int x, int y, int z);
    [[nodiscard]] static BlockShapeSet rayTrace(const World& world, BlockState state,
                                                 int x, int y, int z);
    [[nodiscard]] static std::optional<BlockBox> selectionBounds(const World& world, BlockState state,
                                                                  int x, int y, int z);

    [[nodiscard]] static bool isReplaceable(BlockState state);
    [[nodiscard]] static bool isNormalCube(BlockState state);
    [[nodiscard]] static bool isTopSolid(const World& world, int x, int y, int z);
    [[nodiscard]] static bool hasSolidFace(const World& world, int x, int y, int z, Face face);
};
