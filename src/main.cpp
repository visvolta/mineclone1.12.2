#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>

#include "rendering/Camera.hpp"
#include "client/ScaledResolution.hpp"
#include "client/FrontEnd.hpp"
#include "items/ItemRegistry.hpp"
#include "environment/Environment.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/BlockInteraction.hpp"
#include "player/Player.hpp"
#include "rendering/DebugRenderer.hpp"
#include "rendering/BlockEntityRenderer.hpp"
#include "rendering/BiomeColors.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/EnvironmentRenderer.hpp"
#include "rendering/GameHud.hpp"
#include "rendering/EntityRenderer.hpp"
#include "rendering/TextureAtlas.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/Raycast.hpp"
#include "save/WorldSave.hpp"
#include "world/World.hpp"
#include "world/BlockEntitySystem.hpp"
#include "world/DynamicBlockSystem.hpp"
#include "world/ItemEntitySystem.hpp"
#include "entity/EntityManager.hpp"
#include "world/RedstoneSystem.hpp"
#include "worldgen/ChunkStreamer.hpp"
#include "worldgen/WorldConfig.hpp"

namespace {

constexpr int initialWidth = 1280;
constexpr int initialHeight = 720;
constexpr double tickDuration = 1.0 / 20.0;

Camera camera;
bool firstMouseEvent = true;
bool cursorCaptured = true;
double previousMouseX = initialWidth / 2.0;
double previousMouseY = initialHeight / 2.0;
double pendingScroll = 0.0;

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouseCallback(GLFWwindow*, double xPosition, double yPosition) {
    if (!cursorCaptured) {
        previousMouseX = xPosition;
        previousMouseY = yPosition;
        firstMouseEvent = true;
        return;
    }
    if (firstMouseEvent) {
        previousMouseX = xPosition;
        previousMouseY = yPosition;
        firstMouseEvent = false;
        return;
    }
    camera.look(static_cast<float>(xPosition - previousMouseX),
                static_cast<float>(previousMouseY - yPosition));
    previousMouseX = xPosition;
    previousMouseY = yPosition;
}

void scrollCallback(GLFWwindow*, double, double yOffset) {
    if (cursorCaptured) pendingScroll += yOffset;
}

bool keyDown(GLFWwindow* window, int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

PlayerInput readPlayerInput(GLFWwindow* window, bool jumpPressed) {
    PlayerInput input;
    input.forward = (keyDown(window, GLFW_KEY_W) ? 1.0F : 0.0F) -
                    (keyDown(window, GLFW_KEY_S) ? 1.0F : 0.0F);
    input.strafe = (keyDown(window, GLFW_KEY_D) ? 1.0F : 0.0F) -
                   (keyDown(window, GLFW_KEY_A) ? 1.0F : 0.0F);
    input.jump = keyDown(window, GLFW_KEY_SPACE);
    input.jumpPressed = jumpPressed;
    input.sneak = keyDown(window, GLFW_KEY_LEFT_SHIFT);
    input.sprint = keyDown(window, GLFW_KEY_LEFT_CONTROL) && input.forward >= 0.8F;
    return input;
}

void setCursorCaptured(GLFWwindow* window, bool captured) {
    cursorCaptured = captured;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    firstMouseEvent = true;
    pendingScroll = 0.0;
}

void updateWindowTitle(GLFWwindow* window, const Player& player,
                       const ItemRegistry& items, const WorldConfig& config,
                       double framesPerSecond, std::size_t loadedChunks,
                       const ChunkStreamer& streamer, const LightingEngine& lighting,
                       const WorldRenderer& renderer) {
    const char* mode = player.gameMode() == GameMode::Survival ? "Survival" : "Creative";
    const WorldRenderStats& stats = renderer.stats();
    std::ostringstream title;
    title << "Blockcraft 1.12.2 | " << std::fixed << std::setprecision(0) << framesPerSecond << " FPS"
          << " | chunks " << loadedChunks
          << " | gen " << streamer.pendingGenerationCount()
          << " | light " << lighting.pendingCount()
          << " | mesh " << renderer.pendingMeshCount()
          << " | visible " << stats.visibleSections << '/' << stats.totalSections
          << " | draws " << stats.drawCalls
          << " | " << worldTypeName(config.worldType)
          << " | " << mode;
    const ItemStack& held = player.inventory().selected();
    if (!held.empty()) title << " | " << items.get(held.itemId).displayName;
    glfwSetWindowTitle(window, title.str().c_str());
}

double spawnHeight(const World& world, int x, int z) {
    for (int y = chunkHeight - 1; y >= 0; --y) {
        if (BlockRegistry::get(world.getBlock(x, y, z)).opaque)
            return static_cast<double>(y + 1);
    }
    return 80.0;
}

} // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight, "Blockcraft 1.12.2", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create the window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    setCursorCaptured(window, true);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        std::cerr << "Failed to load OpenGL functions.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    try {
        const WorldConfig clientDefaults = WorldConfig::load(BLOCKCRAFT_CONFIG_PATH);
        glfwSwapInterval(clientDefaults.vsync ? 1 : 0);

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        // Stage 8 starts in the 1.12.2-style title screen rather than creating
        // an implicit technical world. The frontend owns its temporary ImGui
        // context and releases it before GameHud constructs the in-game one.
        setCursorCaptured(window, false);
        auto frontEnd = std::make_unique<FrontEnd>(
            window, BLOCKCRAFT_ASSET_ROOT, BLOCKCRAFT_SAVES_ROOT, clientDefaults);
        std::optional<std::filesystem::path> selectedWorld = frontEnd->run();
        if (!selectedWorld || glfwWindowShouldClose(window) == GLFW_TRUE) break;

