#include "player/BlockInteraction.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <array>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemRegistry.hpp"
#include "items/ItemStack.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/Player.hpp"
#include "rendering/WorldRenderer.hpp"
#include "survival/MiningRules.hpp"
#include "world/ItemEntitySystem.hpp"
#include "world/DynamicBlockSystem.hpp"
#include "world/RedstoneSystem.hpp"
#include "world/World.hpp"

namespace {
bool samePosition(const glm::ivec3& a,const glm::ivec3& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool sameBreakingItem(const ItemStack& a,const ItemStack& b){
    if(a.empty()&&b.empty()) return true; if(a.empty()||b.empty()) return false;
    const auto stats=SurvivalRules::toolStats(a.itemId);
    return a.itemId==b.itemId && a.nbt==b.nbt && (stats.maxUses>0 || a.damage==b.damage);
}
}

void BlockInteraction::commitEdit(World& world,LightingEngine& lighting,WorldRenderer& renderer,const glm::ivec3& p,BlockState state){
    const BlockState old=world.getBlock(p.x,p.y,p.z); world.setBlock(p.x,p.y,p.z,state); blockEntities_.blockChanged(world,p,old,state);
    dynamicBlocks_.neighborChanged(world,p);
    constexpr std::array<glm::ivec3,6> around{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    redstone_.neighborChanged(world,p);
    for (const glm::ivec3& d : around) { dynamicBlocks_.neighborChanged(world,p+d); redstone_.neighborChanged(world,p+d); }
    const auto changes=lighting.blockChangedSync(p.x,p.y,p.z); renderer.blockChangedSync(p.x,p.y,p.z,changes);
}
void BlockInteraction::applyPlan(World& world,LightingEngine& lighting,WorldRenderer& renderer,const PlacementPlan& plan,bool notify){
    std::vector<glm::ivec3> changed; changed.reserve(plan.changes.size());
    for(const auto& e:plan.changes){if(world.getBlock(e.position.x,e.position.y,e.position.z)==e.state)continue;commitEdit(world,lighting,renderer,e.position,e.state);changed.push_back(e.position);}
    if(!notify||changed.empty())return;
    const auto added=placement_.onBlockAdded(world,changed); if(!added.empty()){PlacementPlan p;p.consumeItem=false;p.changes=added;applyPlan(world,lighting,renderer,p,false);}
    const auto reactions=placement_.neighborReactions(world,changed); if(!reactions.empty()){PlacementPlan p;p.consumeItem=false;p.changes=reactions;applyPlan(world,lighting,renderer,p,false);}
}

void BlockInteraction::destroyBlock(World& world,LightingEngine& lighting,WorldRenderer& renderer,Player& player,const glm::ivec3& pos,BlockState old,bool creative){
    if(blockId(old)==0) return;
    if(!creative){
        ItemStack& held=player.inventory().selected();
        const bool harvest=SurvivalRules::canHarvest(old,held);
        if(auto drop=SurvivalRules::primaryDrop(old,items_,harvest)){
            itemEntities_.spawn(*drop, glm::dvec3(pos) + glm::dvec3(0.5, 0.35, 0.5));
        }
        if(BlockRegistry::get(old).hardness!=0.0F) { const ToolStats stats=SurvivalRules::toolStats(held.itemId); if(stats.toolClass!=ToolClass::None&&stats.toolClass!=ToolClass::Hoe) static_cast<void>(SurvivalRules::damageTool(held, stats.toolClass == ToolClass::Sword ? 2 : 1)); }
    }
    PlacementPlan p;p.consumeItem=false;p.changes.push_back({pos,makeBlockState(static_cast<std::uint16_t>(BlockId::Air))});applyPlan(world,lighting,renderer,p);
    soundEvents_.push_back({BlockSoundEventType::Break,pos,old});
}

void BlockInteraction::tick(World& world,LightingEngine& lighting,WorldRenderer& renderer,Player& player,const glm::vec3& look,bool attacking,bool usingBlock){
    if(useDelay_>0)--useDelay_; if(attackDelay_>0)--attackDelay_;
    const float reach=player.gameMode()==GameMode::Creative?5.0F:4.5F;
    const auto hit=raycastBlocks(world,player.eyePosition(),look,reach);

    if(usingBlock&&useDelay_==0&&hit){
        ItemStack specialEjected;
        if(blockEntities_.useSpecial(world,*hit,player,specialEjected)){
            if(!specialEjected.empty()) itemEntities_.spawn(specialEjected, glm::dvec3(hit->block) + glm::dvec3(0.5,0.8,0.5));
            useDelay_=4;
        }
        else if(const auto action=blockEntities_.activate(world,*hit)){pendingBlockEntityAction_=action;useDelay_=4;}
        else if(const auto activation=placement_.activation(world,player,look,*hit)){applyPlan(world,lighting,renderer,*activation);useDelay_=4;}
        else {ItemStack& held=player.inventory().selected();const ItemStack placed=held;if(const auto plan=placement_.placement(world,player,look,*hit,held)){
            applyPlan(world,lighting,renderer,*plan);for(const auto& e:plan->changes){blockEntities_.placedFromItem(e.position,e.state,placed);const BlockId id=static_cast<BlockId>(blockId(e.state));if((id==BlockId::StandingSign||id==BlockId::WallSign)&&!pendingBlockEntityAction_)pendingBlockEntityAction_=BlockEntityAction{BlockEntityActionType::EditSign,e.position};}
            if(plan->consumeItem&&player.gameMode()!=GameMode::Creative)held.shrink(1);useDelay_=4;}}
    }

    if(!attacking||!hit||player.dead()){breakingBlock_.reset();breakingItem_.clear();breakProgress_=0;stepSoundTickCounter_=0;return;}
    if(attackDelay_>0)return;
    if(player.gameMode()==GameMode::Creative){if(SurvivalRules::toolStats(player.inventory().selected().itemId).toolClass==ToolClass::Sword)return;destroyBlock(world,lighting,renderer,player,hit->block,hit->state,true);attackDelay_=5;breakingBlock_.reset();breakProgress_=0;return;}

    const ItemStack& held=player.inventory().selected();
    if(!breakingBlock_||!samePosition(*breakingBlock_,hit->block)||!sameBreakingItem(breakingItem_,held)){
        breakingBlock_=hit->block;breakingItem_=held;breakProgress_=0;stepSoundTickCounter_=0;
    }
    const BlockDefinition& definition=BlockRegistry::get(hit->state);if(definition.hardness<0)return;
    const MiningResult mining=SurvivalRules::mining(hit->state,held,definition.hardness);
    breakProgress_+=mining.relativeHardness;
    if(std::fmod(stepSoundTickCounter_,4.0F)==0.0F) soundEvents_.push_back({BlockSoundEventType::Hit,hit->block,hit->state});
    stepSoundTickCounter_+=1.0F;
    if(breakProgress_>=1.0F){destroyBlock(world,lighting,renderer,player,hit->block,hit->state,false);breakingBlock_.reset();breakingItem_.clear();breakProgress_=0;stepSoundTickCounter_=0;attackDelay_=5;}
}
