#include "rendering/GameHud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <glm/geometric.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image.h>

#include "blocks/BlockRegistry.hpp"
#include "client/ScaledResolution.hpp"
#include "lighting/LightingEngine.hpp"
#include "player/BlockInteraction.hpp"
#include "player/Player.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/Camera.hpp"
#include "rendering/TextureAtlas.hpp"
#include "rendering/TextureAtlasData.hpp"
#include "rendering/WorldRenderer.hpp"
#include "world/World.hpp"
#include "worldgen/ChunkStreamer.hpp"
#include "worldgen/WorldConfig.hpp"

namespace {

ImTextureID textureId(GLuint value) {
    return static_cast<ImTextureID>(value);
}

std::string biomeName(int id) {
    switch (id) {
        case 0: return "Ocean"; case 1: return "Plains"; case 2: return "Desert";
        case 3: return "Extreme Hills"; case 4: return "Forest"; case 5: return "Taiga";
        case 6: return "Swampland"; case 7: return "River"; case 8: return "Hell";
        case 9: return "Sky"; case 10: return "FrozenOcean"; case 11: return "FrozenRiver";
        case 12: return "Ice Plains"; case 13: return "Ice Mountains"; case 14: return "MushroomIsland";
        case 15: return "MushroomIslandShore"; case 16: return "Beach"; case 17: return "DesertHills";
        case 18: return "ForestHills"; case 19: return "TaigaHills"; case 20: return "Extreme Hills Edge";
        case 21: return "Jungle"; case 22: return "JungleHills"; case 23: return "JungleEdge";
        case 24: return "Deep Ocean"; case 25: return "Stone Beach"; case 26: return "Cold Beach";
        case 27: return "Birch Forest"; case 28: return "Birch Forest Hills"; case 29: return "Roofed Forest";
        case 30: return "Cold Taiga"; case 31: return "Cold Taiga Hills"; case 32: return "Mega Taiga";
        case 33: return "Mega Taiga Hills"; case 34: return "Extreme Hills+"; case 35: return "Savanna";
        case 36: return "Savanna Plateau"; case 37: return "Mesa"; case 38: return "Mesa Plateau F";
        case 39: return "Mesa Plateau"; case 127: return "The Void";
        case 129: return "Sunflower Plains"; case 130: return "Desert M"; case 131: return "Extreme Hills M";
        case 132: return "Flower Forest"; case 133: return "Taiga M"; case 134: return "Swampland M";
        case 140: return "Ice Plains Spikes"; case 149: return "Jungle M"; case 151: return "JungleEdge M";
        case 155: return "Birch Forest M"; case 156: return "Birch Forest Hills M";
        case 157: return "Roofed Forest M"; case 158: return "Cold Taiga M";
        case 160: return "Mega Spruce Taiga"; case 161: return "Mega Spruce Taiga Hills";
        case 162: return "Extreme Hills+ M"; case 163: return "Savanna M"; case 164: return "Savanna Plateau M";
        case 165: return "Mesa (Bryce)"; case 166: return "Mesa Plateau F M"; case 167: return "Mesa Plateau M";
        default: return "Unknown";
    }
}

std::string facingName(const glm::vec3& front, std::string& description) {
    if (std::abs(front.x) > std::abs(front.z)) {
        if (front.x > 0.0F) { description = "Towards positive X"; return "east"; }
        description = "Towards negative X"; return "west";
    }
    if (front.z > 0.0F) { description = "Towards positive Z"; return "south"; }
    description = "Towards negative Z"; return "north";
}

std::string glString(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value == nullptr ? std::string("unknown") : std::string(reinterpret_cast<const char*>(value));
}

} // namespace

GameHud::GameHud(GLFWwindow* window, const std::filesystem::path& assetRoot, TextureAtlas& blockAtlas)
    : window_(window), blockAtlas_(blockAtlas) {
    if (window_ == nullptr) throw std::invalid_argument("GameHud requires a GLFW window");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No ImGui-owned persistence or generated UI files. Minecraft-facing HUD
    // pixels below come exclusively from the extracted 1.12.2 resource tree.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true))
        throw std::runtime_error("Could not initialize Dear ImGui GLFW backend");
    if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
        throw std::runtime_error("Could not initialize Dear ImGui OpenGL3 backend");

    widgetsTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/widgets.png", 256, 256);
    std::vector<unsigned char> asciiPixels;
    asciiTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/font/ascii.png", 128, 128, &asciiPixels);
    buildAsciiWidths(asciiPixels);
}

