#include "rendering/ModelLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

#include "core/Json.hpp"
#include "rendering/TextureAtlasData.hpp"

namespace {

constexpr float pi = 3.14159265358979323846F;

struct ElementRotationSpec {
    bool present = false;
    glm::vec3 origin{0.5F};
    char axis = 'y';
    float angle = 0.0F;
    bool rescale = false;
};

struct FaceSpec {
    std::optional<std::array<float, 4>> uv;
    std::string texture;
    std::optional<Face> cullFace;
    int rotation = 0;
    int tintIndex = -1;
};

Face parseFace(std::string_view name) {
    if (name == "down") return Face::Down;
    if (name == "up") return Face::Up;
    if (name == "north") return Face::North;
    if (name == "south") return Face::South;
    if (name == "west") return Face::West;
    if (name == "east") return Face::East;
    throw std::runtime_error("Unknown Minecraft model face: " + std::string(name));
}

glm::vec3 readVec3(const JsonValue& value, const char* label) {
    if (!value.isArray() || value.asArray().size() != 3)
        throw std::runtime_error(std::string("Expected 3 values for Minecraft model ") + label);
    return {
        static_cast<float>(value.asArray()[0].asNumber()),
        static_cast<float>(value.asArray()[1].asNumber()),
        static_cast<float>(value.asArray()[2].asNumber())
    };
}

std::array<float, 4> readUv(const JsonValue& value) {
    if (!value.isArray() || value.asArray().size() != 4)
        throw std::runtime_error("Minecraft model face UV must contain four values");
    return {
        static_cast<float>(value.asArray()[0].asNumber()),
        static_cast<float>(value.asArray()[1].asNumber()),
        static_cast<float>(value.asArray()[2].asNumber()),
        static_cast<float>(value.asArray()[3].asNumber())
    };
}

std::array<float, 4> defaultUv(Face face, const glm::vec3& from, const glm::vec3& to) {
    // BlockPart#getFaceUvs from Minecraft 1.12.2.
    switch (face) {
        case Face::Down: return {from.x, 16.0F - to.z, to.x, 16.0F - from.z};
        case Face::Up: return {from.x, from.z, to.x, to.z};
        case Face::North: return {16.0F - to.x, 16.0F - to.y, 16.0F - from.x, 16.0F - from.y};
        case Face::South: return {from.x, 16.0F - to.y, to.x, 16.0F - from.y};
        case Face::West: return {from.z, 16.0F - to.y, to.z, 16.0F - from.y};
        case Face::East: return {16.0F - to.z, 16.0F - to.y, 16.0F - from.z, 16.0F - from.y};
    }
    return {};
}

std::array<glm::vec3, 4> facePositions(Face face, const glm::vec3& from16,
                                       const glm::vec3& to16) {
    const glm::vec3 from = from16 / 16.0F;
    const glm::vec3 to = to16 / 16.0F;
    // EnumFaceDirection vertex order from 1.12.2.
    switch (face) {
        case Face::Down:
            return {{{from.x, from.y, to.z}, {from.x, from.y, from.z},
                     {to.x, from.y, from.z}, {to.x, from.y, to.z}}};
        case Face::Up:
            return {{{from.x, to.y, from.z}, {from.x, to.y, to.z},
                     {to.x, to.y, to.z}, {to.x, to.y, from.z}}};
        case Face::North:
            return {{{to.x, to.y, from.z}, {to.x, from.y, from.z},
                     {from.x, from.y, from.z}, {from.x, to.y, from.z}}};
        case Face::South:
            return {{{from.x, to.y, to.z}, {from.x, from.y, to.z},
                     {to.x, from.y, to.z}, {to.x, to.y, to.z}}};
        case Face::West:
            return {{{from.x, to.y, from.z}, {from.x, from.y, from.z},
                     {from.x, from.y, to.z}, {from.x, to.y, to.z}}};
        case Face::East:
            return {{{to.x, to.y, to.z}, {to.x, from.y, to.z},
                     {to.x, from.y, from.z}, {to.x, to.y, from.z}}};
    }
    return {};
}

glm::vec3 rotateAround(const glm::vec3& value, const glm::vec3& origin,
                       char axis, float degrees, bool rescale) {
    glm::vec3 p = value - origin;
    const float radians = degrees * pi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    glm::vec3 rotated = p;
    switch (axis) {
        case 'x': rotated = {p.x, p.y * c - p.z * s, p.y * s + p.z * c}; break;
        case 'y': rotated = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c}; break;
        case 'z': rotated = {p.x * c - p.y * s, p.x * s + p.y * c, p.z}; break;
        default: break;
    }
    if (rescale && degrees != 0.0F) {
        const float scale = 1.0F / std::cos(std::abs(radians));
        if (axis != 'x') rotated.x *= scale;
        if (axis != 'y') rotated.y *= scale;
        if (axis != 'z') rotated.z *= scale;
    }
    return rotated + origin;
}

