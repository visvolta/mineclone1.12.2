#include "world/DynamicBlockSystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "blocks/BlockRegistry.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

namespace {
constexpr std::array<glm::ivec3, 6> neighbors{{
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
}};
constexpr std::array<glm::ivec3, 4> horizontal{{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}}};
std::uint64_t posKey(const glm::ivec3& p) {
    const std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.x) + 0x2000000LL) & 0x3FFFFFFULL;
    const std::uint64_t z = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.z) + 0x2000000LL) & 0x3FFFFFFULL;
    return (x << 38U) | (z << 12U) | (static_cast<std::uint64_t>(p.y) & 0xFFFULL);
}
bool isAir(BlockState s){ return blockId(s)==0; }
bool isWater(BlockId id){ return id==BlockId::Water || id==BlockId::FlowingWater; }
bool isLava(BlockId id){ return id==BlockId::Lava || id==BlockId::FlowingLava; }
bool solidTop(BlockState s){ if(isAir(s)) return false; const auto& d=BlockRegistry::get(s); return d.fullCube; }
}

DynamicBlockSystem::DynamicBlockSystem(std::uint64_t seed)
    : random_(static_cast<std::mt19937::result_type>(seed ^ (seed >> 32U))) {}

void DynamicBlockSystem::schedule(const glm::ivec3& position, BlockState expectedState, int delayTicks, int priority) {
    if (position.y < 0 || position.y >= chunkHeight) return;
    const std::uint64_t k = posKey(position) ^ (static_cast<std::uint64_t>(blockId(expectedState)) << 1U);
    if (!scheduledKeys_.insert(k).second) return;
    scheduled_.push(ScheduledTick{gameTime_ + static_cast<std::uint64_t>(std::max(delayTicks,1)), sequence_++, position, expectedState, priority});
}

bool DynamicBlockSystem::set(World& world, const glm::ivec3& p, BlockState state, std::vector<glm::ivec3>& changed) {
    if (p.y < 0 || p.y >= chunkHeight) return false;
    if (world.getBlock(p.x,p.y,p.z)==state) return false;
    world.setBlock(p.x,p.y,p.z,state); changed.push_back(p);
    for (const auto& d:neighbors) neighborChanged(world,p+d);
    return true;
}

void DynamicBlockSystem::scanChunk(World& world, int chunkX, int chunkZ) {
    const Chunk* chunk = world.findChunk(chunkX, chunkZ);
    if (chunk == nullptr) return;
    for (int y = 0; y < chunkHeight; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
        const BlockState state = chunk->get(x,y,z);
        const BlockId id = static_cast<BlockId>(blockId(state));
        if (id == BlockId::FlowingWater || id == BlockId::Water || id == BlockId::FlowingLava || id == BlockId::Lava ||
            id == BlockId::Sand || id == BlockId::Gravel || id == BlockId::ConcretePowder || id == BlockId::Fire || id == BlockId::TNT)
            neighborChanged(world, {chunkX*16+x,y,chunkZ*16+z});
    }
}

void DynamicBlockSystem::neighborChanged(World& world, const glm::ivec3& p) {
    const BlockState state=world.getBlock(p.x,p.y,p.z); const BlockId id=static_cast<BlockId>(blockId(state));
    if (id==BlockId::FlowingWater || id==BlockId::Water) schedule(p,state,5);
    else if (id==BlockId::FlowingLava || id==BlockId::Lava) schedule(p,state,30);
    else if (id==BlockId::Sand || id==BlockId::Gravel || id==BlockId::ConcretePowder) schedule(p,state,2);
    else if (id==BlockId::Fire) schedule(p,state,30);
    else if (id==BlockId::TNT) {
        for(const auto& d:neighbors) if(static_cast<BlockId>(blockId(world.getBlock(p.x+d.x,p.y+d.y,p.z+d.z)))==BlockId::Fire){ schedule(p,state,80,-1); break; }
    }
}

bool DynamicBlockSystem::canReplace(BlockState state) const {
    const BlockId id=static_cast<BlockId>(blockId(state));
    return id==BlockId::Air || id==BlockId::TallGrass || id==BlockId::DeadBush || id==BlockId::Fire || id==BlockId::SnowLayer || id==BlockId::FlowingWater || id==BlockId::Water || id==BlockId::FlowingLava || id==BlockId::Lava;
}

