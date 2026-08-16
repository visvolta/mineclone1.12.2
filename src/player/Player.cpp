#include "player/Player.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"

namespace {

constexpr double playerWidth = 0.6;
constexpr double playerHeight = 1.8;
constexpr double eyeHeight = 1.62;
constexpr double stepHeight = 0.6;
constexpr double gravity = 0.08;
constexpr double verticalDrag = 0.9800000190734863;
constexpr double groundSlipperiness = 0.6;
constexpr double baseFriction = 0.91;
constexpr float walkSpeed = 0.1F;
constexpr float airControl = 0.02F;
constexpr float flySpeed = 0.05F;

bool solid(const World& world, int x, int y, int z) {
    const BlockState state = world.getBlock(x, y, z);
    return blockId(state) != 0 && BlockRegistry::get(state).fullCube;
}

template <typename Function>
void forEachCollision(const World& world, const Aabb& area, Function&& function) {
    constexpr double epsilon = 1.0e-7;
    const int minX = static_cast<int>(std::floor(area.minimum.x + epsilon));
    const int minY = static_cast<int>(std::floor(area.minimum.y + epsilon));
    const int minZ = static_cast<int>(std::floor(area.minimum.z + epsilon));
    const int maxX = static_cast<int>(std::floor(area.maximum.x - epsilon));
    const int maxY = static_cast<int>(std::floor(area.maximum.y - epsilon));
    const int maxZ = static_cast<int>(std::floor(area.maximum.z - epsilon));

    for (int y = minY; y <= maxY; ++y)
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x)
                if (solid(world, x, y, z)) function(Aabb{{x, y, z}, {x + 1.0, y + 1.0, z + 1.0}});
}

bool collides(const World& world, const Aabb& box) {
    bool result = false;
    forEachCollision(world, box, [&](const Aabb& block) { result = result || box.intersects(block); });
    return result;
}

double clipY(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{box.minimum.x, std::min(box.minimum.y, box.minimum.y + offset), box.minimum.z},
                     {box.maximum.x, std::max(box.maximum.y, box.maximum.y + offset), box.maximum.z}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.x <= box.minimum.x || block.minimum.x >= box.maximum.x ||
            block.maximum.z <= box.minimum.z || block.minimum.z >= box.maximum.z) return;
        if (offset > 0.0 && box.maximum.y <= block.minimum.y)
            offset = std::min(offset, block.minimum.y - box.maximum.y);
        else if (offset < 0.0 && box.minimum.y >= block.maximum.y)
            offset = std::max(offset, block.maximum.y - box.minimum.y);
    });
    return offset;
}

double clipX(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{std::min(box.minimum.x, box.minimum.x + offset), box.minimum.y, box.minimum.z},
                     {std::max(box.maximum.x, box.maximum.x + offset), box.maximum.y, box.maximum.z}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.y <= box.minimum.y || block.minimum.y >= box.maximum.y ||
            block.maximum.z <= box.minimum.z || block.minimum.z >= box.maximum.z) return;
        if (offset > 0.0 && box.maximum.x <= block.minimum.x)
            offset = std::min(offset, block.minimum.x - box.maximum.x);
        else if (offset < 0.0 && box.minimum.x >= block.maximum.x)
            offset = std::max(offset, block.maximum.x - box.minimum.x);
    });
    return offset;
}

double clipZ(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{box.minimum.x, box.minimum.y, std::min(box.minimum.z, box.minimum.z + offset)},
                     {box.maximum.x, box.maximum.y, std::max(box.maximum.z, box.maximum.z + offset)}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.x <= box.minimum.x || block.minimum.x >= box.maximum.x ||
            block.maximum.y <= box.minimum.y || block.minimum.y >= box.maximum.y) return;
        if (offset > 0.0 && box.maximum.z <= block.minimum.z)
            offset = std::min(offset, block.minimum.z - box.maximum.z);
        else if (offset < 0.0 && box.minimum.z >= block.maximum.z)
            offset = std::max(offset, block.maximum.z - box.minimum.z);
    });
    return offset;
}

