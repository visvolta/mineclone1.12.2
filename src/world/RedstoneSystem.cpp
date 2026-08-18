#include "world/RedstoneSystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "blocks/BlockRegistry.hpp"
#include "player/Player.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/Chunk.hpp"
#include "world/ItemEntitySystem.hpp"
#include "world/World.hpp"

namespace {
constexpr std::array<glm::ivec3, 6> six{{
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
}};
constexpr std::array<glm::ivec3, 4> horizontal{{
    {1,0,0},{-1,0,0},{0,0,1},{0,0,-1}
}};

BlockId idOf(BlockState s) { return static_cast<BlockId>(blockId(s)); }
bool isWire(BlockState s) { return idOf(s) == BlockId::RedstoneWire; }
bool isButton(BlockId id) { return id == BlockId::StoneButton || id == BlockId::WoodenButton; }
bool isPlate(BlockId id) {
    return id == BlockId::StonePressurePlate || id == BlockId::WoodenPressurePlate ||
           id == BlockId::LightWeightedPressurePlate || id == BlockId::HeavyWeightedPressurePlate;
}
bool isRepeater(BlockId id) { return id == BlockId::UnpoweredRepeater || id == BlockId::PoweredRepeater; }
bool isComparator(BlockId id) { return id == BlockId::UnpoweredComparator || id == BlockId::PoweredComparator; }
bool isPiston(BlockId id) { return id == BlockId::Piston || id == BlockId::StickyPiston; }
bool isDoor(BlockId id) {
    return id == BlockId::WoodenDoor || id == BlockId::IronDoor || id == BlockId::SpruceDoor ||
           id == BlockId::BirchDoor || id == BlockId::JungleDoor || id == BlockId::AcaciaDoor ||
           id == BlockId::DarkOakDoor;
}
bool isGate(BlockId id) {
    return id == BlockId::FenceGate || id == BlockId::SpruceFenceGate || id == BlockId::BirchFenceGate ||
           id == BlockId::JungleFenceGate || id == BlockId::AcaciaFenceGate || id == BlockId::DarkOakFenceGate;
}
bool isPoweredRail(BlockId id) { return id == BlockId::GoldenRail || id == BlockId::ActivatorRail; }

bool same(const glm::ivec3& a, const glm::ivec3& b) { return a.x==b.x && a.y==b.y && a.z==b.z; }

glm::ivec3 cardinalDirection(std::uint8_t meta) {
    switch (meta & 7U) {
        case 0: return {0,-1,0};
        case 1: return {0,1,0};
        case 2: return {0,0,-1};
        case 3: return {0,0,1};
        case 4: return {-1,0,0};
        case 5: return {1,0,0};
        default: return {0,0,0};
    }
}

glm::ivec3 horizontalDirection(std::uint8_t meta) {
    switch (meta & 3U) {
        case 0: return {0,0,1};   // south
        case 1: return {-1,0,0};  // west
        case 2: return {0,0,-1};  // north
        case 3: return {1,0,0};   // east
        default: return {0,0,0};
    }
}

glm::ivec3 leverAttachment(std::uint8_t meta) {
    switch (meta & 7U) {
        case 1: return {-1,0,0};
        case 2: return {1,0,0};
        case 3: return {0,0,-1};
        case 4: return {0,0,1};
        case 5: case 6: return {0,-1,0};
        case 0: case 7: return {0,1,0};
        default: return {0,0,0};
    }
}

glm::ivec3 buttonAttachment(std::uint8_t meta) {
    switch (meta & 7U) {
        case 0: return {0,1,0};
        case 1: return {-1,0,0};
        case 2: return {1,0,0};
        case 3: return {0,0,-1};
        case 4: return {0,0,1};
        case 5: return {0,-1,0};
        default: return {0,0,0};
    }
}

bool replaceableForPiston(BlockId id) {
    return id == BlockId::Air || id == BlockId::Fire || id == BlockId::TallGrass || id == BlockId::DeadBush ||
           id == BlockId::SnowLayer || id == BlockId::FlowingWater || id == BlockId::Water ||
           id == BlockId::FlowingLava || id == BlockId::Lava;
}

bool immovableForPiston(BlockId id) {
    return id == BlockId::Bedrock || id == BlockId::Obsidian || id == BlockId::Barrier ||
           id == BlockId::PistonExtension || id == BlockId::PistonHead || id == BlockId::EndPortal ||
           id == BlockId::EndPortalFrame || id == BlockId::EndGateway || id == BlockId::StructureBlock;
}

int clampPower(int value) { return std::clamp(value, 0, 15); }
}