        frontEnd->showLoading("Loading world", "Reading world data", 5);
        ItemRegistry itemRegistry(BLOCKCRAFT_ASSET_ROOT);
        frontEnd->showLoading("Loading world", "Loading item registry", 12);
        WorldSave worldSave(*selectedWorld, itemRegistry);
        const WorldConfig config = worldSave.loadConfig(clientDefaults);
        frontEnd->showLoading("Loading world", "Reading level.dat", 18);
        glfwSwapInterval(config.vsync ? 1 : 0);

        std::cout << "World folder: " << selectedWorld->string() << "\n"
                  << "World seed: " << config.seed << " (" << config.seedText << ")\n"
                  << "World type: " << worldTypeName(config.worldType) << "\n"
                  << "View distance: " << config.viewDistance << " chunks\n"
                  << "GUI scale: " << config.guiScale << " (0 = 1.12.2 Auto)\n"
                  << "Day cycle: " << (config.daylightCycle ?
                      std::to_string(config.dayCycleSeconds) + " seconds" : "disabled") << "\n"
                  << "Weather: " << weatherOverrideName(config.weatherOverride) << "\n"
                  << "Terrain/light/mesh workers: " << ThreadPool::recommendedWorkerCount() << "\n";
#ifdef BLOCKCRAFT_OPTIMIZED_DEBUG
        std::cout << "Build: optimized MSVC Debug with symbols\n";
#endif
#ifndef NDEBUG
        std::cout << "Runtime assertions: enabled\n";
#endif

        World world;
        BlockEntitySystem blockEntities;
        DynamicBlockSystem dynamicBlocks(static_cast<std::uint64_t>(config.seed));
        RedstoneSystem redstone;
        EntityManager entities(&itemRegistry);
        ItemEntitySystem itemEntities(entities);
        dynamicBlocks.setEntityManager(&entities);
        Player player({0.5, 80.0, 0.5});
        LoadedPlayerState loadedPlayer = worldSave.loadPlayer(player, &blockEntities);

