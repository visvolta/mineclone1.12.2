#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "world/RuntimeBlockEntity.hpp"

class BlockEntitySystem;
class World;

namespace BlockEntityLogic {

// Feature-specific ticking kept outside the manager. The manager owns lookup,
// lifecycle and dispatch; these functions own the actual block behaviour.
void tickFurnace(World& world, RuntimeBlockEntity& entity, std::vector<glm::ivec3>& changedBlocks);
void tickHopper(World& world, BlockEntitySystem& system, RuntimeBlockEntity& entity);
void tickBrewing(RuntimeBlockEntity& entity);
void tickBeacon(const World& world, RuntimeBlockEntity& entity);
void tickSpawner(RuntimeBlockEntity& entity, std::uint64_t stableKey);

} // namespace BlockEntityLogic
