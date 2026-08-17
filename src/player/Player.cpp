#include "player/Player.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include "blocks/BlockShape.hpp"
#include "blocks/BlockRegistry.hpp"
#include "survival/MiningRules.hpp"
#include "world/World.hpp"

namespace {

constexpr double playerWidth = 0.6;
constexpr double playerHeight = 1.8;
constexpr double eyeHeight = 1.62;
constexpr double stepHeight = 0.6;
constexpr double gravity = 0.08;
constexpr double verticalDrag = 0.9800000190734863;
constexpr double groundSlipperiness = 0.6;
constexpr double baseFriction = 0.91;
constexpr float walkSpeed = 0.1F;
constexpr float airControl = 0.02F;
constexpr float flySpeed = 0.05F;

template <typename Function>
void forEachCollision(const World& world, const Aabb& area, Function&& function) {
    constexpr double epsilon = 1.0e-7;
    const int minX = static_cast<int>(std::floor(area.minimum.x + epsilon));
    const int minY = static_cast<int>(std::floor(area.minimum.y + epsilon));
    const int minZ = static_cast<int>(std::floor(area.minimum.z + epsilon));
    const int maxX = static_cast<int>(std::floor(area.maximum.x - epsilon));
    const int maxY = static_cast<int>(std::floor(area.maximum.y - epsilon));
    const int maxZ = static_cast<int>(std::floor(area.maximum.z - epsilon));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockState state = world.getBlock(x, y, z);
                const BlockShapeSet shape = BlockShapes::collision(world, state, x, y, z);
                for (const BlockBox& box : shape) {
                    function(Aabb{{x + box.minX, y + box.minY, z + box.minZ},
                                  {x + box.maxX, y + box.maxY, z + box.maxZ}});
                }
            }
        }
    }
}

bool collides(const World& world, const Aabb& box) {
    bool result = false;
    forEachCollision(world, box, [&](const Aabb& block) { result = result || box.intersects(block); });
    return result;
}

double clipY(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{box.minimum.x, std::min(box.minimum.y, box.minimum.y + offset), box.minimum.z},
                     {box.maximum.x, std::max(box.maximum.y, box.maximum.y + offset), box.maximum.z}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.x <= box.minimum.x || block.minimum.x >= box.maximum.x ||
            block.maximum.z <= box.minimum.z || block.minimum.z >= box.maximum.z) return;
        if (offset > 0.0 && box.maximum.y <= block.minimum.y)
            offset = std::min(offset, block.minimum.y - box.maximum.y);
        else if (offset < 0.0 && box.minimum.y >= block.maximum.y)
            offset = std::max(offset, block.maximum.y - box.minimum.y);
    });
    return offset;
}

double clipX(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{std::min(box.minimum.x, box.minimum.x + offset), box.minimum.y, box.minimum.z},
                     {std::max(box.maximum.x, box.maximum.x + offset), box.maximum.y, box.maximum.z}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.y <= box.minimum.y || block.minimum.y >= box.maximum.y ||
            block.maximum.z <= box.minimum.z || block.minimum.z >= box.maximum.z) return;
        if (offset > 0.0 && box.maximum.x <= block.minimum.x)
            offset = std::min(offset, block.minimum.x - box.maximum.x);
        else if (offset < 0.0 && box.minimum.x >= block.maximum.x)
            offset = std::max(offset, block.maximum.x - box.minimum.x);
    });
    return offset;
}

double clipZ(const World& world, const Aabb& box, double offset) {
    const Aabb swept{{box.minimum.x, box.minimum.y, std::min(box.minimum.z, box.minimum.z + offset)},
                     {box.maximum.x, box.maximum.y, std::max(box.maximum.z, box.maximum.z + offset)}};
    forEachCollision(world, swept, [&](const Aabb& block) {
        if (block.maximum.x <= box.minimum.x || block.minimum.x >= box.maximum.x ||
            block.maximum.y <= box.minimum.y || block.minimum.y >= box.maximum.y) return;
        if (offset > 0.0 && box.maximum.z <= block.minimum.z)
            offset = std::min(offset, block.minimum.z - box.maximum.z);
        else if (offset < 0.0 && box.minimum.z >= block.maximum.z)
            offset = std::max(offset, block.maximum.z - box.minimum.z);
    });
    return offset;
}

