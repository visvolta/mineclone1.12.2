#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>

#include "rendering/Camera.hpp"
#include "environment/Environment.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/BlockInteraction.hpp"
#include "player/Player.hpp"
#include "rendering/DebugRenderer.hpp"
#include "rendering/BiomeColors.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/EnvironmentRenderer.hpp"
#include "rendering/GameHud.hpp"
#include "rendering/TextureAtlas.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"
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
                       const BlockInteraction& interaction, const WorldConfig& config,
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
          << " | " << mode
          << " | " << interaction.selectedDefinition().name;
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
        const WorldConfig config = WorldConfig::load(BLOCKCRAFT_CONFIG_PATH);
        glfwSwapInterval(config.vsync ? 1 : 0);
        std::cout << "World seed: " << config.seed << " (" << config.seedText << ")\n"
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
        BiomeColors::load(BLOCKCRAFT_ASSET_ROOT);
        ChunkStreamer chunkStreamer(world, config, config.viewDistance);
        chunkStreamer.prime(0.0, 0.0, 1);
        LightingEngine lightingEngine(world, chunkStreamer.workers());
        for (const auto& [chunkKey, chunk] : world.chunks()) {
            static_cast<void>(chunkKey);
            lightingEngine.chunkLoaded(chunk->x(), chunk->z());
        }
        BlockRenderResources blockRenderResources(BLOCKCRAFT_ASSET_ROOT);
        TextureAtlas atlas(blockRenderResources.atlas());
        WorldRenderer worldRenderer(world, atlas, blockRenderResources, chunkStreamer.workers());
        Environment environment(config);
        EnvironmentRenderer environmentRenderer(environment);
        DebugRenderer debugRenderer(blockRenderResources);
        GameHud gameHud(window, BLOCKCRAFT_ASSET_ROOT, atlas);
        Player player({0.5, spawnHeight(world, 0, 0), 0.5});
        BlockInteraction interaction;
        camera.setPosition(player.eyePosition());
        updateWindowTitle(window, player, interaction, config, 0.0, world.chunkCount(),
                          chunkStreamer, lightingEngine, worldRenderer);

        double previousTime = glfwGetTime();
        double accumulator = 0.0;
        bool showDebug = false;
        bool f3WasDown = false;
        bool escapeWasDown = false;
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

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            const bool escapeDown = keyDown(window, GLFW_KEY_ESCAPE);
            if (escapeDown && !escapeWasDown) {
                paused = !paused;
                setCursorCaptured(window, !paused);
                accumulator = 0.0;
                jumpPressPending = false;
            }
            escapeWasDown = escapeDown;

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
                if (gDown && !gWasDown) player.toggleGameMode();
                gWasDown = gDown;

                for (int index = 0; index < 9; ++index) {
                    const bool down = keyDown(window, GLFW_KEY_1 + index);
                    if (down && !numberWasDown[static_cast<std::size_t>(index)])
                        interaction.selectNumber(index + 1);
                    numberWasDown[static_cast<std::size_t>(index)] = down;
                }
                if (pendingScroll != 0.0) {
                    interaction.scroll(pendingScroll > 0.0 ? 1 : -1);
                    pendingScroll = 0.0;
                }

                const bool jumpDown = keyDown(window, GLFW_KEY_SPACE);
                jumpPressPending = jumpPressPending || (jumpDown && !jumpWasDown);
                jumpWasDown = jumpDown;

                while (accumulator >= tickDuration) {
                    player.tick(world, readPlayerInput(window, jumpPressPending), camera.front());
                    interaction.tick(world, lightingEngine, worldRenderer, player, camera.front(),
                                     glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
                                     glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
                    environment.tick(world);
                    jumpPressPending = false;
                    accumulator -= tickDuration;
                }
            } else {
                gWasDown = keyDown(window, GLFW_KEY_G);
                jumpWasDown = keyDown(window, GLFW_KEY_SPACE);
                for (int index = 0; index < 9; ++index)
                    numberWasDown[static_cast<std::size_t>(index)] = keyDown(window, GLFW_KEY_1 + index);
            }

            camera.setPosition(player.interpolatedEyePosition(
                paused ? 0.0F : static_cast<float>(accumulator / tickDuration)));

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
                const glm::mat4 projection = glm::perspective(glm::radians(70.0F), aspect, 0.05F, 500.0F);
                const glm::mat4 view = camera.viewMatrix();
                environmentRenderer.renderSky(environmentFrame, camera.position(), view, projection);
                worldRenderer.render(view, projection, environmentFrame);
                environmentRenderer.renderClouds(environmentFrame, camera.position(), view, projection);
                environmentRenderer.renderWeather(environmentFrame, world, camera.position(), view, projection);

                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                const float reach = player.gameMode() == GameMode::Creative ? 5.0F : 4.5F;
                currentHit = raycastBlocks(world, camera.position(), camera.front(), reach);
                if (!paused) {
                    debugRenderer.renderBreakOverlay(world, currentHit, interaction.breakProgress(), view, projection);
                    debugRenderer.renderOutline(world, currentHit, view, projection);
                    if (!showDebug) debugRenderer.renderCrosshair(framebufferWidth, framebufferHeight);
                }
            }

            gameHud.beginFrame();
            gameHud.render(world, player, camera, interaction, config, chunkStreamer,
                           lightingEngine, worldRenderer, currentHit,
                           framebufferWidth, framebufferHeight, displayedFps,
                           showDebug, paused);
            gameHud.endFrame();

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

            ++titleFrameCount;
            const double titleElapsed = currentTime - titleIntervalStart;
            if (titleElapsed >= 0.5) {
                displayedFps = static_cast<double>(titleFrameCount) / titleElapsed;
                updateWindowTitle(window, player, interaction, config, displayedFps,
                                  world.chunkCount(), chunkStreamer, lightingEngine, worldRenderer);
                titleIntervalStart = currentTime;
                titleFrameCount = 0;
            }
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
