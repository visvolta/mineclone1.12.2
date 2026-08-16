#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "rendering/ChunkMesher.hpp"
#include "rendering/Shader.hpp"

class BlockRenderResources;
class TextureAtlas;
class ThreadPool;
class World;
struct LightingChange;
struct EnvironmentFrame;

class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    void upload(const MeshData& data, bool translucent = false);
    void sortTranslucent(const glm::vec3& cameraLocal);
    void draw() const;
    [[nodiscard]] bool empty() const { return indexCount_ == 0; }

private:
    void release();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
    bool translucent_ = false;
    bool hasSortPosition_ = false;
    glm::vec3 lastSortPosition_{};
    std::vector<std::uint32_t> baseIndices_;
    std::vector<glm::vec3> quadCenters_;
};

struct WorldRenderStats {
    std::size_t totalSections = 0;
    std::size_t visibleSections = 0;
    std::size_t culledSections = 0;
    std::size_t drawCalls = 0;
};

class WorldRenderer {
public:
    WorldRenderer(const World& world, TextureAtlas& atlas,
                  const BlockRenderResources& resources, ThreadPool& workers);
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    void processCompletedMeshes(double budgetMilliseconds = 1.5);
    void render(const glm::mat4& view, const glm::mat4& projection,
                const EnvironmentFrame& environment);
    void blockChangedSync(int blockX, int blockY, int blockZ,
                          const std::vector<LightingChange>& lightingChanges);
    void chunkLoaded(int chunkX, int chunkZ);
    void chunkLightingChanged(int chunkX, int chunkZ);
    void chunkUnloaded(int chunkX, int chunkZ);

    [[nodiscard]] std::size_t pendingMeshCount() const;
    [[nodiscard]] const WorldRenderStats& stats() const { return stats_; }

private:
    struct SectionKey {
        int chunkX = 0;
        int sectionY = 0;
        int chunkZ = 0;
        bool operator==(const SectionKey&) const = default;
    };
    struct SectionKeyHash {
        std::size_t operator()(const SectionKey& value) const;
    };
    struct RenderSection {
        SectionKey key;
        std::array<GpuMesh, static_cast<std::size_t>(RenderLayer::Count)> layers;
        glm::mat4 model{1.0F};
    };
    struct MeshJobState {
        std::uint64_t desiredVersion = 0;
        std::uint64_t appliedVersion = 0;
        bool queued = false;
        bool pending = false;
    };
    struct CompletionQueue;

    void requestSection(int chunkX, int sectionY, int chunkZ);
    void rebuildSectionSync(const SectionKey& key);
    void applyMeshData(const SectionKey& key, const SectionMeshData& data);
    [[nodiscard]] bool enqueueNextPending();
    [[nodiscard]] bool processOneCompleted();
    void requestChunkSections(int chunkX, int chunkZ);
    [[nodiscard]] bool lightingNeighborhoodReady(int chunkX, int chunkZ) const;
    void renderLayer(std::size_t layer, const std::vector<RenderSection*>& visible,
                     const glm::vec3* cameraWorld = nullptr);

    const World& world_;
    TextureAtlas& atlas_;
    const BlockRenderResources& resources_;
    ThreadPool& workers_;
    Shader shader_;
    GLint modelLocation_ = -1;
    GLint viewLocation_ = -1;
    GLint projectionLocation_ = -1;
    GLint atlasLocation_ = -1;
    GLint renderLayerLocation_ = -1;
    GLint skyLightSubtractedLocation_ = -1;
    GLint fogColorLocation_ = -1;
    GLint fogModeLocation_ = -1;
    GLint fogStartLocation_ = -1;
    GLint fogEndLocation_ = -1;
    GLint fogDensityLocation_ = -1;
    std::shared_ptr<CompletionQueue> completions_;
    std::uint64_t nextMeshVersion_ = 0;
    std::deque<SectionKey> pendingSections_;
    std::unordered_map<SectionKey, RenderSection, SectionKeyHash> sections_;
    std::unordered_map<SectionKey, MeshJobState, SectionKeyHash> meshJobs_;
    std::vector<RenderSection*> visibleSections_;
    std::vector<RenderSection*> translucentSections_;
    WorldRenderStats stats_;
};
