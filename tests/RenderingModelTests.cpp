#include <cassert>
#include <iostream>

#include "blocks/BlockRegistry.hpp"
#include "rendering/BlockRenderPath.hpp"
#include "rendering/BlockRenderResources.hpp"

int main() {
    const auto& chest=BlockRegistry::get(makeBlockState(static_cast<std::uint16_t>(BlockId::Chest)));
    assert(!chest.opaque && !chest.fullCube && chest.lightOpacity==0);
    const auto& trapped=BlockRegistry::get(makeBlockState(static_cast<std::uint16_t>(BlockId::TrappedChest)));
    assert(!trapped.opaque && !trapped.fullCube);
    assert(blockRenderPath(BlockId::Chest)==BlockRenderPath::BlockEntityRenderer);
    assert(blockRenderPath(BlockId::Bed)==BlockRenderPath::BlockEntityRenderer);
    BlockRenderResources resources(BLOCKCRAFT_ASSET_ROOT);
    assert(resources.models().hasBlockState("minecraft:stone_slab"));
    assert(resources.models().hasBlockState("minecraft:oak_stairs"));
    const BlockModelManager& models = resources.models();
    const BakedBlockModel* stoneItem = models.itemModel("stone");
    const BakedBlockModel* slabItem = models.itemModel("stone_slab");
    assert(stoneItem != nullptr && !stoneItem->quads.empty());
    assert(slabItem != nullptr && !slabItem->quads.empty());
    std::cout << "Rendering/model parity tests passed.\n";
}
