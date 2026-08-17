#include "world/ItemEntitySystem.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "blocks/BlockShape.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {
bool solidAt(const World& world, const glm::dvec3& p) {
    const int x=static_cast<int>(std::floor(p.x));
    const int y=static_cast<int>(std::floor(p.y));
    const int z=static_cast<int>(std::floor(p.z));
    return !BlockShapes::collision(world,world.getBlock(x,y,z),x,y,z).empty();
}
}

void ItemEntitySystem::spawn(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity) {
    if (stack.empty()) return;
    ItemEntity e; e.id=nextId_++; e.stack=std::move(stack); e.position=position; e.previousPosition=position; e.velocity=velocity;
    entities_.push_back(std::move(e));
}

void ItemEntitySystem::tick(const World& world, Player& player) {
    for (ItemEntity& e:entities_) {
        if (e.removed||e.stack.empty()) {e.removed=true;continue;}
        e.previousPosition=e.position;
        if (e.pickupDelay>0) --e.pickupDelay;
        ++e.age;
        e.rotation += 2.0F;
        if (e.age>=6000) { e.removed=true; continue; }

        e.velocity.y -= 0.04;
        glm::dvec3 next=e.position+e.velocity;
        if (solidAt(world,{next.x,e.position.y,next.z})) {e.velocity.x*=-0.5;e.velocity.z*=-0.5;next.x=e.position.x;next.z=e.position.z;}
        if (solidAt(world,{next.x,next.y-0.125,next.z})) {
            if (e.velocity.y<0.0) e.velocity.y*=-0.5;
            next.y=e.position.y;
            e.velocity.x*=0.588;
            e.velocity.z*=0.588;
        }
        e.position=next;
        e.velocity*=glm::dvec3(0.98,0.98,0.98);

        if (e.pickupDelay==0 && !player.dead()) {
            const glm::dvec3 d=e.position-player.feetPosition();
            if (d.x*d.x+d.y*d.y+d.z*d.z <= 2.25) {
                ItemStack remaining=e.stack;
                player.inventory().addStack(remaining);
                e.stack=remaining;
                if (e.stack.empty()) e.removed=true;
            }
        }
    }
    clearRemoved();
}

void ItemEntitySystem::clearRemoved() {
    entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const ItemEntity& e){return e.removed;}),entities_.end());
}
