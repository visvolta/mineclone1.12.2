#include "rendering/WorldRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/ThreadPool.hpp"
#include "environment/Environment.hpp"
#include "lighting/LightingEngine.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/ChunkMesher.hpp"
#include "rendering/TextureAtlas.hpp"
#include "world/World.hpp"

namespace {

constexpr std::string_view vertexSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec2 vertexUv;
layout (location = 2) in vec3 vertexTint;
layout (location = 3) in vec2 vertexOverlayUv;
layout (location = 4) in float vertexOverlay;
layout (location = 5) in float vertexShade;
layout (location = 6) in float vertexSkyLight;
layout (location = 7) in float vertexBlockLight;
out vec2 uv;
out vec3 tint;
out vec2 overlayUv;
out float overlay;
out float shade;
out vec2 lightLevels;
out vec3 viewPosition;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    uv = vertexUv;
    tint = vertexTint;
    overlayUv = vertexOverlayUv;
    overlay = vertexOverlay;
    shade = vertexShade;
    lightLevels = vec2(vertexSkyLight, vertexBlockLight);
    vec4 cameraPosition = view * model * vec4(vertexPosition, 1.0);
    viewPosition = cameraPosition.xyz;
    gl_Position = projection * cameraPosition;
}
)glsl";

constexpr std::string_view fragmentSource = R"glsl(
#version 330 core
in vec2 uv;
in vec3 tint;
in vec2 overlayUv;
in float overlay;
in float shade;
in vec2 lightLevels;
in vec3 viewPosition;
out vec4 fragmentColor;
uniform sampler2D atlas;
uniform int renderLayer;
uniform float skyLightSubtracted;
uniform vec3 fogColor;
uniform int fogMode;
uniform float fogStart;
uniform float fogEnd;
uniform float fogDensity;

float vanillaBrightness(float level) {
    float darkness = 1.0 - clamp(level, 0.0, 15.0) / 15.0;
    return (1.0 - darkness) / (darkness * 3.0 + 1.0);
}

vec3 vanillaLightmap(vec2 levels) {
    // EntityRenderer#updateLightmap with Stage 6's vanilla sky subtraction,
    // default gamma, and neutral torch flicker. Keeping the channels separate
    // preserves warm block light instead of taking max(sky, block).
    float sky = vanillaBrightness(max(0.0, levels.x - skyLightSubtracted));
    float block = vanillaBrightness(levels.y) * 1.5;
    float greenBlock = block * ((block * 0.6 + 0.4) * 0.6 + 0.4);
    float blueBlock = block * (block * block * 0.6 + 0.4);
    return clamp(vec3(sky + block, sky + greenBlock, sky + blueBlock) * 0.96 + 0.03,
                 vec3(0.0), vec3(1.0));
}

void main() {
    vec4 base = texture(atlas, uv);
    if (overlay > 0.5) {
        vec4 top = texture(atlas, overlayUv);
        top.rgb *= tint;
        base = mix(base, top, top.a);
    } else {
        base.rgb *= tint;
    }
    // 1.12.2 cutout models use alpha testing while the translucent layer
    // keeps partial alpha (water, stained glass, ice).
    if (renderLayer == 3) {
        if (base.a <= 0.001) discard;
    } else if (renderLayer == 1) {
        // CUTOUT_MIPPED uses the stricter vanilla alpha threshold.
        if (base.a < 0.5) discard;
    } else if (base.a < 0.1) {
        discard;
    }
    base.rgb *= shade;
    base.rgb *= vanillaLightmap(lightLevels);
    float distanceToCamera = length(viewPosition);
    float fogFactor = fogMode == 1
        ? exp(-fogDensity * distanceToCamera)
        : clamp((fogEnd - distanceToCamera) / max(fogEnd - fogStart, 0.001), 0.0, 1.0);
    fragmentColor = vec4(mix(fogColor, base.rgb, fogFactor), base.a);
}
)glsl";

struct Plane {
    glm::vec3 normal{};
    float distance = 0.0F;
};