glm::vec3 rotateModel(glm::vec3 value, int rotationX, int rotationY) {
    // ModelRotation builds matrix Y * X with negative input angles.
    value = rotateAround(value, glm::vec3(0.5F), 'x', static_cast<float>(-rotationX), false);
    value = rotateAround(value, glm::vec3(0.5F), 'y', static_cast<float>(-rotationY), false);
    return value;
}

Face faceFromNormal(const glm::vec3& normal) {
    const glm::vec3 absolute{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
    if (absolute.y >= absolute.x && absolute.y >= absolute.z)
        return normal.y >= 0.0F ? Face::Up : Face::Down;
    if (absolute.z >= absolute.x)
        return normal.z >= 0.0F ? Face::South : Face::North;
    return normal.x >= 0.0F ? Face::East : Face::West;
}

Face rotateFace(Face face, int rotationX, int rotationY) {
    glm::vec3 normal{};
    switch (face) {
        case Face::Down: normal.y = -1.0F; break;
        case Face::Up: normal.y = 1.0F; break;
        case Face::North: normal.z = -1.0F; break;
        case Face::South: normal.z = 1.0F; break;
        case Face::West: normal.x = -1.0F; break;
        case Face::East: normal.x = 1.0F; break;
    }
    normal = rotateAround(normal, glm::vec3(0.0F), 'x', static_cast<float>(-rotationX), false);
    normal = rotateAround(normal, glm::vec3(0.0F), 'y', static_cast<float>(-rotationY), false);
    return faceFromNormal(normal);
}


struct UvSpec {
    std::array<float, 4> rect{};
    int rotation = 0;
};

std::pair<glm::vec3, glm::vec3> faceUvBasis(Face face) {
    const auto vertices = facePositions(face, glm::vec3(0.0F), glm::vec3(16.0F));
    return {vertices[3] - vertices[0], vertices[1] - vertices[0]};
}

glm::vec3 rotateModelDirection(glm::vec3 value, int rotationX, int rotationY) {
    value = rotateAround(value, glm::vec3(0.0F), 'x', static_cast<float>(-rotationX), false);
    value = rotateAround(value, glm::vec3(0.0F), 'y', static_cast<float>(-rotationY), false);
    return value;
}

int uvLockQuarterTurns(Face sourceFace, int rotationX, int rotationY) {
    const Face destinationFace = rotateFace(sourceFace, rotationX, rotationY);
    auto [sourceU, sourceV] = faceUvBasis(sourceFace);
    const auto [destinationU, destinationV] = faceUvBasis(destinationFace);
    sourceU = rotateModelDirection(sourceU, rotationX, rotationY);
    sourceV = rotateModelDirection(sourceV, rotationX, rotationY);
    const auto aligned = [](const glm::vec3& left, const glm::vec3& right) {
        return left.x * right.x + left.y * right.y + left.z * right.z > 0.5F;
    };
    if (aligned(sourceU, destinationU) && aligned(sourceV, destinationV)) return 0;
    if (aligned(sourceU, destinationV) && aligned(sourceV, -destinationU)) return 1;
    if (aligned(sourceU, -destinationU) && aligned(sourceV, -destinationV)) return 2;
    if (aligned(sourceU, -destinationV) && aligned(sourceV, destinationU)) return 3;
    return 0;
}

float uvVertexU(const UvSpec& uv, int vertex) {
    const int rotated = (vertex + uv.rotation / 90) % 4;
    return (rotated == 0 || rotated == 1) ? uv.rect[0] : uv.rect[2];
}

float uvVertexV(const UvSpec& uv, int vertex) {
    const int rotated = (vertex + uv.rotation / 90) % 4;
    return (rotated == 0 || rotated == 3) ? uv.rect[1] : uv.rect[3];
}

UvSpec applyUvLock(const UvSpec& input, Face sourceFace, int rotationX, int rotationY) {
    const int reverse0 = (4 - input.rotation / 90) % 4;
    const int reverse2 = (2 + (4 - input.rotation / 90)) % 4;
    const float u0 = uvVertexU(input, reverse0);
    const float v0 = uvVertexV(input, reverse0);
    const float u2 = uvVertexU(input, reverse2);
    const float v2 = uvVertexV(input, reverse2);

    switch (uvLockQuarterTurns(sourceFace, rotationX, rotationY)) {
        case 1: return {{{16.0F - v0, u2, 16.0F - v2, u0}}, 90};
        case 2: return {{{16.0F - u0, 16.0F - v0, 16.0F - u2, 16.0F - v2}}, 0};
        case 3: return {{{v2, 16.0F - u0, v0, 16.0F - u2}}, 270};
        default: return {{{u0, v0, u2, v2}}, 0};
    }
}

std::vector<std::string> splitAlternatives(std::string_view value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find('|', start);
        result.emplace_back(value.substr(start, separator == std::string_view::npos ? value.size() - start : separator - start));
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return result;
}

int vanillaWeightedRandomIndex(std::int64_t random, int totalWeight) {
    // WeightedBakedModel#getRandomModel in Minecraft 1.12.2 uses
    // Math.abs((int)rand >> 16) % totalWeight. Reproduce the Java int cast
    // and arithmetic right shift without relying on implementation-defined
    // signed-shift behavior.
    const std::uint32_t low = static_cast<std::uint32_t>(random);
    std::int32_t shifted = static_cast<std::int32_t>(low >> 16U);
    if ((low & 0x80000000U) != 0U) shifted = static_cast<std::int32_t>(low >> 16U) - 65536;
    const std::int64_t magnitude = shifted < 0 ? -static_cast<std::int64_t>(shifted)
                                               : static_cast<std::int64_t>(shifted);
    return static_cast<int>(magnitude % std::max(1, totalWeight));
}

std::filesystem::path resourcePath(const std::filesystem::path& assetRoot,
                                   std::string_view canonical,
                                   std::string_view category) {
    const std::size_t colon = canonical.find(':');
    const std::string nameSpace = colon == std::string_view::npos ? "minecraft" : std::string(canonical.substr(0, colon));
    std::string path = colon == std::string_view::npos ? std::string(canonical) : std::string(canonical.substr(colon + 1));
    return assetRoot / "assets" / nameSpace / std::string(category) / (path + ".json");
}

} // namespace

