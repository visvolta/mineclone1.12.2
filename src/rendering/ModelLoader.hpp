#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "blocks/BlockRegistry.hpp"

class TextureAtlasData;

using ModelProperties = std::map<std::string, std::string, std::less<>>;

struct BakedModelQuad {
    std::array<glm::vec3, 4> positions{};
    std::array<glm::vec2, 4> uvs{};
    Face face = Face::Up;
    std::optional<Face> cullFace;
    int tintIndex = -1;
    bool shade = true;
};

struct BakedBlockModel {
    std::vector<BakedModelQuad> quads;
    bool ambientOcclusion = true;
};

struct BlockModelState {
    std::string resourceName;
    ModelProperties properties;
    std::string variantName;
};

class BlockModelManager {
public:
    BlockModelManager(const std::filesystem::path& assetRoot, const TextureAtlasData& atlas);

    [[nodiscard]] bool hasBlockState(std::string_view resourceName) const;
    [[nodiscard]] std::vector<const BakedBlockModel*> select(
        const BlockModelState& state, std::int64_t positionRandom) const;

    // GUI ItemBlock rendering uses the actual assets/minecraft/models/item/*.json
    // model, including inventory-only parents such as fence_inventory rather
    // than guessing from an in-world blockstate with empty neighbours.
    [[nodiscard]] const BakedBlockModel* itemModel(std::string_view resourceName) const {
        std::string path(resourceName);
        if (path.starts_with("minecraft:")) path.erase(0, 10);
        if (path.starts_with("item/")) path.erase(0, 5);
        const std::string canonical = normalizeModelName("minecraft:item/" + path, false);
        return bakedModel(canonical, 0, 0, false).get();
    }

private:
    struct ModelApplication;
    struct VariantRule;
    struct MultipartRule;
    struct BlockStateDefinition;
    struct RawElement;
    struct ResolvedModel;

    [[nodiscard]] static std::string normalizeBlockStateName(std::string_view name);
    [[nodiscard]] static std::string normalizeModelName(std::string_view name, bool fromBlockState);
    [[nodiscard]] static bool conditionMatches(const ModelProperties& properties,
                                               const ModelProperties& required);
    [[nodiscard]] const BakedBlockModel* choose(const std::vector<ModelApplication>& choices,
                                                std::int64_t random) const;

    void loadBlockStates();
    [[nodiscard]] BlockStateDefinition parseBlockState(const std::filesystem::path& path) const;
    [[nodiscard]] std::shared_ptr<const BakedBlockModel> bakedModel(
        const std::string& model, int rotationX, int rotationY, bool uvLock) const;
    [[nodiscard]] ResolvedModel resolvedModel(const std::string& model,
                                              std::vector<std::string>& stack) const;

    std::filesystem::path assetRoot_;
    const TextureAtlasData& atlas_;
    std::unordered_map<std::string, std::shared_ptr<const BlockStateDefinition>> blockStates_;
    mutable std::unordered_map<std::string, std::shared_ptr<const BakedBlockModel>> bakedModels_;
    mutable std::unordered_map<std::string, std::shared_ptr<const ResolvedModel>> resolvedModels_;
};