std::uint64_t RedstoneSystem::key(const glm::ivec3& p) {
    const std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.x) + 0x2000000LL) & 0x3FFFFFFULL;
    const std::uint64_t z = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.z) + 0x2000000LL) & 0x3FFFFFFULL;
    return (x << 38U) | (z << 12U) | (static_cast<std::uint64_t>(p.y) & 0xFFFULL);
}

void RedstoneSystem::markAround(const glm::ivec3& p) {
    const auto add = [&](const glm::ivec3& q) {
        if (q.y < 0 || q.y >= chunkHeight) return;
        const std::uint64_t k = key(q);
        if (dirtyKeys_.insert(k).second) dirtyPositions_.push_back(q);
    };
    add(p);
    for (const auto& d : six) add(p + d);
    for (const auto& d : horizontal) {
        add(p + d + glm::ivec3(0,1,0));
        add(p + d + glm::ivec3(0,-1,0));
    }
}

void RedstoneSystem::registerRecurring(const World& world, const glm::ivec3& p) {
    if (p.y < 0 || p.y >= chunkHeight) return;
    const BlockId id = idOf(world.getBlock(p.x,p.y,p.z));
    const bool recurring = isPlate(id) || id == BlockId::DetectorRail ||
        id == BlockId::DaylightDetector || id == BlockId::DaylightDetectorInverted ||
        isComparator(id) || id == BlockId::TrappedChest;
    if (!recurring) return;
    const std::uint64_t k = key(p);
    if (recurringKeys_.insert(k).second) recurringPositions_.push_back(p);
}

void RedstoneSystem::scanLoadedWorld(const World& world) {
    for (const auto& [unused, chunk] : world.chunks()) {
        (void)unused;
        if (chunk) scanChunk(world, chunk->x(), chunk->z());
    }
}

void RedstoneSystem::scanChunk(const World& world, int chunkX, int chunkZ) {
    const Chunk* chunk = world.findChunk(chunkX, chunkZ);
    if (!chunk) return;
    for (int y=0; y<chunkHeight; ++y) for (int z=0; z<16; ++z) for (int x=0; x<16; ++x) {
        const BlockId id = idOf(chunk->get(x,y,z));
        if (id == BlockId::RedstoneWire || id == BlockId::Lever || isButton(id) || isPlate(id) ||
            id == BlockId::RedstoneTorch || id == BlockId::UnlitRedstoneTorch ||
            id == BlockId::RedstoneLamp || id == BlockId::LitRedstoneLamp || isRepeater(id) ||
            isComparator(id) || id == BlockId::Observer || id == BlockId::DaylightDetector ||
            id == BlockId::DaylightDetectorInverted || isPiston(id) || isDoor(id) ||
            id == BlockId::Trapdoor || id == BlockId::IronTrapdoor || isGate(id) ||
            id == BlockId::Dispenser || id == BlockId::Dropper || id == BlockId::Hopper ||
            id == BlockId::GoldenRail || id == BlockId::DetectorRail || id == BlockId::ActivatorRail) {
            const glm::ivec3 p{chunkX*16+x,y,chunkZ*16+z};
            markAround(p);
            registerRecurring(world,p);
        }
    }
}

void RedstoneSystem::neighborChanged(const World& world, const glm::ivec3& position) {
    registerRecurring(world, position);
    for (const auto& d : six) {
        registerRecurring(world, position + d);
        const glm::ivec3 observer = position + d;
        const BlockState s = world.getBlock(observer.x, observer.y, observer.z);
        if (idOf(s) != BlockId::Observer) continue;
        const glm::ivec3 observed = observer + cardinalDirection(blockMetadata(s));
        if (same(observed, position)) observerTriggerKeys_.insert(key(observer));
    }
    markAround(position);
}

void RedstoneSystem::schedule(const glm::ivec3& p, DelayedKind kind, int delay,
                              BlockState expected, bool targetPowered) {
    delayed_.push(DelayedUpdate{gameTime_ + static_cast<std::uint64_t>(std::max(delay,1)), sequence_++, p, kind, expected, targetPowered});
}

bool RedstoneSystem::set(World& world, const glm::ivec3& p, BlockState state,
                         std::vector<glm::ivec3>& changed) {
    if (p.y < 0 || p.y >= chunkHeight) return false;
    if (world.getBlock(p.x,p.y,p.z) == state) return false;
    world.setBlock(p.x,p.y,p.z,state);
    changed.push_back(p);
    neighborChanged(world,p);
    return true;
}