        frontEnd->showLoading("Loading world", "Loading biome colours", 22);
        BiomeColors::load(BLOCKCRAFT_ASSET_ROOT);
        ChunkStreamer chunkStreamer(world, config, config.viewDistance, &worldSave, &blockEntities, &dynamicBlocks, &entities);
        frontEnd->showLoading("Loading world", "Building terrain", 25);
        chunkStreamer.prime(loadedPlayer.position.x, loadedPlayer.position.z, 1,
            [&](float progress) {
                frontEnd->showLoading("Loading world", "Building terrain",
                    25 + static_cast<int>(progress * 30.0F));
            });
        if (!loadedPlayer.hasPlayer) {
            loadedPlayer.position.y = spawnHeight(
                world, static_cast<int>(std::floor(loadedPlayer.position.x)),
                static_cast<int>(std::floor(loadedPlayer.position.z)));
            player.restoreState(loadedPlayer.position, glm::dvec3{0.0}, loadedPlayer.gameMode, loadedPlayer.selectedHotbar);
            player.setRespawnPosition(loadedPlayer.position);
        }

        frontEnd->showLoading("Loading world", "Lighting terrain", 58);
        LightingEngine lightingEngine(world, chunkStreamer.workers());
        for (const auto& [chunkKey, chunk] : world.chunks()) {
            static_cast<void>(chunkKey);
            lightingEngine.chunkLoaded(chunk->x(), chunk->z());
        }
        frontEnd->showLoading("Loading world", "Loading block models", 66);
        BlockRenderResources blockRenderResources(BLOCKCRAFT_ASSET_ROOT);
        frontEnd->showLoading("Loading world", "Building texture atlas", 74);
        TextureAtlas atlas(blockRenderResources.atlas());
        blockEntities.scanLoadedWorld(world);
        for (const auto& [unused, chunk] : world.chunks()) {
            (void)unused;
            if (chunk) { dynamicBlocks.scanChunk(world, chunk->x(), chunk->z()); redstone.scanChunk(world, chunk->x(), chunk->z()); }
        }
        frontEnd->showLoading("Loading world", "Preparing world renderer", 82);
        WorldRenderer worldRenderer(world, atlas, blockRenderResources, chunkStreamer.workers());
        Environment environment(config);
        worldSave.loadEnvironment(environment);
        frontEnd->showLoading("Loading world", "Loading weather and time", 88);
        EnvironmentRenderer environmentRenderer(environment);
        DebugRenderer debugRenderer(blockRenderResources);
        frontEnd->showLoading("Loading world", "Joining world", 100);
        frontEnd.reset();
        GameHud gameHud(window, BLOCKCRAFT_ASSET_ROOT, atlas, itemRegistry, blockRenderResources, blockEntities);
        BlockEntityRenderer blockEntityRenderer(BLOCKCRAFT_ASSET_ROOT, blockEntities);
        EntityRenderer entityRenderer(BLOCKCRAFT_ASSET_ROOT, itemRegistry, blockRenderResources, atlas);
        BlockInteraction interaction(itemRegistry, blockEntities, itemEntities, dynamicBlocks, redstone);
        camera.setPosition(player.eyePosition());
        setCursorCaptured(window, true);
        updateWindowTitle(window, player, itemRegistry, config, 0.0, world.chunkCount(),
                          chunkStreamer, lightingEngine, worldRenderer);

