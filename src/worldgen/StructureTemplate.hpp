#pragma once

#include <string>
#include <vector>

#include "blocks/BlockState.hpp"

class JavaRandom;
class World;

struct TemplateBlock {
    int x = 0;
    int y = 0;
    int z = 0;
    BlockState state = 0;
};

class StructureTemplate {
public:
    static StructureTemplate load(const std::string& path);

    [[nodiscard]] int sizeX() const { return sizeX_; }
    [[nodiscard]] int sizeY() const { return sizeY_; }
    [[nodiscard]] int sizeZ() const { return sizeZ_; }
    [[nodiscard]] int rotatedSizeX(int rotation) const;
    [[nodiscard]] int rotatedSizeZ(int rotation) const;

    void place(World& world, JavaRandom& random, int originX, int originY, int originZ,
               int rotation, float integrity, int clipChunkX, int clipChunkZ) const;

private:
    int sizeX_ = 0;
    int sizeY_ = 0;
    int sizeZ_ = 0;
    std::vector<TemplateBlock> blocks_;
};