bool RedstoneSystem::canProvidePower(BlockState state) const {
    const BlockId id = idOf(state);
    return id == BlockId::RedstoneBlock || id == BlockId::Lever || isButton(id) || isPlate(id) ||
           id == BlockId::RedstoneWire || id == BlockId::RedstoneTorch || isRepeater(id) ||
           isComparator(id) || id == BlockId::Observer || id == BlockId::DaylightDetector ||
           id == BlockId::DaylightDetectorInverted || id == BlockId::DetectorRail;
}

int RedstoneSystem::comparatorOutputAt(const glm::ivec3& p) const {
    const auto it = comparatorOutputs_.find(key(p));
    return it == comparatorOutputs_.end() ? 0 : it->second;
}

int RedstoneSystem::directPower(const World& world, const glm::ivec3& source,
                                const glm::ivec3& toward,
                                const BlockEntitySystem* blockEntities) const {
    const BlockState state = world.getBlock(source.x,source.y,source.z);
    const BlockId id = idOf(state);
    const std::uint8_t meta = blockMetadata(state);
    if (id == BlockId::RedstoneBlock) return 15;
    if (id == BlockId::Lever && (meta & 8U)) return 15;
    if (isButton(id) && (meta & 8U)) return 15;
    if (id == BlockId::StonePressurePlate || id == BlockId::WoodenPressurePlate) return (meta & 1U) ? 15 : 0;
    if (id == BlockId::LightWeightedPressurePlate || id == BlockId::HeavyWeightedPressurePlate) return meta & 15U;
    if (id == BlockId::RedstoneWire) return meta & 15U;
    if (id == BlockId::RedstoneTorch) {
        const glm::ivec3 attachment = source + leverAttachment(meta); // same legacy support directions except standing aliases
        return same(toward, attachment) ? 0 : 15;
    }
    if (id == BlockId::PoweredRepeater) {
        const glm::ivec3 front = source + horizontalDirection(meta);
        return same(front,toward) ? 15 : 0;
    }
    if (id == BlockId::PoweredComparator || id == BlockId::UnpoweredComparator) {
        const glm::ivec3 front = source + horizontalDirection(meta);
        return same(front,toward) ? comparatorOutputAt(source) : 0;
    }
    if (id == BlockId::Observer && (meta & 8U)) {
        const glm::ivec3 observed = cardinalDirection(meta);
        const glm::ivec3 output = source - observed;
        return same(output,toward) ? 15 : 0;
    }
    if (id == BlockId::DaylightDetector || id == BlockId::DaylightDetectorInverted) return meta & 15U;
    if (id == BlockId::DetectorRail && (meta & 8U)) return 15;
    if (blockEntities != nullptr && id == BlockId::TrappedChest) {
        const RuntimeBlockEntity* entity = blockEntities->find(source);
        if (entity != nullptr) return std::clamp(entity->viewers,0,15);
    }
    return 0;
}

int RedstoneSystem::getWeakPower(const World& world, const glm::ivec3& source,
                                 const glm::ivec3& toward,
                                 const BlockEntitySystem* blockEntities) const {
    return directPower(world,source,toward,blockEntities);
}

int RedstoneSystem::getStrongPower(const World& world, const glm::ivec3& source,
                                   const glm::ivec3& toward,
                                   const BlockEntitySystem* blockEntities) const {
    const BlockState state = world.getBlock(source.x,source.y,source.z);
    const BlockId id = idOf(state);
    const std::uint8_t meta = blockMetadata(state);
    const int weak = directPower(world,source,toward,blockEntities);
    if (weak == 0) return 0;
    if (id == BlockId::Lever) return same(source + leverAttachment(meta), toward) ? 15 : 0;
    if (isButton(id)) return same(source + buttonAttachment(meta), toward) ? 15 : 0;
    if (isPlate(id)) return toward.y < source.y ? weak : 0;
    if (id == BlockId::RedstoneTorch) return toward.y > source.y ? 15 : 0;
    if (id == BlockId::PoweredRepeater || isComparator(id) || id == BlockId::Observer) return weak;
    if (id == BlockId::RedstoneBlock) return 15;
    return 0;
}

int RedstoneSystem::maxNeighborPower(const World& world, const glm::ivec3& p,
                                     const BlockEntitySystem* blockEntities,
                                     bool includeWires) const {
    int power=0;
    for (const auto& d:six) {
        const glm::ivec3 q=p+d;
        if (!includeWires && isWire(world.getBlock(q.x,q.y,q.z))) continue;
        power=std::max(power,directPower(world,q,p,blockEntities));
    }
    return clampPower(power);
}

bool RedstoneSystem::isBlockPowered(const World& world, const glm::ivec3& p,
                                    const BlockEntitySystem* blockEntities) const {
    return maxNeighborPower(world,p,blockEntities,true)>0;
}