struct BlockModelManager::RawElement {
    glm::vec3 from{};
    glm::vec3 to{};
    ElementRotationSpec rotation{};
    std::map<Face, FaceSpec> faces;
    bool shade = true;
};

struct BlockModelManager::ResolvedModel {
    std::map<std::string, std::string, std::less<>> textures;
    std::vector<RawElement> elements;
    bool ambientOcclusion = true;
};

struct BlockModelManager::ModelApplication {
    std::string model;
    int rotationX = 0;
    int rotationY = 0;
    bool uvLock = false;
    int weight = 1;
    std::shared_ptr<const BakedBlockModel> baked;
};

struct BlockModelManager::VariantRule {
    ModelProperties properties;
    std::string variantName;
    bool normal = false;
    std::vector<ModelApplication> applications;
};

struct BlockModelManager::MultipartRule {
    struct Condition {
        enum class Type { All, Any, Property } type = Type::All;
        std::string property;
        std::vector<std::string> accepted;
        bool negated = false;
        std::vector<Condition> children;

        [[nodiscard]] bool matches(const ModelProperties& properties) const {
            if (type == Type::All) {
                return std::all_of(children.begin(), children.end(),
                    [&](const Condition& child) { return child.matches(properties); });
            }
            if (type == Type::Any) {
                return std::any_of(children.begin(), children.end(),
                    [&](const Condition& child) { return child.matches(properties); });
            }
            const auto iterator = properties.find(property);
            const bool found = iterator != properties.end() &&
                std::find(accepted.begin(), accepted.end(), iterator->second) != accepted.end();
            return negated ? !found : found;
        }
    };

