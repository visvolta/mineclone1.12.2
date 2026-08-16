#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rendering/BlockRenderPath.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/ModelLoader.hpp"
#include "rendering/TextureAtlasData.hpp"

// Immutable CPU-side rendering resources. They are loaded once from the
// extracted Minecraft 1.12.2 resource tree and are safe to read from meshing workers.
class BlockRenderResources {
public:
    class ModelAccess {
    public:
        explicit ModelAccess(const BlockModelManager& models) : models_(models) {}

        [[nodiscard]] bool hasBlockState(std::string_view resourceName) const {
            if (models_.hasBlockState(resourceName)) return true;
            const auto path = classifiedPath(resourceName);
            if (!path) return false;
            if (*path == BlockRenderPath::IntentionallyInvisible ||
                *path == BlockRenderPath::BlockEntityRenderer) return true;
            if (*path == BlockRenderPath::JsonModel) {
                throw std::runtime_error("Missing Minecraft 1.12.2 blockstate/model for classified JSON path: " +
                                         std::string(resourceName));
            }
            return false;
        }

        [[nodiscard]] std::vector<const BakedBlockModel*> select(
            const BlockModelState& state, std::int64_t positionRandom) const {
            std::vector<const BakedBlockModel*> selected = models_.select(state, positionRandom);
            if (!selected.empty()) return selected;
            const auto path = classifiedPath(state.resourceName);
            if (!path) return selected;
            if (*path == BlockRenderPath::IntentionallyInvisible ||
                *path == BlockRenderPath::BlockEntityRenderer) {
                // ENTITYBLOCK_ANIMATED and INVISIBLE are deliberate no-geometry
                // outcomes in BlockRendererDispatcher. Returning one empty baked
                // model makes ChunkMesher treat the path as handled instead of
                // dropping into its old legacy cube fallback.
                return {&emptyModel()};
            }
            if (*path == BlockRenderPath::JsonModel) {
                throw std::runtime_error("Minecraft 1.12.2 JSON blockstate selected no baked model: " +
                                         state.resourceName);
            }
            return selected;
        }

        operator const BlockModelManager&() const noexcept { return models_; }

    private:
        [[nodiscard]] static std::string_view pathPart(std::string_view name) noexcept {
            const std::size_t colon = name.find(':');
            return colon == std::string_view::npos ? name : name.substr(colon + 1);
        }

        [[nodiscard]] static std::optional<BlockRenderPath> classifiedPath(std::string_view resourceName) {
            const std::string_view requested = pathPart(resourceName);
            for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
                if (!BlockRegistry::isRegisteredId(numericId)) continue;
                if (pathPart(BlockRegistry::legacyName(numericId)) == requested)
                    return blockRenderPath(static_cast<BlockId>(numericId));
            }
            return std::nullopt;
        }

        [[nodiscard]] static const BakedBlockModel& emptyModel() noexcept {
            static const BakedBlockModel model{};
            return model;
        }

        const BlockModelManager& models_;
    };

    explicit BlockRenderResources(const std::filesystem::path& assetRoot,
                                  int maximumTextureSize = TextureAtlasData::maximumSupportedTextureSize)
        : atlas_(assetRoot, maximumTextureSize), models_(assetRoot, atlas_) {
        validateIntentionalRenderPaths();
    }

    [[nodiscard]] const TextureAtlasData& atlas() const { return atlas_; }
    [[nodiscard]] ModelAccess models() const { return ModelAccess(models_); }

private:
    void validateIntentionalRenderPaths() const {
        const auto air = [](int, int, int) { return makeBlockState(0); };
        for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
            if (!BlockRegistry::isRegisteredId(numericId)) continue;
            if (blockRenderPath(static_cast<BlockId>(numericId)) != BlockRenderPath::JsonModel) continue;

            bool resolved = false;
            for (std::uint8_t meta = 0; meta < 16 && !resolved; ++meta) {
                const BlockModelState state = resolveBlockModelState(makeBlockState(numericId, meta), air);
                if (!models_.hasBlockState(state.resourceName)) continue;
                resolved = !models_.select(state, 0).empty();
            }
            if (!resolved) {
                throw std::runtime_error(
                    "Minecraft 1.12.2 JSON render path has no baked model for block ID " +
                    std::to_string(numericId) + " (" +
                    std::string(BlockRegistry::legacyName(numericId)) + ")");
            }
        }
    }

    TextureAtlasData atlas_;
    BlockModelManager models_;
};
