#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "blocks/BlockRegistry.hpp"

static constexpr BlockState block(BlockId id, std::uint8_t meta = 0) {
    return makeBlockState(static_cast<std::uint16_t>(id), meta);
}

int main() {
    static_assert(BlockRegistry::vanillaRegisteredBlockCount == 254);
    static_assert(static_cast<std::uint16_t>(BlockId::Air) == 0);
    static_assert(static_cast<std::uint16_t>(BlockId::ConcretePowder) == 252);
    static_assert(static_cast<std::uint16_t>(BlockId::StructureBlock) == 255);

    std::size_t count = 0;
    for (std::uint16_t id = 0; id <= 255; ++id) {
        const bool registered = BlockRegistry::isRegisteredId(id);
        if (registered) {
            ++count;
            assert(!BlockRegistry::legacyName(id).empty());
            assert(BlockRegistry::get(makeBlockState(id)).name == BlockRegistry::legacyName(id));
        }
    }
    assert(count == 254);
    assert(!BlockRegistry::isRegisteredId(253));
    assert(!BlockRegistry::isRegisteredId(254));
    assert(BlockRegistry::legacyName(253).empty());
    assert(BlockRegistry::legacyName(254).empty());

    assert(BlockRegistry::legacyName(0) == "air");
    assert(BlockRegistry::legacyName(67) == "stone_stairs");
    assert(BlockRegistry::legacyName(103) == "melon_block");
    assert(BlockRegistry::legacyName(165) == "slime");
    assert(BlockRegistry::legacyName(213) == "magma");
    assert(BlockRegistry::legacyName(252) == "concrete_powder");
    assert(BlockRegistry::legacyName(255) == "structure_block");

    for (std::uint16_t id = 0; id <= 255; ++id) {
        if (!BlockRegistry::isRegisteredId(id)) continue;
        for (std::uint8_t metadata = 0; metadata < 16; ++metadata) {
            const BlockState state = makeBlockState(id, metadata);
            assert(blockId(state) == id);
            assert(blockMetadata(state) == metadata);
        }
    }

    const BlockState maxLegacy = makeBlockState(255, 15);
    assert(blockId(maxLegacy) == 255);
    assert(blockMetadata(maxLegacy) == 15);

    // Every state the legacy registry can expose must remain safe for the
    // current renderer even when the real vanilla model is not implemented yet.
    for (std::uint16_t id = 0; id <= 255; ++id) {
        if (!BlockRegistry::isRegisteredId(id)) continue;
        for (std::uint8_t metadata = 0; metadata < 16; ++metadata) {
            const BlockState state = makeBlockState(id, metadata);
            const BlockDefinition& definition = BlockRegistry::get(state);
            assert(!definition.name.empty());
            assert(static_cast<unsigned>(definition.layer) < 3U);
            assert(definition.lightValue <= 15U);
            for (Face face : {Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East}) {
                const auto texture = static_cast<std::uint16_t>(BlockRegistry::texture(state, face));
                assert(texture < static_cast<std::uint16_t>(TextureId::FinalCount));
            }
        }
    }

    assert(BlockRegistry::get(block(BlockId::Torch)).lightValue == 14);
    assert(BlockRegistry::get(block(BlockId::Glowstone)).lightValue == 15);
    assert(BlockRegistry::get(block(BlockId::Magma)).lightValue == 3);
    assert(BlockRegistry::get(block(BlockId::LitFurnace)).lightValue == 13);
    assert(BlockRegistry::get(block(BlockId::RedstoneTorch)).lightValue == 7);
    assert(BlockRegistry::get(block(BlockId::SeaLantern)).lightValue == 15);
    assert(BlockRegistry::get(block(BlockId::EndRod)).lightValue == 14);

    assert(BlockRegistry::get(block(BlockId::Stone)).hardness == 1.5F);
    assert(BlockRegistry::get(block(BlockId::Bedrock)).hardness < 0.0F);
    assert(BlockRegistry::get(block(BlockId::Concrete)).hardness == 1.8F);
    assert(BlockRegistry::get(block(BlockId::ConcretePowder)).hardness == 0.5F);

    std::cout << "Block registry parity checks passed.\n";
    return 0;
}