GameHud::~GameHud() {
    if (asciiTexture_ != 0) glDeleteTextures(1, &asciiTexture_);
    if (widgetsTexture_ != 0) glDeleteTextures(1, &widgetsTexture_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

GLuint GameHud::loadExactTexture(const std::filesystem::path& path, int expectedWidth,
                                 int expectedHeight, std::vector<unsigned char>* rgba) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width != expectedWidth || height != expectedHeight) {
        if (pixels != nullptr) stbi_image_free(pixels);
        throw std::runtime_error("Missing or invalid Minecraft 1.12.2 asset: " + path.string());
    }
    if (rgba != nullptr)
        rgba->assign(pixels, pixels + static_cast<std::size_t>(width * height * 4));

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);
    return texture;
}

void GameHud::buildAsciiWidths(const std::vector<unsigned char>& pixels) {
    charWidths_.fill(1);
    for (int character = 0; character < 256; ++character) {
        if (character == 32) {
            charWidths_[32] = 4;
            continue;
        }
        const int cellX = (character & 15) * 8;
        const int cellY = (character >> 4) * 8;
        int right = 7;
        for (; right >= 0; --right) {
            bool transparent = true;
            for (int row = 0; row < 8; ++row) {
                const std::size_t alpha = static_cast<std::size_t>(((cellY + row) * 128 + cellX + right) * 4 + 3);
                if (pixels[alpha] != 0) { transparent = false; break; }
            }
            if (!transparent) break;
        }
        charWidths_[static_cast<std::size_t>(character)] = std::max(1, right + 2);
    }
}

float GameHud::textWidth(std::string_view text) const {
    float width = 0.0F;
    for (unsigned char character : text) width += static_cast<float>(charWidths_[character]);
    return width;
}

void GameHud::drawText(float x, float y, std::string_view text, int scaleFactor, bool rightAligned) const {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float cursor = rightAligned ? x - textWidth(text) : x;
    const ImGuiIO& io = ImGui::GetIO();
    const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    for (unsigned char character : text) {
        const int advance = charWidths_[character];
        if (character != ' ') {
            const int glyphX = (character & 15) * 8;
            const int glyphY = (character >> 4) * 8;
            const int visible = std::clamp(advance - 1, 1, 8);
            const ImVec2 uv0(static_cast<float>(glyphX) / 128.0F,
                             static_cast<float>(glyphY) / 128.0F);
            const ImVec2 uv1(static_cast<float>(glyphX + visible) / 128.0F,
                             static_cast<float>(glyphY + 8) / 128.0F);
            draw->AddImage(textureId(asciiTexture_),
                ImVec2(cursor * scaleX, y * scaleY),
                ImVec2((cursor + visible) * scaleX, (y + 8.0F) * scaleY), uv0, uv1,
                IM_COL32(224, 224, 224, 255));
        }
        cursor += static_cast<float>(advance);
    }
}

void GameHud::drawDebugLine(float x, float y, std::string_view text, int scaleFactor,
                            bool rightAligned) const {
    if (text.empty()) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float width = textWidth(text);
    const float left = rightAligned ? x - width : x;
    const ImGuiIO& io = ImGui::GetIO();
    const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    draw->AddRectFilled(ImVec2((left - 1.0F) * scaleX, (y - 1.0F) * scaleY),
                        ImVec2((left + width + 1.0F) * scaleX, (y + 8.0F) * scaleY),
                        IM_COL32(80, 80, 80, 144));
    drawText(x, y, text, scaleFactor, rightAligned);
}

