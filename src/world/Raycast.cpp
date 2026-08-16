#include "world/Raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "world/World.hpp"

namespace {

float firstBoundary(float origin, float direction) {
    if (direction > 0.0F) return (std::floor(origin) + 1.0F - origin) / direction;
    if (direction < 0.0F) return (origin - std::floor(origin)) / -direction;
    return std::numeric_limits<float>::infinity();
}

float reciprocalMagnitude(float value) {
    return value == 0.0F ? std::numeric_limits<float>::infinity() : std::abs(1.0F / value);
}

int stepFor(float value) { return value > 0.0F ? 1 : (value < 0.0F ? -1 : 0); }

} // namespace

std::optional<RaycastHit> raycastBlocks(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maximumDistance
) {
    glm::ivec3 voxel(
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z))
    );
    glm::ivec3 previous = voxel;
    const glm::ivec3 step(stepFor(direction.x), stepFor(direction.y), stepFor(direction.z));
    glm::vec3 tMax(firstBoundary(origin.x, direction.x), firstBoundary(origin.y, direction.y), firstBoundary(origin.z, direction.z));
    const glm::vec3 tDelta(reciprocalMagnitude(direction.x), reciprocalMagnitude(direction.y), reciprocalMagnitude(direction.z));
    float distance = 0.0F;

    while (distance <= maximumDistance) {
        const BlockState state = world.getBlock(voxel.x, voxel.y, voxel.z);
        if (blockId(state) != 0) return RaycastHit{voxel, previous, state, distance};

        previous = voxel;
        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            voxel.x += step.x;
            distance = tMax.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.z) {
            voxel.y += step.y;
            distance = tMax.y;
            tMax.y += tDelta.y;
        } else {
            voxel.z += step.z;
            distance = tMax.z;
            tMax.z += tDelta.z;
        }
    }
    return std::nullopt;
}