class Frustum {
public:
    explicit Frustum(const glm::mat4& matrix) {
        const glm::vec4 row0{matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
        const glm::vec4 row1{matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
        const glm::vec4 row2{matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
        const glm::vec4 row3{matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
        setPlane(0, row3 + row0);
        setPlane(1, row3 - row0);
        setPlane(2, row3 + row1);
        setPlane(3, row3 - row1);
        setPlane(4, row3 + row2);
        setPlane(5, row3 - row2);
    }

    [[nodiscard]] bool containsSection(int chunkX, int sectionY, int chunkZ) const {
        const glm::vec3 center{
            chunkX * chunkSize + chunkSize * 0.5F,
            sectionY * sectionSize + sectionSize * 0.5F,
            chunkZ * chunkSize + chunkSize * 0.5F
        };
        constexpr glm::vec3 extent{sectionSize * 0.5F};
        for (const Plane& plane : planes_) {
            const float radius = glm::dot(glm::abs(plane.normal), extent);
            if (glm::dot(plane.normal, center) + plane.distance + radius < 0.0F) return false;
        }
        return true;
    }

private:
    void setPlane(std::size_t index, const glm::vec4& coefficients) {
        const glm::vec3 normal{coefficients};
        const float length = glm::length(normal);
        planes_[index] = length > 0.0F
            ? Plane{normal / length, coefficients.w / length}
            : Plane{};
    }
    std::array<Plane, 6> planes_{};
};

} // namespace

struct WorldRenderer::CompletionQueue {
    struct CompletedMesh {
        SectionKey key;
        std::uint64_t version = 0;
        SectionMeshData data;
    };
    std::mutex mutex;
    std::deque<CompletedMesh> meshes;
};

GpuMesh::~GpuMesh() { release(); }

GpuMesh::GpuMesh(GpuMesh&& other) noexcept { *this = std::move(other); }

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept {
    if (this == &other) return *this;
    release();
    vao_ = std::exchange(other.vao_, 0);
    vbo_ = std::exchange(other.vbo_, 0);
    ebo_ = std::exchange(other.ebo_, 0);
    indexCount_ = std::exchange(other.indexCount_, 0);
    translucent_ = std::exchange(other.translucent_, false);
    hasSortPosition_ = std::exchange(other.hasSortPosition_, false);
    lastSortPosition_ = other.lastSortPosition_;
    baseIndices_ = std::move(other.baseIndices_);
    quadCenters_ = std::move(other.quadCenters_);
    return *this;
}

void GpuMesh::release() {
    if (ebo_ != 0) glDeleteBuffers(1, &ebo_);
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = ebo_ = 0;
    indexCount_ = 0;
    translucent_ = false;
    hasSortPosition_ = false;
    baseIndices_.clear();
    quadCenters_.clear();
}

void GpuMesh::upload(const MeshData& data, bool translucent) {
    release();
    if (data.indices.empty()) return;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(MeshVertex)), data.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(std::uint32_t)), data.indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(MeshVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, x)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, u)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, red)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, overlayU)));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, overlay)));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, shade)));
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, skyLight)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(MeshVertex, blockLight)));
    for (GLuint attribute = 0; attribute < 8; ++attribute) glEnableVertexAttribArray(attribute);
    indexCount_ = static_cast<GLsizei>(data.indices.size());
    translucent_ = translucent;
    if (translucent_) {
        baseIndices_ = data.indices;
        quadCenters_.reserve(data.indices.size() / 6);
        for (std::size_t offset = 0; offset + 5 < data.indices.size(); offset += 6) {
            std::array<std::uint32_t, 6> unique{};
            std::size_t uniqueCount = 0;
            for (std::size_t i = 0; i < 6; ++i) {
                const std::uint32_t candidate = data.indices[offset + i];
                bool seen = false;
                for (std::size_t j = 0; j < uniqueCount; ++j) seen = seen || unique[j] == candidate;
                if (!seen && uniqueCount < unique.size()) unique[uniqueCount++] = candidate;
            }
            glm::vec3 center{};
            for (std::size_t i = 0; i < uniqueCount; ++i) {
                const MeshVertex& vertex = data.vertices[unique[i]];
                center += glm::vec3(vertex.x, vertex.y, vertex.z);
            }
            if (uniqueCount > 0) center /= static_cast<float>(uniqueCount);
            quadCenters_.push_back(center);
        }
    }
}

