#include "player/BlockInteraction.hpp"

#include <algorithm>
#include <vector>

#include "blocks/BlockRegistry.hpp"
#include "items/ItemStack.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/Player.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/World.hpp"

namespace {

bool samePosition(const glm::ivec3& a, const glm::ivec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

void BlockInteraction::commitEdit(World& world, LightingEngine& lighting,
                                  WorldRenderer& renderer, const glm::ivec3& position,
                                  BlockState state) {
    world.setBlock(position.x, position.y, position.z, state);
    const std::vector<LightingChange> changes =
        lighting.blockChangedSync(position.x, position.y, position.z);
    renderer.blockChangedSync(position.x, position.y, position.z, changes);
}

void BlockInteraction::applyPlan(World& world, LightingEngine& lighting,
                                 WorldRenderer& renderer, const PlacementPlan& plan,
                                 bool notifyNeighbors) {
    std::vector<glm::ivec3> changed;
    changed.reserve(plan.changes.size());
    for (const PlannedBlockChange& edit : plan.changes) {
        if (world.getBlock(edit.position.x, edit.position.y, edit.position.z) == edit.state) continue;
        commitEdit(world, lighting, renderer, edit.position, edit.state);
        changed.push_back(edit.position);
    }

    if (!notifyNeighbors || changed.empty()) return;

    const std::vector<PlannedBlockChange> added = placement_.onBlockAdded(world, changed);
    if (!added.empty()) {
        PlacementPlan addedPlan;
        addedPlan.consumeItem = false;
        addedPlan.changes = added;
        applyPlan(world, lighting, renderer, addedPlan, false);
    }

    // World#setBlockState(..., 11) ultimately invokes neighbor notifications.
    // Keep the dispatch outside the interaction controller: Stage 6 currently
    // handles attachment/survival reactions and leaves redstone/tick observers
    // to later systems on the same hook.
    const std::vector<PlannedBlockChange> reactions = placement_.neighborReactions(world, changed);
    if (reactions.empty()) return;
    PlacementPlan reactionPlan;
    reactionPlan.consumeItem = false;
    reactionPlan.changes = reactions;
    applyPlan(world, lighting, renderer, reactionPlan, false);
}

void BlockInteraction::removeBlock(World& world, LightingEngine& lighting,
                                   WorldRenderer& renderer, const glm::ivec3& position) {
    PlacementPlan plan;
    plan.consumeItem = false;
    plan.changes.push_back({position, makeBlockState(static_cast<std::uint16_t>(BlockId::Air))});
    applyPlan(world, lighting, renderer, plan);
}

void BlockInteraction::tick(World& world, LightingEngine& lighting, WorldRenderer& renderer,
                            Player& player, const glm::vec3& lookDirection,
                            bool attacking, bool usingBlock) {
    if (useDelay_ > 0) --useDelay_;
    const float reach = player.gameMode() == GameMode::Creative ? 5.0F : 4.5F;
    const auto hit = raycastBlocks(world, player.eyePosition(), lookDirection, reach);

    if (usingBlock && useDelay_ == 0 && hit) {
        // PlayerControllerMP first offers the clicked block an activation before
        // falling through to the held ItemStack's onItemUse path.
        if (const auto activation = placement_.activation(world, player, lookDirection, *hit)) {
            applyPlan(world, lighting, renderer, *activation);
            useDelay_ = 4;
        } else {
            ItemStack& held = player.inventory().selected();
            if (const auto plan = placement_.placement(world, player, lookDirection, *hit, held)) {
                applyPlan(world, lighting, renderer, *plan);
                if (plan->consumeItem && player.gameMode() != GameMode::Creative) held.shrink(1);
                useDelay_ = 4;
            }
        }
    }

    if (!attacking || !hit) {
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (attackDelay_ > 0) {
        --attackDelay_;
        return;
    }

    if (player.gameMode() == GameMode::Creative) {
        removeBlock(world, lighting, renderer, hit->block);
        attackDelay_ = 5;
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        return;
    }

    if (!breakingBlock_ || !samePosition(*breakingBlock_, hit->block)) {
        breakingBlock_ = hit->block;
        breakProgress_ = 0.0F;
    }

    const BlockDefinition& definition = BlockRegistry::get(hit->state);
    if (definition.hardness < 0.0F) return;
    const float divisor = definition.requiresTool ? 100.0F : 30.0F;
    breakProgress_ += 1.0F / std::max(definition.hardness, 1.0e-6F) / divisor;
    if (breakProgress_ >= 1.0F) {
        removeBlock(world, lighting, renderer, hit->block);
        breakingBlock_.reset();
        breakProgress_ = 0.0F;
        attackDelay_ = 5;
    }
}
