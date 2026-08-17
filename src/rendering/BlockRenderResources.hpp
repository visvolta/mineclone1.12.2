#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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
        explicit ModelAccess(const BlockRenderResources& owner) : owner_(owner), models_(owner.models_) {}

        [[nodiscard]] bool hasBlockState(std::string_view resourceName) const {
            if (models_.hasBlockState(resourceName)) return true;
            const auto id = classifiedId(resourceName);
            if (!id) return false;
            const BlockRenderPath path = blockRenderPath(*id);
            if (path == BlockRenderPath::IntentionallyInvisible ||
                path == BlockRenderPath::BlockEntityRenderer ||
                path == BlockRenderPath::StaticCustomRenderer) return true;
            if (path == BlockRenderPath::JsonModel) {
                throw std::runtime_error("Missing Minecraft 1.12.2 blockstate/model for classified JSON path: " +
                                         std::string(resourceName));
            }
            return false;
        }

        [[nodiscard]] std::vector<const BakedBlockModel*> select(
            const BlockModelState& state, std::int64_t positionRandom) const {
            const auto id = classifiedId(state.resourceName);
            if (id && blockRenderPath(*id) == BlockRenderPath::StaticCustomRenderer)
                return {&owner_.staticCustomModel(*id)};

            std::vector<const BakedBlockModel*> selected = models_.select(state, positionRandom);
            if (!selected.empty()) {
                if (id && owner_.needsPartialModelLightingFix(*id))
                    return owner_.partialModelCopies(selected);
                return selected;
            }
            if (!id) return selected;
            const BlockRenderPath path = blockRenderPath(*id);
            if (path == BlockRenderPath::IntentionallyInvisible ||
                path == BlockRenderPath::BlockEntityRenderer) {
                // ENTITYBLOCK_ANIMATED and INVISIBLE are deliberate no-geometry
                // outcomes in BlockRendererDispatcher. Returning one empty baked
                // model makes ChunkMesher treat the path as handled instead of
                // dropping into its old legacy cube fallback.
                return {&emptyModel()};
            }
            if (path == BlockRenderPath::JsonModel) {
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

        [[nodiscard]] static std::optional<BlockId> classifiedId(std::string_view resourceName) {
            const std::string_view requested = pathPart(resourceName);
            for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
                if (!BlockRegistry::isRegisteredId(numericId)) continue;
                if (pathPart(BlockRegistry::legacyName(numericId)) == requested)
                    return static_cast<BlockId>(numericId);
            }
            return std::nullopt;
        }

        [[nodiscard]] static const BakedBlockModel& emptyModel() noexcept {
            static const BakedBlockModel model{};
            return model;
        }

        const BlockRenderResources& owner_;
        const BlockModelManager& models_;
    };

    explicit BlockRenderResources(const std::filesystem::path& assetRoot,
                                  int maximumTextureSize = TextureAtlasData::maximumSupportedTextureSize)
        : atlas_(assetRoot, maximumTextureSize), models_(assetRoot, atlas_) {
        validateIntentionalRenderPaths();
    }

    [[nodiscard]] const TextureAtlasData& atlas() const { return atlas_; }
    [[nodiscard]] ModelAccess models() const { return ModelAccess(*this); }

