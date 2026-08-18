#include "survival/FoodStats.hpp"

#include <algorithm>

void FoodStats::addStats(int food, float saturationModifier) {
    foodLevel_ = std::min(foodLevel_ + food, 20);
    saturationLevel_ = std::min(saturationLevel_ + static_cast<float>(food) * saturationModifier * 2.0F,
                                static_cast<float>(foodLevel_));
}

void FoodStats::addExhaustion(float amount) {
    exhaustionLevel_ = std::min(exhaustionLevel_ + std::max(0.0F, amount), 40.0F);
}

void FoodStats::tick(bool shouldHeal, float health, bool naturalRegeneration) {
    previousFoodLevel_ = foodLevel_;
    pendingHeal_ = 0.0F;
    pendingStarveDamage_ = 0.0F;

    if (exhaustionLevel_ > 4.0F) {
        exhaustionLevel_ -= 4.0F;
        if (saturationLevel_ > 0.0F) {
            saturationLevel_ = std::max(saturationLevel_ - 1.0F, 0.0F);
        } else {
            foodLevel_ = std::max(foodLevel_ - 1, 0);
        }
    }

    // FoodStats::onUpdate in 1.12.2 has a fast saturated-regeneration path
    // before the normal 80-tick regeneration path.
    if (naturalRegeneration && saturationLevel_ > 0.0F && shouldHeal && foodLevel_ >= 20) {
        ++foodTimer_;
        if (foodTimer_ >= 10) {
            const float charge = std::min(saturationLevel_, 6.0F);
            pendingHeal_ = charge / 6.0F;
            addExhaustion(charge);
            foodTimer_ = 0;
        }
    } else if (naturalRegeneration && foodLevel_ >= 18 && shouldHeal) {
        ++foodTimer_;
        if (foodTimer_ >= 80) {
            pendingHeal_ = 1.0F;
            addExhaustion(6.0F);
            foodTimer_ = 0;
        }
    } else if (foodLevel_ <= 0) {
        ++foodTimer_;
        if (foodTimer_ >= 80) {
            // Normal difficulty starvation stops at one heart-half (1 health).
            if (health > 1.0F) pendingStarveDamage_ = 1.0F;
            foodTimer_ = 0;
        }
    } else {
        foodTimer_ = 0;
    }
}

void FoodStats::restore(int foodLevel, float saturation, float exhaustion, int timer) {
    foodLevel_ = std::clamp(foodLevel, 0, 20);
    previousFoodLevel_ = foodLevel_;
    saturationLevel_ = std::clamp(saturation, 0.0F, static_cast<float>(foodLevel_));
    exhaustionLevel_ = std::clamp(exhaustion, 0.0F, 40.0F);
    foodTimer_ = std::max(0, timer);
    pendingHeal_ = 0.0F;
    pendingStarveDamage_ = 0.0F;
}

float FoodStats::consumePendingHeal() noexcept {
    const float value = pendingHeal_;
    pendingHeal_ = 0.0F;
    return value;
}

float FoodStats::consumePendingStarveDamage() noexcept {
    const float value = pendingStarveDamage_;
    pendingStarveDamage_ = 0.0F;
    return value;
}

FoodProperties foodProperties(std::uint16_t itemId, std::uint16_t damage) {
    // 1.12.2 ItemFood values. Saturation modifier is the constructor's second
    // parameter; FoodStats multiplies it by food*2 exactly as vanilla does.
    switch (itemId) {
        case 260: return {4, 0.3F, false}; // apple
        case 282: return {6, 0.6F, false}; // mushroom stew
        case 297: return {5, 0.6F, false}; // bread
        case 319: return {3, 0.3F, false}; // raw porkchop
        case 320: return {8, 0.8F, false}; // cooked porkchop
        case 322: return {4, damage == 1 ? 1.2F : 1.2F, true}; // golden apples
        case 349: // fish
            return damage == 0 ? FoodProperties{2,0.1F,false}
                 : damage == 1 ? FoodProperties{2,0.1F,false}
                 : damage == 2 ? FoodProperties{1,0.1F,false}
                               : FoodProperties{1,0.1F,false};
        case 350: // cooked fish/salmon
            return damage == 1 ? FoodProperties{6,0.8F,false} : FoodProperties{5,0.6F,false};
        case 357: return {2, 0.1F, false}; // cookie
        case 360: return {2, 0.3F, false}; // melon
        case 363: return {3, 0.3F, false}; // raw beef
        case 364: return {8, 0.8F, false}; // steak
        case 365: return {2, 0.3F, false}; // raw chicken
        case 366: return {6, 0.6F, false}; // cooked chicken
        case 367: return {4, 0.1F, false}; // rotten flesh
        case 391: return {3, 0.6F, false}; // carrot
        case 392: return {1, 0.3F, false}; // potato
        case 393: return {5, 0.6F, false}; // baked potato
        case 396: return {6, 1.2F, false}; // golden carrot
        case 400: return {8, 0.3F, false}; // pumpkin pie
        case 411: return {2, 0.3F, false}; // raw rabbit
        case 412: return {5, 0.6F, false}; // cooked rabbit
        case 413: return {10, 0.6F, false}; // rabbit stew
        case 423: return {2, 0.3F, false}; // raw mutton
        case 424: return {6, 0.8F, false}; // cooked mutton
        case 434: return {1, 0.6F, false}; // beetroot
        case 436: return {6, 0.6F, false}; // beetroot soup
        default: return {};
    }
}