void GameHud::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GameHud::renderHotbar(const BlockInteraction& interaction, int scaledWidth,
                           int scaledHeight, int scaleFactor) const {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    const float center = static_cast<float>(scaledWidth / 2);
    const float x = center - 91.0F;
    const float y = static_cast<float>(scaledHeight - 22);
    const auto sx = [scaleX](float value) { return value * scaleX; };
    const auto sy = [scaleY](float value) { return value * scaleY; };

    draw->AddImage(textureId(widgetsTexture_), ImVec2(sx(x), sy(y)),
                   ImVec2(sx(x + 182.0F), sy(y + 22.0F)),
                   ImVec2(0.0F, 0.0F), ImVec2(182.0F / 256.0F, 22.0F / 256.0F));
    const float selectorX = x - 1.0F + static_cast<float>(interaction.selectedIndex()) * 20.0F;
    draw->AddImage(textureId(widgetsTexture_), ImVec2(sx(selectorX), sy(y - 1.0F)),
                   ImVec2(sx(selectorX + 24.0F), sy(y + 21.0F)),
                   ImVec2(0.0F, 22.0F / 256.0F), ImVec2(24.0F / 256.0F, 44.0F / 256.0F));

    // Stage 6 will replace these development stacks with ItemStacks and the
    // vanilla baked item-model renderer. Until then, every icon is still a
    // real 1.12.2 JAR block texture, never a generated/placeholder asset.
    for (std::size_t slot = 0; slot < BlockInteraction::hotbarSize(); ++slot) {
        const BlockState state = makeBlockState(static_cast<std::uint16_t>(BlockInteraction::hotbarBlock(slot)));
        const AtlasSprite& sprite = blockAtlas_.data().sprite(BlockRegistry::texture(state, Face::Up));
        const float itemX = center - 90.0F + static_cast<float>(slot * 20) + 2.0F;
        const float itemY = static_cast<float>(scaledHeight - 19);
        draw->AddImage(textureId(blockAtlas_.id()),
            ImVec2(sx(itemX), sy(itemY)), ImVec2(sx(itemX + 16.0F), sy(itemY + 16.0F)),
            ImVec2(sprite.bounds.u0, sprite.bounds.v0), ImVec2(sprite.bounds.u1, sprite.bounds.v1));
    }
}

