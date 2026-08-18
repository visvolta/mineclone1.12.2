#include "world/ItemEntitySystem.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

#include <glm/geometric.hpp>

#include "blocks/BlockRegistry.hpp"
#include "blocks/BlockShape.hpp"
#include "items/ItemRegistry.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {
std::mt19937& rng() { static std::mt19937 r{0x1122BEEFu}; return r; }
double randomRange(double a, double b) { std::uniform_real_distribution<double> d(a,b); return d(rng()); }

bool collidableAt(const World& world, int x, int y, int z) {
    return !BlockShapes::collision(world, world.getBlock(x,y,z), x,y,z).empty();
}
BlockId blockAt(const World& world, const glm::dvec3& p) {
    return static_cast<BlockId>(blockId(world.getBlock(static_cast<int>(std::floor(p.x)),
                                                       static_cast<int>(std::floor(p.y)),
                                                       static_cast<int>(std::floor(p.z)))));
}
bool sameStack(const ItemStack& a, const ItemStack& b) {
    return a.itemId == b.itemId && a.damage == b.damage && a.nbt == b.nbt;
}
}

void ItemEntitySystem::spawn(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity) {
    if (stack.empty()) return;
    ItemEntity e;
    e.id = nextId_++;
    e.stack = std::move(stack);
    e.position = position;
    e.previousPosition = position;
    if (glm::dot(velocity, velocity) < 1.0e-12) {
        e.velocity = glm::dvec3(randomRange(-0.1, 0.1), 0.20000000298023224, randomRange(-0.1, 0.1));
    } else {
        e.velocity = velocity;
    }
    e.hoverStart = static_cast<float>(randomRange(0.0, 6.283185307179586));
    e.rotationYaw = static_cast<float>(randomRange(0.0, 360.0));
    entities_.push_back(std::move(e));
}

void ItemEntitySystem::mergeNearby() {
    for (std::size_t i=0;i<entities_.size();++i) {
        ItemEntity& a=entities_[i];
        if(a.removed||a.stack.empty()||a.pickupDelay==32767||a.age==-32768) continue;
        for(std::size_t j=i+1;j<entities_.size();++j) {
            ItemEntity& b=entities_[j];
            if(b.removed||b.stack.empty()||b.pickupDelay==32767||b.age==-32768||!sameStack(a.stack,b.stack)) continue;
            const glm::dvec3 d=a.position-b.position;
            if(std::abs(d.x)>0.75||std::abs(d.z)>0.75||std::abs(d.y)>0.25) continue;
            ItemEntity* dst=&a; ItemEntity* src=&b;
            if(dst->stack.count < src->stack.count) std::swap(dst,src);
            const int limit=items_.get(dst->stack.itemId).maxStackSize;
            if(dst->stack.count + src->stack.count > limit) continue;
            dst->stack.count += src->stack.count;
            dst->pickupDelay=std::max(dst->pickupDelay,src->pickupDelay);
            dst->age=std::min(dst->age,src->age);
            src->removed=true;
        }
    }
}

void ItemEntitySystem::tick(const World& world, Player& player) {
    bool shouldMerge=false;
    for (ItemEntity& e : entities_) {
        if (e.removed || e.stack.empty()) { e.removed=true; continue; }
        e.previousPosition=e.position;
        if(e.pickupDelay>0 && e.pickupDelay!=32767) --e.pickupDelay;
        if(e.age!=-32768) ++e.age;
        if(e.age>=6000){e.removed=true;continue;}

        e.velocity.y -= 0.03999999910593033;
        const BlockId here=blockAt(world,e.position);
        if(here==BlockId::Lava||here==BlockId::FlowingLava){
            e.velocity.y=0.20000000298023224;
            e.velocity.x=randomRange(-0.2,0.2);
            e.velocity.z=randomRange(-0.2,0.2);
        }
        e.inWater = here==BlockId::Water || here==BlockId::FlowingWater;

        glm::dvec3 next=e.position+e.velocity;
        e.onGround=false;
        const int bx=static_cast<int>(std::floor(next.x));
        const int bz=static_cast<int>(std::floor(next.z));
        const int below=static_cast<int>(std::floor(next.y-0.126));
        if(e.velocity.y<=0.0 && collidableAt(world,bx,below,bz)){
            next.y=static_cast<double>(below+1)+0.125;
            e.onGround=true;
            e.velocity.y*=-0.5;
        }
        e.position=next;
        float friction=0.98F;
        if(e.onGround) friction=0.588F; // stone/default slipperiness 0.6 * 0.98
        e.velocity.x*=static_cast<double>(friction);
        e.velocity.y*=0.9800000190734863;
        e.velocity.z*=static_cast<double>(friction);

        if(e.age%25==0) shouldMerge=true;
        if(e.pickupDelay==0&&!player.dead()){
            const Aabb pb=player.bounds();
            const glm::dvec3 center=(pb.minimum+pb.maximum)*0.5;
            const glm::dvec3 d=e.position-center;
            if(std::abs(d.x)<=1.0&&std::abs(d.z)<=1.0&&std::abs(d.y)<=1.0){
                ItemStack remaining=e.stack;
                player.inventory().addStack(remaining, items_.get(remaining.itemId).maxStackSize);
                e.stack=remaining;
                if(e.stack.empty())e.removed=true;
            }
        }
    }
    if(shouldMerge) mergeNearby();
    clearRemoved();
}

void ItemEntitySystem::clearRemoved() {
    entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const ItemEntity& e){return e.removed;}),entities_.end());
}
