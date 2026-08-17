#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>

#include "items/ItemRegistry.hpp"
#include "items/ItemStack.hpp"
#include "world/Raycast.hpp"

struct GLFWwindow;
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
    GameHud(GLFWwindow* window, const std::filesystem::path& assetRoot,
            TextureAtlas& blockAtlas, const ItemRegistry& items);
    ~GameHud();
    GameHud(const GameHud&) = delete;
    GameHud& operator=(const GameHud&) = delete;

    void beginFrame();
    void render(const World& world, Player& player, const Camera& camera,
                const WorldConfig& config, const ChunkStreamer& streamer,
                const LightingEngine& lighting, const WorldRenderer& renderer,
                const std::optional<RaycastHit>& hit,
                int framebufferWidth, int framebufferHeight, double framesPerSecond,
                bool showDebug, bool paused, bool inventoryOpen);
    void endFrame();

private:
    GLuint loadExactTexture(const std::filesystem::path& path, int expectedWidth,
                            int expectedHeight, std::vector<unsigned char>* rgba = nullptr);
    void buildAsciiWidths(const std::vector<unsigned char>& pixels);
    [[nodiscard]] float textWidth(std::string_view text) const;
    void drawText(float x, float y, std::string_view text, int scaleFactor,
                  bool rightAligned = false, unsigned int color = 0xFFE0E0E0U) const;
    void drawDebugLine(float x, float y, std::string_view text, int scaleFactor,
                       bool rightAligned = false) const;
    void drawStack(const ItemStack& stack, float x, float y, int scaleFactor,
                   bool count = true) const;
    void drawTooltip(const ItemStack& stack, float mouseX, float mouseY,
                     int scaledWidth, int scaledHeight, int scaleFactor) const;
    void renderHotbar(const Player& player, int scaledWidth, int scaledHeight,
                      int scaleFactor) const;
    void renderInventory(Player& player, bool creative, int scaledWidth,
                         int scaledHeight, int scaleFactor);
    void renderSurvivalInventory(Player& player, int scaledWidth,
                                 int scaledHeight, int scaleFactor);
    void renderCreativeInventory(Player& player, int scaledWidth,
                                 int scaledHeight, int scaleFactor);
    void interactInventorySlot(ItemStack& slot, bool rightClick, bool creative);
    void renderDebug(const World& world, const Player& player, const Camera& camera,
                     const WorldConfig& config, const ChunkStreamer& streamer,
                     const LightingEngine& lighting, const WorldRenderer& renderer,
                     const std::optional<RaycastHit>& hit, int scaledWidth, int scaledHeight,
                     int scaleFactor, int framebufferWidth, int framebufferHeight,
                     double framesPerSecond) const;

    GLFWwindow* window_ = nullptr;
    TextureAtlas& blockAtlas_;
    const ItemRegistry& items_;
    GLuint widgetsTexture_ = 0;
    GLuint asciiTexture_ = 0;
    GLuint inventoryTexture_ = 0;
    GLuint creativeItemsTexture_ = 0;
    GLuint creativeSearchTexture_ = 0;
    GLuint creativeInventoryTexture_ = 0;
    GLuint creativeTabsTexture_ = 0;
    std::array<int, 256> charWidths_{};
    ItemStack cursorStack_{};
    CreativeTab selectedCreativeTab_ = CreativeTab::BuildingBlocks;
    std::string searchText_;
    int creativeScrollRow_ = 0;
};