int RedstoneSystem::containerSignal(const World& world, const glm::ivec3& p,
                                    const BlockEntitySystem& blockEntities) const {
    const int slots=blockEntities.containerSlotCount(world,p);
    if(slots<=0) return 0;
    double fullness=0.0; int occupied=0;
    for(int i=0;i<slots;++i){
        const ItemStack& stack=blockEntities.containerSlot(world,p,i);
        if(stack.empty()) continue;
        ++occupied;
        fullness += static_cast<double>(stack.count)/64.0;
    }
    if(occupied==0) return 0;
    return std::clamp(1 + static_cast<int>(std::floor((fullness/static_cast<double>(slots))*14.0)),1,15);
}

int RedstoneSystem::comparatorInput(const World& world, const glm::ivec3& p,
                                    BlockState state, const BlockEntitySystem& blockEntities) const {
    const glm::ivec3 front=horizontalDirection(blockMetadata(state));
    const glm::ivec3 rear=p-front;
    int input=directPower(world,rear,p,&blockEntities);
    input=std::max(input,containerSignal(world,rear,blockEntities));
    return clampPower(input);
}

bool RedstoneSystem::railPowered(const World& world, const glm::ivec3& p,
                                  const BlockEntitySystem& blockEntities, int depth) const {
    if (depth < 0) return false;
    const BlockState state = world.getBlock(p.x,p.y,p.z);
    const BlockId id = idOf(state);
    if (!isPoweredRail(id)) return false;
    if (maxNeighborPower(world,p,&blockEntities,true) > 0) return true;
    if (depth == 0) return false;
    for (const glm::ivec3& d : horizontal) {
        for (int dy=-1; dy<=1; ++dy) {
            const glm::ivec3 q = p + d + glm::ivec3(0,dy,0);
            const BlockId qid = idOf(world.getBlock(q.x,q.y,q.z));
            if (qid != id) continue;
            if (railPowered(world,q,blockEntities,depth-1)) return true;
        }
    }
    return false;
}

bool RedstoneSystem::repeaterLocked(const World& world, const glm::ivec3& p,
                                    BlockState state, const BlockEntitySystem& blockEntities) const {
    const glm::ivec3 front=horizontalDirection(blockMetadata(state));
    const glm::ivec3 sideA{front.z,0,-front.x};
    const glm::ivec3 sideB{-front.z,0,front.x};
    const auto poweredRepeaterAt=[&](const glm::ivec3& q){
        const BlockState s=world.getBlock(q.x,q.y,q.z); const BlockId id=idOf(s);
        if(id!=BlockId::PoweredRepeater && id!=BlockId::PoweredComparator) return false;
        return directPower(world,q,p,&blockEntities)>0;
    };
    return poweredRepeaterAt(p+sideA)||poweredRepeaterAt(p+sideB);
}

bool RedstoneSystem::pistonCanMove(const World& world, const BlockEntitySystem& blockEntities,
                                   const glm::ivec3& p, const glm::ivec3& dir,
                                   std::vector<glm::ivec3>& line) const {
    line.clear();
    for(int n=1;n<=13;++n){
        const glm::ivec3 q=p+dir*n;
        if(q.y<0||q.y>=chunkHeight) return false;
        const BlockId id=idOf(world.getBlock(q.x,q.y,q.z));
        if(replaceableForPiston(id)) return true;
        if(immovableForPiston(id) || blockEntities.find(q)!=nullptr) return false;
        if(n>12) return false;
        line.push_back(q);
    }
    return false;
}

void RedstoneSystem::extendPiston(World& world, BlockEntitySystem& blockEntities,
                                  const glm::ivec3& p, BlockState state,
                                  std::vector<glm::ivec3>& changed){
    if(blockMetadata(state)&8U) return;
    const glm::ivec3 dir=cardinalDirection(blockMetadata(state));
    if(dir==glm::ivec3(0)) return;
    std::vector<glm::ivec3> line;
    if(!pistonCanMove(world,blockEntities,p,dir,line)) return;
    std::vector<std::pair<glm::ivec3, BlockState>> moving;
    moving.reserve(line.size());
    for (const glm::ivec3& from : line)
        moving.emplace_back(from + dir, world.getBlock(from.x,from.y,from.z));
    for (const glm::ivec3& from : line) set(world,from,makeBlockState(0),changed);
    for (const auto& [to,moved] : moving) {
        const BlockState movingState=makeBlockState(static_cast<std::uint16_t>(BlockId::PistonExtension),blockMetadata(state)&7U);
        set(world,to,movingState,changed);
        pistonMotions_[key(to)] = PistonMotion{p,to,moved,true,false};
        schedule(to,DelayedKind::PistonRefresh,1,movingState);
    }
    const glm::ivec3 front=p+dir;
    const std::uint8_t headMeta=static_cast<std::uint8_t>((blockMetadata(state)&7U) | (idOf(state)==BlockId::StickyPiston?8U:0U));
    set(world,front,makeBlockState(static_cast<std::uint16_t>(BlockId::PistonHead),headMeta),changed);
    set(world,p,makeBlockState(static_cast<std::uint16_t>(idOf(state)),static_cast<std::uint8_t>(blockMetadata(state)|8U)),changed);
}

