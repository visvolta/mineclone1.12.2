#include "entity/EntityManager.hpp"
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

#include "items/ItemRegistry.hpp"
#include "entity/EntitySerialization.hpp"
#include "world/Chunk.hpp"
#include "player/Player.hpp"
#include "world/World.hpp"

EntityManager::EntityManager(const ItemRegistry* items):items_(items){}
void EntityManager::initialize(Entity& e){e.id=nextId_++;if(e.uuid.most==0&&e.uuid.least==0)e.uuid={random_(),random_()};e.previousPosition=e.position;}
EntityItem& EntityManager::spawnItem(ItemStack stack,glm::dvec3 p,glm::dvec3 v){auto& e=create<EntityItem>();e.stack=std::move(stack);e.position=e.previousPosition=p;if(glm::dot(v,v)<1e-12){std::uniform_real_distribution<double>d(-0.1,0.1);e.velocity={d(random_),0.20000000298023224,d(random_)};}else e.velocity=v;std::uniform_real_distribution<float>a(0,6.2831853F),y(0,360);e.hoverStart=a(random_);e.yaw=e.previousYaw=y(random_);return e;}
EntityFallingBlock& EntityManager::spawnFallingBlock(BlockState s,glm::dvec3 p){auto&e=create<EntityFallingBlock>();e.blockState=s;e.position=e.previousPosition=p;e.width=0.98F;e.height=0.98F;return e;}
EntityTNTPrimed& EntityManager::spawnTnt(glm::dvec3 p,int fuse){auto&e=create<EntityTNTPrimed>();e.position=e.previousPosition=p;e.fuse=fuse;e.width=0.98F;e.height=0.98F;std::uniform_real_distribution<double>d(-0.02,0.02);e.velocity={d(random_),0.2,d(random_)};return e;}
EntityXPOrb& EntityManager::spawnXpOrb(glm::dvec3 p,int value){auto&e=create<EntityXPOrb>();e.position=e.previousPosition=p;e.xpValue=std::max(value,1);e.width=e.height=0.5F;std::uniform_real_distribution<double>d(-0.1,0.1);e.velocity={d(random_),0.2,d(random_)};return e;}
EntityArrow& EntityManager::spawnArrow(glm::dvec3 p,glm::dvec3 v){auto&e=create<EntityArrow>();e.position=e.previousPosition=p;e.velocity=v;e.width=e.height=0.5F;return e;}
EntityBoat& EntityManager::spawnBoat(glm::dvec3 p,int type){auto&e=create<EntityBoat>();e.position=e.previousPosition=p;e.boatType=type;e.width=1.375F;e.height=0.5625F;return e;}
EntityMinecart& EntityManager::spawnMinecart(glm::dvec3 p,int type){auto&e=create<EntityMinecart>();e.position=e.previousPosition=p;e.minecartType=type;e.width=0.98F;e.height=0.7F;return e;}
void EntityManager::tick(World& world,Player& player){for(auto& e:entities_)if(e&&!e->removed&&world.findChunk(e->chunkX(),e->chunkZ()))e->tick(world,player,items_);mergeItems();clearRemoved();}
void EntityManager::clearRemoved(){entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const auto&e){return !e||e->removed;}),entities_.end());}
void EntityManager::mergeItems(){for(size_t i=0;i<entities_.size();++i){auto*a=dynamic_cast<EntityItem*>(entities_[i].get());if(!a||a->removed||a->pickupDelay==32767)continue;for(size_t j=i+1;j<entities_.size();++j){auto*b=dynamic_cast<EntityItem*>(entities_[j].get());if(!b||b->removed||b->pickupDelay==32767)continue;if(a->stack.itemId!=b->stack.itemId||a->stack.damage!=b->stack.damage||a->stack.nbt!=b->stack.nbt)continue;glm::dvec3 d=a->position-b->position;if(std::abs(d.x)>.75||std::abs(d.z)>.75||std::abs(d.y)>.25)continue;EntityItem*dst=a,*src=b;if(dst->stack.count<src->stack.count)std::swap(dst,src);const int limit=items_ ? items_->get(dst->stack.itemId).maxStackSize : 64;if(dst->stack.count+src->stack.count>limit)continue;dst->stack.count+=src->stack.count;dst->pickupDelay=std::max(dst->pickupDelay,src->pickupDelay);dst->age=std::min(dst->age,src->age);src->removed=true;}}}
Entity* EntityManager::find(EntityId id){for(auto&e:entities_)if(e&&e->id==id)return e.get();return nullptr;} const Entity* EntityManager::find(EntityId id)const{for(auto&e:entities_)if(e&&e->id==id)return e.get();return nullptr;}
std::vector<const Entity*> EntityManager::entitiesInChunk(int x,int z)const{std::vector<const Entity*> r;for(const auto&e:entities_)if(e&&!e->removed&&e->chunkX()==x&&e->chunkZ()==z)r.push_back(e.get());return r;}
void EntityManager::removeChunk(int x,int z){for(auto&e:entities_)if(e&&e->chunkX()==x&&e->chunkZ()==z)e->removed=true;clearRemoved();}

void EntityManager::restoreChunk(Chunk& chunk){
    auto stored=chunk.takeEntityNbt();
    for(auto& bytes:stored){
        auto e=EntitySerialization::decode(bytes,items_);
        if(e){ initialize(*e); entities_.push_back(std::move(e)); }
        else chunk.addEntityNbt(std::move(bytes));
    }
}
std::vector<std::vector<std::uint8_t>> EntityManager::serializeChunk(int x,int z) const{
    std::vector<std::vector<std::uint8_t>> out;
    for(const auto&e:entities_) if(e&&!e->removed&&e->chunkX()==x&&e->chunkZ()==z) out.push_back(EntitySerialization::encode(*e,items_));
    return out;
}

void EntityManager::detachChunk(Chunk& chunk){
    for(auto& bytes:serializeChunk(chunk.x(),chunk.z())) chunk.addEntityNbt(std::move(bytes));
    removeChunk(chunk.x(),chunk.z());
}