void GpuMesh::sortTranslucent(const glm::vec3& cameraLocal) {
    if (!translucent_ || baseIndices_.empty() || quadCenters_.empty()) return;
    if (hasSortPosition_ && glm::dot(cameraLocal - lastSortPosition_, cameraLocal - lastSortPosition_) < 0.25F) return;
    lastSortPosition_ = cameraLocal;
    hasSortPosition_ = true;

    std::vector<std::size_t> order(quadCenters_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const glm::vec3 dl = quadCenters_[left] - cameraLocal;
        const glm::vec3 dr = quadCenters_[right] - cameraLocal;
        return glm::dot(dl, dl) > glm::dot(dr, dr);
    });

    std::vector<std::uint32_t> sorted;
    sorted.reserve(baseIndices_.size());
    for (const std::size_t quad : order) {
        const std::size_t offset = quad * 6;
        if (offset + 5 >= baseIndices_.size()) continue;
        sorted.insert(sorted.end(), baseIndices_.begin() + static_cast<std::ptrdiff_t>(offset),
                      baseIndices_.begin() + static_cast<std::ptrdiff_t>(offset + 6));
    }
    // GL_ELEMENT_ARRAY_BUFFER is VAO state in core OpenGL. Binding an EBO
    // without first binding this mesh's VAO rewires whichever block mesh was
    // drawn previously. The first translucent sort would therefore steal the
    // EBO from a cutout mesh (commonly tall grass), and later translucent sorts
    // could corrupt other water meshes until a section rebuild recreated the
    // VAOs. Keep the sort update scoped to this translucent mesh.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(sorted.size() * sizeof(std::uint32_t)), sorted.data());
}