void RedstoneSystem::retractPiston(World& world, BlockEntitySystem& blockEntities,
                                   const glm::ivec3& p, BlockState state,
                                   std::vector<glm::ivec3>& changed){
    if((blockMetadata(state)&8U)==0U) return;
    const glm::ivec3 dir=cardinalDirection(blockMetadata(state));
    const glm::ivec3 front=p+dir;
    if(idOf(world.getBlock(front.x,front.y,front.z))==BlockId::PistonHead)
        set(world,front,makeBlockState(0),changed);
    if(idOf(state)==BlockId::StickyPiston){
        const glm::ivec3 pull=front+dir; const BlockState pulled=world.getBlock(pull.x,pull.y,pull.z); const BlockId pid=idOf(pulled);
        if(!replaceableForPiston(pid)&&!immovableForPiston(pid)&&blockEntities.find(pull)==nullptr){
            set(world,pull,makeBlockState(0),changed);
            const BlockState movingState=makeBlockState(static_cast<std::uint16_t>(BlockId::PistonExtension),blockMetadata(state)&7U);
            set(world,front,movingState,changed);
            pistonMotions_[key(front)] = PistonMotion{p,front,pulled,true,true};
            schedule(front,DelayedKind::PistonRefresh,1,movingState);
        }
    }
    set(world,p,makeBlockState(static_cast<std::uint16_t>(idOf(state)),static_cast<std::uint8_t>(blockMetadata(state)&7U)),changed);
}

void RedstoneSystem::fireDispenser(World& /*world*/, BlockEntitySystem& blockEntities,
                                   ItemEntitySystem* itemEntities, const glm::ivec3& p,
                                   BlockState state){
    RuntimeBlockEntity* entity=blockEntities.find(p); if(!entity) return;
    int chosen=-1;
    for(int i=0;i<9;++i){if(!entity->inventory[static_cast<std::size_t>(i)].empty()){chosen=i;break;}}
    if(chosen<0) return;
    ItemStack out=entity->inventory[static_cast<std::size_t>(chosen)]; out.count=1;
    entity->inventory[static_cast<std::size_t>(chosen)].shrink(1);
    const glm::ivec3 dir=cardinalDirection(blockMetadata(state));
    const glm::dvec3 center=glm::dvec3(p)+glm::dvec3(0.5)+glm::dvec3(dir)*0.65;
    const double speed=idOf(state)==BlockId::Dispenser?0.20:0.12;
    if (itemEntities != nullptr) itemEntities->spawn(out,center,glm::dvec3(dir)*speed+glm::dvec3(0.0,0.08,0.0));
}

void RedstoneSystem::processDelayed(World& world, BlockEntitySystem& blockEntities,
                                    ItemEntitySystem* itemEntities,
                                    std::vector<glm::ivec3>& changed){
    while(!delayed_.empty()&&delayed_.top().due<=gameTime_){
        const DelayedUpdate u=delayed_.top();delayed_.pop();
        const BlockState state=world.getBlock(u.position.x,u.position.y,u.position.z);
        const BlockId id=idOf(state);
        switch(u.kind){
            case DelayedKind::ButtonRelease:
                if(isButton(id)&&(blockMetadata(state)&8U)) set(world,u.position,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(blockMetadata(state)&7U)),changed);
                break;
            case DelayedKind::RepeaterTransition:
                if(isRepeater(id)){
                    if(repeaterLocked(world,u.position,state,blockEntities)) break;
                    const BlockId next=u.targetPowered?BlockId::PoweredRepeater:BlockId::UnpoweredRepeater;
                    set(world,u.position,makeBlockState(static_cast<std::uint16_t>(next),blockMetadata(state)),changed);
                }
                break;
            case DelayedKind::ComparatorRefresh: markAround(u.position); break;
            case DelayedKind::TorchRefresh: markAround(u.position); break;
            case DelayedKind::LampOff:
                if(id==BlockId::LitRedstoneLamp&&!isBlockPowered(world,u.position,&blockEntities))
                    set(world,u.position,makeBlockState(static_cast<std::uint16_t>(BlockId::RedstoneLamp),blockMetadata(state)),changed);
                break;
            case DelayedKind::ObserverOff:
                if(id==BlockId::Observer&&(blockMetadata(state)&8U))
                    set(world,u.position,makeBlockState(static_cast<std::uint16_t>(BlockId::Observer),static_cast<std::uint8_t>(blockMetadata(state)&7U)),changed);
                break;
            case DelayedKind::PistonRefresh: {
                const auto motion = pistonMotions_.find(key(u.position));
                if (motion != pistonMotions_.end()) {
                    if (idOf(state) == BlockId::PistonExtension && motion->second.hasMovedState)
                        set(world,u.position,motion->second.movedState,changed);
                    pistonMotions_.erase(motion);
                } else markAround(u.position);
                break;
            }
            case DelayedKind::DispenserFire:
                if((id==BlockId::Dispenser||id==BlockId::Dropper)&&isBlockPowered(world,u.position,&blockEntities))
                    fireDispenser(world,blockEntities,itemEntities,u.position,state);
                break;
        }
    }
}