struct MoveResult {
    Aabb box;
    double x;
    double y;
    double z;
};

MoveResult clippedMove(const World& world, Aabb box, double x, double y, double z) {
    y = clipY(world, box, y);
    box = box.moved(0.0, y, 0.0);
    x = clipX(world, box, x);
    box = box.moved(x, 0.0, 0.0);
    z = clipZ(world, box, z);
    box = box.moved(0.0, 0.0, z);
    return {box, x, y, z};
}

} // namespace

Aabb Aabb::moved(double x, double y, double z) const {
    const glm::dvec3 offset{x, y, z};
    return {minimum + offset, maximum + offset};
}

bool Aabb::intersects(const Aabb& other) const {
    return maximum.x > other.minimum.x && minimum.x < other.maximum.x &&
           maximum.y > other.minimum.y && minimum.y < other.maximum.y &&
           maximum.z > other.minimum.z && minimum.z < other.maximum.z;
}

Player::Player(glm::dvec3 feetPosition) : position_(feetPosition), previousPosition_(feetPosition) {}

glm::vec3 Player::eyePosition() const {
    return glm::vec3(position_.x, position_.y + eyeHeight, position_.z);
}

glm::vec3 Player::interpolatedEyePosition(float partialTick) const {
    const glm::dvec3 interpolated = previousPosition_ + (position_ - previousPosition_) *
        static_cast<double>(std::clamp(partialTick, 0.0F, 1.0F));
    return glm::vec3(interpolated.x, interpolated.y + eyeHeight, interpolated.z);
}

Aabb Player::bounds() const {
    const double radius = playerWidth * 0.5;
    return {{position_.x - radius, position_.y, position_.z - radius},
            {position_.x + radius, position_.y + playerHeight, position_.z + radius}};
}

bool Player::intersectsBlock(const World& world, const glm::ivec3& block) const {
    const BlockState state = world.getBlock(block.x, block.y, block.z);
    const BlockShapeSet shape = BlockShapes::collision(world, state, block.x, block.y, block.z);
    const Aabb player = bounds();
    for (const BlockBox& box : shape) {
        if (player.intersects(Aabb{{block.x + box.minX, block.y + box.minY, block.z + box.minZ},
                                   {block.x + box.maxX, block.y + box.maxY, block.z + box.maxZ}}))
            return true;
    }
    return false;
}

void Player::toggleGameMode() {
    gameMode_ = gameMode_ == GameMode::Survival ? GameMode::Creative : GameMode::Survival;
    if (gameMode_ == GameMode::Survival) flying_ = false;
    flyToggleTimer_ = 0;
}

void Player::moveRelative(float strafe, float forward, float amount, const glm::vec3& lookDirection) {
    float lengthSquared = strafe * strafe + forward * forward;
    if (lengthSquared < 1.0e-4F) return;
    const float scale = amount / std::max(1.0F, std::sqrt(lengthSquared));
    strafe *= scale;
    forward *= scale;

    glm::vec2 forwardFlat(lookDirection.x, lookDirection.z);
    if (glm::dot(forwardFlat, forwardFlat) < 1.0e-6F) forwardFlat = glm::vec2(0.0F, -1.0F);
    forwardFlat = glm::normalize(forwardFlat);
    const glm::vec2 right(-forwardFlat.y, forwardFlat.x);
    const glm::vec2 movement = forwardFlat * forward + right * strafe;
    velocity_.x += movement.x;
    velocity_.z += movement.y;
}

