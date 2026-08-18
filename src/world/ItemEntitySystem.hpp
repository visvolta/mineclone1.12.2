#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "items/ItemStack.hpp"

class ItemRegistry;
class Player;
class World;

struct ItemEntity {
    std::uint64_t id = 0;
    ItemStack stack{};
    glm::dvec3 position = glm::dvec3(0.0);
    glm::dvec3 previousPosition = glm::dvec3(0.0);
    glm::dvec3 velocity = glm::dvec3(0.0);
    int age = 0;
    int pickupDelay = 10;
    int health = 5;
    float hoverStart = 0.0F;
    float rotationYaw = 0.0F;
    bool onGround = false;
    bool inWater = false;
    bool removed = false;
};

class ItemEntitySystem {
public:
    explicit ItemEntitySystem(const ItemRegistry& items) : items_(items) {}
    void spawn(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity = glm::dvec3(0.0));
    void tick(const World& world, Player& player);
    void clearRemoved();
    [[nodiscard]] const std::vector<ItemEntity>& entities() const { return entities_; }

private:
    void mergeNearby();
    const ItemRegistry& items_;
    std::uint64_t nextId_ = 1;
    std::vector<ItemEntity> entities_;
};