void RedstoneSystem::updateWireNetwork(World& world, BlockEntitySystem& blockEntities,
                                       const std::vector<glm::ivec3>& seeds,
                                       std::vector<glm::ivec3>& changed){
    std::vector<glm::ivec3> queue;
    std::unordered_set<std::uint64_t> queued;
    auto enqueueWire = [&](const glm::ivec3& q) {
        if (q.y < 0 || q.y >= chunkHeight) return;
        if (idOf(world.getBlock(q.x,q.y,q.z)) != BlockId::RedstoneWire) return;
        const std::uint64_t k = key(q);
        if (queued.insert(k).second) queue.push_back(q);
    };
    for (const glm::ivec3& seed : seeds) {
        enqueueWire(seed);
        for (const auto& d : horizontal) {
            enqueueWire(seed+d);
            enqueueWire(seed+d+glm::ivec3(0,1,0));
            enqueueWire(seed+d+glm::ivec3(0,-1,0));
        }
    }

    std::size_t cursor = 0;
    std::size_t safety = 0;
    while (cursor < queue.size() && safety++ < 131072U) {
        const glm::ivec3 p = queue[cursor++];
        queued.erase(key(p));
        const BlockState current = world.getBlock(p.x,p.y,p.z);
        if (idOf(current) != BlockId::RedstoneWire) continue;
        int desired = maxNeighborPower(world,p,&blockEntities,false);
        for (const auto& d : horizontal) {
            for (int dy=-1; dy<=1; ++dy) {
                const glm::ivec3 q=p+d+glm::ivec3(0,dy,0);
                const BlockState qs=world.getBlock(q.x,q.y,q.z);
                if (isWire(qs)) desired=std::max(desired,static_cast<int>(blockMetadata(qs)&15U)-1);
            }
        }
        desired=clampPower(desired);
        if (static_cast<int>(blockMetadata(current)&15U) == desired) continue;
        world.setBlock(p.x,p.y,p.z,makeBlockState(static_cast<std::uint16_t>(BlockId::RedstoneWire),static_cast<std::uint8_t>(desired)));
        changed.push_back(p);
        neighborChanged(world,p);
        for (const auto& d : horizontal) {
            enqueueWire(p+d);
            enqueueWire(p+d+glm::ivec3(0,1,0));
            enqueueWire(p+d+glm::ivec3(0,-1,0));
        }
    }
}

