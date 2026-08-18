#pragma once

#include <cstdint>

struct FoodProperties {
    int hunger = 0;
    float saturationModifier = 0.0F;
    bool alwaysEdible = false;
};

class FoodStats {
public:
    void addStats(int food, float saturationModifier);
    void addExhaustion(float amount);
    void tick(bool shouldHeal, float health, bool naturalRegeneration = true);

    [[nodiscard]] int foodLevel() const noexcept { return foodLevel_; }
    [[nodiscard]] int previousFoodLevel() const noexcept { return previousFoodLevel_; }
    [[nodiscard]] float saturationLevel() const noexcept { return saturationLevel_; }
    [[nodiscard]] float exhaustionLevel() const noexcept { return exhaustionLevel_; }
    [[nodiscard]] int foodTimer() const noexcept { return foodTimer_; }
    [[nodiscard]] bool needsFood() const noexcept { return foodLevel_ < 20; }

    void restore(int foodLevel, float saturation, float exhaustion, int timer = 0);

    // The Player owns health/damage, so FoodStats exposes per-tick effects instead
    // of directly depending on Player. These are cleared after each tick.
    [[nodiscard]] float consumePendingHeal() noexcept;
    [[nodiscard]] float consumePendingStarveDamage() noexcept;

private:
    int foodLevel_ = 20;
    int previousFoodLevel_ = 20;
    float saturationLevel_ = 5.0F;
    float exhaustionLevel_ = 0.0F;
    int foodTimer_ = 0;
    float pendingHeal_ = 0.0F;
    float pendingStarveDamage_ = 0.0F;
};

[[nodiscard]] FoodProperties foodProperties(std::uint16_t itemId, std::uint16_t damage = 0);