struct MoveResult {
    Aabb box;
    double x;
    double y;
    double z;
};

MoveResult clippedMove(const World& world, Aabb box, double x, double y, double z) {
    y = clipY(world, box, y);
    box = box.moved(0.0, y, 0.0);
    x = clipX(world, box, x);
    box = box.moved(x, 0.0, 0.0);
    z = clipZ(world, box, z);
    box = box.moved(0.0, 0.0, z);
    return {box, x, y, z};
}

} // namespace

Aabb Aabb::moved(double x, double y, double z) const {
    const glm::dvec3 offset{x, y, z};
    return {minimum + offset, maximum + offset};
}

bool Aabb::intersects(const Aabb& other) const {
    return maximum.x > other.minimum.x && minimum.x < other.maximum.x &&
           maximum.y > other.minimum.y && minimum.y < other.maximum.y &&
           maximum.z > other.minimum.z && minimum.z < other.maximum.z;
}

Player::Player(glm::dvec3 feetPosition) : position_(feetPosition), previousPosition_(feetPosition) {}

glm::vec3 Player::eyePosition() const {
    return glm::vec3(position_.x, position_.y + eyeHeight, position_.z);
}

glm::vec3 Player::interpolatedEyePosition(float partialTick) const {
    const glm::dvec3 interpolated = previousPosition_ + (position_ - previousPosition_) *
        static_cast<double>(std::clamp(partialTick, 0.0F, 1.0F));
    return glm::vec3(interpolated.x, interpolated.y + eyeHeight, interpolated.z);
}

Aabb Player::bounds() const {
    const double radius = playerWidth * 0.5;
    return {{position_.x - radius, position_.y, position_.z - radius},
            {position_.x + radius, position_.y + playerHeight, position_.z + radius}};
}

bool Player::intersectsBlock(const glm::ivec3& block) const {
    return bounds().intersects(Aabb{{block.x, block.y, block.z}, {block.x + 1.0, block.y + 1.0, block.z + 1.0}});
}

void Player::toggleGameMode() {
    gameMode_ = gameMode_ == GameMode::Survival ? GameMode::Creative : GameMode::Survival;
    if (gameMode_ == GameMode::Survival) flying_ = false;
    flyToggleTimer_ = 0;
}

void Player::moveRelative(float strafe, float forward, float amount, const glm::vec3& lookDirection) {
    float lengthSquared = strafe * strafe + forward * forward;
    if (lengthSquared < 1.0e-4F) return;
    const float scale = amount / std::max(1.0F, std::sqrt(lengthSquared));
    strafe *= scale;
    forward *= scale;

    glm::vec2 forwardFlat(lookDirection.x, lookDirection.z);
    if (glm::dot(forwardFlat, forwardFlat) < 1.0e-6F) forwardFlat = {0.0F, -1.0F};
    forwardFlat = glm::normalize(forwardFlat);
    const glm::vec2 right(-forwardFlat.y, forwardFlat.x);
    const glm::vec2 movement = forwardFlat * forward + right * strafe;
    velocity_.x += movement.x;
    velocity_.z += movement.y;
}

