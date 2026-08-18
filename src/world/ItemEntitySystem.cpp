#include "world/ItemEntitySystem.hpp"
#include "entity/EntityManager.hpp"
void ItemEntitySystem::spawn(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity){
    if(!stack.empty()) entities_.spawnItem(std::move(stack),position,velocity);
}
void ItemEntitySystem::spawnArrow(glm::dvec3 position, glm::dvec3 velocity){ entities_.spawnArrow(position,velocity); }
void ItemEntitySystem::spawnBoat(glm::dvec3 position, int type){ entities_.spawnBoat(position,type); }
void ItemEntitySystem::spawnMinecart(glm::dvec3 position, int type){ entities_.spawnMinecart(position,type); }
void ItemEntitySystem::spawnExperience(glm::dvec3 position, int value){ entities_.spawnXpOrb(position,value); }
std::vector<ItemEntity> ItemEntitySystem::entities() const {
    std::vector<ItemEntity> out;
    for (const auto& entity : entities_.entities()) {
        if (!entity || entity->removed) continue;
        if (const auto* item = dynamic_cast<const EntityItem*>(entity.get())) out.push_back(*item);
        else { ItemEntity proxy; proxy.id=entity->id; proxy.position=entity->position; proxy.previousPosition=entity->previousPosition; proxy.velocity=entity->velocity; proxy.removed=false; out.push_back(std::move(proxy)); }
    }
    return out;
}
