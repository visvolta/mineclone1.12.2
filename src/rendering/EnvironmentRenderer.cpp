#include "rendering/EnvironmentRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "rendering/Shader.hpp"
#include "blocks/BlockRegistry.hpp"
#include "environment/Environment.hpp"
#include "rendering/CloudGeometry.hpp"
#include "world/World.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {

constexpr float pi = 3.14159265358979323846F;

using Vertex = EnvironmentVertex;

class Mesh {
public:
    Mesh() {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
    }
    ~Mesh() {
        if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    }
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const std::vector<Vertex>& vertices, GLenum usage = GL_STATIC_DRAW) {
        count_ = static_cast<GLsizei>(vertices.size());
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                     vertices.data(), usage);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, uv)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, color)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
    }

    void draw() const {
        if (count_ == 0) return;
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei count_ = 0;
};

GLuint loadTexture(const char* filename, bool repeat) {
    const std::filesystem::path path = std::filesystem::path(BLOCKCRAFT_ASSET_ROOT) /
        "assets/minecraft/textures/environment" / filename;
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) throw std::runtime_error("Could not load Minecraft environment texture: " + path.string());
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return texture;
}

void triangle(std::vector<Vertex>& output, const Vertex& a, const Vertex& b, const Vertex& c) {
    output.push_back(a); output.push_back(b); output.push_back(c);
}

void quad(std::vector<Vertex>& output, const Vertex& a, const Vertex& b,
          const Vertex& c, const Vertex& d) {
    triangle(output, a, b, c);
    triangle(output, a, c, d);
}

std::vector<Vertex> skyCube() {
    std::vector<Vertex> result;
    constexpr float extent = 200.0F;
    constexpr glm::vec4 white{1.0F};
    const auto v = [&](float x, float y, float z) { return Vertex{{x, y, z}, {}, white}; };
    quad(result, v(-extent, extent, -extent), v(-extent, extent, extent),
         v(extent, extent, extent), v(extent, extent, -extent));
    quad(result, v(-extent, -extent, extent), v(-extent, -extent, -extent),
         v(extent, -extent, -extent), v(extent, -extent, extent));
    quad(result, v(-extent, -extent, -extent), v(-extent, -extent, extent),
         v(-extent, extent, extent), v(-extent, extent, -extent));
    quad(result, v(extent, -extent, extent), v(extent, -extent, -extent),
         v(extent, extent, -extent), v(extent, extent, extent));
    quad(result, v(-extent, -extent, extent), v(extent, -extent, extent),
         v(extent, extent, extent), v(-extent, extent, extent));
    quad(result, v(extent, -extent, -extent), v(-extent, -extent, -extent),
         v(-extent, extent, -extent), v(extent, extent, -extent));
    return result;
}

std::vector<Vertex> stars() {
    std::vector<Vertex> result;
    result.reserve(1500 * 6);
    JavaRandom random(10842);
    for (int i = 0; i < 1500; ++i) {
        double x = random.nextFloat() * 2.0F - 1.0F;
        double y = random.nextFloat() * 2.0F - 1.0F;
        double z = random.nextFloat() * 2.0F - 1.0F;
        const double size = 0.15 + random.nextFloat() * 0.1F;
        double lengthSquared = x * x + y * y + z * z;
        if (lengthSquared >= 1.0 || lengthSquared <= 0.01) continue;
        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        x *= inverseLength; y *= inverseLength; z *= inverseLength;
        const double centerX = x * 100.0;
        const double centerY = y * 100.0;
        const double centerZ = z * 100.0;
        const double yaw = std::atan2(x, z);
        const double sinYaw = std::sin(yaw);
        const double cosYaw = std::cos(yaw);
        const double pitch = std::atan2(std::sqrt(x * x + z * z), y);
        const double sinPitch = std::sin(pitch);
        const double cosPitch = std::cos(pitch);
        const double rotation = random.nextDouble() * pi * 2.0;
        const double sinRotation = std::sin(rotation);
        const double cosRotation = std::cos(rotation);
        std::array<Vertex, 4> corners{};
        for (int corner = 0; corner < 4; ++corner) {
            const double localX = ((corner & 2) - 1) * size;
            const double localY = (((corner + 1) & 2) - 1) * size;
            const double rotatedX = localX * cosRotation - localY * sinRotation;
            const double rotatedY = localY * cosRotation + localX * sinRotation;
            const double horizontal = rotatedX * sinPitch;
            const double vertical = -rotatedX * cosPitch;
            corners[static_cast<std::size_t>(corner)].position = {
                static_cast<float>(centerX + vertical * sinYaw - rotatedY * cosYaw),
                static_cast<float>(centerY + horizontal),
                static_cast<float>(centerZ + rotatedY * sinYaw + vertical * cosYaw)};
        }
        quad(result, corners[0], corners[1], corners[2], corners[3]);
    }
    return result;
}