void Player::moveWithCollisions(const World& world, double x, double y, double z, bool sneaking) {
    Aabb start = bounds();

    if (sneaking && onGround_) {
        constexpr double increment = 0.05;
        while (x != 0.0 && !collides(world, start.moved(x, -stepHeight, 0.0)))
            x = std::abs(x) < increment ? 0.0 : x - std::copysign(increment, x);
        while (z != 0.0 && !collides(world, start.moved(0.0, -stepHeight, z)))
            z = std::abs(z) < increment ? 0.0 : z - std::copysign(increment, z);
        while (x != 0.0 && z != 0.0 && !collides(world, start.moved(x, -stepHeight, z))) {
            x = std::abs(x) < increment ? 0.0 : x - std::copysign(increment, x);
            z = std::abs(z) < increment ? 0.0 : z - std::copysign(increment, z);
        }
    }

    const double requestedX = x;
    const double requestedY = y;
    const double requestedZ = z;
    MoveResult result = clippedMove(world, start, x, y, z);

    const bool horizontalCollision = requestedX != result.x || requestedZ != result.z;
    const bool canStep = onGround_ || (requestedY != result.y && requestedY < 0.0);
    if (stepHeight > 0.0 && canStep && horizontalCollision) {
        MoveResult stepped = clippedMove(world, start, requestedX, stepHeight, requestedZ);
        const double down = clipY(world, stepped.box, -stepHeight);
        stepped.box = stepped.box.moved(0.0, down, 0.0);
        stepped.y += down;
        if (stepped.x * stepped.x + stepped.z * stepped.z > result.x * result.x + result.z * result.z)
            result = stepped;
    }

    position_.x = (result.box.minimum.x + result.box.maximum.x) * 0.5;
    position_.y = result.box.minimum.y;
    position_.z = (result.box.minimum.z + result.box.maximum.z) * 0.5;
    onGround_ = requestedY < 0.0 && requestedY != result.y;
    if (requestedX != result.x) velocity_.x = 0.0;
    if (requestedY != result.y) velocity_.y = 0.0;
    if (requestedZ != result.z) velocity_.z = 0.0;
}

void Player::tick(const World& world, const PlayerInput& input, const glm::vec3& lookDirection) {
    previousPosition_ = position_;
    if (hurtResistantTime_ > 0) --hurtResistantTime_;
    if (flyToggleTimer_ > 0) --flyToggleTimer_;

    if (dead_) {
        velocity_ *= 0.8;
        return;
    }

    if (gameMode_ == GameMode::Creative && input.jumpPressed) {
        if (flyToggleTimer_ == 0) flyToggleTimer_ = 7;
        else {
            flying_ = !flying_;
            flyToggleTimer_ = 0;
            velocity_.y = 0.0;
        }
    }

    float strafe = input.strafe;
    float forward = input.forward;
    if (input.sneak) { strafe *= 0.3F; forward *= 0.3F; }

    if (flying_) {
        const float movementAmount = flySpeed * (input.sprint ? 2.0F : 1.0F);
        moveRelative(strafe, forward, movementAmount, lookDirection);
        if (input.sneak) velocity_.y -= flySpeed * 3.0F;
        if (input.jump) velocity_.y += flySpeed * 3.0F;
        const double previousVerticalVelocity = velocity_.y;
        moveWithCollisions(world, velocity_.x, velocity_.y, velocity_.z, false);
        velocity_.x *= baseFriction; velocity_.z *= baseFriction; velocity_.y = previousVerticalVelocity * 0.6;
        if (onGround_) flying_ = false;
        fallDistance_ = 0.0F;
    } else {
        if (input.jump && onGround_) {
            velocity_.y = 0.42F;
            if (input.sprint) {
                glm::vec2 forwardFlat(lookDirection.x, lookDirection.z);
                if (glm::dot(forwardFlat, forwardFlat) > 1.0e-6F) {
                    forwardFlat = glm::normalize(forwardFlat);
                    velocity_.x += forwardFlat.x * 0.2; velocity_.z += forwardFlat.y * 0.2;
                }
            }
        }
        const double friction = onGround_ ? groundSlipperiness * baseFriction : baseFriction;
        const float movementSpeed = walkSpeed * (input.sprint && forward >= 0.8F ? 1.3F : 1.0F);
        const float acceleration = onGround_ ? movementSpeed * static_cast<float>(0.16277136 / (friction * friction * friction))
                                             : airControl * (input.sprint ? 1.3F : 1.0F);
        moveRelative(strafe, forward, acceleration, lookDirection);
        moveWithCollisions(world, velocity_.x, velocity_.y, velocity_.z, input.sneak);
        const double fallen = previousPosition_.y - position_.y;
        if (!onGround_ && fallen > 0.0) fallDistance_ += static_cast<float>(fallen);
        if (onGround_ && fallDistance_ > 0.0F) {
            const int damage = static_cast<int>(std::ceil(fallDistance_ - 3.0F));
            if (damage > 0) hurt(static_cast<float>(damage), DamageType::Fall);
            fallDistance_ = 0.0F;
        }
        velocity_.y -= gravity; velocity_.y *= verticalDrag; velocity_.x *= friction; velocity_.z *= friction;
    }
    tickSurvival(world);
}

