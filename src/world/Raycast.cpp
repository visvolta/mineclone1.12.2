#include "world/Raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "blocks/BlockShape.hpp"
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

glm::ivec3 faceNormal(Face face) {
    switch (face) {
        case Face::Down: return {0, -1, 0};
        case Face::Up: return {0, 1, 0};
        case Face::North: return {0, 0, -1};
        case Face::South: return {0, 0, 1};
        case Face::West: return {-1, 0, 0};
        case Face::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}

struct BoxHit {
    float distance = 0.0F;
    Face face = Face::Up;
};

std::optional<BoxHit> intersectBox(const glm::vec3& origin, const glm::vec3& direction,
                                   const glm::ivec3& block, const BlockBox& localBox,
                                   float maximumDistance) {
    constexpr float epsilon = 1.0e-6F;
    const glm::vec3 minimum{
        static_cast<float>(block.x + localBox.minX),
        static_cast<float>(block.y + localBox.minY),
        static_cast<float>(block.z + localBox.minZ)
    };
    const glm::vec3 maximum{
        static_cast<float>(block.x + localBox.maxX),
        static_cast<float>(block.y + localBox.maxY),
        static_cast<float>(block.z + localBox.maxZ)
    };

    float entry = 0.0F;
    float exit = maximumDistance;
    Face entryFace = Face::Up;
    Face exitFace = Face::Down;

    const auto axis = [&](float rayOrigin, float rayDirection, float boxMin, float boxMax,
                          Face minFace, Face maxFace, float& nearValue, float& farValue,
                          Face& nearFace, Face& farFace) -> bool {
        if (std::abs(rayDirection) < epsilon)
            return rayOrigin >= boxMin - epsilon && rayOrigin <= boxMax + epsilon;
        const float inverse = 1.0F / rayDirection;
        float first = (boxMin - rayOrigin) * inverse;
        float second = (boxMax - rayOrigin) * inverse;
        Face firstFace = minFace;
        Face secondFace = maxFace;
        if (first > second) {
            std::swap(first, second);
            std::swap(firstFace, secondFace);
        }
        if (first > nearValue) {
            nearValue = first;
            nearFace = firstFace;
        }
        if (second < farValue) {
            farValue = second;
            farFace = secondFace;
        }
        return nearValue <= farValue + epsilon;
    };

    if (!axis(origin.x, direction.x, minimum.x, maximum.x,
              Face::West, Face::East, entry, exit, entryFace, exitFace) ||
        !axis(origin.y, direction.y, minimum.y, maximum.y,
              Face::Down, Face::Up, entry, exit, entryFace, exitFace) ||
        !axis(origin.z, direction.z, minimum.z, maximum.z,
              Face::North, Face::South, entry, exit, entryFace, exitFace))
        return std::nullopt;

    if (exit < -epsilon || entry > maximumDistance + epsilon) return std::nullopt;
    if (entry >= 0.0F) return BoxHit{entry, entryFace};
    return BoxHit{std::max(0.0F, exit), exitFace};
}

} // namespace

std::optional<RaycastHit> raycastBlocks(
    const World& world,
    const glm::vec3& origin,
    const glm::vec3& inputDirection,
    float maximumDistance
) {
    if (maximumDistance < 0.0F) return std::nullopt;
    const float directionLength = glm::length(inputDirection);
    if (directionLength <= 1.0e-7F) return std::nullopt;
    const glm::vec3 direction = inputDirection / directionLength;

    glm::ivec3 voxel(
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z))
    );
    const glm::ivec3 step(stepFor(direction.x), stepFor(direction.y), stepFor(direction.z));
    glm::vec3 tMax(firstBoundary(origin.x, direction.x),
                   firstBoundary(origin.y, direction.y),
                   firstBoundary(origin.z, direction.z));
    const glm::vec3 tDelta(reciprocalMagnitude(direction.x),
                           reciprocalMagnitude(direction.y),
                           reciprocalMagnitude(direction.z));
    float voxelEntry = 0.0F;

    while (voxelEntry <= maximumDistance) {
        const float voxelExit = std::min({tMax.x, tMax.y, tMax.z, maximumDistance});
        const BlockState state = world.getBlock(voxel.x, voxel.y, voxel.z);
        if (blockId(state) != 0) {
            const BlockShapeSet shape = BlockShapes::rayTrace(world, state, voxel.x, voxel.y, voxel.z);
            std::optional<BoxHit> closest;
            for (const BlockBox& box : shape) {
                const auto hit = intersectBox(origin, direction, voxel, box, maximumDistance);
                if (!hit || hit->distance + 1.0e-5F < voxelEntry || hit->distance > voxelExit + 1.0e-5F)
                    continue;
                if (!closest || hit->distance < closest->distance) closest = hit;
            }
            if (closest) {
                const glm::ivec3 adjacent = voxel + faceNormal(closest->face);
                return RaycastHit{
                    voxel,
                    adjacent,
                    state,
                    closest->distance,
                    closest->face,
                    origin + direction * closest->distance
                };
            }
        }

        if (voxelExit >= maximumDistance) break;
        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            voxel.x += step.x;
            voxelEntry = tMax.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.z) {
            voxel.y += step.y;
            voxelEntry = tMax.y;
            tMax.y += tDelta.y;
        } else {
            voxel.z += step.z;
            voxelEntry = tMax.z;
            tMax.z += tDelta.z;
        }
    }
    return std::nullopt;
}