std::vector<Vertex> celestialQuad(float size, float y, float u0, float v0, float u1, float v1) {
    std::vector<Vertex> result;
    quad(result, {{-size, y, -size}, {u0, v0}, {}}, {{size, y, -size}, {u1, v0}, {}},
         {{size, y, size}, {u1, v1}, {}}, {{-size, y, size}, {u0, v1}, {}});
    return result;
}

std::vector<Vertex> sunriseFan(float angle) {
    const float horizonCosine = std::cos(angle * pi * 2.0F);
    if (horizonCosine < -0.4F || horizonCosine > 0.4F) return {};
    const float phase = horizonCosine / 0.4F * 0.5F + 0.5F;
    float alpha = 1.0F - (1.0F - std::sin(phase * pi)) * 0.99F;
    alpha *= alpha;
    const glm::vec4 centerColor{phase * 0.3F + 0.7F, phase * phase * 0.7F + 0.2F, 0.2F, alpha};
    const glm::vec4 edgeColor{centerColor.r, centerColor.g, centerColor.b, 0.0F};
    std::vector<Vertex> result;
    result.reserve(16 * 3);
    for (int segment = 0; segment < 16; ++segment) {
        const float a0 = segment * pi * 2.0F / 16.0F;
        const float a1 = (segment + 1) * pi * 2.0F / 16.0F;
        triangle(result, {{0.0F, 0.0F, 0.0F}, {}, centerColor},
                 {{std::sin(a0) * 120.0F, std::cos(a0) * 120.0F,
                   -std::cos(a0) * 40.0F * alpha}, {}, edgeColor},
                 {{std::sin(a1) * 120.0F, std::cos(a1) * 120.0F,
                   -std::cos(a1) * 40.0F * alpha}, {}, edgeColor});
    }
    return result;
}

float vanillaBrightness(float level) {
    const float darkness = 1.0F - std::clamp(level, 0.0F, 15.0F) / 15.0F;
    return (1.0F - darkness) / (darkness * 3.0F + 1.0F);
}

glm::vec3 weatherLight(const World& world, int x, int y, int z,
                       const EnvironmentFrame& frame, bool snow) {
    const float sky = vanillaBrightness(std::max(0.0F,
        static_cast<float>(world.getSkyLight(x, y, z)) - frame.skyLightSubtracted));
    const float block = vanillaBrightness(static_cast<float>(world.getBlockLight(x, y, z))) * 1.5F;
    glm::vec3 light = glm::clamp(glm::vec3(
        sky + block,
        sky + block * ((block * 0.6F + 0.4F) * 0.6F + 0.4F),
        sky + block * (block * block * 0.6F + 0.4F)) * 0.96F + 0.03F,
        glm::vec3(0.0F), glm::vec3(1.0F));
    if (snow) light = light * 0.75F + 0.25F;
    return light;
}

