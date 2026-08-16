#pragma once

#include <cstdint>

class World;

class CaveGenerator {
public:
    explicit CaveGenerator(std::int64_t worldSeed) : worldSeed_(worldSeed) {}
    void generate(World& world, int chunkX, int chunkZ, bool caves, bool ravines) const;

private:
    std::int64_t worldSeed_;
};