private:
    [[nodiscard]] static bool needsPartialModelLightingFix(BlockId id) noexcept {
        // These are non-full / attachment / floor models in 1.12.2. Several of
        // them still inherit the registry's historical generic full-cube flags,
        // which made the mesher treat their own/neighbor faces as occluders and
        // drove AO samples to near black. The baked resource model is the source
        // of truth for geometry, so these models must not use full-cube cullFace
        // shortcuts or full-cube ambient occlusion assumptions.
        switch (id) {
            case BlockId::GoldenRail: case BlockId::DetectorRail: case BlockId::Rail:
            case BlockId::ActivatorRail: case BlockId::Torch: case BlockId::RedstoneTorch:
            case BlockId::UnlitRedstoneTorch: case BlockId::Ladder: case BlockId::Lever:
            case BlockId::StonePressurePlate: case BlockId::WoodenPressurePlate:
            case BlockId::LightWeightedPressurePlate: case BlockId::HeavyWeightedPressurePlate:
            case BlockId::StoneButton: case BlockId::WoodenButton: case BlockId::RedstoneWire:
            case BlockId::UnpoweredRepeater: case BlockId::PoweredRepeater:
            case BlockId::UnpoweredComparator: case BlockId::PoweredComparator:
            case BlockId::Trapdoor: case BlockId::IronTrapdoor: case BlockId::Vine:
            case BlockId::SnowLayer: case BlockId::Cocoa: case BlockId::Carpet:
            case BlockId::StandingSign: case BlockId::WallSign:
            case BlockId::StandingBanner: case BlockId::WallBanner:
            case BlockId::StoneSlab: case BlockId::WoodenSlab: case BlockId::StoneSlab2:
            case BlockId::PurpurSlab: case BlockId::OakStairs: case BlockId::StoneStairs:
            case BlockId::BrickStairs: case BlockId::StoneBrickStairs: case BlockId::NetherBrickStairs:
            case BlockId::SandstoneStairs: case BlockId::SpruceStairs: case BlockId::BirchStairs:
            case BlockId::JungleStairs: case BlockId::QuartzStairs: case BlockId::AcaciaStairs:
            case BlockId::DarkOakStairs: case BlockId::RedSandstoneStairs: case BlockId::PurpurStairs:
            case BlockId::Fence: case BlockId::SpruceFence: case BlockId::BirchFence:
            case BlockId::JungleFence: case BlockId::DarkOakFence: case BlockId::AcaciaFence:
            case BlockId::NetherBrickFence: case BlockId::CobblestoneWall:
            case BlockId::IronBars: case BlockId::GlassPane: case BlockId::StainedGlassPane:
            case BlockId::FenceGate: case BlockId::SpruceFenceGate: case BlockId::BirchFenceGate:
            case BlockId::JungleFenceGate: case BlockId::DarkOakFenceGate: case BlockId::AcaciaFenceGate:
            case BlockId::Farmland: case BlockId::GrassPath: case BlockId::Cactus:
            case BlockId::Anvil: case BlockId::Hopper:
            case BlockId::EnchantingTable: case BlockId::BrewingStand: case BlockId::Cauldron:
            case BlockId::EndPortalFrame: case BlockId::DragonEgg: case BlockId::Cake:
            case BlockId::DaylightDetector: case BlockId::DaylightDetectorInverted:
            case BlockId::FlowerPot: case BlockId::Skull:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] std::vector<const BakedBlockModel*> partialModelCopies(
        const std::vector<const BakedBlockModel*>& originals) const {
        std::vector<const BakedBlockModel*> result;
        result.reserve(originals.size());
        std::lock_guard lock(adjustedModelMutex_);
        for (const BakedBlockModel* original : originals) {
            if (original == nullptr) continue;
            auto found = adjustedModels_.find(original);
            if (found == adjustedModels_.end()) {
                auto copy = std::make_unique<BakedBlockModel>(*original);
                // The historical registry still marks several thin blocks as
                // full cubes. Do not let that force full-cube AO onto their baked
                // geometry. Preserve Mojang's cullFace declarations, however:
                // stripping them creates internal/overdraw faces and is the wrong
                // fix for neighbour occlusion.
                copy->ambientOcclusion = false;
                found = adjustedModels_.emplace(original, std::move(copy)).first;
            }
            result.push_back(found->second.get());
        }
        return result;
    }

    [[nodiscard]] static std::string_view shulkerColor(BlockId id) {
        constexpr std::array<std::string_view, 16> colors = {
            "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
            "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"
        };
        const auto first = static_cast<std::uint16_t>(BlockId::WhiteShulkerBox);
        const auto value = static_cast<std::uint16_t>(id);
        if (value < first || value >= first + colors.size()) return "white";
        return colors[static_cast<std::size_t>(value - first)];
    }

    [[nodiscard]] static BakedModelQuad cubeFace(Face face, const AtlasBounds& uv) {
        BakedModelQuad quad;
        quad.face = face;
        quad.cullFace = face;
        quad.tintIndex = -1;
        quad.shade = true;
        switch (face) {
            case Face::Down:
                quad.positions = {{{0,0,1},{0,0,0},{1,0,0},{1,0,1}}}; break;
            case Face::Up:
                quad.positions = {{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}}; break;
            case Face::North:
                quad.positions = {{{1,0,0},{0,0,0},{0,1,0},{1,1,0}}}; break;
            case Face::South:
                quad.positions = {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}}; break;
            case Face::West:
                quad.positions = {{{0,0,0},{0,0,1},{0,1,1},{0,1,0}}}; break;
            case Face::East:
                quad.positions = {{{1,0,1},{1,0,0},{1,1,0},{1,1,1}}}; break;
        }
        quad.uvs = {{{uv.u0,uv.v1},{uv.u1,uv.v1},{uv.u1,uv.v0},{uv.u0,uv.v0}}};
        return quad;
    }

    [[nodiscard]] const BakedBlockModel& staticCustomModel(BlockId id) const {
        const auto first = static_cast<std::uint16_t>(BlockId::WhiteShulkerBox);
        const auto value = static_cast<std::uint16_t>(id);
        if (value < first || value > static_cast<std::uint16_t>(BlockId::BlackShulkerBox))
            throw std::runtime_error("No Stage 6 static custom renderer for block " + std::to_string(value));
        const std::size_t index = static_cast<std::size_t>(value - first);
        std::lock_guard lock(customModelMutex_);
        if (!shulkerModels_[index]) {
            const std::string spriteName = "minecraft:blocks/shulker_top_" + std::string(shulkerColor(id));
            if (!atlas_.contains(spriteName))
                throw std::runtime_error("Missing Minecraft 1.12.2 shulker texture: " + spriteName);
            const AtlasBounds bounds = atlas_.sprite(spriteName).bounds;
            auto model = std::make_unique<BakedBlockModel>();
            // Closed shulker boxes are rendered as a static, full closed shell
            // until the block-entity animation stage. This uses Mojang's exact
            // per-colour JAR texture rather than a placeholder and gives Stage 6
            // a visible, correctly facing building block immediately.
            model->ambientOcclusion = false;
            for (Face face : {Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East})
                model->quads.push_back(cubeFace(face, bounds));
            shulkerModels_[index] = std::move(model);
        }
        return *shulkerModels_[index];
    }

    void validateIntentionalRenderPaths() const {
        const auto air = [](int, int, int) { return makeBlockState(0); };
        for (std::uint16_t numericId = 0; numericId < 256; ++numericId) {
            if (!BlockRegistry::isRegisteredId(numericId)) continue;
            const BlockRenderPath path = blockRenderPath(static_cast<BlockId>(numericId));
            if (path == BlockRenderPath::StaticCustomRenderer) {
                static_cast<void>(staticCustomModel(static_cast<BlockId>(numericId)));
                continue;
            }
            if (path != BlockRenderPath::JsonModel) continue;

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
    mutable std::mutex adjustedModelMutex_;
    mutable std::unordered_map<const BakedBlockModel*, std::unique_ptr<BakedBlockModel>> adjustedModels_;
    mutable std::mutex customModelMutex_;
    mutable std::array<std::unique_ptr<BakedBlockModel>, 16> shulkerModels_{};
};
