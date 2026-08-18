#pragma once

#include <vector>
#include <glm/vec3.hpp>
#include "items/ItemStack.hpp"
#include "entity/Entity.hpp"

class EntityManager;

using ItemEntity = EntityItem;

// Compatibility facade retained for block interaction/redstone call sites.
// Stage 14 moved ownership/ticking/persistence to EntityManager/EntityItem.
class ItemEntitySystem {
public:
    explicit ItemEntitySystem(EntityManager& entities) : entities_(entities) {}
    void spawn(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity = glm::dvec3(0.0));
    void spawnArrow(glm::dvec3 position, glm::dvec3 velocity);
    void spawnBoat(glm::dvec3 position, int type = 0);
    void spawnMinecart(glm::dvec3 position, int type = 0);
    void spawnExperience(glm::dvec3 position, int value);
    [[nodiscard]] std::vector<ItemEntity> entities() const;
private:
    EntityManager& entities_;
};
