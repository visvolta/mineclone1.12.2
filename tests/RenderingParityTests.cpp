#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "rendering/BlockRenderPath.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/BlockStateModelMap.hpp"

int main() {
    const BlockRenderResources resources(BLOCKCRAFT_ASSET_ROOT);
    const BlockModelManager& models = resources.models();
    const auto air = [](int, int, int) { return makeBlockState(0); };

    std::array<int, 5> paths{};
    int registered = 0;
    int checkedJsonStates = 0;

    for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
        if (!BlockRegistry::isRegisteredId(numericId)) continue;
        ++registered;
        const BlockId id = static_cast<BlockId>(numericId);
        const BlockRenderPath path = blockRenderPath(id);
        ++paths[static_cast<std::size_t>(path)];

        if (path == BlockRenderPath::IntentionallyInvisible ||
            path == BlockRenderPath::BlockEntityRenderer) {
            const BlockModelState state = resolveBlockModelState(makeBlockState(numericId, 0), air);
            const auto access = resources.models();
            assert(access.hasBlockState(state.resourceName));
            const auto selected = access.select(state, 0);
            assert(selected.size() == 1);
            assert(selected.front() != nullptr);
            assert(selected.front()->quads.empty());
            continue;
        }
        if (path == BlockRenderPath::StaticCustomRenderer) {
            const BlockModelState state = resolveBlockModelState(makeBlockState(numericId, 0), air);
            const auto access = resources.models();
            assert(access.hasBlockState(state.resourceName));
            const auto selected = access.select(state, 0);
            assert(selected.size() == 1);
            assert(selected.front() != nullptr);
            assert(!selected.front()->quads.empty());
            continue;
        }
        if (path != BlockRenderPath::JsonModel) continue;
        bool selectedState = false;
        for (std::uint8_t meta = 0; meta < 16; ++meta) {
            const BlockState state = makeBlockState(numericId, meta);
            const BlockModelState modelState = resolveBlockModelState(state, air);
            if (modelState.resourceName.empty() || !models.hasBlockState(modelState.resourceName)) continue;
            const auto selected = models.select(modelState, 0);
            if (selected.empty()) continue;
            for (const BakedBlockModel* model : selected) assert(model != nullptr);
            selectedState = true;
            ++checkedJsonStates;
        }
        assert(selectedState);
    }

    assert(registered == 254);
    assert(paths[static_cast<std::size_t>(BlockRenderPath::JsonModel)] == 219);
    assert(paths[static_cast<std::size_t>(BlockRenderPath::CustomFluid)] == 4);
    assert(paths[static_cast<std::size_t>(BlockRenderPath::IntentionallyInvisible)] == 3);
    assert(paths[static_cast<std::size_t>(BlockRenderPath::StaticCustomRenderer)] == 0);
    assert(paths[static_cast<std::size_t>(BlockRenderPath::BlockEntityRenderer)] == 28);
    assert(checkedJsonStates >= 219);

    std::cout << "Rendering parity table: 219 JSON, 4 fluid, 3 invisible, 0 static custom, 28 block-entity renderer.\n";
    return 0;
}