std::vector<Vertex> lightningBolt(const glm::vec3& position, std::int64_t seed) {
    std::vector<Vertex> result;
    std::array<double, 8> offsetX{};
    std::array<double, 8> offsetZ{};
    double runningX = 0.0;
    double runningZ = 0.0;
    JavaRandom initial(seed);
    for (int index = 7; index >= 0; --index) {
        offsetX[static_cast<std::size_t>(index)] = runningX;
        offsetZ[static_cast<std::size_t>(index)] = runningZ;
        runningX += initial.nextInt(11) - 5;
        runningZ += initial.nextInt(11) - 5;
    }

    const glm::vec4 color{0.45F, 0.45F, 0.5F, 0.3F};
    for (int layer = 0; layer < 4; ++layer) {
        JavaRandom random(seed);
        for (int branch = 0; branch < 3; ++branch) {
            const int first = branch == 0 ? 7 : 7 - branch;
            const int last = branch == 0 ? 0 : first - 2;
            double nextX = offsetX[static_cast<std::size_t>(first)] - runningX;
            double nextZ = offsetZ[static_cast<std::size_t>(first)] - runningZ;
            for (int segment = first; segment >= last; --segment) {
                const double previousX = nextX;
                const double previousZ = nextZ;
                const int spread = branch == 0 ? 11 : 31;
                const int center = branch == 0 ? 5 : 15;
                nextX += random.nextInt(spread) - center;
                nextZ += random.nextInt(spread) - center;
                double upperRadius = 0.1 + layer * 0.2;
                double lowerRadius = 0.1 + layer * 0.2;
                if (branch == 0) {
                    upperRadius *= segment * 0.1 + 1.0;
                    lowerRadius *= (segment - 1) * 0.1 + 1.0;
                }
                std::array<glm::vec3, 5> upper{};
                std::array<glm::vec3, 5> lower{};
                for (int corner = 0; corner < 5; ++corner) {
                    double upperX = position.x + 0.5 - upperRadius;
                    double upperZ = position.z + 0.5 - upperRadius;
                    double lowerX = position.x + 0.5 - lowerRadius;
                    double lowerZ = position.z + 0.5 - lowerRadius;
                    if (corner == 1 || corner == 2) {
                        upperX += upperRadius * 2.0;
                        lowerX += lowerRadius * 2.0;
                    }
                    if (corner == 2 || corner == 3) {
                        upperZ += upperRadius * 2.0;
                        lowerZ += lowerRadius * 2.0;
                    }
                    upper[static_cast<std::size_t>(corner)] = {
                        static_cast<float>(upperX + previousX), position.y + (segment + 1) * 16.0F,
                        static_cast<float>(upperZ + previousZ)};
                    lower[static_cast<std::size_t>(corner)] = {
                        static_cast<float>(lowerX + nextX), position.y + segment * 16.0F,
                        static_cast<float>(lowerZ + nextZ)};
                }
                for (int side = 0; side < 4; ++side) {
                    quad(result, {lower[static_cast<std::size_t>(side)], {}, color},
                         {upper[static_cast<std::size_t>(side)], {}, color},
                         {upper[static_cast<std::size_t>(side + 1)], {}, color},
                         {lower[static_cast<std::size_t>(side + 1)], {}, color});
                }
            }
        }
    }
    return result;
}

constexpr std::string_view vertexSource = R"glsl(
#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec2 vertexUv;
layout (location = 2) in vec4 vertexColor;
out vec2 uv;
out vec4 color;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 uvOffset;
void main() {
    uv = vertexUv + uvOffset;
    color = vertexColor;
    gl_Position = projection * view * model * vec4(vertexPosition, 1.0);
}
)glsl";

constexpr std::string_view fragmentSource = R"glsl(
#version 330 core
in vec2 uv;
in vec4 color;
out vec4 fragmentColor;
uniform sampler2D image;
uniform bool textured;
uniform bool alphaCutout;
uniform vec4 globalColor;
void main() {
    vec4 sampleColor = textured ? texture(image, uv) : vec4(1.0);
    if (alphaCutout && sampleColor.a < 0.1) discard;
    fragmentColor = sampleColor * color * globalColor;
}
)glsl";

} // namespace