        double previousTime = glfwGetTime();
        double accumulator = 0.0;
        bool showDebug = false;
        bool f3WasDown = false;
        bool escapeWasDown = false;
        bool eWasDown = false;
        bool middleWasDown = false;
        bool inventoryOpen = false;
        bool paused = false;
        bool gWasDown = false;
        bool jumpWasDown = false;
        bool jumpPressPending = false;
        std::array<bool, 9> numberWasDown{};
        double titleIntervalStart = previousTime;
        std::size_t titleFrameCount = 0;
        double displayedFps = 0.0;
        using FrameClock = std::chrono::steady_clock;
        const auto targetFrameDuration = config.targetFps > 0
            ? std::chrono::duration_cast<FrameClock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(config.targetFps)))
            : FrameClock::duration::zero();
        auto nextFrameDeadline = FrameClock::now();
        double lastAutosave = previousTime;
        constexpr double autosaveIntervalSeconds = 30.0;
        bool returnToTitle = false;
        bool worldMouseArmed = true;

        while (glfwWindowShouldClose(window) == GLFW_FALSE && !returnToTitle) {
            glfwPollEvents();

            const bool escapeDown = keyDown(window, GLFW_KEY_ESCAPE);
            if (escapeDown && !escapeWasDown && !player.dead()) {
                if (inventoryOpen) {
                    inventoryOpen = false;
                    gameHud.closeBlockEntityScreen(&player);
                    gameHud.closePlayerCrafting(player);
                    for (ItemStack& dropped : gameHud.takeCraftingDrops())
                        itemEntities.spawn(dropped, player.feetPosition() + glm::dvec3(0.0, 0.5, 0.0));
                    setCursorCaptured(window, true);
                    worldMouseArmed = false;
                } else {
                    paused = !paused;
                    setCursorCaptured(window, !paused);
                    if (!paused) worldMouseArmed = false;
                    accumulator = 0.0;
                    jumpPressPending = false;
                }
            }
            escapeWasDown = escapeDown;

            const bool eDown = keyDown(window, GLFW_KEY_E);
            if (eDown && !eWasDown && !paused && !player.dead() && !gameHud.capturesTextInput()) {
                if (inventoryOpen && gameHud.hasBlockEntityScreen()) {
                    gameHud.closeBlockEntityScreen(&player);
                    gameHud.closePlayerCrafting(player);
                    for (ItemStack& dropped : gameHud.takeCraftingDrops())
                        itemEntities.spawn(dropped, player.feetPosition() + glm::dvec3(0.0, 0.5, 0.0));
                    inventoryOpen = false;
                } else {
                    gameHud.closeBlockEntityScreen(&player);
                    if (inventoryOpen) {
                        gameHud.closePlayerCrafting(player);
                        for (ItemStack& dropped : gameHud.takeCraftingDrops())
                            itemEntities.spawn(dropped, player.feetPosition() + glm::dvec3(0.0, 0.5, 0.0));
                    }
                    inventoryOpen = !inventoryOpen;
                }
                setCursorCaptured(window, !inventoryOpen);
                if (!inventoryOpen) worldMouseArmed = false;
                jumpPressPending = false;
            }
            eWasDown = eDown;

            const bool f3Down = keyDown(window, GLFW_KEY_F3);
            if (f3Down && !f3WasDown) showDebug = !showDebug;
            f3WasDown = f3Down;

            const double currentTime = glfwGetTime();
            if (paused) {
                accumulator = 0.0;
                previousTime = currentTime;
            } else {
                accumulator += std::min(currentTime - previousTime, 0.25);
                previousTime = currentTime;
            }

            if (!paused) {
                const bool gDown = keyDown(window, GLFW_KEY_G);
                if (!inventoryOpen && gDown && !gWasDown) player.toggleGameMode();
                gWasDown = gDown;

                for (int index = 0; index < 9; ++index) {
                    const bool down = keyDown(window, GLFW_KEY_1 + index);
                    if (!inventoryOpen && down && !numberWasDown[static_cast<std::size_t>(index)])
                        player.inventory().selectHotbar(static_cast<std::size_t>(index));
                    numberWasDown[static_cast<std::size_t>(index)] = down;
                }
                if (!inventoryOpen && pendingScroll != 0.0) {
                    player.inventory().scroll(pendingScroll > 0.0 ? 1 : -1);
                    pendingScroll = 0.0;
                }

                const bool jumpDown = keyDown(window, GLFW_KEY_SPACE);
                jumpPressPending = jumpPressPending || (jumpDown && !jumpWasDown);
                jumpWasDown = jumpDown;

                while (accumulator >= tickDuration) {
                    const bool leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                    const bool rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                    if (!inventoryOpen && !paused && !worldMouseArmed && !leftMouseDown && !rightMouseDown)
                        worldMouseArmed = true;
                    const bool allowWorldMouse = !inventoryOpen && worldMouseArmed;
                    PlayerInput tickInput = inventoryOpen ? PlayerInput{} : readPlayerInput(window, jumpPressPending);
                    if (allowWorldMouse) tickInput.useItem = rightMouseDown;
                    player.tick(world, tickInput, camera.front());
                    if (!inventoryOpen) {
                        interaction.tick(world, lightingEngine, worldRenderer, player, camera.front(),
                                         allowWorldMouse && leftMouseDown,
                                         allowWorldMouse && rightMouseDown);
                        if (const auto action = interaction.takeBlockEntityAction()) {
                            if (action->type == BlockEntityActionType::OpenChest ||
                                action->type == BlockEntityActionType::OpenShulker ||
                                action->type == BlockEntityActionType::OpenFurnace ||
                                action->type == BlockEntityActionType::OpenCraftingTable ||
                                action->type == BlockEntityActionType::OpenHopper ||
                                action->type == BlockEntityActionType::OpenBrewingStand ||
                                action->type == BlockEntityActionType::OpenEnchantingTable ||
                                action->type == BlockEntityActionType::OpenBeacon ||
                                action->type == BlockEntityActionType::OpenEnderChest ||
                                action->type == BlockEntityActionType::OpenJukebox ||
                                action->type == BlockEntityActionType::OpenFlowerPot ||
                                action->type == BlockEntityActionType::OpenDispenser ||
                                action->type == BlockEntityActionType::OpenDropper ||
                                action->type == BlockEntityActionType::EditSign) {
                                gameHud.openBlockEntityScreen(*action);
                                inventoryOpen = true;
                                setCursorCaptured(window, false);
                            }
                            if (action->type == BlockEntityActionType::Sleep) {
                                // In a single-player world with no hostile-entity system yet,
                                // EntityPlayer#trySleep's only meaningful Stage 7 gate is night.
                                // Advance to the next vanilla day boundary when sleeping at night.
                                const double dayTime = std::fmod(environment.dayTime(), 24000.0);
                                if (dayTime >= 12541.0 && dayTime < 23460.0) {
                                    const double nextDay = (std::floor(environment.dayTime() / 24000.0) + 1.0) * 24000.0;
                                    environment.setDayTime(nextDay);
                                }
                            }
                        }
                    }
                    // Stage 13 keeps simulation ordering explicit: scheduled world updates,
                    // redstone power propagation/devices, random ticks, block entities, entities, environment.
                    const auto applyWorldChanges = [&](const std::vector<glm::ivec3>& changes, bool notifyRedstone) {
                        for (const glm::ivec3& changed : changes) {
                            blockEntities.rescanPosition(world, changed);
                            dynamicBlocks.neighborChanged(world, changed);
                            if (notifyRedstone) redstone.neighborChanged(world, changed);
                            const auto lightingChanges = lightingEngine.blockChangedSync(changed.x, changed.y, changed.z);
                            worldRenderer.blockChangedSync(changed.x, changed.y, changed.z, lightingChanges);
                        }
                    };
                    applyWorldChanges(dynamicBlocks.tickScheduled(world), true);
                    applyWorldChanges(redstone.tick(world, blockEntities, &itemEntities, player, environment.dayTime()), false);
                    applyWorldChanges(dynamicBlocks.tickRandom(world), true);
                    applyWorldChanges(blockEntities.tick(world), true);
                    entities.tick(world, player);
                    environment.tick(world);
                    // Stage 9 sound events are intentionally hooks only; consume them so
                    // a future audio engine can bind exact 1.12.2 SoundType resources without
                    // allowing the queue to grow while sounds are not implemented.
                    for (const BlockSoundEvent& event : interaction.takeSoundEvents()) static_cast<void>(event);
                    jumpPressPending = false;
                    accumulator -= tickDuration;
                }
            } else {
                gWasDown = keyDown(window, GLFW_KEY_G);
                jumpWasDown = keyDown(window, GLFW_KEY_SPACE);
                for (int index = 0; index < 9; ++index)
                    numberWasDown[static_cast<std::size_t>(index)] = keyDown(window, GLFW_KEY_1 + index);
            }

            if (player.dead()) { inventoryOpen=false; paused=false; setCursorCaptured(window,false); }
            const float cameraPartial = paused ? 0.0F : static_cast<float>(accumulator / tickDuration);
            camera.setPosition(player.interpolatedEyePosition(cameraPartial));
            camera.setHurtEffect(player.hurtCameraStrength(cameraPartial), player.attackedAtYaw());

            const ChunkStreamChanges streamChanges = chunkStreamer.update(
                player.feetPosition().x, player.feetPosition().z,
                camera.front().x, camera.front().z, config.streamMainThreadBudgetMs);
            for (const ChunkCoordinate coordinate : streamChanges.unloaded) {
                lightingEngine.chunkUnloaded(coordinate.x, coordinate.z);
                worldRenderer.chunkUnloaded(coordinate.x, coordinate.z);
            }
            for (const ChunkCoordinate coordinate : streamChanges.loaded) {
                lightingEngine.chunkLoaded(coordinate.x, coordinate.z);
                worldRenderer.chunkLoaded(coordinate.x, coordinate.z);
                blockEntities.scanChunk(world, coordinate.x, coordinate.z);
                dynamicBlocks.scanChunk(world, coordinate.x, coordinate.z);
                redstone.scanChunk(world, coordinate.x, coordinate.z);
            }
            const std::vector<LightingChange> lightChanges = lightingEngine.process(
                player.feetPosition().x, player.feetPosition().z, config.lightMainThreadBudgetMs);
            for (const LightingChange change : lightChanges)
                worldRenderer.chunkLightingChanged(change.chunkX, change.chunkZ);
            worldRenderer.processCompletedMeshes(config.meshMainThreadBudgetMs);

            const float partialTick = paused ? 0.0F : static_cast<float>(accumulator / tickDuration);
            const EnvironmentFrame environmentFrame = environment.sample(
                world, camera.position(), camera.front(), partialTick);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glClearColor(environmentFrame.fogColor.r, environmentFrame.fogColor.g,
                         environmentFrame.fogColor.b, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            std::optional<RaycastHit> currentHit;
            if (framebufferHeight > 0) {
                const float aspect = static_cast<float>(framebufferWidth) /
                                     static_cast<float>(framebufferHeight);
                const BlockId cameraBlock = static_cast<BlockId>(blockId(world.getBlock(
                    static_cast<int>(std::floor(camera.position().x)), static_cast<int>(std::floor(camera.position().y)),
                    static_cast<int>(std::floor(camera.position().z)))));
                const bool underwater = cameraBlock == BlockId::Water || cameraBlock == BlockId::FlowingWater;
                const glm::mat4 projection = glm::perspective(glm::radians(underwater ? 60.0F : 70.0F), aspect, 0.05F, 500.0F);
                const glm::mat4 view = camera.viewMatrix();
                environmentRenderer.renderSky(environmentFrame, camera.position(), view, projection);
                worldRenderer.render(view, projection, environmentFrame);
                blockEntityRenderer.render(world, view, projection, partialTick);
                entityRenderer.render(entities, view, projection, partialTick);
                environmentRenderer.renderClouds(environmentFrame, camera.position(), view, projection);
                environmentRenderer.renderWeather(environmentFrame, world, camera.position(), view, projection);

                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                const float reach = player.gameMode() == GameMode::Creative ? 5.0F : 4.5F;
                currentHit = raycastBlocks(world, camera.position(), camera.front(), reach);
                if (!paused && !inventoryOpen) {
                    debugRenderer.renderBreakOverlay(world, currentHit, interaction.breakProgress(), view, projection);
                    debugRenderer.renderOutline(world, currentHit, view, projection);
                    if (!showDebug) {
                        const ScaledResolution crosshairScale = ScaledResolution::fromDisplay(
                            framebufferWidth, framebufferHeight, config.guiScale, false);
                        debugRenderer.renderCrosshair(framebufferWidth, framebufferHeight, crosshairScale.scaleFactor);
                    }
                }

                const bool middleDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
                if (!paused && !inventoryOpen && currentHit && middleDown && !middleWasDown &&
                    player.gameMode() == GameMode::Creative) {
                    ItemStack picked = itemRegistry.stackForBlock(currentHit->state, 64);
                    if (!picked.empty()) {
                        picked.count = itemRegistry.get(picked.itemId).maxStackSize;
                        player.inventory().pickCreative(picked);
                    }
                }
                middleWasDown = middleDown;
            }

            gameHud.beginFrame();
            gameHud.render(world, player, camera, config, chunkStreamer,
                           lightingEngine, worldRenderer, currentHit,
                           framebufferWidth, framebufferHeight, displayedFps, environmentFrame.worldTime,
                           showDebug, paused, inventoryOpen);
            for (const ExperienceDrop& drop : gameHud.takeExperienceDrops())
                if (drop.value > 0) itemEntities.spawnExperience(drop.position, drop.value);
            gameHud.endFrame();
            if (gameHud.consumeScreenCloseRequest()) {
                inventoryOpen = false;
                setCursorCaptured(window, true);
                worldMouseArmed = false;
            }
            if (gameHud.consumeResumeRequest()) {
                paused = false;
                setCursorCaptured(window, true);
                worldMouseArmed = false;
                accumulator = 0.0;
                previousTime = glfwGetTime();
            }
            if (gameHud.consumeRespawnRequest()) {
                player.respawn();
                paused = false; inventoryOpen = false;
                setCursorCaptured(window, true);
                accumulator = 0.0; previousTime = glfwGetTime();
            }
            if (gameHud.consumeReturnToTitleRequest()) {
                returnToTitle = true;
            }

            glfwSwapBuffers(window);

            if (!config.vsync && targetFrameDuration > FrameClock::duration::zero()) {
                nextFrameDeadline += targetFrameDuration;
                const auto now = FrameClock::now();
                if (nextFrameDeadline > now) {
                    std::this_thread::sleep_until(nextFrameDeadline);
                } else if (now - nextFrameDeadline > targetFrameDuration) {
                    nextFrameDeadline = now;
                }
            }

            if (currentTime - lastAutosave >= autosaveIntervalSeconds) {
                dynamicBlocks.syncLoadedChunkScheduledTicks(world);
                worldSave.saveAll(world, blockEntities, config, player, environment, &entities);
                lastAutosave = currentTime;
            }

            ++titleFrameCount;
            const double titleElapsed = currentTime - titleIntervalStart;
            if (titleElapsed >= 0.5) {
                displayedFps = static_cast<double>(titleFrameCount) / titleElapsed;
                updateWindowTitle(window, player, itemRegistry, config, displayedFps,
                                  world.chunkCount(), chunkStreamer, lightingEngine, worldRenderer);
                titleIntervalStart = currentTime;
                titleFrameCount = 0;
            }
        }

        // Flush the streamer's LRU before saving the live chunks so both active
        // and recently visited terrain survive a clean process restart.
        chunkStreamer.flushCache();
        dynamicBlocks.syncLoadedChunkScheduledTicks(world);
        worldSave.saveAll(world, blockEntities, config, player, environment, &entities);
        if (returnToTitle && glfwWindowShouldClose(window) == GLFW_FALSE) {
            setCursorCaptured(window, false);
            glfwSetWindowTitle(window, "Blockcraft 1.12.2");
            continue;
        }
        break;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