void RedstoneSystem::updateBlock(World& world, BlockEntitySystem& blockEntities,
                                 ItemEntitySystem* itemEntities, const Player& player,
                                 double dayTime, const glm::ivec3& p,
                                 std::vector<glm::ivec3>& changed){
    (void)itemEntities;
    const BlockState state=world.getBlock(p.x,p.y,p.z); const BlockId id=idOf(state); const std::uint8_t meta=blockMetadata(state);
    if(id==BlockId::Air) return;

    if(isButton(id)&& (meta&8U)){
        schedule(p,DelayedKind::ButtonRelease,id==BlockId::WoodenButton?30:20,state); return;
    }
    if(isPlate(id)){
        bool playerOn=std::floor(player.feetPosition().x)==p.x && std::floor(player.feetPosition().z)==p.z && std::abs(player.feetPosition().y-(p.y+0.0625))<1.2;
        int count=playerOn?1:0;
        if(id!=BlockId::StonePressurePlate){
            if (itemEntities != nullptr) for(const ItemEntity& e:itemEntities->entities()) if(!e.removed&&std::floor(e.position.x)==p.x&&std::floor(e.position.z)==p.z&&e.position.y>=p.y&&e.position.y<p.y+1.0)++count;
        }
        int power=0;
        if(id==BlockId::StonePressurePlate||id==BlockId::WoodenPressurePlate) power=count>0?15:0;
        else if(id==BlockId::LightWeightedPressurePlate) power=std::min(count,15);
        else power=count<=0?0:std::min(15,(count+9)/10);
        const std::uint8_t next=(id==BlockId::StonePressurePlate||id==BlockId::WoodenPressurePlate)?static_cast<std::uint8_t>(power>0):static_cast<std::uint8_t>(power);
        if(meta!=next) set(world,p,makeBlockState(static_cast<std::uint16_t>(id),next),changed);
        return;
    }
    if(id==BlockId::RedstoneTorch||id==BlockId::UnlitRedstoneTorch){
        const glm::ivec3 support=p+leverAttachment(meta);
        const bool shouldOff = directPower(world, support, p, &blockEntities) > 0 ||
                               isBlockPowered(world, support, &blockEntities);
        const bool lit=id==BlockId::RedstoneTorch;
        if(lit==shouldOff) schedule(p,DelayedKind::TorchRefresh,2,state,!shouldOff);
        // Execute refresh synchronously after the 2-tick guard has elapsed by checking previous power.
        const bool was=previousPower_[key(p)]; previousPower_[key(p)]=shouldOff;
        if(was==shouldOff && lit==shouldOff){
            const BlockId next=shouldOff?BlockId::UnlitRedstoneTorch:BlockId::RedstoneTorch;
            set(world,p,makeBlockState(static_cast<std::uint16_t>(next),meta),changed);
        }
        return;
    }
    if(id==BlockId::RedstoneLamp||id==BlockId::LitRedstoneLamp){
        const bool powered=isBlockPowered(world,p,&blockEntities);
        if(powered&&id==BlockId::RedstoneLamp) set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::LitRedstoneLamp),meta),changed);
        else if(!powered&&id==BlockId::LitRedstoneLamp) schedule(p,DelayedKind::LampOff,4,state);
        return;
    }
    if(isRepeater(id)){
        if(repeaterLocked(world,p,state,blockEntities)) return;
        const glm::ivec3 front=horizontalDirection(meta); const glm::ivec3 rear=p-front;
        const bool input=directPower(world,rear,p,&blockEntities)>0;
        const bool powered=id==BlockId::PoweredRepeater;
        if(input!=powered){const int delay=((meta>>2U)&3U)+1;schedule(p,DelayedKind::RepeaterTransition,delay*2,state,input);} return;
    }
    if(isComparator(id)){
        const glm::ivec3 front=horizontalDirection(meta); const glm::ivec3 sideA{front.z,0,-front.x}; const glm::ivec3 sideB{-front.z,0,front.x};
        const int rear=comparatorInput(world,p,state,blockEntities);
        const int side=std::max(directPower(world,p+sideA,p,&blockEntities),directPower(world,p+sideB,p,&blockEntities));
        const bool subtract=(meta&4U)!=0U; const int output=subtract?std::max(rear-side,0):(rear>=side?rear:0);
        const std::uint64_t k=key(p); const int old=comparatorOutputAt(p); comparatorOutputs_[k]=output;
        const bool powered=id==BlockId::PoweredComparator; const bool should=output>0;
        if(powered!=should) set(world,p,makeBlockState(static_cast<std::uint16_t>(should?BlockId::PoweredComparator:BlockId::UnpoweredComparator),meta),changed);
        if(old!=output) markAround(p);
        return;
    }
    if(id==BlockId::Observer){
        // Only a change on the observed face starts the two-tick output pulse.
        const std::uint64_t k = key(p);
        if (observerTriggerKeys_.erase(k) != 0U && (meta&8U)==0U) {
            set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Observer),static_cast<std::uint8_t>(meta|8U)),changed);
            schedule(p,DelayedKind::ObserverOff,2,state);
        }
        return;
    }
    if(id==BlockId::DaylightDetector||id==BlockId::DaylightDetectorInverted){
        const double phase=std::fmod(dayTime,24000.0)/24000.0;
        const double sun=std::max(0.0,std::cos((phase-0.25)*2.0*3.14159265358979323846));
        int power=std::min<int>(15,world.getSkyLight(p.x,p.y+1,p.z));
        power=static_cast<int>(std::round(power*sun)); if(id==BlockId::DaylightDetectorInverted) power=15-power;
        power=clampPower(power);
        if (static_cast<int>(meta & 15U) != power)
            set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(power)),changed);
        return;
    }
    if(isPiston(id)){
        const bool powered=isBlockPowered(world,p,&blockEntities); const bool extended=(meta&8U)!=0U;
        if(powered&&!extended) extendPiston(world,blockEntities,p,state,changed); else if(!powered&&extended) retractPiston(world,blockEntities,p,state,changed); return;
    }
    if(isDoor(id)){
        glm::ivec3 lower=p; if(meta&8U) --lower.y; const BlockState lowerState=world.getBlock(lower.x,lower.y,lower.z); if(idOf(lowerState)!=id) return;
        const bool powered=isBlockPowered(world,lower,&blockEntities)||isBlockPowered(world,lower+glm::ivec3(0,1,0),&blockEntities);
        const bool open=(blockMetadata(lowerState)&4U)!=0U; if(powered!=open)set(world,lower,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(blockMetadata(lowerState)^4U)),changed); return;
    }
    if(id==BlockId::Trapdoor||id==BlockId::IronTrapdoor||isGate(id)){
        const bool powered=isBlockPowered(world,p,&blockEntities); const bool open=(meta&4U)!=0U;
        if (powered != open)
            set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(meta^4U)),changed);
        return;
    }
    if(id==BlockId::Dispenser||id==BlockId::Dropper){
        const bool powered=isBlockPowered(world,p,&blockEntities); const bool was=previousPower_[key(p)]; previousPower_[key(p)]=powered;
        if (powered && !was) schedule(p,DelayedKind::DispenserFire,4,state,true);
        return;
    }
    if(id==BlockId::Hopper){
        const bool powered=isBlockPowered(world,p,&blockEntities); const bool disabled=(meta&8U)!=0U;
        if (powered != disabled)
            set(world,p,makeBlockState(static_cast<std::uint16_t>(BlockId::Hopper),static_cast<std::uint8_t>((meta&7U)|(powered?8U:0U))),changed);
        return;
    }
    if(id==BlockId::DetectorRail){
        bool occupied=std::floor(player.feetPosition().x)==p.x&&std::floor(player.feetPosition().z)==p.z&&std::abs(player.feetPosition().y-p.y)<1.5;
        if (itemEntities != nullptr) for(const ItemEntity& e:itemEntities->entities())if(!e.removed&&std::floor(e.position.x)==p.x&&std::floor(e.position.z)==p.z&&std::abs(e.position.y-p.y)<1.0)occupied=true;
        const bool old=(meta&8U)!=0U;if(old!=occupied)set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>((meta&7U)|(occupied?8U:0U))),changed);return;
    }
    if(isPoweredRail(id)){
        const bool powered=railPowered(world,p,blockEntities,8); const bool old=(meta&8U)!=0U;
        if (old != powered)
            set(world,p,makeBlockState(static_cast<std::uint16_t>(id),static_cast<std::uint8_t>((meta&7U)|(powered?8U:0U))),changed);
        return;
    }
}