void Player::moveWithCollisions(const World& world, double x, double y, double z, bool sneaking) {
    Aabb start = bounds();

    // Vanilla prevents a sneaking grounded player from walking over an edge in 0.05-block steps.
    if (sneaking && onGround_) {
        constexpr double increment = 0.05;
        while (x != 0.0 && !collides(world, start.moved(x, -stepHeight, 0.0)))
            x = std::abs(x) < increment ? 0.0 : x - std::copysign(increment, x);
        while (z != 0.0 && !collides(world, start.moved(0.0, -stepHeight, z)))
            z = std::abs(z) < increment ? 0.0 : z - std::copysign(increment, z);
        while (x != 0.0 && z != 0.0 && !collides(world, start.moved(x, -stepHeight, z))) {
            x = std::abs(x) < increment ? 0.0 : x - std::copysign(increment, x);
            z = std::abs(z) < increment ? 0.0 : z - std::copysign(increment, z);
        }
    }

    const double requestedX = x;
    const double requestedY = y;
    const double requestedZ = z;
    MoveResult result = clippedMove(world, start, x, y, z);

    const bool horizontalCollision = requestedX != result.x || requestedZ != result.z;
    const bool canStep = onGround_ || (requestedY != result.y && requestedY < 0.0);
    if (stepHeight > 0.0 && canStep && horizontalCollision) {
        MoveResult stepped = clippedMove(world, start, requestedX, stepHeight, requestedZ);
        const double down = clipY(world, stepped.box, -stepHeight);
        stepped.box = stepped.box.moved(0.0, down, 0.0);
        stepped.y += down;
        if (stepped.x * stepped.x + stepped.z * stepped.z > result.x * result.x + result.z * result.z)
            result = stepped;
    }

    position_.x = (result.box.minimum.x + result.box.maximum.x) * 0.5;
    position_.y = result.box.minimum.y;
    position_.z = (result.box.minimum.z + result.box.maximum.z) * 0.5;
    onGround_ = requestedY < 0.0 && requestedY != result.y;
    if (requestedX != result.x) velocity_.x = 0.0;
    if (requestedY != result.y) velocity_.y = 0.0;
    if (requestedZ != result.z) velocity_.z = 0.0;
}

void Player::tick(const World& world, const PlayerInput& input, const glm::vec3& lookDirection) {
    previousPosition_ = position_;
    if (flyToggleTimer_ > 0) --flyToggleTimer_;

    if (gameMode_ == GameMode::Creative && input.jumpPressed) {
        if (flyToggleTimer_ == 0) flyToggleTimer_ = 7;
        else {
            flying_ = !flying_;
            flyToggleTimer_ = 0;
            velocity_.y = 0.0;
        }
    }

    float strafe = input.strafe;
    float forward = input.forward;
    if (input.sneak) {
        strafe *= 0.3F;
        forward *= 0.3F;
    }

    if (flying_) {
        const float movementAmount = flySpeed * (input.sprint ? 2.0F : 1.0F);
        moveRelative(strafe, forward, movementAmount, lookDirection);
        if (input.sneak) velocity_.y -= flySpeed * 3.0F;
        if (input.jump) velocity_.y += flySpeed * 3.0F;
        const double previousVerticalVelocity = velocity_.y;
        moveWithCollisions(world, velocity_.x, velocity_.y, velocity_.z, false);
        velocity_.x *= baseFriction;
        velocity_.z *= baseFriction;
        velocity_.y = previousVerticalVelocity * 0.6;
        if (onGround_) flying_ = false;
    } else {
        if (input.jump && onGround_) {
            velocity_.y = 0.42F;
            if (input.sprint) {
                glm::vec2 forwardFlat(lookDirection.x, lookDirection.z);
                if (glm::dot(forwardFlat, forwardFlat) > 1.0e-6F) {
                    forwardFlat = glm::normalize(forwardFlat);
                    velocity_.x += forwardFlat.x * 0.2;
                    velocity_.z += forwardFlat.y * 0.2;
                }
            }
        }

        const double friction = onGround_ ? groundSlipperiness * baseFriction : baseFriction;
        const float movementSpeed = walkSpeed * (input.sprint && forward >= 0.8F ? 1.3F : 1.0F);
        const float acceleration = onGround_
            ? movementSpeed * static_cast<float>(0.16277136 / (friction * friction * friction))
            : airControl * (input.sprint ? 1.3F : 1.0F);
        moveRelative(strafe, forward, acceleration, lookDirection);
        moveWithCollisions(world, velocity_.x, velocity_.y, velocity_.z, input.sneak);
        velocity_.y -= gravity;
        velocity_.y *= verticalDrag;
        velocity_.x *= friction;
        velocity_.z *= friction;
    }

    if (position_.y < -64.0) {
        position_ = {0.0, 64.0, 14.0};
        previousPosition_ = position_;
        velocity_ = {0.0, 0.0, 0.0};
    }
}
