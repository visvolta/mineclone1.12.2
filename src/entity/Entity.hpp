#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include "blocks/BlockState.hpp"
#include "items/ItemStack.hpp"

class ItemRegistry;
class Player;
class World;

using EntityId = std::uint64_t;

struct EntityUuid {
    std::uint64_t most = 0;
    std::uint64_t least = 0;
    [[nodiscard]] bool operator==(const EntityUuid&) const = default;
};

struct EntityAabb {
    glm::dvec3 minimum{0.0};
    glm::dvec3 maximum{0.0};
    [[nodiscard]] bool intersects(const EntityAabb& other) const;
};

enum class EntityType : std::uint8_t {
    Item,
    FallingBlock,
    PrimedTnt,
    ExperienceOrb,
    Arrow,
    Boat,
    Minecart
};

class Entity {
public:
    virtual ~Entity() = default;
    [[nodiscard]] virtual EntityType type() const = 0;
    [[nodiscard]] virtual std::string vanillaId() const = 0;
    virtual void tick(World& world, Player& player, const ItemRegistry* items);

    EntityId id = 0;
    EntityUuid uuid{};
    glm::dvec3 position{0.0};
    glm::dvec3 previousPosition{0.0};
    glm::dvec3 velocity{0.0};
    float yaw = 0.0F;
    float previousYaw = 0.0F;
    float pitch = 0.0F;
    float previousPitch = 0.0F;
    float width = 0.25F;
    float height = 0.25F;
    bool onGround = false;
    bool removed = false;
    int ticksExisted = 0;
    int dimension = 0;

    [[nodiscard]] EntityAabb bounds() const;
    [[nodiscard]] glm::dvec3 interpolatedPosition(float partialTick) const;
    [[nodiscard]] int chunkX() const;
    [[nodiscard]] int chunkZ() const;
};

class EntityItem final : public Entity {
public:
    ItemStack stack{};
    int age = 0;
    int pickupDelay = 10;
    int health = 5;
    float hoverStart = 0.0F;
    std::string owner;
    std::string thrower;

    [[nodiscard]] EntityType type() const override { return EntityType::Item; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:item"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityFallingBlock final : public Entity {
public:
    BlockState blockState = 0;
    int fallTime = 0;
    bool dropItem = true;
    [[nodiscard]] EntityType type() const override { return EntityType::FallingBlock; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:falling_block"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityTNTPrimed final : public Entity {
public:
    int fuse = 80;
    float explosionRadius = 4.0F;
    [[nodiscard]] EntityType type() const override { return EntityType::PrimedTnt; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:tnt"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityXPOrb final : public Entity {
public:
    int xpValue = 1;
    int age = 0;
    int pickupDelay = 0;
    int health = 5;
    [[nodiscard]] EntityType type() const override { return EntityType::ExperienceOrb; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:xp_orb"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityArrow final : public Entity {
public:
    int life = 0;
    int shake = 0;
    bool inGround = false;
    double damage = 2.0;
    [[nodiscard]] EntityType type() const override { return EntityType::Arrow; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:arrow"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityBoat final : public Entity {
public:
    int boatType = 0;
    [[nodiscard]] EntityType type() const override { return EntityType::Boat; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:boat"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};

class EntityMinecart final : public Entity {
public:
    int minecartType = 0;
    [[nodiscard]] EntityType type() const override { return EntityType::Minecart; }
    [[nodiscard]] std::string vanillaId() const override { return "minecraft:minecart"; }
    void tick(World& world, Player& player, const ItemRegistry* items) override;
};
