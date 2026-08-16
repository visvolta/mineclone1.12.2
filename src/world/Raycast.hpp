#pragma once

#include <optional>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"

class World;

struct RaycastHit {
    glm::ivec3 block;
    glm::ivec3 adjacent;
    BlockState state;
    float distance;
};

[[nodiscard]] std::optional<RaycastHit> raycastBlocks(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximumDistance
);