    Condition condition;
    std::vector<ModelApplication> applications;
};

struct BlockModelManager::BlockStateDefinition {
    std::vector<VariantRule> variants;
    std::vector<MultipartRule> multipart;
};

BlockModelManager::BlockModelManager(const std::filesystem::path& assetRoot,
                                     const TextureAtlasData& atlas)
    : assetRoot_(assetRoot), atlas_(atlas) {
    loadBlockStates();
}

std::string BlockModelManager::normalizeBlockStateName(std::string_view name) {
    std::string value(name);
    if (value.find(':') == std::string::npos) value.insert(0, "minecraft:");
    return value;
}

std::string BlockModelManager::normalizeModelName(std::string_view name, bool fromBlockState) {
    std::string value(name);
    if (value.find(':') == std::string::npos) value.insert(0, "minecraft:");
    const std::size_t colon = value.find(':');
    std::string path = value.substr(colon + 1);
    if (fromBlockState && !path.starts_with("block/") && !path.starts_with("builtin/"))
        path.insert(0, "block/");
    return value.substr(0, colon + 1) + path;
}

bool BlockModelManager::conditionMatches(const ModelProperties& properties,
                                         const ModelProperties& required) {
    if (properties.size() != required.size()) return false;
    for (const auto& [key, value] : required) {
        const auto iterator = properties.find(key);
        if (iterator == properties.end() || iterator->second != value) return false;
    }
    return true;
}

bool BlockModelManager::hasBlockState(std::string_view resourceName) const {
    return blockStates_.contains(normalizeBlockStateName(resourceName));
}

const BakedBlockModel* BlockModelManager::choose(const std::vector<ModelApplication>& choices,
                                                  std::int64_t random) const {
    if (choices.empty()) return nullptr;
    int totalWeight = 0;
    for (const ModelApplication& choice : choices) totalWeight += std::max(1, choice.weight);
    int target = vanillaWeightedRandomIndex(random, totalWeight);
    for (const ModelApplication& choice : choices) {
        target -= std::max(1, choice.weight);
        if (target < 0) return choice.baked.get();
    }
    return choices.back().baked.get();
}

std::vector<const BakedBlockModel*> BlockModelManager::select(
        const BlockModelState& state, std::int64_t positionRandom) const {
    std::vector<const BakedBlockModel*> result;
    const auto iterator = blockStates_.find(normalizeBlockStateName(state.resourceName));
    if (iterator == blockStates_.end()) return result;
    const BlockStateDefinition& definition = *iterator->second;

    for (const VariantRule& rule : definition.variants) {
        if ((rule.normal && state.properties.empty() && state.variantName.empty()) ||
            (!rule.variantName.empty() && state.variantName == rule.variantName) ||
            (!rule.normal && rule.variantName.empty() && conditionMatches(state.properties, rule.properties))) {
            if (const BakedBlockModel* model = choose(rule.applications, positionRandom)) result.push_back(model);
            return result;
        }
    }

    std::int64_t multipartRandom = positionRandom;
    for (const MultipartRule& rule : definition.multipart) {
        if (!rule.condition.matches(state.properties)) continue;
        if (const BakedBlockModel* model = choose(rule.applications, multipartRandom)) result.push_back(model);
        // MultipartBakedModel passes rand++ to each matching selector.
        ++multipartRandom;
    }
    return result;
}

void BlockModelManager::loadBlockStates() {
    const std::filesystem::path directory = assetRoot_ / "assets/minecraft/blockstates";
    if (!std::filesystem::exists(directory))
        throw std::runtime_error("Minecraft blockstate directory is missing: " + directory.string());

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        std::string name = path.stem().string();
        blockStates_.insert_or_assign("minecraft:" + name,
            std::make_shared<const BlockStateDefinition>(parseBlockState(path)));
    }
}

