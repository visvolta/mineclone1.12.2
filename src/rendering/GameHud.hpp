#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>

#include "world/Raycast.hpp"

struct GLFWwindow;
class BlockInteraction;
class Camera;
class LightingEngine;
class Player;
class TextureAtlas;
class World;
class WorldRenderer;
class ChunkStreamer;
struct WorldConfig;

class GameHud {
public:
    GameHud(GLFWwindow* window, const std::filesystem::path& assetRoot, TextureAtlas& blockAtlas);
    ~GameHud();
    GameHud(const GameHud&) = delete;
    GameHud& operator=(const GameHud&) = delete;

    void beginFrame();
    void render(const World& world, const Player& player, const Camera& camera,
                const BlockInteraction& interaction, const WorldConfig& config,
                const ChunkStreamer& streamer, const LightingEngine& lighting,
                const WorldRenderer& renderer, const std::optional<RaycastHit>& hit,
                int framebufferWidth, int framebufferHeight, double framesPerSecond,
                bool showDebug, bool paused);
    void endFrame();

private:
    GLuint loadExactTexture(const std::filesystem::path& path, int expectedWidth,
                            int expectedHeight, std::vector<unsigned char>* rgba = nullptr);
    void buildAsciiWidths(const std::vector<unsigned char>& pixels);
    [[nodiscard]] float textWidth(std::string_view text) const;
    void drawText(float x, float y, std::string_view text, int scaleFactor,
                  bool rightAligned = false) const;
    void drawDebugLine(float x, float y, std::string_view text, int scaleFactor,
                       bool rightAligned = false) const;
    void renderHotbar(const BlockInteraction& interaction, int scaledWidth, int scaledHeight,
                      int scaleFactor) const;
    void renderDebug(const World& world, const Player& player, const Camera& camera,
                     const WorldConfig& config, const ChunkStreamer& streamer,
                     const LightingEngine& lighting, const WorldRenderer& renderer,
                     const std::optional<RaycastHit>& hit, int scaledWidth, int scaledHeight,
                     int scaleFactor, int framebufferWidth, int framebufferHeight,
                     double framesPerSecond) const;

    GLFWwindow* window_ = nullptr;
    TextureAtlas& blockAtlas_;
    GLuint widgetsTexture_ = 0;
    GLuint asciiTexture_ = 0;
    std::array<int, 256> charWidths_{};
};