void DynamicBlockSystem::tickFluid(World& world,const glm::ivec3& p,BlockState state,bool lava,std::vector<glm::ivec3>& changed){
    const BlockId flowing=lava?BlockId::FlowingLava:BlockId::FlowingWater;
    const BlockId still=lava?BlockId::Lava:BlockId::Water;
    const int delay=lava?30:5; const int level=blockMetadata(state)&7;
    const glm::ivec3 below=p+glm::ivec3(0,-1,0); const BlockState belowState=world.getBlock(below.x,below.y,below.z);
    if(canReplace(belowState) && !(lava && isWater(static_cast<BlockId>(blockId(belowState))))){
        set(world,below,makeBlockState(static_cast<std::uint16_t>(flowing),static_cast<std::uint8_t>(std::min(level+1,7))),changed);
        schedule(below,world.getBlock(below.x,below.y,below.z),delay); schedule(p,state,delay); return;
    }
    if(lava && isWater(static_cast<BlockId>(blockId(belowState)))) { set(world,below,makeBlockState(static_cast<std::uint16_t>(BlockId::Stone)),changed); }
    const int next=level+1; if(next<=7){
        for(const auto& d:horizontal){ const glm::ivec3 q=p+d; BlockState qState=world.getBlock(q.x,q.y,q.z); const BlockId qId=static_cast<BlockId>(blockId(qState));
            if(lava && isWater(qId)){ set(world,q,makeBlockState(static_cast<std::uint16_t>(level==0?BlockId::Obsidian:BlockId::Cobblestone)),changed); continue; }
            if(canReplace(qState) && !isWater(qId) && !isLava(qId)){ set(world,q,makeBlockState(static_cast<std::uint16_t>(flowing),static_cast<std::uint8_t>(next)),changed); schedule(q,world.getBlock(q.x,q.y,q.z),delay); }
        }
    }
    if(static_cast<BlockId>(blockId(state))==flowing && level==0) set(world,p,makeBlockState(static_cast<std::uint16_t>(still)),changed);
}

void DynamicBlockSystem::tickFalling(World& world,const glm::ivec3& p,BlockState state,std::vector<glm::ivec3>& changed){
    if (p.y <= 0) return;
    glm::ivec3 dest = p;
    while (dest.y > 0 && canReplace(world.getBlock(dest.x, dest.y - 1, dest.z))) --dest.y;
    if(dest.y!=p.y){ set(world,p,makeBlockState(0),changed); set(world,dest,state,changed); }
}

bool DynamicBlockSystem::hasWaterNearby(const World& world,const glm::ivec3& p,int radius) const {
    for (int x=-radius; x<=radius; ++x) for (int z=-radius; z<=radius; ++z) for (int y=0; y<=1; ++y) {
        BlockId id=static_cast<BlockId>(blockId(world.getBlock(p.x+x,p.y+y,p.z+z)));
        if (isWater(id)) return true;
    }
    return false;
}
bool DynamicBlockSystem::hasLogNearby(const World& world,const glm::ivec3& p,int radius) const {
    for (int x=-radius; x<=radius; ++x) for (int y=-radius; y<=radius; ++y) for (int z=-radius; z<=radius; ++z) {
        BlockId id=static_cast<BlockId>(blockId(world.getBlock(p.x+x,p.y+y,p.z+z)));
        if (id==BlockId::Log || id==BlockId::Log2) return true;
    }
    return false;
}

void DynamicBlockSystem::growTree(World& world,const glm::ivec3& p,BlockState sapling,std::vector<glm::ivec3>& changed){
    const int species=blockMetadata(sapling)&7; const BlockId logId=species>=4?BlockId::Log2:BlockId::Log; const BlockId leavesId=species>=4?BlockId::Leaves2:BlockId::Leaves;
    const std::uint8_t logMeta=static_cast<std::uint8_t>(species&3); const int height=4+static_cast<int>(random_()%3);
    for(int y=0;y<height;++y) set(world,p+glm::ivec3(0,y,0),makeBlockState(static_cast<std::uint16_t>(logId),logMeta),changed);
    for(int y=height-2;y<=height;++y){int r=(y==height)?1:2;for(int x=-r;x<=r;++x)for(int z=-r;z<=r;++z){if(std::abs(x)==r&&std::abs(z)==r&&y==height)continue;glm::ivec3 q=p+glm::ivec3(x,y,z);if(isAir(world.getBlock(q.x,q.y,q.z)))set(world,q,makeBlockState(static_cast<std::uint16_t>(leavesId),logMeta),changed);}}
}