void GpuMesh::draw() const {
    if (indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

std::size_t WorldRenderer::SectionKeyHash::operator()(const SectionKey& value) const {
    std::size_t hash = static_cast<std::uint32_t>(value.chunkX);
    hash ^= static_cast<std::size_t>(static_cast<std::uint32_t>(value.chunkZ)) * 0x9E3779B185EBCA87ULL;
    hash ^= static_cast<std::size_t>(value.sectionY) * 0xC2B2AE3D27D4EB4FULL;
    return hash;
}

WorldRenderer::WorldRenderer(const World& world, TextureAtlas& atlas,
                             const BlockRenderResources& resources, ThreadPool& workers)
    : world_(world), atlas_(atlas), resources_(resources), workers_(workers), shader_(vertexSource, fragmentSource),
      modelLocation_(shader_.uniformLocation("model")),
      viewLocation_(shader_.uniformLocation("view")),
      projectionLocation_(shader_.uniformLocation("projection")),
      atlasLocation_(shader_.uniformLocation("atlas")),
      renderLayerLocation_(shader_.uniformLocation("renderLayer")),
      skyLightSubtractedLocation_(shader_.uniformLocation("skyLightSubtracted")),
      fogColorLocation_(shader_.uniformLocation("fogColor")),
      fogModeLocation_(shader_.uniformLocation("fogMode")),
      fogStartLocation_(shader_.uniformLocation("fogStart")),
      fogEndLocation_(shader_.uniformLocation("fogEnd")),
      fogDensityLocation_(shader_.uniformLocation("fogDensity")),
      completions_(std::make_shared<CompletionQueue>()) {
    shader_.use();
    shader_.setInt(atlasLocation_, 0);
    for (const auto& [chunkKey, chunk] : world.chunks()) {
        static_cast<void>(chunkKey);
        requestChunkSections(chunk->x(), chunk->z());
    }
}

WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::enqueueNextPending() {
    SectionKey key;
    MeshJobState* state = nullptr;
    while (!pendingSections_.empty()) {
        key = pendingSections_.front();
        pendingSections_.pop_front();
        const auto iterator = meshJobs_.find(key);
        if (iterator == meshJobs_.end() || !iterator->second.pending) continue;
        state = &iterator->second;
        state->pending = false;
        break;
    }
    if (state == nullptr) return false;

    const Chunk* chunk = world_.findChunk(key.chunkX, key.chunkZ);
    if (chunk == nullptr) {
        sections_.erase(key);
        meshJobs_.erase(key);
        return true;
    }
    if (!chunk->lightingReady()) {
        // Keep the last coherent GPU mesh during a relight. Erasing it here
        // produced the large temporary holes seen after block edits.
        return true;
    }
    const ChunkSection* section = chunk->section(key.sectionY);
    if (section == nullptr || section->empty()) {
        sections_.erase(key);
        state->appliedVersion = state->desiredVersion;
        return true;
    }
    SectionSnapshot snapshot = ChunkMesher::capture(world_, key.chunkX, key.sectionY, key.chunkZ);
    state->queued = true;
    const std::uint64_t version = state->desiredVersion;
    const std::shared_ptr<CompletionQueue> completions = completions_;
    const BlockRenderResources* resources = &resources_;
    workers_.enqueue(WorkerTaskClass::Meshing, 200000 + key.sectionY,
                     [key, version, snapshot = std::move(snapshot), completions, resources] {
        CompletionQueue::CompletedMesh completed{key, version, ChunkMesher::build(snapshot, *resources)};
        std::lock_guard lock(completions->mutex);
        completions->meshes.push_back(std::move(completed));
    });
    return true;
}

void WorldRenderer::requestSection(int chunkX, int sectionY, int chunkZ) {
    if (sectionY < 0 || sectionY >= sectionCount) return;
    const SectionKey key{chunkX, sectionY, chunkZ};
    MeshJobState& state = meshJobs_[key];
    state.desiredVersion = ++nextMeshVersion_;
    if (!state.queued && !state.pending) {
        state.pending = true;
        pendingSections_.push_back(key);
    }
}

void WorldRenderer::applyMeshData(const SectionKey& key, const SectionMeshData& data) {
    const bool empty = std::all_of(data.begin(), data.end(),
        [](const MeshData& mesh) { return mesh.indices.empty(); });
    if (empty) {
        sections_.erase(key);
        return;
    }

    auto [iterator, inserted] = sections_.try_emplace(key);
    RenderSection& renderSection = iterator->second;
    if (inserted) {
        renderSection.key = key;
        renderSection.model = glm::translate(glm::mat4(1.0F), glm::vec3(
            key.chunkX * chunkSize, key.sectionY * sectionSize, key.chunkZ * chunkSize));
    }
    for (std::size_t layer = 0; layer < renderSection.layers.size(); ++layer)
        renderSection.layers[layer].upload(data[layer], layer == static_cast<std::size_t>(RenderLayer::Translucent));
}

void WorldRenderer::rebuildSectionSync(const SectionKey& key) {
    if (key.sectionY < 0 || key.sectionY >= sectionCount) return;
    MeshJobState& state = meshJobs_[key];
    const std::uint64_t version = ++nextMeshVersion_;
    state.desiredVersion = version;
    state.pending = false;

    const Chunk* chunk = world_.findChunk(key.chunkX, key.chunkZ);
    if (chunk == nullptr || !chunk->lightingReady()) return;
    const ChunkSection* section = chunk->section(key.sectionY);
    if (section == nullptr || section->empty()) {
        sections_.erase(key);
    } else {
        applyMeshData(key, ChunkMesher::build(world_, key.chunkX, key.sectionY, key.chunkZ, resources_));
    }
    state.appliedVersion = version;
}

void WorldRenderer::requestChunkSections(int chunkX, int chunkZ) {
    const Chunk* chunk = world_.findChunk(chunkX, chunkZ);
    if (chunk == nullptr || !chunk->lightingReady()) return;
    // Highest populated sections normally contain the visible terrain surface,
    // so schedule them first to close visible gaps before underground geometry.
    for (int sectionY = sectionCount - 1; sectionY >= 0; --sectionY) {
        const ChunkSection* section = chunk->section(sectionY);
        if (section != nullptr && !section->empty()) requestSection(chunkX, sectionY, chunkZ);
    }
}

bool WorldRenderer::lightingNeighborhoodReady(int chunkX, int chunkZ) const {
    const Chunk* center = world_.findChunk(chunkX, chunkZ);
    if (center == nullptr || !center->lightingReady()) return false;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* neighbor = world_.findChunk(chunkX + dx, chunkZ + dz);
            if (neighbor != nullptr && !neighbor->lightingReady()) return false;
        }
    }
    return true;
}

