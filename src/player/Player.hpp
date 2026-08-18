#pragma once

#include <glm/vec3.hpp>

#include "player/PlayerInventory.hpp"
#include "survival/FoodStats.hpp"

class World;

enum class GameMode { Survival, Creative };
enum class DamageType { Generic, Fall, Drown, Fire, Lava, Cactus, Void, Starve };

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
    bool useItem = false;
};

class Player {
public:
    explicit Player(glm::dvec3 feetPosition);

    void tick(const World& world, const PlayerInput& input, const glm::vec3& lookDirection);
    void toggleGameMode();
    void restoreState(glm::dvec3 feetPosition, glm::dvec3 velocity, GameMode mode, std::size_t selectedHotbar) {
        position_ = feetPosition; previousPosition_ = feetPosition; velocity_ = velocity; gameMode_ = mode;
        flying_ = false; onGround_ = false; inventory_.selectHotbar(selectedHotbar);
    }

    [[nodiscard]] GameMode gameMode() const { return gameMode_; }
    [[nodiscard]] bool flying() const { return flying_; }
    [[nodiscard]] bool onGround() const { return onGround_; }
    [[nodiscard]] glm::vec3 eyePosition() const;
    [[nodiscard]] glm::vec3 interpolatedEyePosition(float partialTick) const;
    [[nodiscard]] const glm::dvec3& feetPosition() const { return position_; }
    [[nodiscard]] const glm::dvec3& velocity() const { return velocity_; }
    [[nodiscard]] Aabb bounds() const;
    [[nodiscard]] bool intersectsBlock(const World& world, const glm::ivec3& block) const;
    [[nodiscard]] PlayerInventory& inventory() { return inventory_; }
    [[nodiscard]] float health() const { return health_; }
    [[nodiscard]] float maxHealth() const { return 20.0F; }
    [[nodiscard]] int air() const { return air_; }
    [[nodiscard]] int fireTicks() const { return fireTicks_; }
    [[nodiscard]] bool dead() const { return dead_; }
    [[nodiscard]] int armorValue() const;
    [[nodiscard]] const FoodStats& foodStats() const { return foodStats_; }
    [[nodiscard]] FoodStats& foodStats() { return foodStats_; }
    [[nodiscard]] int ticksExisted() const { return ticksExisted_; }
    [[nodiscard]] int hurtTime() const { return hurtTime_; }
    [[nodiscard]] int maxHurtTime() const { return maxHurtTime_; }
    [[nodiscard]] float attackedAtYaw() const { return attackedAtYaw_; }
    [[nodiscard]] float hurtCameraStrength(float partialTick) const;
    [[nodiscard]] int itemUseTicks() const { return itemUseTicks_; }
    [[nodiscard]] bool usingItem() const { return usingItem_; }
    [[nodiscard]] float itemUseProgress(float partialTick = 0.0F) const;
    [[nodiscard]] int experienceTotal() const { return experienceTotal_; }
    [[nodiscard]] int experienceLevel() const { return experienceLevel_; }
    [[nodiscard]] float experienceProgress() const { return experienceProgress_; }
    void addExperience(int amount);
    void heal(float amount);
    void addExhaustion(float amount) { if (gameMode_ == GameMode::Survival) foodStats_.addExhaustion(amount); }
    bool hurt(float amount, DamageType type = DamageType::Generic);
    void respawn();
    void setRespawnPosition(glm::dvec3 value) { respawnPosition_ = value; }
    void restoreSurvival(float health, int air, int fireTicks, bool dead = false,
                         int foodLevel = 20, float saturation = 5.0F, float exhaustion = 0.0F,
                         int experienceTotal = 0, int experienceLevel = 0, float experienceProgress = 0.0F);
    [[nodiscard]] const PlayerInventory& inventory() const { return inventory_; }

private:
    void moveWithCollisions(const World& world, double x, double y, double z, bool sneaking);
    void moveRelative(float strafe, float forward, float amount, const glm::vec3& lookDirection);
    void tickSurvival(const World& world);
    void tickItemUse(bool useHeldItem);

    glm::dvec3 position_;
    glm::dvec3 previousPosition_;
    glm::dvec3 velocity_{0.0};
    PlayerInventory inventory_{};
    GameMode gameMode_ = GameMode::Survival;
    bool flying_ = false;
    bool onGround_ = false;
    int flyToggleTimer_ = 0;
    glm::dvec3 respawnPosition_{0.5, 80.0, 0.5};
    float health_ = 20.0F;
    float fallDistance_ = 0.0F;
    int air_ = 300;
    int fireTicks_ = 0;
    int hurtResistantTime_ = 0;
    int fireDamageTicker_ = 0;
    bool dead_ = false;
    FoodStats foodStats_{};
    int ticksExisted_ = 0;
    int hurtTime_ = 0;
    int maxHurtTime_ = 10;
    float attackedAtYaw_ = 0.0F;
    bool usingItem_ = false;
    int itemUseTicks_ = 0;
    std::uint16_t usingItemId_ = 0;
    int experienceTotal_ = 0;
    int experienceLevel_ = 0;
    float experienceProgress_ = 0.0F;
};
