#pragma once
#include <memory>
#include <vector>
#include "entity/Entity.hpp"
class ItemRegistry;
namespace EntitySerialization {
[[nodiscard]] std::vector<std::uint8_t> encode(const Entity& entity, const ItemRegistry* items);
[[nodiscard]] std::unique_ptr<Entity> decode(const std::vector<std::uint8_t>& bytes, const ItemRegistry* items);
}