BlockModelManager::BlockStateDefinition BlockModelManager::parseBlockState(
        const std::filesystem::path& path) const {
    BlockStateDefinition definition;
    const JsonValue root = JsonValue::parseFile(path);

    auto parseApplicationObject = [&](const JsonValue& value) -> ModelApplication {
        if (!value.isObject()) throw std::runtime_error("Blockstate model application is not an object: " + path.string());
        ModelApplication application;
        application.model = normalizeModelName(value.at("model").asString(), true);
        application.rotationX = value.intOr("x", 0);
        application.rotationY = value.intOr("y", 0);
        application.uvLock = value.boolOr("uvlock", false);
        application.weight = std::max(1, value.intOr("weight", 1));
        if (application.rotationX % 90 != 0 || application.rotationY % 90 != 0)
            throw std::runtime_error("Blockstate rotation is not a multiple of 90 degrees: " + path.string());
        application.rotationX = ((application.rotationX % 360) + 360) % 360;
        application.rotationY = ((application.rotationY % 360) + 360) % 360;
        application.baked = bakedModel(application.model, application.rotationX,
                                       application.rotationY, application.uvLock);
        return application;
    };

    auto parseApplications = [&](const JsonValue& value) {
        std::vector<ModelApplication> applications;
        if (value.isArray()) {
            for (const JsonValue& item : value.asArray()) applications.push_back(parseApplicationObject(item));
        } else {
            applications.push_back(parseApplicationObject(value));
        }
        // WeightedBakedModel.Builder sorts descending by weight. Java's
        // Collections.sort is stable, so equal-weight JSON entries retain
        // their resource order.
        std::stable_sort(applications.begin(), applications.end(),
            [](const ModelApplication& left, const ModelApplication& right) {
                return left.weight > right.weight;
            });
        return applications;
    };

    if (const JsonValue* variants = root.find("variants")) {
        if (!variants->isObject()) throw std::runtime_error("Blockstate variants is not an object: " + path.string());
        for (const auto& [key, value] : variants->asObject()) {
            VariantRule rule;
            rule.normal = key == "normal" || key.empty();
            if (!rule.normal && key.find('=') == std::string::npos) {
                rule.variantName = key;
            } else if (!rule.normal) {
                std::size_t start = 0;
                while (start <= key.size()) {
                    const std::size_t comma = key.find(',', start);
                    const std::string_view token(key.data() + start,
                        (comma == std::string::npos ? key.size() : comma) - start);
                    const std::size_t equals = token.find('=');
                    if (equals == std::string_view::npos)
                        throw std::runtime_error("Invalid blockstate variant key: " + key);
                    rule.properties.emplace(std::string(token.substr(0, equals)),
                                            std::string(token.substr(equals + 1)));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            rule.applications = parseApplications(value);
            definition.variants.push_back(std::move(rule));
        }
    }

    if (const JsonValue* multipart = root.find("multipart")) {
        if (!multipart->isArray()) throw std::runtime_error("Blockstate multipart is not an array: " + path.string());
        std::function<MultipartRule::Condition(const JsonValue&)> parseCondition;
        parseCondition = [&](const JsonValue& value) -> MultipartRule::Condition {
            MultipartRule::Condition condition;
            if (!value.isObject()) throw std::runtime_error("Multipart condition is not an object: " + path.string());
            condition.type = MultipartRule::Condition::Type::All;
            for (const auto& [key, childValue] : value.asObject()) {
                if (key == "OR" || key == "AND") {
                    MultipartRule::Condition group;
                    group.type = key == "OR" ? MultipartRule::Condition::Type::Any : MultipartRule::Condition::Type::All;
                    if (!childValue.isArray()) throw std::runtime_error("Multipart OR/AND must be an array: " + path.string());
                    for (const JsonValue& child : childValue.asArray()) group.children.push_back(parseCondition(child));
                    condition.children.push_back(std::move(group));
                    continue;
                }
                if (!childValue.isString() && !childValue.isBool())
                    throw std::runtime_error("Multipart property condition must be a string or boolean: " + path.string());
                MultipartRule::Condition property;
                property.type = MultipartRule::Condition::Type::Property;
                property.property = key;
                std::string text = childValue.isBool() ? (childValue.asBool() ? "true" : "false") : childValue.asString();
                if (!text.empty() && text.front() == '!') {
                    property.negated = true;
                    text.erase(text.begin());
                }
                property.accepted = splitAlternatives(text);
                condition.children.push_back(std::move(property));
            }
            return condition;
        };

        for (const JsonValue& part : multipart->asArray()) {
            if (!part.isObject()) throw std::runtime_error("Multipart entry is not an object: " + path.string());
            MultipartRule rule;
            if (const JsonValue* when = part.find("when")) rule.condition = parseCondition(*when);
            else rule.condition.type = MultipartRule::Condition::Type::All;
            rule.applications = parseApplications(part.at("apply"));
            definition.multipart.push_back(std::move(rule));
        }
    }
    return definition;
}

std::shared_ptr<const BakedBlockModel> BlockModelManager::bakedModel(
        const std::string& model, int rotationX, int rotationY, bool uvLock) const {
    const std::string key = model + "|x=" + std::to_string(rotationX) + "|y=" +
        std::to_string(rotationY) + "|uv=" + (uvLock ? "1" : "0");
    if (const auto iterator = bakedModels_.find(key); iterator != bakedModels_.end()) return iterator->second;

    std::vector<std::string> stack;
    const ResolvedModel resolved = resolvedModel(model, stack);
    auto baked = std::make_shared<BakedBlockModel>();
    baked->ambientOcclusion = resolved.ambientOcclusion;

    auto resolveTexture = [&](std::string texture) {
        std::set<std::string> seen;
        while (!texture.empty() && texture.front() == '#') {
            const std::string variable = texture.substr(1);
            if (!seen.insert(variable).second) return std::string("minecraft:missingno");
            const auto iterator = resolved.textures.find(variable);
            if (iterator == resolved.textures.end()) return std::string("minecraft:missingno");
            texture = iterator->second;
        }
        return TextureAtlasData::normalizeResourceName(texture);
    };

    for (const RawElement& element : resolved.elements) {
        for (const auto& [sourceFace, faceSpec] : element.faces) {
            BakedModelQuad quad;
            quad.positions = facePositions(sourceFace, element.from, element.to);
            if (element.rotation.present) {
                for (glm::vec3& vertex : quad.positions)
                    vertex = rotateAround(vertex, element.rotation.origin, element.rotation.axis,
                                          element.rotation.angle, element.rotation.rescale);
            }
            for (glm::vec3& vertex : quad.positions) vertex = rotateModel(vertex, rotationX, rotationY);

            const glm::vec3 firstEdge = quad.positions[1] - quad.positions[0];
            const glm::vec3 secondEdge = quad.positions[2] - quad.positions[0];
            const glm::vec3 normal{
                firstEdge.y * secondEdge.z - firstEdge.z * secondEdge.y,
                firstEdge.z * secondEdge.x - firstEdge.x * secondEdge.z,
                firstEdge.x * secondEdge.y - firstEdge.y * secondEdge.x
            };
            quad.face = faceFromNormal(normal);
            if (faceSpec.cullFace) quad.cullFace = rotateFace(*faceSpec.cullFace, rotationX, rotationY);
            quad.tintIndex = faceSpec.tintIndex;
            quad.shade = element.shade;

            UvSpec uv{faceSpec.uv.value_or(defaultUv(sourceFace, element.from, element.to)),
                      faceSpec.rotation};
            if (uvLock) uv = applyUvLock(uv, sourceFace, rotationX, rotationY);

            const AtlasSprite& sprite = atlas_.sprite(resolveTexture(faceSpec.texture));
            for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex) {
                const float modelU = uvVertexU(uv, vertexIndex);
                const float modelV = uvVertexV(uv, vertexIndex);
                quad.uvs[static_cast<std::size_t>(vertexIndex)] = {sprite.bounds.u(modelU), sprite.bounds.v(modelV)};
            }
            baked->quads.push_back(std::move(quad));
        }
    }

    bakedModels_.emplace(key, baked);
    return baked;
}

BlockModelManager::ResolvedModel BlockModelManager::resolvedModel(
        const std::string& model, std::vector<std::string>& stack) const {
    if (const auto iterator = resolvedModels_.find(model); iterator != resolvedModels_.end()) return *iterator->second;
    if (std::find(stack.begin(), stack.end(), model) != stack.end())
        throw std::runtime_error("Minecraft model parent cycle involving: " + model);
    stack.push_back(model);

    ResolvedModel result;
    const std::size_t colon = model.find(':');
    const std::string path = colon == std::string::npos ? model : model.substr(colon + 1);
    if (path.starts_with("builtin/")) {
        stack.pop_back();
        auto stored = std::make_shared<ResolvedModel>(result);
        resolvedModels_.emplace(model, stored);
        return result;
    }

    const std::filesystem::path file = resourcePath(assetRoot_, model, "models");
    if (!std::filesystem::exists(file)) {
        stack.pop_back();
        auto stored = std::make_shared<ResolvedModel>(result);
        resolvedModels_.emplace(model, stored);
        return result;
    }
    const JsonValue root = JsonValue::parseFile(file);

    if (const JsonValue* parent = root.find("parent")) {
        const std::string parentName = normalizeModelName(parent->asString(), false);
        result = resolvedModel(parentName, stack);
    }
    if (const JsonValue* ambientOcclusion = root.find("ambientocclusion"))
        result.ambientOcclusion = ambientOcclusion->asBool();
    if (const JsonValue* textures = root.find("textures")) {
        for (const auto& [key, value] : textures->asObject()) result.textures.insert_or_assign(key, value.asString());
    }

    if (const JsonValue* elements = root.find("elements")) {
        if (!elements->isArray()) throw std::runtime_error("Minecraft model elements is not an array: " + file.string());
        result.elements.clear();
        for (const JsonValue& elementValue : elements->asArray()) {
            if (!elementValue.isObject()) throw std::runtime_error("Minecraft model element is not an object: " + file.string());
            RawElement element;
            element.from = readVec3(elementValue.at("from"), "from");
            element.to = readVec3(elementValue.at("to"), "to");
            element.shade = elementValue.boolOr("shade", true);

            if (const JsonValue* rotation = elementValue.find("rotation")) {
                element.rotation.present = true;
                element.rotation.origin = readVec3(rotation->at("origin"), "rotation origin") / 16.0F;
                const std::string axis = rotation->at("axis").asString();
                if (axis != "x" && axis != "y" && axis != "z")
                    throw std::runtime_error("Invalid Minecraft element rotation axis: " + axis);
                element.rotation.axis = axis.front();
                element.rotation.angle = static_cast<float>(rotation->at("angle").asNumber());
                const float absolute = std::abs(element.rotation.angle);
                if (element.rotation.angle != 0.0F && absolute != 22.5F && absolute != 45.0F)
                    throw std::runtime_error("Invalid Minecraft element rotation angle in: " + file.string());
                element.rotation.rescale = rotation->boolOr("rescale", false);
            }

            const JsonValue& faces = elementValue.at("faces");
            for (const auto& [faceName, faceValue] : faces.asObject()) {
                FaceSpec face;
                if (const JsonValue* uv = faceValue.find("uv")) face.uv = readUv(*uv);
                face.texture = faceValue.at("texture").asString();
                if (const JsonValue* cull = faceValue.find("cullface")) face.cullFace = parseFace(cull->asString());
                face.rotation = faceValue.intOr("rotation", 0);
                if (face.rotation < 0 || face.rotation > 270 || face.rotation % 90 != 0)
                    throw std::runtime_error("Invalid Minecraft face UV rotation in: " + file.string());
                face.tintIndex = faceValue.intOr("tintindex", -1);
                element.faces.emplace(parseFace(faceName), std::move(face));
            }
            if (element.faces.empty()) throw std::runtime_error("Minecraft model element has no faces: " + file.string());
            result.elements.push_back(std::move(element));
        }
    }

    stack.pop_back();
    auto stored = std::make_shared<ResolvedModel>(result);
    resolvedModels_.emplace(model, stored);
    return result;
}