bool DynamicBlockSystem::flammable(BlockState state) const {
    const BlockId id=static_cast<BlockId>(blockId(state));
    return id==BlockId::Planks||id==BlockId::Log||id==BlockId::Log2||id==BlockId::Leaves||id==BlockId::Leaves2||id==BlockId::Wool||id==BlockId::Bookshelf||id==BlockId::TNT||id==BlockId::HayBlock||id==BlockId::Fence||id==BlockId::WoodenSlab;
}

void DynamicBlockSystem::tickFire(World& world,const glm::ivec3& p,BlockState state,std::vector<glm::ivec3>& changed){
    int age=blockMetadata(state)&15; bool support=solidTop(world.getBlock(p.x,p.y-1,p.z)); bool fuel=false;for(const auto& d:neighbors)fuel|=flammable(world.getBlock(p.x+d.x,p.y+d.y,p.z+d.z));
    if(!support&&!fuel){set(world,p,makeBlockState(0),changed);return;} if(age<15 && random_()%3==0) set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Fire),static_cast<std::uint8_t>(age+1)),changed);
    for(const auto& d:neighbors){glm::ivec3 q=p+d;BlockState qState=world.getBlock(q.x,q.y,q.z);if(flammable(qState)&&random_()%8==0){if(static_cast<BlockId>(blockId(qState))==BlockId::TNT)schedule(q,qState,80,-1);else if(isAir(world.getBlock(q.x,q.y+1,q.z)))set(world,q+glm::ivec3(0,1,0),makeBlockState(static_cast<std::uint16_t>(BlockId::Fire)),changed);}}
    schedule(p,world.getBlock(p.x,p.y,p.z),30);
}

void DynamicBlockSystem::tickPlant(World& world,const glm::ivec3& p,BlockState state,std::vector<glm::ivec3>& changed){
    const BlockId id=static_cast<BlockId>(blockId(state)); const int light=std::max(world.getSkyLight(p.x,p.y+1,p.z),world.getBlockLight(p.x,p.y+1,p.z));
    if(id==BlockId::Sapling){if(light>=9 && random_()%7==0){int stage=(blockMetadata(state)>>3)&1;if(stage==0)set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Sapling),static_cast<std::uint8_t>((blockMetadata(state)&7)|8)),changed);else growTree(world,p,state,changed);}return;}
    if(id==BlockId::Wheat||id==BlockId::Carrots||id==BlockId::Potatoes){int age=blockMetadata(state)&7;if(light>=9&&age<7&&random_()%4==0)set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(age+1)),changed);return;}
    if(id==BlockId::Beetroots){int age=blockMetadata(state)&3;if(light>=9&&age<3&&random_()%5==0)set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(age+1)),changed);return;}
    if(id==BlockId::Farmland){int moisture=blockMetadata(state)&7;if(hasWaterNearby(world,p,4)){if(moisture<7)set(world,p,makeBlockState(static_cast<std::uint16_t>(id),7),changed);}else if(moisture>0)set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(moisture-1)),changed);else{BlockId above=static_cast<BlockId>(blockId(world.getBlock(p.x,p.y+1,p.z)));if(above!=BlockId::Wheat&&above!=BlockId::Carrots&&above!=BlockId::Potatoes&&above!=BlockId::Beetroots)set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Dirt)),changed);}return;}
    if(id==BlockId::Grass){if(light<4&&BlockRegistry::get(world.getBlock(p.x,p.y+1,p.z)).lightOpacity>2){set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Dirt)),changed);return;}if(light>=9)for(int n=0;n<4;++n){glm::ivec3 q=p+glm::ivec3(static_cast<int>(random_()%3)-1,static_cast<int>(random_()%5)-3,static_cast<int>(random_()%3)-1);if(static_cast<BlockId>(blockId(world.getBlock(q.x,q.y,q.z)))==BlockId::Dirt&&world.getSkyLight(q.x,q.y+1,q.z)>=4)set(world,q,makeBlockState(static_cast<std::uint16_t>(BlockId::Grass)),changed);}return;}
    if(id==BlockId::Leaves||id==BlockId::Leaves2){if(!hasLogNearby(world,p,4)&&random_()%4==0)set(world,p,makeBlockState(0),changed);return;}
    if(id==BlockId::Cactus||id==BlockId::Reeds){int column=1;while(column<3&&static_cast<BlockId>(blockId(world.getBlock(p.x,p.y-column,p.z)))==id)++column;if(column<3&&isAir(world.getBlock(p.x,p.y+1,p.z))&&random_()%8==0)set(world,p+glm::ivec3(0,1,0),makeBlockState(static_cast<std::uint16_t>(id)),changed);return;}
    if(id==BlockId::BrownMushroom||id==BlockId::RedMushroom){if(light<13&&random_()%25==0){glm::ivec3 q=p+glm::ivec3(static_cast<int>(random_()%7)-3,static_cast<int>(random_()%3)-1,static_cast<int>(random_()%7)-3);if(isAir(world.getBlock(q.x,q.y,q.z))&&solidTop(world.getBlock(q.x,q.y-1,q.z)))set(world,q,makeBlockState(static_cast<std::uint16_t>(id)),changed);}return;}
    if(id==BlockId::Ice||id==BlockId::SnowLayer){if(world.getBlockLight(p.x,p.y,p.z)>11)set(world,p,id==BlockId::Ice?makeBlockState(static_cast<std::uint16_t>(BlockId::Water)):makeBlockState(0),changed);return;}
}