bool WorldRenderer::processOneCompleted() {
    CompletionQueue::CompletedMesh completed;
    {
        std::lock_guard lock(completions_->mutex);
        if (completions_->meshes.empty()) return false;
        completed = std::move(completions_->meshes.front());
        completions_->meshes.pop_front();
    }

    const auto stateIterator = meshJobs_.find(completed.key);
    if (stateIterator != meshJobs_.end()) {
        MeshJobState& state = stateIterator->second;
        state.queued = false;
        if (completed.version == state.desiredVersion &&
            world_.findChunk(completed.key.chunkX, completed.key.chunkZ) != nullptr) {
            applyMeshData(completed.key, completed.data);
            state.appliedVersion = completed.version;
        }
        if (state.desiredVersion > state.appliedVersion && !state.pending && !state.queued) {
            state.pending = true;
            pendingSections_.push_back(completed.key);
        }
    }
    return true;
}

void WorldRenderer::processCompletedMeshes(double budgetMilliseconds) {
    const auto start = std::chrono::steady_clock::now();
    bool preferCompletion = true;
    while (true) {
        bool didWork = preferCompletion ? processOneCompleted() : enqueueNextPending();
        if (!didWork) didWork = preferCompletion ? enqueueNextPending() : processOneCompleted();
        if (!didWork) break;
        preferCompletion = !preferCompletion;
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= budgetMilliseconds) break;
    }
}

void WorldRenderer::blockChangedSync(int blockX, int blockY, int blockZ,
                                     const std::vector<LightingChange>& lightingChanges) {
    std::unordered_set<SectionKey, SectionKeyHash> dirty;
    for (const LightingChange& change : lightingChanges) {
        for (int sectionY = 0; sectionY < sectionCount; ++sectionY) {
            if ((change.dirtySections & static_cast<std::uint16_t>(1U << sectionY)) != 0)
                dirty.insert({change.chunkX, sectionY, change.chunkZ});
        }
    }

    const int chunkX = World::floorDiv16(blockX);
    const int chunkZ = World::floorDiv16(blockZ);
    const int sectionY = blockY / sectionSize;
    dirty.insert({chunkX, sectionY, chunkZ});
    const int localX = World::floorMod16(blockX);
    const int localZ = World::floorMod16(blockZ);
    const int localY = blockY & 15;
    if (localX == 0) dirty.insert({chunkX - 1, sectionY, chunkZ});
    if (localX == 15) dirty.insert({chunkX + 1, sectionY, chunkZ});
    if (localZ == 0) dirty.insert({chunkX, sectionY, chunkZ - 1});
    if (localZ == 15) dirty.insert({chunkX, sectionY, chunkZ + 1});
    if (localY == 0) dirty.insert({chunkX, sectionY - 1, chunkZ});
    if (localY == 15) dirty.insert({chunkX, sectionY + 1, chunkZ});

    for (const SectionKey& key : dirty) rebuildSectionSync(key);
}

void WorldRenderer::chunkLoaded(int chunkX, int chunkZ) {
    // Newly inserted chunks are deliberately unlit. The lighting completion
    // path requests their meshes once the loaded 3x3 neighborhood is coherent.
    if (!lightingNeighborhoodReady(chunkX, chunkZ)) return;
    requestChunkSections(chunkX, chunkZ);
    requestChunkSections(chunkX - 1, chunkZ);
    requestChunkSections(chunkX + 1, chunkZ);
    requestChunkSections(chunkX, chunkZ - 1);
    requestChunkSections(chunkX, chunkZ + 1);
}

void WorldRenderer::chunkLightingChanged(int chunkX, int chunkZ) {
    // A completion can make any touching chunk's light/AO halo coherent.
    // Request each qualifying chunk once here instead of remeshing all
    // neighbors after every partial worker result.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (lightingNeighborhoodReady(chunkX + dx, chunkZ + dz))
                requestChunkSections(chunkX + dx, chunkZ + dz);
}

void WorldRenderer::chunkUnloaded(int chunkX, int chunkZ) {
    std::erase_if(sections_, [=](const auto& entry) {
        return entry.first.chunkX == chunkX && entry.first.chunkZ == chunkZ;
    });
    std::erase_if(meshJobs_, [=](const auto& entry) {
        return entry.first.chunkX == chunkX && entry.first.chunkZ == chunkZ;
    });
    requestChunkSections(chunkX - 1, chunkZ);
    requestChunkSections(chunkX + 1, chunkZ);
    requestChunkSections(chunkX, chunkZ - 1);
    requestChunkSections(chunkX, chunkZ + 1);
}

