#pragma once

#include <glm/vec3.hpp>

class World;

enum class GameMode { Survival, Creative };

struct Aabb {
    glm::dvec3 minimum;
    glm::dvec3 maximum;

    [[nodiscard]] Aabb moved(double x, double y, double z) const;
    [[nodiscard]] bool intersects(const Aabb& other) const;
};

struct PlayerInput {
    float strafe = 0.0F;
    float forward = 0.0F;
    bool jump = false;
    bool jumpPressed = false;
    bool sneak = false;
    bool sprint = false;
};

class Player {
public:
    explicit Player(glm::dvec3 feetPosition);

    void tick(const World& world, const PlayerInput& input, const glm::vec3& lookDirection);
    void toggleGameMode();

    [[nodiscard]] GameMode gameMode() const { return gameMode_; }
    [[nodiscard]] bool flying() const { return flying_; }
    [[nodiscard]] bool onGround() const { return onGround_; }
    [[nodiscard]] glm::vec3 eyePosition() const;
    [[nodiscard]] glm::vec3 interpolatedEyePosition(float partialTick) const;
    [[nodiscard]] const glm::dvec3& feetPosition() const { return position_; }
    [[nodiscard]] const glm::dvec3& velocity() const { return velocity_; }
    [[nodiscard]] Aabb bounds() const;
    [[nodiscard]] bool intersectsBlock(const glm::ivec3& block) const;

private:
    void moveWithCollisions(const World& world, double x, double y, double z, bool sneaking);
    void moveRelative(float strafe, float forward, float amount, const glm::vec3& lookDirection);

    glm::dvec3 position_;
    glm::dvec3 previousPosition_;
    glm::dvec3 velocity_{0.0};
    GameMode gameMode_ = GameMode::Survival;
    bool flying_ = false;
    bool onGround_ = false;
    int flyToggleTimer_ = 0;
};
