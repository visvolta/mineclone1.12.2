#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>
#include <glm/vec3.hpp>

#include "items/ItemRegistry.hpp"
#include "crafting/CraftingSystem.hpp"
#include "items/ItemStack.hpp"
#include "world/Raycast.hpp"
#include "world/BlockEntitySystem.hpp"

struct GLFWwindow;
class Camera;
class LightingEngine;
class Player;
class TextureAtlas;
class BlockRenderResources;
class World;
class WorldRenderer;
class ChunkStreamer;
struct WorldConfig;
struct ExperienceDrop { glm::dvec3 position{0.0}; int value = 0; };

class GameHud {
public:
    GameHud(GLFWwindow* window, const std::filesystem::path& assetRoot,
            TextureAtlas& blockAtlas, const ItemRegistry& items,
            const BlockRenderResources& resources, BlockEntitySystem& blockEntities);
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

    void openBlockEntityScreen(const BlockEntityAction& action);
    void closeBlockEntityScreen(Player* player = nullptr);
    void closePlayerCrafting(Player& player);
    [[nodiscard]] std::vector<ItemStack> takeCraftingDrops();
    [[nodiscard]] std::vector<ExperienceDrop> takeExperienceDrops();
    [[nodiscard]] bool hasBlockEntityScreen() const { return activeBlockEntityAction_.has_value(); }
    bool consumeScreenCloseRequest() { const bool value=screenCloseRequested_; screenCloseRequested_=false; return value; }
    bool consumeResumeRequest() { const bool value=resumeRequested_; resumeRequested_=false; return value; }
    bool consumeReturnToTitleRequest() { const bool value=returnToTitleRequested_; returnToTitleRequested_=false; return value; }
    bool consumeRespawnRequest() { const bool value=respawnRequested_; respawnRequested_=false; return value; }
    [[nodiscard]] bool capturesTextInput() const { return searchFocused_ || (activeBlockEntityAction_ && activeBlockEntityAction_->type == BlockEntityActionType::EditSign); }

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
    bool drawBlockModelStack(const ItemStack& stack, float x, float y, int scaleFactor) const;
    bool drawBuiltinEntityStack(const ItemStack& stack, float x, float y, int scaleFactor) const;
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
    void renderBlockEntityScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderContainerScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderFurnaceScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderHopperScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderDispenserScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderBrewingScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderEnchantingScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderBeaconScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderCraftingTableScreen(Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderSignEditor(int scaledWidth, int scaledHeight, int scaleFactor);
    void renderPauseMenu(int scaledWidth, int scaledHeight, int scaleFactor);
    void renderSurvivalStatus(const Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    void renderDeathScreen(const Player& player, int scaledWidth, int scaledHeight, int scaleFactor);
    bool menuButton(int id, float x, float y, float width, std::string_view label,
                    int scaleFactor, bool enabled = true);
    void renderDebug(const World& world, const Player& player, const Camera& camera,
                     const WorldConfig& config, const ChunkStreamer& streamer,
                     const LightingEngine& lighting, const WorldRenderer& renderer,
                     const std::optional<RaycastHit>& hit, int scaledWidth, int scaledHeight,
                     int scaleFactor, int framebufferWidth, int framebufferHeight,
                     double framesPerSecond) const;

    GLFWwindow* window_ = nullptr;
    TextureAtlas& blockAtlas_;
    const ItemRegistry& items_;
    const BlockRenderResources& resources_;
    BlockEntitySystem& blockEntities_;
    CraftingSystem crafting_;
    GLuint widgetsTexture_ = 0;
    GLuint iconsTexture_ = 0;
    GLuint asciiTexture_ = 0;
    GLuint inventoryTexture_ = 0;
    GLuint creativeItemsTexture_ = 0;
    GLuint creativeSearchTexture_ = 0;
    GLuint creativeInventoryTexture_ = 0;
    GLuint creativeTabsTexture_ = 0;
    GLuint generic54Texture_ = 0;
    GLuint craftingTableTexture_ = 0;
    GLuint furnaceTexture_ = 0;
    GLuint hopperTexture_ = 0;
    GLuint dispenserTexture_ = 0;
    GLuint brewingTexture_ = 0;
    GLuint enchantingTexture_ = 0;
    GLuint beaconTexture_ = 0;
    GLuint chestItemTexture_ = 0;
    GLuint trappedChestItemTexture_ = 0;
    GLuint enderChestItemTexture_ = 0;
    std::array<GLuint, 16> bedItemTextures_{};
    std::array<GLuint, 16> shulkerItemTextures_{};
    std::array<int, 256> charWidths_{};
    ItemStack cursorStack_{};
    std::array<ItemStack, 4> playerCraftGrid_{};
    std::array<ItemStack, 9> tableCraftGrid_{};
    int hudUpdateCounter_ = 0;
    int playerHealthDisplay_ = 20;
    int lastPlayerHealthDisplay_ = 20;
    int healthFlashUntil_ = 0;
    std::uint64_t lastHealthChangeMs_ = 0;
    CreativeTab selectedCreativeTab_ = CreativeTab::BuildingBlocks;
    std::string searchText_;
    bool searchFocused_ = false;
    int creativeScrollRow_ = 0;
    std::optional<BlockEntityAction> activeBlockEntityAction_;
    int signEditLine_ = 0;
    bool screenCloseRequested_ = false;
    bool resumeRequested_ = false;
    bool returnToTitleRequested_ = false;
    bool respawnRequested_ = false;
    bool frameHostOpen_ = false;
    std::vector<ItemStack> pendingCraftingDrops_{};
    std::vector<ExperienceDrop> experienceDrops_{};
};