struct EnvironmentRenderer::Implementation {
    explicit Implementation(const Environment& environmentIn)
        : environment(environmentIn), shader(vertexSource, fragmentSource),
          modelLocation(shader.uniformLocation("model")),
          viewLocation(shader.uniformLocation("view")),
          projectionLocation(shader.uniformLocation("projection")),
          globalColorLocation(shader.uniformLocation("globalColor")),
          uvOffsetLocation(shader.uniformLocation("uvOffset")),
          texturedLocation(shader.uniformLocation("textured")),
          alphaCutoutLocation(shader.uniformLocation("alphaCutout")),
          imageLocation(shader.uniformLocation("image")),
          sunTexture(loadTexture("sun.png", false)),
          moonTexture(loadTexture("moon_phases.png", false)),
          cloudTexture(loadTexture("clouds.png", true)),
          rainTexture(loadTexture("rain.png", true)), snowTexture(loadTexture("snow.png", true)) {
        sky.upload(skyCube());
        starMesh.upload(stars());
        cloudMesh.upload(buildFancyCloudGeometry());
        sunMesh.upload(celestialQuad(30.0F, 100.0F, 0.0F, 0.0F, 1.0F, 1.0F));
        for (int phase = 0; phase < 8; ++phase) {
            const int column = phase % 4;
            const int row = phase / 4;
            constexpr float inset = 0.001F;
            const float u0 = column / 4.0F + inset;
            const float u1 = (column + 1) / 4.0F - inset;
            const float v0 = row / 2.0F + inset;
            const float v1 = (row + 1) / 2.0F - inset;
            moonMeshes[static_cast<std::size_t>(phase)].upload(
                celestialQuad(20.0F, -100.0F, u1, v1, u0, v0));
        }
        shader.use();
        shader.setInt(imageLocation, 0);
        rainVertices.reserve(441 * 6);
        snowVertices.reserve(441 * 6);
    }

    ~Implementation() {
        const std::array textures{sunTexture, moonTexture, cloudTexture, rainTexture, snowTexture};
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    }

    void uniforms(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection,
                  const glm::vec4& color, bool textured, bool cutout, const glm::vec2& uvOffset = {}) {
        shader.use();
        shader.setMat4(modelLocation, model);
        shader.setMat4(viewLocation, view);
        shader.setMat4(projectionLocation, projection);
        glUniform4fv(globalColorLocation, 1, glm::value_ptr(color));
        glUniform2fv(uvOffsetLocation, 1, glm::value_ptr(uvOffset));
        glUniform1i(texturedLocation, textured ? 1 : 0);
        glUniform1i(alphaCutoutLocation, cutout ? 1 : 0);
    }

    int precipitationHeight(const World& world, int x, int z) {
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
                                  static_cast<std::uint32_t>(z);
        const int chunkX = World::floorDiv16(x);
        const int chunkZ = World::floorDiv16(z);
        const std::uint64_t epoch = world.chunkEpoch(chunkX, chunkZ);
        const auto cached = precipitationHeights.find(key);
        if (cached != precipitationHeights.end() && cached->second.epoch == epoch)
            return cached->second.height;

        int height = 0;
        for (int y = chunkHeight - 1; y >= 0; --y) {
            const BlockDefinition& block = BlockRegistry::get(world.getBlock(x, y, z));
            if (block.opaque || block.fullCube) { height = y + 1; break; }
        }
        if (precipitationHeights.size() >= 8192) precipitationHeights.clear();
        precipitationHeights.insert_or_assign(key, HeightEntry{epoch, height});
        return height;
    }

    const Environment& environment;
    Shader shader;
    GLint modelLocation = -1;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    GLint globalColorLocation = -1;
    GLint uvOffsetLocation = -1;
    GLint texturedLocation = -1;
    GLint alphaCutoutLocation = -1;
    GLint imageLocation = -1;
    GLuint sunTexture = 0;
    GLuint moonTexture = 0;
    GLuint cloudTexture = 0;
    GLuint rainTexture = 0;
    GLuint snowTexture = 0;
    Mesh sky;
    Mesh starMesh;
    Mesh sunriseMesh;
    Mesh sunMesh;
    std::array<Mesh, 8> moonMeshes;
    Mesh cloudMesh;
    Mesh weatherMesh;
    Mesh lightningMesh;
    std::vector<Vertex> rainVertices;
    std::vector<Vertex> snowVertices;
    std::int64_t lightningMeshSeed = 0;
    bool lightningMeshValid = false;
    struct HeightEntry { std::uint64_t epoch = 0; int height = 0; };
    std::unordered_map<std::uint64_t, HeightEntry> precipitationHeights;
};

EnvironmentRenderer::EnvironmentRenderer(const Environment& environment)
    : implementation_(std::make_unique<Implementation>(environment)) {}

EnvironmentRenderer::~EnvironmentRenderer() = default;