std::size_t WorldRenderer::pendingMeshCount() const {
    return static_cast<std::size_t>(std::count_if(meshJobs_.begin(), meshJobs_.end(),
        [](const auto& entry) { return entry.second.queued || entry.second.pending; }));
}

void WorldRenderer::renderLayer(std::size_t layer, const std::vector<RenderSection*>& visible,
                                const glm::vec3* cameraWorld) {
    glUniform1i(renderLayerLocation_, static_cast<GLint>(layer));
    for (RenderSection* section : visible) {
        GpuMesh& mesh = section->layers[layer];
        if (mesh.empty()) continue;
        if (cameraWorld != nullptr && layer == static_cast<std::size_t>(RenderLayer::Translucent)) {
            const glm::vec3 sectionOrigin{
                static_cast<float>(section->key.chunkX * chunkSize),
                static_cast<float>(section->key.sectionY * sectionSize),
                static_cast<float>(section->key.chunkZ * chunkSize)
            };
            mesh.sortTranslucent(*cameraWorld - sectionOrigin);
        }
        shader_.setMat4(modelLocation_, section->model);
        mesh.draw();
        ++stats_.drawCalls;
    }
}

void WorldRenderer::render(const glm::mat4& view, const glm::mat4& projection,
                           const EnvironmentFrame& environment) {
    const Frustum frustum(projection * view);
    visibleSections_.clear();
    visibleSections_.reserve(sections_.size());
    for (auto& [key, section] : sections_) {
        if (frustum.containsSection(key.chunkX, key.sectionY, key.chunkZ)) visibleSections_.push_back(&section);
    }
    stats_.totalSections = sections_.size();
    stats_.visibleSections = visibleSections_.size();
    stats_.culledSections = stats_.totalSections - stats_.visibleSections;
    stats_.drawCalls = 0;

    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec3 cameraWorld{inverseView[3]};

    // Vanilla's translucent chunk path sorts from back to front. Section order
    // is sorted first, then each translucent EBO is re-ordered by quad center.
    translucentSections_ = visibleSections_;
    std::stable_sort(translucentSections_.begin(), translucentSections_.end(),
        [&](const RenderSection* left, const RenderSection* right) {
            const glm::vec3 leftCenter{
                left->key.chunkX * chunkSize + sectionSize * 0.5F,
                left->key.sectionY * sectionSize + sectionSize * 0.5F,
                left->key.chunkZ * chunkSize + sectionSize * 0.5F
            };
            const glm::vec3 rightCenter{
                right->key.chunkX * chunkSize + sectionSize * 0.5F,
                right->key.sectionY * sectionSize + sectionSize * 0.5F,
                right->key.chunkZ * chunkSize + sectionSize * 0.5F
            };
            const glm::vec3 dl = leftCenter - cameraWorld;
            const glm::vec3 dr = rightCenter - cameraWorld;
            return glm::dot(dl, dl) > glm::dot(dr, dr);
        });

    shader_.use();
    shader_.setMat4(viewLocation_, view);
    shader_.setMat4(projectionLocation_, projection);
    glUniform1f(skyLightSubtractedLocation_, environment.skyLightSubtracted);
    glUniform3f(fogColorLocation_, environment.fogColor.r, environment.fogColor.g, environment.fogColor.b);
    glUniform1i(fogModeLocation_, environment.fogMode == FogMode::Exponential ? 1 : 0);
    glUniform1f(fogStartLocation_, environment.fogStart);
    glUniform1f(fogEndLocation_, environment.fogEnd);
    glUniform1f(fogDensityLocation_, environment.fogDensity);

    atlas_.updateAnimations(environment.rendererTicks);
    atlas_.bind(0);
    renderLayer(static_cast<std::size_t>(RenderLayer::Solid), visibleSections_);
    renderLayer(static_cast<std::size_t>(RenderLayer::CutoutMipped), visibleSections_);
    renderLayer(static_cast<std::size_t>(RenderLayer::Cutout), visibleSections_);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    renderLayer(static_cast<std::size_t>(RenderLayer::Translucent), translucentSections_, &cameraWorld);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
