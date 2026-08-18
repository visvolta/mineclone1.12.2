#include "entity/Entity.hpp"
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

#include "blocks/BlockRegistry.hpp"
#include "blocks/BlockShape.hpp"
#include "items/ItemRegistry.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

namespace {
bool collidable(const World& world, const glm::dvec3& p) {
    const int x=static_cast<int>(std::floor(p.x));
    const int y=static_cast<int>(std::floor(p.y));
    const int z=static_cast<int>(std::floor(p.z));
    return !BlockShapes::collision(world, world.getBlock(x,y,z), x,y,z).empty();
}
BlockId blockAt(const World& world,const glm::dvec3& p){return static_cast<BlockId>(blockId(world.getBlock(static_cast<int>(std::floor(p.x)),static_cast<int>(std::floor(p.y)),static_cast<int>(std::floor(p.z)))));}
void simpleGravityMove(Entity& e,World& world,double gravity,double drag){
    e.velocity.y-=gravity;
    glm::dvec3 next=e.position+e.velocity;
    e.onGround=false;
    glm::dvec3 foot(next.x,next.y-0.01,next.z);
    if(e.velocity.y<=0.0&&collidable(world,foot)){next.y=std::floor(next.y)+1.001;e.velocity.y=0.0;e.onGround=true;}
    e.position=next;
    e.velocity.x*=drag;e.velocity.y*=drag;e.velocity.z*=drag;
    if(e.onGround){e.velocity.x*=0.6;e.velocity.z*=0.6;}
}
}

bool EntityAabb::intersects(const EntityAabb& o) const{return maximum.x>o.minimum.x&&minimum.x<o.maximum.x&&maximum.y>o.minimum.y&&minimum.y<o.maximum.y&&maximum.z>o.minimum.z&&minimum.z<o.maximum.z;}
EntityAabb Entity::bounds() const { const double hw=static_cast<double>(width)*0.5; return {{position.x-hw,position.y,position.z-hw},{position.x+hw,position.y+height,position.z+hw}}; }
glm::dvec3 Entity::interpolatedPosition(float p) const { const double a=std::clamp(static_cast<double>(p),0.0,1.0); return previousPosition+(position-previousPosition)*a; }
int Entity::chunkX() const { return static_cast<int>(std::floor(position.x/16.0)); }
int Entity::chunkZ() const { return static_cast<int>(std::floor(position.z/16.0)); }
void Entity::tick(World&,Player&,const ItemRegistry*){previousPosition=position;previousYaw=yaw;previousPitch=pitch;++ticksExisted;}

void EntityItem::tick(World& world,Player& player,const ItemRegistry* items){
    Entity::tick(world,player,items); if(stack.empty()){removed=true;return;} if(pickupDelay>0&&pickupDelay!=32767)--pickupDelay; if(age!=-32768)++age; if(age>=6000){removed=true;return;}
    velocity.y-=0.03999999910593033;
    const BlockId here=blockAt(world,position); if(here==BlockId::Lava||here==BlockId::FlowingLava){velocity.y=0.2;velocity.x*=0.2;velocity.z*=0.2;}
    glm::dvec3 next=position+velocity; onGround=false; const glm::dvec3 foot(next.x,next.y-0.126,next.z);
    if(velocity.y<=0.0&&collidable(world,foot)){next.y=std::floor(next.y)+1.125;onGround=true;velocity.y*=-0.5;}
    position=next; const double friction=onGround?0.588:0.98; velocity.x*=friction;velocity.y*=0.9800000190734863;velocity.z*=friction;
    if(pickupDelay==0&&!player.dead()) { const auto pb=player.bounds(); const glm::dvec3 c=(pb.minimum+pb.maximum)*0.5; const glm::dvec3 d=position-c; if(std::abs(d.x)<=1.0&&std::abs(d.y)<=1.0&&std::abs(d.z)<=1.0){ ItemStack remaining=stack; player.inventory().addStack(remaining,items ? items->get(remaining.itemId).maxStackSize : 64); stack=remaining; if(stack.empty()) removed=true; }}
}

void EntityFallingBlock::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);++fallTime;if(blockState==0){removed=true;return;} simpleGravityMove(*this,world,0.04,0.98); if(onGround){const int x=static_cast<int>(std::floor(position.x)),y=static_cast<int>(std::floor(position.y)),z=static_cast<int>(std::floor(position.z)); if(blockId(world.getBlock(x,y,z))==0) world.setBlock(x,y,z,blockState); removed=true;} if(fallTime>600||position.y<0)removed=true;}

void EntityTNTPrimed::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);simpleGravityMove(*this,world,0.04,0.98);if(--fuse>0)return;removed=true;const int r=static_cast<int>(std::ceil(explosionRadius));const glm::ivec3 c(static_cast<int>(std::floor(position.x)),static_cast<int>(std::floor(position.y)),static_cast<int>(std::floor(position.z)));for(int x=-r;x<=r;++x)for(int y=-r;y<=r;++y)for(int z=-r;z<=r;++z){if(x*x+y*y+z*z>r*r)continue;const glm::ivec3 p=c+glm::ivec3(x,y,z);const BlockId id=static_cast<BlockId>(blockId(world.getBlock(p.x,p.y,p.z)));if(id!=BlockId::Air&&id!=BlockId::Bedrock&&id!=BlockId::Obsidian)world.setBlock(p.x,p.y,p.z,makeBlockState(0));}}

void EntityXPOrb::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);++age;if(pickupDelay>0)--pickupDelay;if(age>=6000){removed=true;return;}glm::dvec3 target=player.feetPosition()+glm::dvec3(0,0.9,0);glm::dvec3 d=target-position;const double dist=glm::length(d);if(dist<8.0&&dist>0.001)velocity = velocity + d/dist*((1.0-dist/8.0)*0.1);simpleGravityMove(*this,world,0.03,0.98);if(pickupDelay==0&&dist<1.25){player.addExperience(xpValue);removed=true;}}

void EntityArrow::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);++life;if(shake>0)--shake;if(inGround){if(life>1200)removed=true;return;}velocity.y-=0.05;const glm::dvec3 next=position+velocity;if(collidable(world,next)){position=next;velocity=glm::dvec3(0.0);inGround=true;shake=7;return;}position=next;velocity*=0.99;if(blockAt(world,position)==BlockId::Water||blockAt(world,position)==BlockId::FlowingWater)velocity*=0.6;}

void EntityBoat::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);const BlockId here=blockAt(world,position);const bool water=here==BlockId::Water||here==BlockId::FlowingWater;if(water)velocity.y+=(0.0-velocity.y)*0.2;else velocity.y-=0.04;position = position + velocity;velocity.x*=0.9;velocity.z*=0.9;velocity.y*=0.95;}

void EntityMinecart::tick(World& world,Player& player,const ItemRegistry* items){Entity::tick(world,player,items);const BlockId below=blockAt(world,position+glm::dvec3(0,-0.2,0));const bool rail=below==BlockId::Rail||below==BlockId::GoldenRail||below==BlockId::DetectorRail||below==BlockId::ActivatorRail;if(!rail)velocity.y-=0.04;else{velocity.y=0.0;position.y=std::floor(position.y)+0.0625;}position = position + velocity;velocity.x*=rail?0.96:0.95;velocity.z*=rail?0.96:0.95;velocity.y*=0.95;}