void EnvironmentRenderer::renderSky(const EnvironmentFrame& frame, const glm::vec3& cameraPosition,
                                    const glm::mat4& view, const glm::mat4& projection) {
    auto& renderer = *implementation_;
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const glm::mat4 skyModel = glm::translate(glm::mat4(1.0F), cameraPosition);
    renderer.uniforms(skyModel, view, projection, glm::vec4(frame.skyColor, 1.0F), false, false);
    renderer.sky.draw();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const std::vector<Vertex> sunrise = sunriseFan(frame.celestialAngle);
    if (!sunrise.empty()) {
        renderer.sunriseMesh.upload(sunrise, GL_STREAM_DRAW);
        glm::mat4 sunriseModel = skyModel;
        sunriseModel = glm::rotate(sunriseModel, glm::radians(90.0F), {1.0F, 0.0F, 0.0F});
        if (std::sin(frame.celestialAngle * pi * 2.0F) < 0.0F)
            sunriseModel = glm::rotate(sunriseModel, glm::radians(180.0F), {0.0F, 0.0F, 1.0F});
        sunriseModel = glm::rotate(sunriseModel, glm::radians(90.0F), {0.0F, 0.0F, 1.0F});
        renderer.uniforms(sunriseModel, view, projection, glm::vec4(1.0F), false, false);
        renderer.sunriseMesh.draw();
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glm::mat4 celestial = skyModel;
    celestial = glm::rotate(celestial, glm::radians(-90.0F), {0.0F, 1.0F, 0.0F});
    celestial = glm::rotate(celestial, frame.celestialAngle * pi * 2.0F, {1.0F, 0.0F, 0.0F});
    renderer.uniforms(celestial, view, projection,
        glm::vec4(frame.starBrightness, frame.starBrightness, frame.starBrightness, frame.starBrightness), false, false);
    renderer.starMesh.draw();

    const float weatherAlpha = 1.0F - frame.rainStrength;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.sunTexture);
    renderer.uniforms(celestial, view, projection, {1.0F, 1.0F, 1.0F, weatherAlpha}, true, true);
    renderer.sunMesh.draw();

    glBindTexture(GL_TEXTURE_2D, renderer.moonTexture);
    renderer.uniforms(celestial, view, projection, {1.0F, 1.0F, 1.0F, weatherAlpha}, true, true);
    renderer.moonMeshes[static_cast<std::size_t>(frame.moonPhase & 7)].draw();
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void EnvironmentRenderer::renderClouds(const EnvironmentFrame& frame, const glm::vec3& cameraPosition,
                                       const glm::mat4& view, const glm::mat4& projection) {
    auto& renderer = *implementation_;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    const float time = static_cast<float>(frame.rendererTicks) + frame.partialTick;
    const float drift = time * 0.03F;
    double cloudX = (static_cast<double>(cameraPosition.x) + drift) / 12.0;
    double cloudZ = static_cast<double>(cameraPosition.z) / 12.0 + 0.33000001311302185;
    // RenderGlobal wraps both texture coordinates every 2048 cloud texels.
    // Retain world-space placement while reproducing its wrapped UV origin and
    // the otherwise easy-to-miss 0.33 Z offset.
    cloudX -= std::floor(cloudX / 2048.0) * 2048.0;
    cloudZ -= std::floor(cloudZ / 2048.0) * 2048.0;
    const float cloudCellX = static_cast<float>(std::floor(cloudX));
    const float cloudCellZ = static_cast<float>(std::floor(cloudZ));
    const float fractionX = static_cast<float>(cloudX - std::floor(cloudX));
    const float fractionZ = static_cast<float>(cloudZ - std::floor(cloudZ));
    const glm::vec3 origin{cameraPosition.x - fractionX * 12.0F, 128.33F,
                           cameraPosition.z - fractionZ * 12.0F};
    glm::mat4 model = glm::translate(glm::mat4(1.0F), origin);
    model = glm::scale(model, {12.0F, 1.0F, 12.0F});
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.cloudTexture);
    renderer.uniforms(model, view, projection, glm::vec4(frame.cloudColor, 1.0F), true, true,
                      {cloudCellX / 256.0F, cloudCellZ / 256.0F});
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    renderer.cloudMesh.draw();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    renderer.cloudMesh.draw();
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void EnvironmentRenderer::renderWeather(const EnvironmentFrame& frame, const World& world,
                                        const glm::vec3& cameraPosition, const glm::mat4& view,
                                        const glm::mat4& projection) {
    if (frame.rainStrength <= 0.0F && !frame.lightningVisible) return;
    auto& renderer = *implementation_;
    const int centerX = static_cast<int>(std::floor(cameraPosition.x));
    const int centerY = static_cast<int>(std::floor(cameraPosition.y));
    const int centerZ = static_cast<int>(std::floor(cameraPosition.z));
    constexpr int radius = 10;
    std::vector<Vertex>& rain = renderer.rainVertices;
    std::vector<Vertex>& snow = renderer.snowVertices;
    rain.clear();
    snow.clear();
    for (int z = centerZ - radius; z <= centerZ + radius; ++z) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const int surface = renderer.precipitationHeight(world, x, z);
            int bottom = std::max(centerY - radius, surface);
            int top = std::max(centerY + radius, surface);
            if (bottom == top) continue;
            const PrecipitationType type = renderer.environment.precipitationAt(world, x, surface, z);
            if (type == PrecipitationType::None) continue;
            float dx = static_cast<float>(x - centerX);
            float dz = static_cast<float>(z - centerZ);
            const float length = std::sqrt(dx * dx + dz * dz);
            if (length > 0.0F) { dx /= length; dz /= length; }
            const float sideX = -dz * 0.5F;
            const float sideZ = dx * 0.5F;
            const float distance = length / radius;
            const float alpha = ((1.0F - distance * distance) *
                (type == PrecipitationType::Rain ? 0.5F : 0.3F) + 0.5F) * frame.rainStrength;
            const std::uint32_t wrappedX = static_cast<std::uint32_t>(x);
            const std::uint32_t wrappedZ = static_cast<std::uint32_t>(z);
            const std::uint32_t hash = (wrappedX * wrappedX * 3121U + wrappedX * 45238971U) ^
                                       (wrappedZ * wrappedZ * 418711U + wrappedZ * 13761U);
            const float scroll = -(static_cast<float>((frame.rendererTicks + (hash & 31U)) % 32U) +
                                   frame.partialTick) / 32.0F;
            std::vector<Vertex>& output = type == PrecipitationType::Rain ? rain : snow;
            const float uvScale = type == PrecipitationType::Rain ? 0.25F : 0.05F;
            const glm::vec3 light = weatherLight(world, x, std::max(surface, centerY), z, frame,
                                                 type == PrecipitationType::Snow);
            const glm::vec4 color{light, std::clamp(alpha, 0.0F, 1.0F)};
            quad(output,
                 {{x + 0.5F - sideX, static_cast<float>(top), z + 0.5F - sideZ}, {0.0F, bottom * uvScale + scroll}, color},
                 {{x + 0.5F + sideX, static_cast<float>(top), z + 0.5F + sideZ}, {1.0F, bottom * uvScale + scroll}, color},
                 {{x + 0.5F + sideX, static_cast<float>(bottom), z + 0.5F + sideZ}, {1.0F, top * uvScale + scroll}, color},
                 {{x + 0.5F - sideX, static_cast<float>(bottom), z + 0.5F - sideZ}, {0.0F, top * uvScale + scroll}, color});
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const glm::mat4 identity{1.0F};
    if (!rain.empty()) {
        renderer.weatherMesh.upload(rain, GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer.rainTexture);
        renderer.uniforms(identity, view, projection, glm::vec4(1.0F), true, true);
        renderer.weatherMesh.draw();
    }
    if (!snow.empty()) {
        renderer.weatherMesh.upload(snow, GL_STREAM_DRAW);
        glBindTexture(GL_TEXTURE_2D, renderer.snowTexture);
        renderer.uniforms(identity, view, projection, glm::vec4(1.0F), true, true);
        renderer.weatherMesh.draw();
    }

    if (frame.lightningVisible) {
        if (!renderer.lightningMeshValid || renderer.lightningMeshSeed != frame.lightningSeed) {
            renderer.lightningMesh.upload(lightningBolt(frame.lightningPosition, frame.lightningSeed));
            renderer.lightningMeshSeed = frame.lightningSeed;
            renderer.lightningMeshValid = true;
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        renderer.uniforms(identity, view, projection, glm::vec4(1.0F), false, false);
        renderer.lightningMesh.draw();
    }
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
