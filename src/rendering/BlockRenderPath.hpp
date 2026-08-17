#pragma once

#include <cstdint>
#include <string_view>

#include "blocks/BlockRegistry.hpp"

enum class BlockRenderPath : std::uint8_t {
    JsonModel,
    CustomFluid,
    IntentionallyInvisible,
    StaticCustomRenderer,
    BlockEntityRenderer
};

[[nodiscard]] constexpr BlockRenderPath blockRenderPath(BlockId id) noexcept {
    switch (id) {
        case BlockId::FlowingWater:
        case BlockId::Water:
        case BlockId::FlowingLava:
        case BlockId::Lava:
            return BlockRenderPath::CustomFluid;

        case BlockId::Air:
        case BlockId::Barrier:
        case BlockId::StructureVoid:
            return BlockRenderPath::IntentionallyInvisible;

        case BlockId::WhiteShulkerBox:
        case BlockId::OrangeShulkerBox:
        case BlockId::MagentaShulkerBox:
        case BlockId::LightBlueShulkerBox:
        case BlockId::YellowShulkerBox:
        case BlockId::LimeShulkerBox:
        case BlockId::PinkShulkerBox:
        case BlockId::GrayShulkerBox:
        case BlockId::SilverShulkerBox:
        case BlockId::CyanShulkerBox:
        case BlockId::PurpleShulkerBox:
        case BlockId::BlueShulkerBox:
        case BlockId::BrownShulkerBox:
        case BlockId::GreenShulkerBox:
        case BlockId::RedShulkerBox:
        case BlockId::BlackShulkerBox:
            return BlockRenderPath::BlockEntityRenderer;

        case BlockId::Bed:
        case BlockId::PistonExtension:
        case BlockId::Chest:
        case BlockId::StandingSign:
        case BlockId::WallSign:
        case BlockId::EndPortal:
        case BlockId::EnderChest:
        case BlockId::Skull:
        case BlockId::TrappedChest:
        case BlockId::StandingBanner:
        case BlockId::WallBanner:
        case BlockId::EndGateway:
            return BlockRenderPath::BlockEntityRenderer;

        default:
            return BlockRenderPath::JsonModel;
    }
}

[[nodiscard]] constexpr BlockRenderPath blockRenderPath(BlockState state) noexcept {
    return blockRenderPath(static_cast<BlockId>(blockId(state)));
}

[[nodiscard]] constexpr std::string_view blockRenderPathName(BlockRenderPath path) noexcept {
    switch (path) {
        case BlockRenderPath::JsonModel: return "json_model";
        case BlockRenderPath::CustomFluid: return "custom_fluid";
        case BlockRenderPath::IntentionallyInvisible: return "intentionally_invisible";
        case BlockRenderPath::StaticCustomRenderer: return "static_custom_renderer";
        case BlockRenderPath::BlockEntityRenderer: return "block_entity_renderer";
    }
    return "unknown";
}
