#pragma once

#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "entity/Entity.hpp"

class Chunk;
class ItemRegistry;
class Player;
class World;

class EntityManager {
public:
    explicit EntityManager(const ItemRegistry* items = nullptr);

    EntityItem& spawnItem(ItemStack stack, glm::dvec3 position, glm::dvec3 velocity = glm::dvec3(0.0));
    EntityFallingBlock& spawnFallingBlock(BlockState state, glm::dvec3 position);
    EntityTNTPrimed& spawnTnt(glm::dvec3 position, int fuse = 80);
    EntityXPOrb& spawnXpOrb(glm::dvec3 position, int value);
    EntityArrow& spawnArrow(glm::dvec3 position, glm::dvec3 velocity);
    EntityBoat& spawnBoat(glm::dvec3 position, int type = 0);
    EntityMinecart& spawnMinecart(glm::dvec3 position, int type = 0);

    void tick(World& world, Player& player);
    void clearRemoved();
    void mergeItems();
    [[nodiscard]] Entity* find(EntityId id);
    [[nodiscard]] const Entity* find(EntityId id) const;
    [[nodiscard]] const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    [[nodiscard]] std::vector<const Entity*> entitiesInChunk(int chunkX, int chunkZ) const;
    void removeChunk(int chunkX, int chunkZ);
    void restoreChunk(Chunk& chunk);
    void detachChunk(Chunk& chunk);
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> serializeChunk(int chunkX, int chunkZ) const;

    template<class T, class... Args> T& create(Args&&... args) {
        auto entity=std::make_unique<T>(std::forward<Args>(args)...);
        initialize(*entity); T& ref=*entity; entities_.push_back(std::move(entity)); return ref;
    }

private:
    void initialize(Entity& entity);
    const ItemRegistry* items_;
    EntityId nextId_ = 1;
    std::mt19937_64 random_{0x1122BEEFCAFED00DULL};
    std::vector<std::unique_ptr<Entity>> entities_;
};