void DynamicBlockSystem::explode(World& world,const glm::ivec3& c,std::vector<glm::ivec3>& changed){
    constexpr int r=4;for(int x=-r;x<=r;++x)for(int y=-r;y<=r;++y)for(int z=-r;z<=r;++z){if(x*x+y*y+z*z>r*r)continue;glm::ivec3 q=c+glm::ivec3(x,y,z);BlockState s=world.getBlock(q.x,q.y,q.z);BlockId id=static_cast<BlockId>(blockId(s));if(id==BlockId::Bedrock||id==BlockId::Obsidian||id==BlockId::Air)continue;if(id==BlockId::TNT&&q!=c)schedule(q,s,10+static_cast<int>(random_()%30),-1);set(world,q,makeBlockState(0),changed);}
}

void DynamicBlockSystem::scheduledTick(World& world,const ScheduledTick& t,std::vector<glm::ivec3>& changed){
    const BlockState state=world.getBlock(t.position.x,t.position.y,t.position.z);if(blockId(state)!=blockId(t.expectedState))return;const BlockId id=static_cast<BlockId>(blockId(state));
    if(isWater(id))tickFluid(world,t.position,state,false,changed);else if(isLava(id))tickFluid(world,t.position,state,true,changed);else if(id==BlockId::Sand||id==BlockId::Gravel||id==BlockId::ConcretePowder)tickFalling(world,t.position,state,changed);else if(id==BlockId::Fire)tickFire(world,t.position,state,changed);else if(id==BlockId::TNT){set(world,t.position,makeBlockState(0),changed);explode(world,t.position,changed);}
}

void DynamicBlockSystem::randomTick(World& world,const glm::ivec3& p,BlockState state,std::vector<glm::ivec3>& changed){
    BlockId id=static_cast<BlockId>(blockId(state));
    if(id==BlockId::Sapling||id==BlockId::Wheat||id==BlockId::Carrots||id==BlockId::Potatoes||id==BlockId::Beetroots||id==BlockId::Farmland||id==BlockId::Grass||id==BlockId::Leaves||id==BlockId::Leaves2||id==BlockId::Cactus||id==BlockId::Reeds||id==BlockId::BrownMushroom||id==BlockId::RedMushroom||id==BlockId::Ice||id==BlockId::SnowLayer)tickPlant(world,p,state,changed);
    else if(id==BlockId::Fire)schedule(p,state,30);
}

std::vector<glm::ivec3> DynamicBlockSystem::tick(World& world){
    ++gameTime_;std::vector<glm::ivec3> changed;std::size_t scheduledBudget=1024;
    while(scheduledBudget--&& !scheduled_.empty()&&scheduled_.top().due<=gameTime_){ScheduledTick t=scheduled_.top();scheduled_.pop();scheduledKeys_.erase(posKey(t.position)^(static_cast<std::uint64_t>(blockId(t.expectedState))<<1U));scheduledTick(world,t,changed);}
    // A bounded random-tick pass keeps large view distances from creating a CPU spike.
    std::size_t chunkBudget=128;for(const auto& [unused,chunk]:world.chunks()){(void)unused;if(!chunk||chunkBudget--==0)break;for(int i=0;i<12;++i){int x=static_cast<int>(random_()%16),z=static_cast<int>(random_()%16),y=static_cast<int>(random_()%chunkHeight);glm::ivec3 p{chunk->x()*16+x,y,chunk->z()*16+z};BlockState s=chunk->get(x,y,z);if(blockId(s)!=0)randomTick(world,p,s,changed);}}
    return changed;
}