void GameHud::renderDebug(const World& world, const Player& player, const Camera& camera,
                          const WorldConfig& config, const ChunkStreamer& streamer,
                          const LightingEngine& lighting, const WorldRenderer& renderer,
                          const std::optional<RaycastHit>& hit, int scaledWidth, int scaledHeight,
                          int scaleFactor, int framebufferWidth, int framebufferHeight,
                          double framesPerSecond) const {
    const glm::dvec3 position = player.feetPosition();
    const int blockX = static_cast<int>(std::floor(position.x));
    const int blockY = static_cast<int>(std::floor(position.y));
    const int blockZ = static_cast<int>(std::floor(position.z));
    const int chunkX = World::floorDiv16(blockX);
    const int chunkZ = World::floorDiv16(blockZ);
    const int localX = World::floorMod16(blockX);
    const int localZ = World::floorMod16(blockZ);
    const Chunk* chunk = world.findChunk(chunkX, chunkZ);
    const int biome = chunk == nullptr ? 1 : chunk->biome(localX, localZ);
    const int sky = world.getSkyLight(blockX, blockY, blockZ);
    const int blockLight = world.getBlockLight(blockX, blockY, blockZ);
    std::string directionDescription;
    const std::string facing = facingName(camera.front(), directionDescription);
    const WorldRenderStats& stats = renderer.stats();

    // GuiIngame replaces the normal crosshair with OpenGlHelper::renderDirections
    // while the full debug overlay is visible. Reproduce the same RGB world-axis
    // cue in scaled GUI space.
    {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const glm::vec3 front = glm::normalize(camera.front());
        const glm::vec3 worldUp{0.0F, 1.0F, 0.0F};
        glm::vec3 right = glm::cross(front, worldUp);
        if (glm::dot(right, right) < 1.0e-6F) right = {1.0F, 0.0F, 0.0F};
        else right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, front));
        const ImGuiIO& io = ImGui::GetIO();
        const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
        const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
        const ImVec2 center(static_cast<float>(scaledWidth) * 0.5F * scaleX,
                            static_cast<float>(scaledHeight) * 0.5F * scaleY);
        const auto axis = [&](glm::vec3 value, ImU32 color) {
            constexpr float length = 10.0F;
            const float dx = glm::dot(value, right) * length * scaleX;
            const float dy = -glm::dot(value, up) * length * scaleY;
            draw->AddLine(center, ImVec2(center.x + dx, center.y + dy), color, std::max(scaleX, scaleY));
        };
        axis({1.0F, 0.0F, 0.0F}, IM_COL32(255, 0, 0, 255));
        axis({0.0F, 1.0F, 0.0F}, IM_COL32(0, 255, 0, 255));
        axis({0.0F, 0.0F, 1.0F}, IM_COL32(0, 0, 255, 255));
    }

    std::vector<std::string> left;
    left.emplace_back("Minecraft 1.12.2 (Blockcraft/C++)");
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(0) << framesPerSecond << " fps"
             << " | C: " << stats.visibleSections << '/' << stats.totalSections
             << " D: " << config.viewDistance;
        left.push_back(line.str());
    }
    {
        std::ostringstream line;
        line << "Chunk updates: gen " << streamer.pendingGenerationCount()
             << ", light " << lighting.pendingCount()
             << ", mesh " << renderer.pendingMeshCount();
        left.push_back(line.str());
    }
    left.emplace_back("");
    {
        std::ostringstream line;
        line << std::fixed << std::setprecision(3) << "XYZ: " << position.x << " / "
             << std::setprecision(5) << position.y << " / " << std::setprecision(3) << position.z;
        left.push_back(line.str());
    }
    left.push_back("Block: " + std::to_string(blockX) + " " + std::to_string(blockY) + " " + std::to_string(blockZ));
    left.push_back("Chunk: " + std::to_string(localX) + " " + std::to_string(blockY & 15) + " " +
                   std::to_string(localZ) + " in " + std::to_string(chunkX) + " " +
                   std::to_string(blockY >> 4) + " " + std::to_string(chunkZ));
    {
        std::ostringstream line;
        const float vanillaYaw = static_cast<float>(std::atan2(-camera.front().x, camera.front().z) * 180.0 / 3.14159265358979323846);
        const float vanillaPitch = -camera.pitch();
        line << std::fixed << std::setprecision(1) << "Facing: " << facing << " (" << directionDescription
             << ") (" << vanillaYaw << " / " << vanillaPitch << ')';
        left.push_back(line.str());
    }
    left.push_back("Biome: " + biomeName(biome));
    left.push_back("Light: " + std::to_string(std::max(sky, blockLight)) + " (" +
                   std::to_string(sky) + " sky, " + std::to_string(blockLight) + " block)");
    if (hit)
        left.push_back("Looking at: " + std::to_string(hit->block.x) + " " +
                       std::to_string(hit->block.y) + " " + std::to_string(hit->block.z));

    std::vector<std::string> right;
    right.emplace_back("C++20 64bit");
    right.emplace_back("CPU: " + std::to_string(std::max(1U, std::thread::hardware_concurrency())) + " logical threads");
    right.emplace_back("");
    right.emplace_back("Display: " + std::to_string(framebufferWidth) + "x" + std::to_string(framebufferHeight) +
                       " (" + glString(GL_VENDOR) + ")");
    right.push_back(glString(GL_RENDERER));
    right.push_back(glString(GL_VERSION));

    if (hit) {
        right.emplace_back("");
        right.emplace_back("minecraft:" + std::string(BlockRegistry::legacyName(blockId(hit->state))));
        const RelativeBlockLookup lookup = [&](int dx, int dy, int dz) {
            return world.getBlock(hit->block.x + dx, hit->block.y + dy, hit->block.z + dz);
        };
        const BlockModelState modelState = resolveBlockModelState(hit->state, lookup);
        for (const auto& [name, value] : modelState.properties)
            right.push_back(name + ": " + value);
    }

    for (std::size_t index = 0; index < left.size(); ++index)
        if (!left[index].empty()) drawDebugLine(2.0F, 2.0F + 9.0F * static_cast<float>(index), left[index], scaleFactor);
    for (std::size_t index = 0; index < right.size(); ++index)
        if (!right[index].empty()) drawDebugLine(static_cast<float>(scaledWidth - 2),
            2.0F + 9.0F * static_cast<float>(index), right[index], scaleFactor, true);
}

void GameHud::render(const World& world, const Player& player, const Camera& camera,
                     const BlockInteraction& interaction, const WorldConfig& config,
                     const ChunkStreamer& streamer, const LightingEngine& lighting,
                     const WorldRenderer& renderer, const std::optional<RaycastHit>& hit,
                     int framebufferWidth, int framebufferHeight, double framesPerSecond,
                     bool showDebug, bool paused) {
    const ScaledResolution scaled = ScaledResolution::fromDisplay(
        framebufferWidth, framebufferHeight, config.guiScale, false);
    renderHotbar(interaction, scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
    if (showDebug)
        renderDebug(world, player, camera, config, streamer, lighting, renderer, hit,
                    scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor,
                    framebufferWidth, framebufferHeight, framesPerSecond);
    if (paused) {
        const std::string label = "Paused";
        drawText(static_cast<float>(scaled.scaledWidth) * 0.5F - textWidth(label) * 0.5F,
                 static_cast<float>(scaled.scaledHeight) * 0.5F - 4.0F,
                 label, scaled.scaleFactor, false);
    }
}

void GameHud::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
