#include <cassert>
#include <cmath>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "entity/EntityManager.hpp"
#include "entity/EntitySerialization.hpp"
#include "player/Player.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

namespace { BlockState b(BlockId id,std::uint8_t m=0){return makeBlockState(static_cast<std::uint16_t>(id),m);} }

int main(){
    World world; (void)world.ensureChunk(0,0); (void)world.ensureChunk(1,0);
    Player player({12.5,2.0,12.5});
    EntityManager entities;

    // 14A: stable runtime identity, UUID, interpolation and chunk membership.
    auto& boat=entities.spawnBoat({1.0,3.0,1.0});
    const EntityId boatId=boat.id; const EntityUuid boatUuid=boat.uuid;
    boat.velocity={0.2,0.0,0.0}; entities.tick(world,player);
    assert(entities.find(boatId)!=nullptr); assert(entities.find(boatId)->uuid==boatUuid);
    const glm::dvec3 half=boat.interpolatedPosition(0.5F);
    assert(half.x>1.0&&half.x<boat.position.x);

    // 14B: falling blocks are real moving entities and place their block on landing.
    world.setBlock(4,0,4,b(BlockId::Stone));
    auto& falling=entities.spawnFallingBlock(b(BlockId::Sand),{4.5,5.0,4.5});
    const EntityId fallingId=falling.id;
    for(int i=0;i<120&&entities.find(fallingId);++i) entities.tick(world,player);
    assert(entities.find(fallingId)==nullptr);
    bool sandFound=false; for(int y=1;y<=5;++y) sandFound|=static_cast<BlockId>(blockId(world.getBlock(4,y,4)))==BlockId::Sand;
    assert(sandFound);

    // Primed TNT owns its fuse as an entity rather than DynamicBlockSystem state.
    world.setBlock(7,2,7,b(BlockId::Stone));
    auto& tnt=entities.spawnTnt({7.5,2.0,7.5},2); const EntityId tntId=tnt.id;
    entities.tick(world,player); entities.tick(world,player);
    assert(entities.find(tntId)==nullptr); assert(blockId(world.getBlock(7,2,7))==0);

    // 14C: XP orbs seek the player and award XP.
    const int beforeXp=player.experienceTotal();
    auto& orb=entities.spawnXpOrb(player.feetPosition()+glm::dvec3(0.3,0.5,0.0),7); const EntityId orbId=orb.id;
    for(int i=0;i<8&&entities.find(orbId);++i)entities.tick(world,player);
    assert(entities.find(orbId)==nullptr); assert(player.experienceTotal()>=beforeXp+7);

    // Arrows use projectile gravity and embed on collision.
    world.setBlock(10,1,10,b(BlockId::Stone));
    auto& arrow=entities.spawnArrow({10.5,3.0,10.5},{0.0,-0.4,0.0}); const EntityId arrowId=arrow.id;
    for(int i=0;i<12;++i)entities.tick(world,player);
    auto* arrowAfter=dynamic_cast<EntityArrow*>(entities.find(arrowId));
    assert(arrowAfter!=nullptr); assert(arrowAfter->inGround);

    // Minecarts remain rail-bound and preserve horizontal motion.
    world.setBlock(2,1,8,b(BlockId::Rail)); world.setBlock(3,1,8,b(BlockId::Rail));
    auto& cart=entities.spawnMinecart({2.5,2.0,8.5}); cart.velocity={0.15,0.0,0.0};
    const double cartX=cart.position.x; entities.tick(world,player); assert(cart.position.x>cartX);

    // Vanilla-style entity NBT roundtrip keeps type/UUID/motion/fuse data.
    EntityTNTPrimed sample; sample.uuid={123,456}; sample.position={18.5,4.0,1.5}; sample.velocity={0.1,0.2,0.3}; sample.fuse=37;
    const auto encoded=EntitySerialization::encode(sample,nullptr);
    auto decoded=EntitySerialization::decode(encoded,nullptr);
    auto* decodedTnt=dynamic_cast<EntityTNTPrimed*>(decoded.get());
    assert(decodedTnt&&decodedTnt->uuid==sample.uuid&&decodedTnt->fuse==37);
    assert(std::abs(decodedTnt->velocity.z-0.3)<1e-9);

    // Chunk detach/restore is the streaming persistence boundary.
    Chunk chunk(1,0); auto& persistent=entities.spawnTnt({18.5,6.0,2.5},55); const EntityUuid persistentUuid=persistent.uuid;
    entities.detachChunk(chunk); assert(!chunk.entityNbt().empty());
    entities.restoreChunk(chunk); bool restored=false;
    for(const auto&e:entities.entities()) if(e&&e->uuid==persistentUuid){auto*x=dynamic_cast<EntityTNTPrimed*>(e.get());restored=x&&x->fuse==55;}
    assert(restored);

    std::cout<<"Stage 14 entity-world tests passed.\n";
    return 0;
}