int Player::armorValue() const {
    int total=0; for (std::size_t i=0;i<4;++i) total += SurvivalRules::armorPoints(inventory_.armor(i).itemId); return total;
}

bool Player::hurt(float amount, DamageType type) {
    if (gameMode_ == GameMode::Creative || dead_ || amount <= 0.0F) return false;
    const bool bypassArmor = type == DamageType::Drown || type == DamageType::Void;
    if (hurtResistantTime_ > 0) return false;
    float applied = amount;
    if (!bypassArmor) {
        float toughness=0.0F; for(std::size_t i=0;i<4;++i) toughness += SurvivalRules::armorToughness(inventory_.armor(i).itemId);
        const float armor=static_cast<float>(armorValue());
        const float effective=std::clamp(armor - applied/(2.0F+toughness/4.0F), armor*0.2F, 20.0F);
        applied *= 1.0F - effective/25.0F;
        const int armorDamage=std::max(1,static_cast<int>(std::ceil(amount/4.0F)));
        static constexpr int durability[20]={55,80,75,65,165,240,225,195,165,240,225,195,363,528,495,429,77,112,105,91};
        for(std::size_t slot=0;slot<4;++slot){
            ItemStack& a=inventory_.armor(slot); if(a.empty()||a.itemId<298||a.itemId>317) continue;
            a.damage=static_cast<std::uint16_t>(a.damage+armorDamage);
            if(a.damage>=durability[a.itemId-298]) a.clear();
        }
    }
    health_=std::max(0.0F,health_-applied);
    hurtResistantTime_=20;
    if (health_<=0.0F) { health_=0.0F; dead_=true; velocity_=glm::dvec3(0.0); flying_=false; }
    return true;
}

void Player::restoreSurvival(float health, int air, int fireTicks, bool dead) {
    health_=std::clamp(health,0.0F,20.0F); air_=std::clamp(air,-20,300); fireTicks_=std::max(0,fireTicks); dead_=dead||health_<=0.0F;
}

void Player::respawn() {
    position_=respawnPosition_; previousPosition_=position_; velocity_=glm::dvec3(0.0); health_=20.0F; air_=300; fireTicks_=0;
    hurtResistantTime_=0; fireDamageTicker_=0; fallDistance_=0.0F; dead_=false; onGround_=false; flying_=false;
}

void Player::tickSurvival(const World& world) {
    if (gameMode_ == GameMode::Creative) { air_=300; fireTicks_=0; return; }
    if (position_.y < -64.0) hurt(4.0F, DamageType::Void);
    const auto at=[&](double x,double y,double z){return static_cast<BlockId>(blockId(world.getBlock(static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z)))));};
    const BlockId eye=at(position_.x,position_.y+eyeHeight,position_.z);
    const bool submerged=eye==BlockId::Water||eye==BlockId::FlowingWater;
    if(submerged){ if(--air_<=-20){air_=0;hurt(2.0F,DamageType::Drown);} } else air_=300;

    bool inLava=false,inFire=false,inCactus=false;
    const Aabb b=bounds();
    for(int y=static_cast<int>(std::floor(b.minimum.y));y<=static_cast<int>(std::floor(b.maximum.y-1e-6));++y)
      for(int z=static_cast<int>(std::floor(b.minimum.z));z<=static_cast<int>(std::floor(b.maximum.z-1e-6));++z)
       for(int x=static_cast<int>(std::floor(b.minimum.x));x<=static_cast<int>(std::floor(b.maximum.x-1e-6));++x){
        const BlockId id=static_cast<BlockId>(blockId(world.getBlock(x,y,z)));
        inLava|=id==BlockId::Lava||id==BlockId::FlowingLava; inFire|=id==BlockId::Fire; inCactus|=id==BlockId::Cactus;
       }
    if(inCactus) hurt(1.0F,DamageType::Cactus);
    if(inLava){ hurt(4.0F,DamageType::Lava); fireTicks_=std::max(fireTicks_,300); }
    if(inFire) fireTicks_=std::max(fireTicks_,160);
    if(submerged) fireTicks_=0;
    if(fireTicks_>0){ --fireTicks_; if(++fireDamageTicker_>=20){fireDamageTicker_=0;hurt(1.0F,DamageType::Fire);} }
    else fireDamageTicker_=0;
}