void RedstoneSystem::updateDirty(World& world, BlockEntitySystem& blockEntities,
                                 ItemEntitySystem* itemEntities, const Player& player,
                                 double dayTime,
                                 std::vector<glm::ivec3>& changed){
    // Dynamic blocks can modify many neighbours in one tick. Snapshot dirty work
    // so new notifications naturally run on the next pass rather than recursing.
    std::vector<glm::ivec3> work; work.swap(dirtyPositions_); dirtyKeys_.clear();
    for(const glm::ivec3& p:work) updateBlock(world,blockEntities,itemEntities,player,dayTime,p,changed);
    updateWireNetwork(world,blockEntities,work,changed);
}

std::vector<glm::ivec3> RedstoneSystem::tick(
    World& world, BlockEntitySystem& blockEntities, ItemEntitySystem* itemEntities,
    const Player& player, double dayTime){
    ++gameTime_;
    std::vector<glm::ivec3> changed;
    processDelayed(world,blockEntities,itemEntities,changed);

    // Sensor/comparator positions are registered as chunks and blocks change,
    // avoiding a full 16x256x16 scan of every loaded chunk every game tick.
    for (const glm::ivec3& p : recurringPositions_) {
        const BlockId id = idOf(world.getBlock(p.x,p.y,p.z));
        if (isPlate(id) || id==BlockId::DetectorRail || id==BlockId::DaylightDetector ||
            id==BlockId::DaylightDetectorInverted || isComparator(id) || id==BlockId::TrappedChest)
            markAround(p);
    }
    updateDirty(world,blockEntities,itemEntities,player,dayTime,changed);
    return changed;
}
