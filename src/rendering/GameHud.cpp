#include "rendering/GameHud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <glm/geometric.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec4.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image.h>

#include "blocks/BlockRegistry.hpp"
#include "client/ScaledResolution.hpp"
#include "lighting/LightingEngine.hpp"
#include "items/ItemRegistry.hpp"
#include "player/BlockInteraction.hpp"
#include "survival/MiningRules.hpp"
#include "player/Player.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/BlockRenderPath.hpp"
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

struct GuiEntityQuad {
    std::array<glm::vec3,4> positions{};
    std::array<glm::vec2,4> uvs{};
    float shade = 1.0F;
    float depth = 0.0F;
};

void appendGuiEntityFace(std::vector<GuiEntityQuad>& out,
                         const std::array<glm::vec3,4>& positions,
                         int u1, int v1, int u2, int v2,
                         int textureWidth, int textureHeight, float shade) {
    GuiEntityQuad quad;
    quad.positions = positions;
    quad.uvs = {{
        {u2 / textureWidth, v1 / textureHeight},
        {u1 / textureWidth, v1 / textureHeight},
        {u1 / textureWidth, v2 / textureHeight},
        {u2 / textureWidth, v2 / textureHeight}
    }};
    quad.shade = shade;
    for (const glm::vec3& p : positions) quad.depth += p.z;
    quad.depth *= 0.25F;
    out.push_back(quad);
}

void appendGuiModelBox(std::vector<GuiEntityQuad>& out, int textureWidth, int textureHeight,
                       int textureU, int textureV, float x, float y, float z,
                       int dx, int dy, int dz, const glm::mat4& transform) {
    const float x0=x/16.0F, y0=y/16.0F, z0=z/16.0F;
    const float x1=(x+dx)/16.0F, y1=(y+dy)/16.0F, z1=(z+dz)/16.0F;
    const auto t=[&](float X,float Y,float Z){
        const glm::vec4 q=transform*glm::vec4(X,Y,Z,1.0F);
        return glm::vec3(q);
    };
    const glm::vec3 p0=t(x0,y0,z0), p1=t(x1,y0,z0), p2=t(x1,y1,z0), p3=t(x0,y1,z0);
    const glm::vec3 p4=t(x0,y0,z1), p5=t(x1,y0,z1), p6=t(x1,y1,z1), p7=t(x0,y1,z1);
    // Same ModelBox/TexturedQuad unwrap used by 1.12.2.  GUI entity items are
    // lit by RenderHelper; these restrained face factors mimic its readable
    // three-dimensional presentation without reusing the world AO path.
    appendGuiEntityFace(out,{p5,p1,p2,p6},textureU+dz+dx,textureV+dz,textureU+dz+dx+dz,textureV+dz+dy,textureWidth,textureHeight,0.78F);
    appendGuiEntityFace(out,{p0,p4,p7,p3},textureU,textureV+dz,textureU+dz,textureV+dz+dy,textureWidth,textureHeight,0.78F);
    appendGuiEntityFace(out,{p5,p4,p0,p1},textureU+dz,textureV,textureU+dz+dx,textureV+dz,textureWidth,textureHeight,0.70F);
    appendGuiEntityFace(out,{p2,p3,p7,p6},textureU+dz+dx,textureV+dz,textureU+dz+dx+dx,textureV,textureWidth,textureHeight,1.0F);
    appendGuiEntityFace(out,{p1,p0,p3,p2},textureU+dz,textureV+dz,textureU+dz+dx,textureV+dz+dy,textureWidth,textureHeight,0.88F);
    appendGuiEntityFace(out,{p4,p5,p6,p7},textureU+dz+dx+dz,textureV+dz,textureU+dz+dx+dz+dx,textureV+dz+dy,textureWidth,textureHeight,0.88F);
}

void drawGuiEntityQuads(GLuint texture, std::vector<GuiEntityQuad> quads,
                        float x, float y, int scaleFactor, float pixelsPerUnit = 13.0F) {
    if (texture == 0 || quads.empty()) return;
    std::sort(quads.begin(), quads.end(), [](const GuiEntityQuad& a, const GuiEntityQuad& b) {
        return a.depth < b.depth;
    });
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    const ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F);
    const float sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    for(const GuiEntityQuad& q:quads){
        std::array<ImVec2,4> p{};
        for(std::size_t i=0;i<4;++i)
            p[i]=ImVec2((x+8.0F+q.positions[i].x*pixelsPerUnit)*sx,
                        (y+8.0F-q.positions[i].y*pixelsPerUnit)*sy);
        const int c=std::clamp(static_cast<int>(std::lround(q.shade*255.0F)),0,255);
        draw->AddImageQuad(textureId(texture),p[0],p[1],p[2],p[3],
            ImVec2(q.uvs[0].x,q.uvs[0].y),ImVec2(q.uvs[1].x,q.uvs[1].y),
            ImVec2(q.uvs[2].x,q.uvs[2].y),ImVec2(q.uvs[3].x,q.uvs[3].y),
            IM_COL32(c,c,c,255));
    }
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

GameHud::GameHud(GLFWwindow* window, const std::filesystem::path& assetRoot, TextureAtlas& blockAtlas, const ItemRegistry& items,
                 const BlockRenderResources& resources, BlockEntitySystem& blockEntities)
    : window_(window), blockAtlas_(blockAtlas), items_(items), resources_(resources), blockEntities_(blockEntities), crafting_(assetRoot, items) {
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
    iconsTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/icons.png", 256, 256);
    std::vector<unsigned char> asciiPixels;
    asciiTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/font/ascii.png", 128, 128, &asciiPixels);
    buildAsciiWidths(asciiPixels);
    inventoryTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/inventory.png", 256, 256);
    creativeItemsTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/creative_inventory/tab_items.png", 256, 256);
    creativeSearchTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/creative_inventory/tab_item_search.png", 256, 256);
    creativeInventoryTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/creative_inventory/tab_inventory.png", 256, 256);
    creativeTabsTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/creative_inventory/tabs.png", 256, 256);
    generic54Texture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/generic_54.png", 256, 256);
    craftingTableTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/crafting_table.png", 256, 256);
    furnaceTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/furnace.png", 256, 256);
    hopperTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/hopper.png", 256, 256);
    dispenserTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/dispenser.png", 256, 256);
    brewingTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/brewing_stand.png", 256, 256);
    enchantingTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/enchanting_table.png", 256, 256);
    beaconTexture_ = loadExactTexture(
        assetRoot / "assets/minecraft/textures/gui/container/beacon.png", 256, 256);

    const auto entityRoot = assetRoot / "assets/minecraft/textures/entity";
    chestItemTexture_ = loadExactTexture(entityRoot / "chest/normal.png", 64, 64);
    trappedChestItemTexture_ = loadExactTexture(entityRoot / "chest/trapped.png", 64, 64);
    enderChestItemTexture_ = loadExactTexture(entityRoot / "chest/ender.png", 64, 64);
    constexpr std::array<std::string_view,16> dyeNames = {
        "white","orange","magenta","light_blue","yellow","lime","pink","gray",
        "silver","cyan","purple","blue","brown","green","red","black"
    };
    for (std::size_t i = 0; i < dyeNames.size(); ++i) {
        bedItemTextures_[i] = loadExactTexture(entityRoot / ("bed/" + std::string(dyeNames[i]) + ".png"), 64, 64);
        shulkerItemTextures_[i] = loadExactTexture(entityRoot / ("shulker/shulker_" + std::string(dyeNames[i]) + ".png"), 64, 64);
    }
}

GameHud::~GameHud() {
    if (activeBlockEntityAction_) blockEntities_.endViewing(*activeBlockEntityAction_);
    glDeleteTextures(static_cast<GLsizei>(bedItemTextures_.size()), bedItemTextures_.data());
    glDeleteTextures(static_cast<GLsizei>(shulkerItemTextures_.size()), shulkerItemTextures_.data());
    if (enderChestItemTexture_ != 0) glDeleteTextures(1, &enderChestItemTexture_);
    if (trappedChestItemTexture_ != 0) glDeleteTextures(1, &trappedChestItemTexture_);
    if (chestItemTexture_ != 0) glDeleteTextures(1, &chestItemTexture_);
    if (beaconTexture_ != 0) glDeleteTextures(1, &beaconTexture_);
    if (enchantingTexture_ != 0) glDeleteTextures(1, &enchantingTexture_);
    if (brewingTexture_ != 0) glDeleteTextures(1, &brewingTexture_);
    if (dispenserTexture_ != 0) glDeleteTextures(1, &dispenserTexture_);
    if (hopperTexture_ != 0) glDeleteTextures(1, &hopperTexture_);
    if (furnaceTexture_ != 0) glDeleteTextures(1, &furnaceTexture_);
    if (craftingTableTexture_ != 0) glDeleteTextures(1, &craftingTableTexture_);
    if (generic54Texture_ != 0) glDeleteTextures(1, &generic54Texture_);
    if (creativeTabsTexture_ != 0) glDeleteTextures(1, &creativeTabsTexture_);
    if (creativeInventoryTexture_ != 0) glDeleteTextures(1, &creativeInventoryTexture_);
    if (creativeSearchTexture_ != 0) glDeleteTextures(1, &creativeSearchTexture_);
    if (creativeItemsTexture_ != 0) glDeleteTextures(1, &creativeItemsTexture_);
    if (inventoryTexture_ != 0) glDeleteTextures(1, &inventoryTexture_);
    if (asciiTexture_ != 0) glDeleteTextures(1, &asciiTexture_);
    if (iconsTexture_ != 0) glDeleteTextures(1, &iconsTexture_);
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
    bool bold = false;
    for (std::size_t i = 0; i < text.size();) {
        unsigned char character = static_cast<unsigned char>(text[i]);
        char code = 0;
        if (character == 0xC2 && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            i += 3;
        } else if (character == 0xA7 && i + 1 < text.size()) {
            code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 1])));
            i += 2;
        }
        if (code != 0) {
            if (code == 'l') bold = true;
            else if (code == 'r' || std::strchr("0123456789abcdef", code) != nullptr) bold = false;
            continue;
        }
        ++i;
        width += static_cast<float>(charWidths_[character]);
        if (bold && character != ' ') width += 1.0F;
    }
    return width;
}

void GameHud::drawText(float x, float y, std::string_view text, int scaleFactor,
                       bool rightAligned, unsigned int color) const {
    static constexpr std::array<unsigned int, 16> vanillaColors = {
        0xFF000000U, 0xFF0000AAU, 0xFF00AA00U, 0xFF00AAAAU,
        0xFFAA0000U, 0xFFAA00AAU, 0xFFFFAA00U, 0xFFAAAAAAU,
        0xFF555555U, 0xFF5555FFU, 0xFF55FF55U, 0xFF55FFFFU,
        0xFFFF5555U, 0xFFFF55FFU, 0xFFFFFF55U, 0xFFFFFFFFU
    };
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float cursor = rightAligned ? x - textWidth(text) : x;
    const ImGuiIO& io = ImGui::GetIO();
    const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    const auto toTint = [](unsigned int rgba) {
        return IM_COL32((rgba >> 16U) & 255U, (rgba >> 8U) & 255U,
                        rgba & 255U, (rgba >> 24U) & 255U);
    };
    const auto shadowColor = [](unsigned int rgba) {
        const unsigned int a = (rgba >> 24U) & 255U;
        const unsigned int r = ((rgba >> 16U) & 255U) >> 2U;
        const unsigned int g = ((rgba >> 8U) & 255U) >> 2U;
        const unsigned int b = (rgba & 255U) >> 2U;
        return (a << 24U) | (r << 16U) | (g << 8U) | b;
    };
    const auto drawGlyph = [&](unsigned char character, float gx, float gy, ImU32 tint) {
        if (character == ' ') return;
        const int advance = charWidths_[character];
        const int glyphX = (character & 15) * 8;
        const int glyphY = (character >> 4) * 8;
        const int visible = std::clamp(advance - 1, 1, 8);
        const ImVec2 uv0(static_cast<float>(glyphX) / 128.0F, static_cast<float>(glyphY) / 128.0F);
        const ImVec2 uv1(static_cast<float>(glyphX + visible) / 128.0F, static_cast<float>(glyphY + 8) / 128.0F);
        draw->AddImage(textureId(asciiTexture_), ImVec2(gx * scaleX, gy * scaleY),
                       ImVec2((gx + visible) * scaleX, (gy + 8.0F) * scaleY), uv0, uv1, tint);
    };

    unsigned int currentColor = color;
    bool bold = false;
    bool underline = false;
    bool strike = false;
    for (std::size_t i = 0; i < text.size();) {
        unsigned char character = static_cast<unsigned char>(text[i]);
        char code = 0;
        if (character == 0xC2 && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            i += 3;
        } else if (character == 0xA7 && i + 1 < text.size()) {
            code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 1])));
            i += 2;
        }
        if (code != 0) {
            constexpr const char* colorCodes = "0123456789abcdef";
            if (const char* found = std::strchr(colorCodes, code)) {
                const std::size_t index = static_cast<std::size_t>(found - colorCodes);
                currentColor = (color & 0xFF000000U) | (vanillaColors[index] & 0x00FFFFFFU);
                bold = underline = strike = false;
            } else if (code == 'l') bold = true;
            else if (code == 'n') underline = true;
            else if (code == 'm') strike = true;
            else if (code == 'r') { currentColor = color; bold = underline = strike = false; }
            continue;
        }
        ++i;
        const int advance = charWidths_[character];
        const ImU32 shadow = toTint(shadowColor(currentColor));
        const ImU32 tint = toTint(currentColor);
        drawGlyph(character, cursor + 1.0F, y + 1.0F, shadow);
        drawGlyph(character, cursor, y, tint);
        if (bold && character != ' ') {
            drawGlyph(character, cursor + 1.0F, y, tint);
            drawGlyph(character, cursor + 2.0F, y + 1.0F, shadow);
        }
        const float decoratedWidth = static_cast<float>(advance + (bold && character != ' ' ? 1 : 0));
        if (strike && character != ' ')
            draw->AddRectFilled(ImVec2(cursor * scaleX, (y + 4.0F) * scaleY),
                                ImVec2((cursor + decoratedWidth) * scaleX, (y + 5.0F) * scaleY), tint);
        if (underline && character != ' ')
            draw->AddRectFilled(ImVec2(cursor * scaleX, (y + 8.0F) * scaleY),
                                ImVec2((cursor + decoratedWidth) * scaleX, (y + 9.0F) * scaleY), tint);
        cursor += decoratedWidth;
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
    // Widgets such as InvisibleButton must live inside a real ImGui window.
    // Previously they were emitted against ImGui's implicit Debug##Default
    // fallback window, which is why Escape could expose a strange ImGui
    // dropdown/debug surface over the Minecraft pause menu.
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##blockcraft-hud-root", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    frameHostOpen_ = true;
}

bool GameHud::drawBuiltinEntityStack(const ItemStack& stack, float x, float y, int scaleFactor) const {
    if (stack.empty()) return false;
    const ItemDefinition& item = items_.get(stack.itemId);
    const auto placed = item.placedBlock;
    const bool isChest = stack.itemId == static_cast<std::uint16_t>(BlockId::Chest) ||
                         stack.itemId == static_cast<std::uint16_t>(BlockId::TrappedChest) ||
                         stack.itemId == static_cast<std::uint16_t>(BlockId::EnderChest) ||
                         (placed && (*placed == BlockId::Chest || *placed == BlockId::TrappedChest || *placed == BlockId::EnderChest));
    const bool isShulker = placed && static_cast<std::uint16_t>(*placed) >= static_cast<std::uint16_t>(BlockId::WhiteShulkerBox) &&
                           static_cast<std::uint16_t>(*placed) <= static_cast<std::uint16_t>(BlockId::BlackShulkerBox);
    const bool isBed = stack.itemId == 355 || (placed && *placed == BlockId::Bed);
    if (!isChest && !isShulker && !isBed) return false;

    std::vector<GuiEntityQuad> quads;
    GLuint texture = 0;

    if (isChest) {
        glm::mat4 view(1.0F);
        view=glm::rotate(view,glm::radians(30.0F),glm::vec3(1,0,0));
        view=glm::rotate(view,glm::radians(45.0F),glm::vec3(0,1,0));
        view=glm::scale(view,glm::vec3(0.625F));
        view=glm::translate(view,glm::vec3(-0.5F,-0.4375F,-0.5F));

        glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(0,1,1));
        root=glm::scale(root,glm::vec3(1,-1,-1));
        root=glm::translate(root,glm::vec3(0.5F));
        root=view*root;
        appendGuiModelBox(quads,64,64,0,19,0,0,0,14,10,14,
                          glm::translate(root,glm::vec3(1,6,1)/16.0F));
        appendGuiModelBox(quads,64,64,0,0,0,-5,-14,14,5,14,
                          glm::translate(root,glm::vec3(1,7,15)/16.0F));
        appendGuiModelBox(quads,64,64,0,0,-1,-2,-15,2,4,1,
                          glm::translate(root,glm::vec3(8,7,15)/16.0F));
        const BlockId chestType = (stack.itemId == static_cast<std::uint16_t>(BlockId::TrappedChest) ||
                                  (placed && *placed == BlockId::TrappedChest)) ? BlockId::TrappedChest :
                                  (stack.itemId == static_cast<std::uint16_t>(BlockId::EnderChest) ||
                                  (placed && *placed == BlockId::EnderChest)) ? BlockId::EnderChest : BlockId::Chest;
        texture = chestType == BlockId::TrappedChest ? trappedChestItemTexture_ :
                  (chestType == BlockId::EnderChest ? enderChestItemTexture_ : chestItemTexture_);
    } else if (isShulker) {
        const std::size_t color = static_cast<std::size_t>(static_cast<std::uint16_t>(*placed) -
                                   static_cast<std::uint16_t>(BlockId::WhiteShulkerBox));
        glm::mat4 view(1.0F);
        view=glm::rotate(view,glm::radians(30.0F),glm::vec3(1,0,0));
        view=glm::rotate(view,glm::radians(45.0F),glm::vec3(0,1,0));
        view=glm::scale(view,glm::vec3(0.625F));
        view=glm::translate(view,glm::vec3(-0.5F));

        glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(0.5F,1.5F,0.5F));
        root=glm::scale(root,glm::vec3(1,-1,-1));
        root=glm::translate(root,glm::vec3(0,1,0));
        root=glm::scale(root,glm::vec3(0.9995F));
        root=glm::translate(root,glm::vec3(0,-1,0));
        root=view*root;
        appendGuiModelBox(quads,64,64,0,28,-8,-8,-8,16,8,16,
                          glm::translate(root,glm::vec3(0,24,0)/16.0F));
        appendGuiModelBox(quads,64,64,0,0,-8,-16,-8,16,12,16,
                          glm::translate(root,glm::vec3(0,24,0)/16.0F));
        texture=shulkerItemTextures_[std::min<std::size_t>(color,15)];
    } else {
        // TileEntityItemStackRenderer sends the bed through TileEntityBedRenderer.
        // Reproduce its full two-piece item model and use the exact colour texture.
        glm::mat4 view(1.0F);
        view=glm::rotate(view,glm::radians(30.0F),glm::vec3(1,0,0));
        view=glm::rotate(view,glm::radians(160.0F),glm::vec3(0,1,0));
        view=glm::scale(view,glm::vec3(0.5325F));
        view=glm::translate(view,glm::vec3(-0.5F,-0.28125F,0.5F));

        const auto appendBedPiece=[&](bool head,float zOffset){
            // i=0 in TileEntityBedRenderer's no-world item path resolves SOUTH.
            glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(1.0F,0.5625F,zOffset+1.0F));
            root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0));
            root=glm::rotate(root,glm::radians(180.0F),glm::vec3(0,0,1));
            root=view*root;
            appendGuiModelBox(quads,64,64,0,head?0:22,0,0,0,16,16,6,root);
            struct Leg { int tv; float bx,by,bz,rz; bool onHead; };
            constexpr std::array<Leg,4> legs={{{0,0,6,-16,0,false},{6,0,6,0,90,true},
                                               {12,-16,6,-16,270,false},{18,-16,6,0,180,true}}};
            for(const Leg& leg:legs){
                if(leg.onHead!=head) continue;
                glm::mat4 lm=glm::rotate(root,glm::radians(leg.rz),glm::vec3(0,0,1));
                lm=glm::rotate(lm,glm::radians(90.0F),glm::vec3(1,0,0));
                appendGuiModelBox(quads,64,64,50,leg.tv,leg.bx,leg.by,leg.bz,3,3,3,lm);
            }
        };
        appendBedPiece(true,0.0F);
        appendBedPiece(false,-1.0F);
        texture=bedItemTextures_[std::min<std::size_t>(stack.damage&15U,15)];
    }

    drawGuiEntityQuads(texture,std::move(quads),x,y,scaleFactor,isBed?11.5F:15.0F);
    return true;
}

bool GameHud::drawBlockModelStack(const ItemStack& stack, float x, float y, int scaleFactor) const {
    if (stack.empty()) return false;
    const ItemDefinition& item = items_.get(stack.itemId);
    if (!item.placedBlock || blockRenderPath(*item.placedBlock) != BlockRenderPath::JsonModel) return false;
    const BlockState state = makeBlockState(static_cast<std::uint16_t>(*item.placedBlock),
                                            static_cast<std::uint8_t>(stack.damage & 15U));
    std::vector<const BakedBlockModel*> models;
    // Most zero-metadata ItemBlocks have a dedicated inventory model (for
    // example fence_inventory) that differs from an isolated world state.
    // Metadata variants are selected through the blockstate model so their
    // exact subtype texture/state is retained.
    if ((stack.damage & 15U) == 0U) {
        const BlockModelManager& modelManager = resources_.models();
        const BakedBlockModel* itemModel = modelManager.itemModel(item.name);
        if (itemModel != nullptr && !itemModel->quads.empty()) models.push_back(itemModel);
    }
    if (models.empty()) {
        const auto air = [](int, int, int) { return makeBlockState(0); };
        const BlockModelState modelState = resolveBlockModelState(state, air);
        models = resources_.models().select(modelState, 0);
    }
    if (models.empty()) return false;

    struct DrawQuad { const BakedModelQuad* quad; std::array<glm::vec3,4> points; float depth; };
    std::vector<DrawQuad> quads;
    glm::mat4 rotation(1.0F);
    rotation = glm::rotate(rotation, glm::radians(30.0F), glm::vec3(1,0,0));
    rotation = glm::rotate(rotation, glm::radians(225.0F), glm::vec3(0,1,0));
    for (const BakedBlockModel* model : models) {
        if (!model) continue;
        for (const BakedModelQuad& q : model->quads) {
            DrawQuad d{&q,{},0.0F};
            for (std::size_t i=0;i<4;++i) {
                const glm::vec4 t=rotation*glm::vec4(q.positions[i]-glm::vec3(0.5F),1.0F);
                d.points[i]=glm::vec3(t);
                d.depth+=t.z;
            }
            d.depth*=0.25F;
            quads.push_back(d);
        }
    }
    if (quads.empty()) return false;
    std::stable_sort(quads.begin(),quads.end(),[](const DrawQuad& a,const DrawQuad& b){return a.depth<b.depth;});
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    const ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F);
    const float sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    constexpr std::array<float,6> shade={0.50F,1.0F,0.80F,0.80F,0.60F,0.60F};
    for(const DrawQuad& d:quads){
        std::array<ImVec2,4> p{};
        for(std::size_t i=0;i<4;++i){
            p[i]=ImVec2((x+8.0F+d.points[i].x*10.0F)*sx,(y+8.0F-d.points[i].y*10.0F)*sy);
        }
        const float sh=d.quad->shade?shade[static_cast<std::size_t>(d.quad->face)]:1.0F;
        const int c=static_cast<int>(std::round(255.0F*sh));
        draw->AddImageQuad(textureId(blockAtlas_.id()),p[0],p[1],p[2],p[3],
            ImVec2(d.quad->uvs[0].x,d.quad->uvs[0].y),ImVec2(d.quad->uvs[1].x,d.quad->uvs[1].y),
            ImVec2(d.quad->uvs[2].x,d.quad->uvs[2].y),ImVec2(d.quad->uvs[3].x,d.quad->uvs[3].y),
            IM_COL32(c,c,c,255));
    }
    return true;
}

void GameHud::drawStack(const ItemStack& stack, float x, float y, int scaleFactor, bool count) const {
    if (stack.empty()) return;
    const ItemDefinition& item = items_.get(stack.itemId);
    bool rendered = drawBuiltinEntityStack(stack,x,y,scaleFactor);
    if (!rendered) rendered = drawBlockModelStack(stack,x,y,scaleFactor);
    if (!rendered) {
        std::vector<std::string> icons = item.iconLayers;
        if (icons.empty() && !item.iconResource.empty()) icons.push_back(item.iconResource);
        if (stack.itemId == 347) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "minecraft:items/clock_%02d", dynamicClockFrame_ & 63);
            icons = {buffer};
        } else if (stack.itemId == 345) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "minecraft:items/compass_%02d", dynamicCompassFrame_ & 31);
            icons = {buffer};
        } else if (stack.itemId == 263 && stack.damage == 1) {
            icons = {blockAtlas_.data().contains("minecraft:items/charcoal")
                ? "minecraft:items/charcoal" : "minecraft:items/coal"};
        } else if (stack.itemId == 349) {
            constexpr std::array<const char*, 4> fish = {"fish_cod_raw", "fish_salmon_raw", "fish_clownfish_raw", "fish_pufferfish_raw"};
            icons = {std::string("minecraft:items/") + fish[std::min<std::size_t>(stack.damage, 3)]};
        } else if (stack.itemId == 350) {
            icons = {std::string("minecraft:items/") + (stack.damage == 1 ? "fish_salmon_cooked" : "fish_cod_cooked")};
        } else if (stack.itemId == 351) {
            constexpr std::array<const char*, 16> dyes = {"black","red","green","brown","blue","purple","cyan","silver","gray","pink","lime","yellow","light_blue","magenta","orange","white"};
            icons = {std::string("minecraft:items/dye_powder_") + dyes[std::min<std::size_t>(stack.damage, 15)]};
        }

        if (icons.empty() && item.placedBlock) {
            const BlockState state = makeBlockState(static_cast<std::uint16_t>(*item.placedBlock), static_cast<std::uint8_t>(stack.damage & 15U));
            icons.push_back(blockAtlas_.data().sprite(BlockRegistry::texture(state, Face::Up)).name);
        }
        if (!icons.empty()) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            const ImGuiIO& io = ImGui::GetIO();
            const float scaleX = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
            const float scaleY = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
            for (const std::string& icon : icons) {
                if (icon.empty() || !blockAtlas_.data().contains(icon)) continue;
                const AtlasSprite& sprite = blockAtlas_.data().sprite(icon);
                if (sprite.name == "minecraft:missingno") continue;
                draw->AddImage(textureId(blockAtlas_.id()), ImVec2(x * scaleX, y * scaleY),
                               ImVec2((x + 16.0F) * scaleX, (y + 16.0F) * scaleY),
                               ImVec2(sprite.bounds.u0, sprite.bounds.v0),
                               ImVec2(sprite.bounds.u1, sprite.bounds.v1));
            }
        }
    }
    if (count && stack.count > 1) {
        const std::string label = std::to_string(stack.count);
        drawText(x + 17.0F, y + 9.0F, label, scaleFactor, true, 0xFFFFFFFFU);
    }
}
void GameHud::drawTooltip(const ItemStack& stack, float mouseX, float mouseY,
                          int scaledWidth, int scaledHeight, int scaleFactor) const {
    if (stack.empty()) return;
    std::string line = items_.stackDisplayName(stack);
    const ToolStats tool = SurvivalRules::toolStats(stack.itemId);
    std::string durability;
    if (tool.maxUses > 0) durability = "Durability: " + std::to_string(std::max(0, tool.maxUses - static_cast<int>(stack.damage))) + " / " + std::to_string(tool.maxUses);
    const float width = std::max(textWidth(line), durability.empty() ? 0.0F : textWidth(durability)) + 8.0F;
    const float height = durability.empty() ? 16.0F : 26.0F;
    float x = mouseX + 12.0F;
    float y = mouseY - 12.0F;
    if (x + width > scaledWidth) x = mouseX - width - 4.0F;
    if (y + height > scaledHeight) y = scaledHeight - height - 2.0F;
    y = std::max(2.0F, y);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    // Minecraft 1.12.2 GuiScreen tooltip geometry, but resolve the original
    // alpha-blended purple edge to opaque pixels so the border cannot show the
    // world through it. The outer purple is the requested #560E81 and the
    // interior remains vanilla #100010.
    const ImU32 purple = IM_COL32(86,14,129,255);
    const ImU32 innerPurple = IM_COL32(43,7,65,255);
    const ImU32 centre = IM_COL32(16,0,16,255);
    draw->AddRectFilled(ImVec2((x-3)*sx,(y-4)*sy), ImVec2((x+width+3)*sx,(y+height+4)*sy), purple);
    draw->AddRectFilled(ImVec2((x-2)*sx,(y-3)*sy), ImVec2((x+width+2)*sx,(y+height+3)*sy), innerPurple);
    draw->AddRectFilled(ImVec2((x-1)*sx,(y-2)*sy), ImVec2((x+width+1)*sx,(y+height+2)*sy), centre);
    drawText(x + 4.0F, y + 4.0F, line, scaleFactor, false, 0xFFFFFFFFU);
    if (!durability.empty()) drawText(x + 4.0F, y + 14.0F, durability, scaleFactor, false, 0xFFAAAAAAU);
}

void GameHud::renderHotbar(const Player& player, int scaledWidth,
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
    const float selectorX = x - 1.0F + static_cast<float>(player.inventory().selectedHotbar()) * 20.0F;
    draw->AddImage(textureId(widgetsTexture_), ImVec2(sx(selectorX), sy(y - 1.0F)),
                   ImVec2(sx(selectorX + 24.0F), sy(y + 21.0F)),
                   ImVec2(0.0F, 22.0F / 256.0F), ImVec2(24.0F / 256.0F, 44.0F / 256.0F));
    for (std::size_t slot = 0; slot < PlayerInventory::hotbarSize; ++slot) {
        const float itemX = center - 90.0F + static_cast<float>(slot * 20) + 2.0F;
        const float itemY = static_cast<float>(scaledHeight - 19);
        drawStack(player.inventory().slot(slot), itemX, itemY, scaleFactor, true);
    }
}

void GameHud::interactInventorySlot(ItemStack& slot, bool rightClick, bool creative) {
    static_cast<void>(creative);
    if (rightClick) {
        if (cursorStack_.empty()) {
            if (slot.empty()) return;
            const int take = (slot.count + 1) / 2;
            cursorStack_ = slot;
            cursorStack_.count = take;
            slot.shrink(take);
            return;
        }
        if (slot.empty()) {
            slot = cursorStack_;
            slot.count = 1;
            cursorStack_.shrink(1);
            return;
        }
        if (slot.sameItem(cursorStack_)) {
            const int limit = items_.get(slot.itemId).maxStackSize;
            if (slot.count < limit) {
                ++slot.count;
                cursorStack_.shrink(1);
            }
        }
        return;
    }

    if (cursorStack_.empty()) {
        cursorStack_ = slot;
        slot.clear();
        return;
    }
    if (slot.empty()) {
        slot = cursorStack_;
        cursorStack_.clear();
        return;
    }
    if (slot.sameItem(cursorStack_)) {
        const int limit = items_.get(slot.itemId).maxStackSize;
        const int moved = std::min(limit - slot.count, cursorStack_.count);
        if (moved > 0) {
            slot.count += moved;
            cursorStack_.shrink(moved);
            return;
        }
    }
    std::swap(slot, cursorStack_);
}

void GameHud::renderSurvivalInventory(Player& player, int scaledWidth,
                                      int scaledHeight, int scaleFactor) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    const float left = static_cast<float>((scaledWidth - 176) / 2);
    const float top = static_cast<float>((scaledHeight - 166) / 2);
    draw->AddImage(textureId(inventoryTexture_), ImVec2(left*sx, top*sy),
                   ImVec2((left+176)*sx,(top+166)*sy), ImVec2(0,0), ImVec2(176.0F/256.0F,166.0F/256.0F));

    const float mx = io.MousePos.x / sx;
    const float my = io.MousePos.y / sy;
    ItemStack hovered{};
    const bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    // ContainerPlayer 2x2 crafting matrix and result slot (1.12.2 positions).
    for (int row = 0; row < 2; ++row) for (int column = 0; column < 2; ++column) {
        const std::size_t index = static_cast<std::size_t>(row * 2 + column);
        const float x = left + 98.0F + column * 18.0F;
        const float y = top + 18.0F + row * 18.0F;
        ItemStack& slot = playerCraftGrid_[index];
        drawStack(slot, x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x + 18 && my >= y && my < y + 18) {
            hovered = slot;
            if (leftClick || rightClick) interactInventorySlot(slot, rightClick, false);
        }
    }
    {
        std::vector<ItemStack> grid(playerCraftGrid_.begin(), playerCraftGrid_.end());
        const CraftingMatch match = crafting_.match(grid, 2, 2);
        const float x = left + 154.0F, y = top + 28.0F;
        if (!match.output.empty()) drawStack(match.output, x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x + 18 && my >= y && my < y + 18) {
            hovered = match.output;
            if (leftClick && !match.output.empty()) {
                std::vector<ItemStack> overflow;
                if (crafting_.takeResult(grid, 2, 2, cursorStack_, &overflow)) {
                    std::copy(grid.begin(), grid.end(), playerCraftGrid_.begin());
                    for (ItemStack& remainder : overflow) player.inventory().addStack(remainder, items_.get(remainder.itemId).maxStackSize);
                }
            }
        }
    }

    // InventoryPlayer exposes four armor slots separately from the 36 main slots.
    // Slot order here is helmet, chest, legs, boots on screen, while storage is
    // boots, legs, chest, helmet to mirror 1.12.2 armorItemInSlot.
    for (int visual = 0; visual < 4; ++visual) {
        const int armorIndex = 3 - visual;
        const float x = left + 8.0F, y = top + 8.0F + visual * 18.0F;
        ItemStack& armor = player.inventory().armor(static_cast<std::size_t>(armorIndex));
        drawStack(armor, x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x+18 && my >= y && my < y+18) {
            hovered = armor;
            if (leftClick) {
                if (cursorStack_.empty()) { cursorStack_ = armor; armor.clear(); }
                else if (SurvivalRules::isArmorForSlot(cursorStack_.itemId, armorIndex)) std::swap(cursorStack_, armor);
            } else if (rightClick && armor.empty() && !cursorStack_.empty() &&
                       SurvivalRules::isArmorForSlot(cursorStack_.itemId, armorIndex)) {
                armor = cursorStack_; armor.count = 1; cursorStack_.shrink(1);
            }
        }
    }
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 9; ++column) {
        const std::size_t index = static_cast<std::size_t>(9 + row * 9 + column);
        const float x = left + 8.0F + column * 18.0F;
        const float y = top + 84.0F + row * 18.0F;
        drawStack(player.inventory().slot(index), x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x+18 && my >= y && my < y+18) {
            hovered = player.inventory().slot(index);
            if (leftClick || rightClick) interactInventorySlot(player.inventory().slot(index), rightClick, false);
        }
    }
    for (int column = 0; column < 9; ++column) {
        const std::size_t index = static_cast<std::size_t>(column);
        const float x = left + 8.0F + column * 18.0F;
        const float y = top + 142.0F;
        drawStack(player.inventory().slot(index), x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x+18 && my >= y && my < y+18) {
            hovered = player.inventory().slot(index);
            if (leftClick || rightClick) interactInventorySlot(player.inventory().slot(index), rightClick, false);
        }
    }
    if (!hovered.empty()) drawTooltip(hovered, mx, my, scaledWidth, scaledHeight, scaleFactor);
    if (!cursorStack_.empty()) drawStack(cursorStack_, mx - 8.0F, my - 8.0F, scaleFactor);
}

void GameHud::renderCreativeInventory(Player& player, int scaledWidth,
                                      int scaledHeight, int scaleFactor) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    const float left = static_cast<float>((scaledWidth - 195) / 2);
    const float top = static_cast<float>((scaledHeight - 136) / 2);
    const GLuint background = selectedCreativeTab_ == CreativeTab::Search ? creativeSearchTexture_ :
                              selectedCreativeTab_ == CreativeTab::Inventory ? creativeInventoryTexture_ : creativeItemsTexture_;

    const float mx = io.MousePos.x / sx;
    const float my = io.MousePos.y / sy;
    const bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // CreativeTabs indices/columns are kept identical to 1.12.2 so the exact
    // tabs.png atlas can be sampled with GuiContainerCreative#drawTab's UVs.
    constexpr std::array<CreativeTab, 12> tabs = {
        CreativeTab::BuildingBlocks, CreativeTab::Decorations, CreativeTab::Redstone,
        CreativeTab::Transportation, CreativeTab::Hotbar, CreativeTab::Search,
        CreativeTab::Misc, CreativeTab::Food, CreativeTab::Tools,
        CreativeTab::Combat, CreativeTab::Brewing, CreativeTab::Inventory
    };
    const auto tabIcon = [](CreativeTab tab) -> ItemStack {
        switch (tab) {
            case CreativeTab::BuildingBlocks: return {45, 1, 0, {}};      // brick block
            case CreativeTab::Decorations: return {175, 1, 5, {}};       // peony
            case CreativeTab::Redstone: return {331, 1, 0, {}};          // redstone
            case CreativeTab::Transportation: return {27, 1, 0, {}};     // powered rail
            case CreativeTab::Hotbar: return {47, 1, 0, {}};             // bookshelf
            case CreativeTab::Search: return {345, 1, 0, {}};            // compass
            case CreativeTab::Misc: return {327, 1, 0, {}};              // lava bucket
            case CreativeTab::Food: return {260, 1, 0, {}};              // apple
            case CreativeTab::Tools: return {258, 1, 0, {}};             // iron axe
            case CreativeTab::Combat: return {283, 1, 0, {}};            // golden sword
            case CreativeTab::Brewing: return {373, 1, 0, {}};           // water-potion bottle base
            case CreativeTab::Inventory: return {54, 1, 0, {}};          // chest
        }
        return {};
    };
    const auto tabRect = [&](CreativeTab tab) {
        const int index = static_cast<int>(tab);
        const int column = index % 6;
        float x = left + static_cast<float>(28 * column);
        if (column == 5) x = left + 195.0F - static_cast<float>(28 * (6 - column));
        else if (column > 0) x += static_cast<float>(column);
        const bool topRow = index < 6;
        const float y = topRow ? top - 28.0F : top + 132.0F;
        return std::array<float, 4>{x, y, static_cast<float>(column), topRow ? 1.0F : 0.0F};
    };
    const auto drawTab = [&](CreativeTab tab, bool selected) {
        const auto rect = tabRect(tab);
        const float x = rect[0], y = rect[1];
        const int column = static_cast<int>(rect[2]);
        const bool topRow = rect[3] != 0.0F;
        const float u = static_cast<float>(column * 28) / 256.0F;
        int sourceY = selected ? 32 : 0;
        if (!topRow) sourceY += 64;
        draw->AddImage(textureId(creativeTabsTexture_), ImVec2(x*sx, y*sy),
                       ImVec2((x+28.0F)*sx, (y+32.0F)*sy),
                       ImVec2(u, static_cast<float>(sourceY)/256.0F),
                       ImVec2(u+28.0F/256.0F, static_cast<float>(sourceY+32)/256.0F));
        const float iconY = y + 8.0F + (topRow ? 1.0F : -1.0F);
        drawStack(tabIcon(tab), x + 6.0F, iconY, scaleFactor, false);
    };

    for (CreativeTab tab : tabs)
        if (tab != selectedCreativeTab_) drawTab(tab, false);

    draw->AddImage(textureId(background), ImVec2(left*sx, top*sy), ImVec2((left+195)*sx,(top+136)*sy),
                   ImVec2(0,0), ImVec2(195.0F/256.0F,136.0F/256.0F));

    ItemStack hovered{};
    CreativeTab hoveredTab = selectedCreativeTab_;
    bool hasHoveredTab = false;
    for (CreativeTab tab : tabs) {
        const auto rect = tabRect(tab);
        if (mx >= rect[0] && mx <= rect[0] + 28.0F && my >= rect[1] && my <= rect[1] + 32.0F) {
            hoveredTab = tab;
            hasHoveredTab = true;
            if (leftClick) {
                selectedCreativeTab_ = tab;
                creativeScrollRow_ = 0;
                searchFocused_ = (tab == CreativeTab::Search);
                if (tab != CreativeTab::Search) searchText_.clear();
            }
        }
    }

    if (selectedCreativeTab_ == CreativeTab::Search) {
        const bool overSearch = mx >= left + 82.0F && mx < left + 171.0F &&
                                my >= top + 5.0F && my < top + 17.0F;
        if (leftClick && overSearch) {
            searchFocused_ = true;
            // Creative inventory already releases the GLFW cursor; explicitly
            // keep it free when the search field acquires keyboard focus.
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (leftClick && !overSearch && !hasHoveredTab) {
            searchFocused_ = false;
        }
        if (searchFocused_) {
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !searchText_.empty()) searchText_.pop_back();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) searchText_.clear();
            for (ImWchar c : io.InputQueueCharacters) {
                if (c >= 32 && c < 127 && searchText_.size() < 50)
                    searchText_.push_back(static_cast<char>(c));
            }
        }
        drawText(left + 84.0F, top + 7.0F, searchText_, scaleFactor, false, 0xFFFFFFFFU);
    } else if (selectedCreativeTab_ != CreativeTab::Inventory) {
        searchFocused_ = false;
        drawText(left + 8.0F, top + 6.0F, ItemRegistry::tabName(selectedCreativeTab_), scaleFactor,
                 false, 0xFF404040U);
    }

    if (selectedCreativeTab_ == CreativeTab::Inventory) {
        // Creative inventory tab uses tab_inventory.png and relocates the normal
        // player inventory slots exactly into the 195x136 creative container.
        for (int row = 0; row < 3; ++row) for (int column = 0; column < 9; ++column) {
            const std::size_t index = static_cast<std::size_t>(9 + row * 9 + column);
            const float x = left + 9.0F + column * 18.0F;
            const float y = top + 54.0F + row * 18.0F;
            drawStack(player.inventory().slot(index), x + 1.0F, y + 1.0F, scaleFactor);
            if (mx >= x && mx < x+18 && my >= y && my < y+18) {
                hovered = player.inventory().slot(index);
                if (leftClick || rightClick) interactInventorySlot(player.inventory().slot(index), rightClick, true);
            }
        }
        for (int column = 0; column < 9; ++column) {
            const float x = left + 9.0F + column * 18.0F;
            const float y = top + 112.0F;
            drawStack(player.inventory().slot(static_cast<std::size_t>(column)), x + 1.0F, y + 1.0F, scaleFactor);
            if (mx >= x && mx < x+18 && my >= y && my < y+18) {
                hovered = player.inventory().slot(static_cast<std::size_t>(column));
                if (leftClick || rightClick) interactInventorySlot(player.inventory().slot(static_cast<std::size_t>(column)), rightClick, true);
            }
        }
        // The vanilla creative inventory bin slot is at 173,112; dropping the
        // cursor stack on it clears the carried stack.
        const float binX = left + 173.0F, binY = top + 112.0F;
        if (mx >= binX && mx < binX + 18.0F && my >= binY && my < binY + 18.0F && leftClick)
            cursorStack_.clear();
        drawTab(selectedCreativeTab_, true);
        if (!hovered.empty()) drawTooltip(hovered, mx, my, scaledWidth, scaledHeight, scaleFactor);
        if (!cursorStack_.empty()) drawStack(cursorStack_, mx - 8.0F, my - 8.0F, scaleFactor);
        if (hasHoveredTab) {
            const std::string label(ItemRegistry::tabName(hoveredTab));
            const float w = textWidth(label) + 8.0F;
            draw->AddRectFilled(ImVec2((mx+10)*sx,(my+8)*sy), ImVec2((mx+10+w)*sx,(my+22)*sy), IM_COL32(16,0,16,240));
            drawText(mx+14,my+11,label,scaleFactor,false,0xFFFFFFFFU);
        }
        return;
    }

    std::vector<ItemStack> visible;
    if (selectedCreativeTab_ == CreativeTab::Search) visible = items_.searchStacks(searchText_);
    else if (selectedCreativeTab_ == CreativeTab::Hotbar) {
        // Saved-toolbar persistence is a later settings/save concern; expose the
        // current nine-slot toolbar in the same 9-column container immediately.
        for (std::size_t i = 0; i < PlayerInventory::hotbarSize; ++i)
            visible.push_back(player.inventory().slot(i));
    } else visible = items_.creativeStacks(selectedCreativeTab_);

    const int rows = std::max(0, (static_cast<int>(visible.size()) + 8) / 9);
    const int maxScroll = std::max(0, rows - 5);
    const float wheel = io.MouseWheel;
    if (wheel != 0.0F && maxScroll > 0)
        creativeScrollRow_ = std::clamp(creativeScrollRow_ - (wheel > 0.0F ? 1 : -1), 0, maxScroll);

    // Vanilla scrollbar track is x=175,y=18..130 and uses tabs.png u=232.
    if (selectedCreativeTab_ != CreativeTab::Hotbar) {
        const float trackX = left + 175.0F;
        const float trackY = top + 18.0F;
        const bool canScroll = maxScroll > 0;
        float fraction = maxScroll > 0 ? static_cast<float>(creativeScrollRow_) / static_cast<float>(maxScroll) : 0.0F;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && mx >= trackX && mx < trackX + 14.0F &&
            my >= trackY && my < trackY + 112.0F && canScroll) {
            fraction = std::clamp((my - trackY - 7.5F) / 97.0F, 0.0F, 1.0F);
            creativeScrollRow_ = std::clamp(static_cast<int>(std::lround(fraction * maxScroll)), 0, maxScroll);
        }
        const float knobY = trackY + 97.0F * fraction;
        const float u0 = (232.0F + (canScroll ? 0.0F : 12.0F)) / 256.0F;
        draw->AddImage(textureId(creativeTabsTexture_), ImVec2(trackX*sx,knobY*sy),
                       ImVec2((trackX+12.0F)*sx,(knobY+15.0F)*sy),
                       ImVec2(u0,0.0F), ImVec2(u0+12.0F/256.0F,15.0F/256.0F));
    }

    for (int row = 0; row < 5; ++row) for (int column = 0; column < 9; ++column) {
        const int itemIndex = (creativeScrollRow_ + row) * 9 + column;
        if (itemIndex < 0 || itemIndex >= static_cast<int>(visible.size())) continue;
        const ItemStack stack = visible[static_cast<std::size_t>(itemIndex)];
        if (stack.empty()) continue;
        const ItemDefinition& definition = items_.get(stack.itemId);
        const float x = left + 9.0F + column*18.0F;
        const float y = top + 18.0F + row*18.0F;
        drawStack(stack, x + 1.0F, y + 1.0F, scaleFactor, false);
        if (mx >= x && mx < x+18 && my >= y && my < y+18) {
            hovered = stack;
            if (leftClick || rightClick) {
                cursorStack_ = stack;
                cursorStack_.count = rightClick ? 1 : definition.maxStackSize;
            }
        }
    }

    for (int column = 0; column < 9; ++column) {
        const float x = left + 9.0F + column*18.0F;
        const float y = top + 112.0F;
        drawStack(player.inventory().slot(static_cast<std::size_t>(column)), x + 1.0F, y + 1.0F, scaleFactor);
        if (mx >= x && mx < x+18 && my >= y && my < y+18) {
            hovered = player.inventory().slot(static_cast<std::size_t>(column));
            if (leftClick || rightClick)
                interactInventorySlot(player.inventory().slot(static_cast<std::size_t>(column)), rightClick, true);
        }
    }

    drawTab(selectedCreativeTab_, true);
    if (hasHoveredTab) {
        const std::string label(ItemRegistry::tabName(hoveredTab));
        const float width = textWidth(label) + 8.0F;
        float tx = mx + 10.0F, ty = my + 8.0F;
        if (tx + width > scaledWidth) tx = mx - width - 4.0F;
        draw->AddRectFilled(ImVec2(tx*sx,ty*sy), ImVec2((tx+width)*sx,(ty+14.0F)*sy), IM_COL32(16,0,16,240));
        drawText(tx+4.0F,ty+3.0F,label,scaleFactor,false,0xFFFFFFFFU);
    } else if (!hovered.empty()) {
        drawTooltip(hovered, mx, my, scaledWidth, scaledHeight, scaleFactor);
    }
    if (!cursorStack_.empty()) drawStack(cursorStack_, mx - 8.0F, my - 8.0F, scaleFactor);
}

void GameHud::renderInventory(Player& player, bool creative, int scaledWidth,
                              int scaledHeight, int scaleFactor) {
    if (creative) renderCreativeInventory(player, scaledWidth, scaledHeight, scaleFactor);
    else renderSurvivalInventory(player, scaledWidth, scaledHeight, scaleFactor);
}

void GameHud::openBlockEntityScreen(const BlockEntityAction& action) {
    if (activeBlockEntityAction_) blockEntities_.endViewing(*activeBlockEntityAction_);
    activeBlockEntityAction_ = action;
    signEditLine_ = 0;
    blockEntities_.beginViewing(action);
}

void GameHud::closeBlockEntityScreen(Player* player) {
    if (activeBlockEntityAction_ && activeBlockEntityAction_->type == BlockEntityActionType::OpenCraftingTable && player != nullptr) {
        for (ItemStack& stack : tableCraftGrid_) {
            if (stack.empty()) continue;
            player->inventory().addStack(stack, items_.get(stack.itemId).maxStackSize);
            if (!stack.empty()) pendingCraftingDrops_.push_back(stack);
            stack.clear();
        }
    }
    if (activeBlockEntityAction_) blockEntities_.endViewing(*activeBlockEntityAction_);
    activeBlockEntityAction_.reset();
    signEditLine_ = 0;
}

void GameHud::closePlayerCrafting(Player& player) {
    for (ItemStack& stack : playerCraftGrid_) {
        if (stack.empty()) continue;
        player.inventory().addStack(stack, items_.get(stack.itemId).maxStackSize);
        if (!stack.empty()) pendingCraftingDrops_.push_back(stack);
        stack.clear();
    }
}

std::vector<ItemStack> GameHud::takeCraftingDrops() {
    std::vector<ItemStack> result;
    result.swap(pendingCraftingDrops_);
    return result;
}

std::vector<ExperienceDrop> GameHud::takeExperienceDrops() {
    auto result=std::move(experienceDrops_);
    experienceDrops_.clear();
    return result;
}

void GameHud::renderContainerScreen(const World& world, Player& player,
                                    int scaledWidth, int scaledHeight, int scaleFactor) {
    if (!activeBlockEntityAction_) return;
    const glm::ivec3 pos = activeBlockEntityAction_->position;
    const int slotCount = blockEntities_.containerSlotCount(world,pos);
    if (slotCount <= 0) return;
    const int rows = slotCount / 9;
    const int ySize = 114 + rows * 18;
    const float left = static_cast<float>((scaledWidth - 176) / 2);
    const float top = static_cast<float>((scaledHeight - ySize) / 2);
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F);
    const float sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float upperHeight=static_cast<float>(rows*18+17);
    draw->AddImage(textureId(generic54Texture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+upperHeight)*sy),
        ImVec2(0,0),ImVec2(176.0F/256.0F,upperHeight/256.0F));
    draw->AddImage(textureId(generic54Texture_),ImVec2(left*sx,(top+upperHeight)*sy),ImVec2((left+176)*sx,(top+upperHeight+96)*sy),
        ImVec2(0,126.0F/256.0F),ImVec2(176.0F/256.0F,222.0F/256.0F));
    drawText(left+8,top+6,blockEntities_.containerTitle(world,pos),scaleFactor,false,0xFF404040U);
    drawText(left+8,top+static_cast<float>(ySize-94),"Inventory",scaleFactor,false,0xFF404040U);
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;
    const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left), rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ItemStack hovered{};
    for(int i=0;i<slotCount;++i){
        const int row=i/9,col=i%9; const float x=left+8+col*18.0F,y=top+18+row*18.0F;
        ItemStack& slot=blockEntities_.containerSlot(world,pos,i); drawStack(slot,x+1,y+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    const float invTop=top+static_cast<float>(rows*18+31);
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){
        const std::size_t idx=static_cast<std::size_t>(9+row*9+col); const float x=left+8+col*18.0F,y=invTop+row*18.0F;
        ItemStack& slot=player.inventory().slot(idx); drawStack(slot,x+1,y+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    const float hotbarY=invTop+58.0F;
    for(int col=0;col<9;++col){
        const float x=left+8+col*18.0F; ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));
        drawStack(slot,x+1,hotbarY+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=hotbarY&&my<hotbarY+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);
    if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}


void GameHud::renderCraftingTableScreen(Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F);
    const float sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-176)/2), top=static_cast<float>((scaledHeight-166)/2);
    draw->AddImage(textureId(craftingTableTexture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+166)*sy),ImVec2(0,0),ImVec2(176.0F/256.0F,166.0F/256.0F));
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy; const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ItemStack hovered{};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){const std::size_t idx=static_cast<std::size_t>(row*3+col);const float x=left+30+col*18.0F,y=top+17+row*18.0F;ItemStack& slot=tableCraftGrid_[idx];drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    std::vector<ItemStack> grid(tableCraftGrid_.begin(),tableCraftGrid_.end());const CraftingMatch match=crafting_.match(grid,3,3);const float rx=left+124,ry=top+35;if(!match.output.empty())drawStack(match.output,rx+1,ry+1,scaleFactor);if(mx>=rx&&mx<rx+18&&my>=ry&&my<ry+18){hovered=match.output;if(lc&&!match.output.empty()){std::vector<ItemStack> overflow;if(crafting_.takeResult(grid,3,3,cursorStack_,&overflow)){std::copy(grid.begin(),grid.end(),tableCraftGrid_.begin());for(ItemStack& remainder:overflow)player.inventory().addStack(remainder, items_.get(remainder.itemId).maxStackSize);}}}
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){const std::size_t idx=static_cast<std::size_t>(9+row*9+col);const float x=left+8+col*18.0F,y=top+84+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){const float x=left+8+col*18.0F,y=top+142;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderFurnaceScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_)return;const glm::ivec3 pos=activeBlockEntityAction_->position;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F),sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-176)/2),top=static_cast<float>((scaledHeight-166)/2);
    draw->AddImage(textureId(furnaceTexture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+166)*sy),ImVec2(0,0),ImVec2(176.0F/256.0F,166.0F/256.0F));
    const float burn=blockEntities_.furnaceBurnProgress(pos),cook=blockEntities_.furnaceCookProgress(pos);
    if(burn>0.0F){const int h=std::clamp(static_cast<int>(burn*13.0F),0,13);if(h>0)draw->AddImage(textureId(furnaceTexture_),ImVec2((left+56)*sx,(top+36+12-h)*sy),ImVec2((left+70)*sx,(top+49)*sy),ImVec2(176.0F/256.0F,(12.0F-h+36.0F)/256.0F),ImVec2(190.0F/256.0F,49.0F/256.0F));}
    if(cook>0.0F){const int w=std::clamp(static_cast<int>(cook*24.0F),0,24);if(w>0)draw->AddImage(textureId(furnaceTexture_),ImVec2((left+79)*sx,(top+34)*sy),ImVec2((left+79+w)*sx,(top+51)*sy),ImVec2(176.0F/256.0F,14.0F/256.0F),ImVec2((176.0F+w)/256.0F,31.0F/256.0F));}
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);ItemStack hovered{};
    constexpr std::array<std::array<float,2>,3> fp={{{56,17},{56,53},{116,35}}};
    for(int i=0;i<3;++i){const float x=left+fp[i][0],y=top+fp[i][1];ItemStack& slot=blockEntities_.containerSlot(world,pos,i);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if((lc||rc)&&i!=2)interactInventorySlot(slot,rc,false);else if(lc&&i==2&&!slot.empty()){const int before=slot.count;if(cursorStack_.empty()){cursorStack_=slot;slot.clear();}else if(cursorStack_.sameItem(slot)){const int limit=items_.get(slot.itemId).maxStackSize;const int moved=std::min(limit-cursorStack_.count,slot.count);cursorStack_.count+=moved;slot.shrink(moved);}if(slot.count<before){const float xp=blockEntities_.takeFurnaceExperience(pos);experienceDrops_.push_back({glm::dvec3(pos)+glm::dvec3(0.5,0.5,0.5),static_cast<int>(std::floor(xp+0.5F))});}}}}
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){const std::size_t idx=static_cast<std::size_t>(9+row*9+col);const float x=left+8+col*18.0F,y=top+84+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){const float x=left+8+col*18.0F,y=top+142;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}


void GameHud::renderHopperScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_) return; const glm::ivec3 pos=activeBlockEntityAction_->position;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F), sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-176)/2), top=static_cast<float>((scaledHeight-133)/2);
    draw->AddImage(textureId(hopperTexture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+133)*sy),ImVec2(0,0),ImVec2(176.0F/256.0F,133.0F/256.0F));
    drawText(left+8,top+6,"Item Hopper",scaleFactor,false,0xFF404040U); drawText(left+8,top+39,"Inventory",scaleFactor,false,0xFF404040U);
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy; const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right); ItemStack hovered{};
    for(int i=0;i<5;++i){float x=left+44+i*18.0F,y=top+20;ItemStack& slot=blockEntities_.containerSlot(world,pos,i);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){std::size_t idx=static_cast<std::size_t>(9+row*9+col);float x=left+8+col*18.0F,y=top+51+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){float x=left+8+col*18.0F,y=top+109;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderDispenserScreen(const World& world, Player& player,
                                    int scaledWidth, int scaledHeight, int scaleFactor) {
    if (!activeBlockEntityAction_) return;
    const glm::ivec3 pos = activeBlockEntityAction_->position;
    const float left = static_cast<float>((scaledWidth - 176) / 2);
    const float top = static_cast<float>((scaledHeight - 166) / 2);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) / std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    draw->AddImage(textureId(dispenserTexture_), ImVec2(left*sx, top*sy),
                   ImVec2((left+176)*sx, (top+166)*sy), ImVec2(0,0),
                   ImVec2(176.0F/256.0F, 166.0F/256.0F));
    drawText(left+8, top+6, blockEntities_.containerTitle(world,pos), scaleFactor, false, 0xFF404040U);
    drawText(left+8, top+72, "Inventory", scaleFactor, false, 0xFF404040U);

    const float mx = io.MousePos.x/sx, my = io.MousePos.y/sy;
    const bool lc = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool rc = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ItemStack hovered{};
    for (int row=0; row<3; ++row) for (int col=0; col<3; ++col) {
        const int index = row*3+col;
        const float x=left+62+col*18.0F, y=top+17+row*18.0F;
        ItemStack& slot=blockEntities_.containerSlot(world,pos,index);
        drawStack(slot,x+1,y+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){
        const std::size_t idx=static_cast<std::size_t>(9+row*9+col);
        const float x=left+8+col*18.0F,y=top+84+row*18.0F;
        ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    for(int col=0;col<9;++col){
        const float x=left+8+col*18.0F,y=top+142;
        ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);
        if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}
    }
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);
    if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderBrewingScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_) return; const glm::ivec3 pos=activeBlockEntityAction_->position;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F), sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-176)/2), top=static_cast<float>((scaledHeight-166)/2);
    draw->AddImage(textureId(brewingTexture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+166)*sy),ImVec2(0,0),ImVec2(176.0F/256.0F,166.0F/256.0F));
    const float progress=blockEntities_.brewingProgress(pos); if(progress>0.0F){const int h=std::clamp(static_cast<int>(progress*28.0F),0,28);draw->AddImage(textureId(brewingTexture_),ImVec2((left+97)*sx,(top+16)*sy),ImVec2((left+106)*sx,(top+16+h)*sy),ImVec2(176.0F/256.0F,0),ImVec2(185.0F/256.0F,h/256.0F));}
    const int fuel=blockEntities_.brewingFuel(pos); if(fuel>0){const int w=std::clamp(fuel*18/20,0,18);draw->AddImage(textureId(brewingTexture_),ImVec2((left+60)*sx,(top+44)*sy),ImVec2((left+60+w)*sx,(top+48)*sy),ImVec2(176.0F/256.0F,29.0F/256.0F),ImVec2((176.0F+w)/256.0F,33.0F/256.0F));}
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);ItemStack hovered{};
    constexpr std::array<std::array<float,2>,5> slots={{{56,51},{79,58},{102,51},{79,17},{17,17}}};
    for(int i=0;i<5;++i){float x=left+slots[i][0],y=top+slots[i][1];ItemStack& slot=blockEntities_.containerSlot(world,pos,i);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){std::size_t idx=static_cast<std::size_t>(9+row*9+col);float x=left+8+col*18.0F,y=top+84+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){float x=left+8+col*18.0F,y=top+142;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderEnchantingScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_) return; const glm::ivec3 pos=activeBlockEntityAction_->position;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F), sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-176)/2), top=static_cast<float>((scaledHeight-166)/2);
    draw->AddImage(textureId(enchantingTexture_),ImVec2(left*sx,top*sy),ImVec2((left+176)*sx,(top+166)*sy),ImVec2(0,0),ImVec2(176.0F/256.0F,166.0F/256.0F));
    drawText(left+8,top+6,"Enchant",scaleFactor,false,0xFF404040U);
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);ItemStack hovered{};
    constexpr std::array<std::array<float,2>,2> slots={{{15,47},{35,47}}}; for(int i=0;i<2;++i){float x=left+slots[i][0],y=top+slots[i][1];ItemStack& slot=blockEntities_.containerSlot(world,pos,i);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int row=0;row<3;++row)for(int col=0;col<9;++col){std::size_t idx=static_cast<std::size_t>(9+row*9+col);float x=left+8+col*18.0F,y=top+84+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){float x=left+8+col*18.0F,y=top+142;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderBeaconScreen(const World& world, Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_) return; const glm::ivec3 pos=activeBlockEntityAction_->position;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F), sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const float left=static_cast<float>((scaledWidth-230)/2), top=static_cast<float>((scaledHeight-219)/2);
    draw->AddImage(textureId(beaconTexture_),ImVec2(left*sx,top*sy),ImVec2((left+230)*sx,(top+219)*sy),ImVec2(0,0),ImVec2(230.0F/256.0F,219.0F/256.0F));
    drawText(left+10,top+6,"Primary Power",scaleFactor,false,0xFF404040U); drawText(left+10,top+76,"Secondary Power",scaleFactor,false,0xFF404040U);
    drawText(left+10,top+105,"Levels: "+std::to_string(blockEntities_.beaconLevels(pos)),scaleFactor,false,0xFF404040U);
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;const bool lc=ImGui::IsMouseClicked(ImGuiMouseButton_Left),rc=ImGui::IsMouseClicked(ImGuiMouseButton_Right);ItemStack hovered{};
    float bx=left+136,by=top+110;ItemStack& beaconSlot=blockEntities_.containerSlot(world,pos,0);drawStack(beaconSlot,bx+1,by+1,scaleFactor);if(mx>=bx&&mx<bx+18&&my>=by&&my<by+18){hovered=beaconSlot;if(lc||rc)interactInventorySlot(beaconSlot,rc,false);}
    const float invTop=top+137;for(int row=0;row<3;++row)for(int col=0;col<9;++col){std::size_t idx=static_cast<std::size_t>(9+row*9+col);float x=left+36+col*18.0F,y=invTop+row*18.0F;ItemStack& slot=player.inventory().slot(idx);drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    for(int col=0;col<9;++col){float x=left+36+col*18.0F,y=top+195;ItemStack& slot=player.inventory().slot(static_cast<std::size_t>(col));drawStack(slot,x+1,y+1,scaleFactor);if(mx>=x&&mx<x+18&&my>=y&&my<y+18){hovered=slot;if(lc||rc)interactInventorySlot(slot,rc,false);}}
    if(!hovered.empty())drawTooltip(hovered,mx,my,scaledWidth,scaledHeight,scaleFactor);if(!cursorStack_.empty())drawStack(cursorStack_,mx-8,my-8,scaleFactor);
}

void GameHud::renderSignEditor(int scaledWidth, int scaledHeight, int scaleFactor) {
    if (!activeBlockEntityAction_) return;
    auto* lines=blockEntities_.signLines(activeBlockEntityAction_->position);
    if(lines==nullptr)return;
    ImGuiIO& io=ImGui::GetIO();
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))signEditLine_=(signEditLine_+3)%4;
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)||ImGui::IsKeyPressed(ImGuiKey_Enter))signEditLine_=(signEditLine_+1)%4;
    if(ImGui::IsKeyPressed(ImGuiKey_Backspace)&&!(*lines)[static_cast<std::size_t>(signEditLine_)].empty())
        (*lines)[static_cast<std::size_t>(signEditLine_)].pop_back();
    for(ImWchar c:io.InputQueueCharacters){
        if(c<32||c>=127)continue; std::string& line=(*lines)[static_cast<std::size_t>(signEditLine_)];
        const char ch=static_cast<char>(c); const float prospective=textWidth(line)+static_cast<float>(charWidths_[static_cast<unsigned char>(ch)]);
        if(prospective<=90.0F)line.push_back(ch);
    }
    const float cx=static_cast<float>(scaledWidth)*0.5F, cy=static_cast<float>(scaledHeight)*0.5F;
    drawText(cx-textWidth("Edit sign message")*0.5F,cy-56,"Edit sign message",scaleFactor,false,0xFFFFFFFFU);
    for(int i=0;i<4;++i){
        std::string display=(*lines)[static_cast<std::size_t>(i)];
        if(i==signEditLine_)display="> "+display+" <";
        drawText(cx-textWidth(display)*0.5F,cy-24+i*10.0F,display,scaleFactor,false,0xFFFFFFFFU);
    }
    // Vanilla-sized Done button using widgets.png (normal button row).
    const float bx=cx-50.0F,by=cy+34.0F;
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F),sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    draw->AddImage(textureId(widgetsTexture_),ImVec2(bx*sx,by*sy),ImVec2((bx+100)*sx,(by+20)*sy),ImVec2(0,66.0F/256.0F),ImVec2(100.0F/256.0F,86.0F/256.0F));
    drawText(cx-textWidth("Done")*0.5F,by+6,"Done",scaleFactor,false,0xFFFFFFFFU);
    const float mx=io.MousePos.x/sx,my=io.MousePos.y/sy;
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&mx>=bx&&mx<bx+100&&my>=by&&my<by+20){ closeBlockEntityScreen(); screenCloseRequested_=true; }
}

void GameHud::renderBlockEntityScreen(const World& world, Player& player,
                                      int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!activeBlockEntityAction_)return;
    if(activeBlockEntityAction_->type==BlockEntityActionType::EditSign)
        renderSignEditor(scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenChest || activeBlockEntityAction_->type==BlockEntityActionType::OpenShulker)
        renderContainerScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenFurnace)
        renderFurnaceScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenHopper)
        renderHopperScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenBrewingStand)
        renderBrewingScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenEnchantingTable)
        renderEnchantingScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenBeacon)
        renderBeaconScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenEnderChest)
        renderContainerScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenDispenser ||
            activeBlockEntityAction_->type==BlockEntityActionType::OpenDropper)
        renderDispenserScreen(world,player,scaledWidth,scaledHeight,scaleFactor);
    else if(activeBlockEntityAction_->type==BlockEntityActionType::OpenCraftingTable)
        renderCraftingTableScreen(player,scaledWidth,scaledHeight,scaleFactor);
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
        if (glm::dot(right, right) < 1.0e-6F) right = glm::vec3(1.0F, 0.0F, 0.0F);
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

bool GameHud::menuButton(int id, float x, float y, float width, std::string_view label,
                         int scaleFactor, bool enabled) {
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) /
        std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) /
        std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    ImGui::SetCursorScreenPos(ImVec2(x * sx, y * sy));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##menu_button", ImVec2(width * sx, 20.0F * sy));
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImGui::PopID();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float v = (enabled ? (hovered ? 86.0F : 66.0F) : 46.0F) / 256.0F;
    const float half = width * 0.5F;
    const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(160,160,160,255);
    draw->AddImage(textureId(widgetsTexture_), ImVec2(x*sx,y*sy),
                   ImVec2((x+half)*sx,(y+20.0F)*sy),
                   ImVec2(0.0F,v), ImVec2(100.0F/256.0F,v+20.0F/256.0F), tint);
    draw->AddImage(textureId(widgetsTexture_), ImVec2((x+half)*sx,y*sy),
                   ImVec2((x+width)*sx,(y+20.0F)*sy),
                   ImVec2((200.0F-half)/256.0F,v),
                   ImVec2(200.0F/256.0F,v+20.0F/256.0F), tint);
    const float textX = x + width*0.5F - textWidth(label)*0.5F;
    drawText(textX, y+6.0F, label, scaleFactor, false,
             enabled ? (hovered ? 0xFFFFFFA0U : 0xFFFFFFFFU) : 0xFFA0A0A0U);
    return clicked;
}

void GameHud::renderPauseMenu(int scaledWidth, int scaledHeight, int scaleFactor) {
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = static_cast<float>(scaleFactor) /
        std::max(io.DisplayFramebufferScale.x, 1.0e-6F);
    const float sy = static_cast<float>(scaleFactor) /
        std::max(io.DisplayFramebufferScale.y, 1.0e-6F);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // GuiScreen#drawDefaultBackground while a world is loaded: dark gradient
    // over the paused scene, not the options-background texture.
    draw->AddRectFilledMultiColor(ImVec2(0,0),
        ImVec2(static_cast<float>(scaledWidth)*sx, static_cast<float>(scaledHeight)*sy),
        IM_COL32(0,0,0,112), IM_COL32(0,0,0,112),
        IM_COL32(0,0,0,176), IM_COL32(0,0,0,176));

    drawText(static_cast<float>(scaledWidth)*0.5F - textWidth("Game menu")*0.5F,
             40.0F, "Game menu", scaleFactor, false, 0xFFFFFFFFU);

    const float center = static_cast<float>(scaledWidth)*0.5F;
    const float base = static_cast<float>(scaledHeight)/4.0F - 16.0F;
    if (menuButton(400, center-100.0F, base+24.0F, 200.0F, "Back to Game", scaleFactor))
        resumeRequested_ = true;
    menuButton(401, center-100.0F, base+48.0F, 98.0F, "Advancements", scaleFactor, false);
    menuButton(402, center+2.0F, base+48.0F, 98.0F, "Statistics", scaleFactor, false);
    menuButton(403, center-100.0F, base+96.0F, 98.0F, "Options...", scaleFactor, false);
    menuButton(404, center+2.0F, base+96.0F, 98.0F, "Open to LAN", scaleFactor, false);
    if (menuButton(405, center-100.0F, base+120.0F, 200.0F,
                   "Save and Quit to Title", scaleFactor))
        returnToTitleRequested_ = true;
}


void GameHud::renderSurvivalStatus(const Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if (player.gameMode() != GameMode::Survival || player.dead()) return;
    ImDrawList* draw=ImGui::GetBackgroundDrawList(); const ImGuiIO& io=ImGui::GetIO();
    const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1.0e-6F);
    const float sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1.0e-6F);
    const auto icon=[&](float x,float y,int u,int v,int w=9,int h=9){draw->AddImage(textureId(iconsTexture_),ImVec2(x*sx,y*sy),ImVec2((x+w)*sx,(y+h)*sy),ImVec2(u/256.0F,v/256.0F),ImVec2((u+w)/256.0F,(v+h)/256.0F));};
    const int tick=player.ticksExisted();
    const int health=std::clamp(static_cast<int>(std::ceil(player.health())),0,20);
    if(playerHealthDisplay_==0&&lastPlayerHealthDisplay_==0){playerHealthDisplay_=health;lastPlayerHealthDisplay_=health;}
    if(health<playerHealthDisplay_ && player.hurtTime()>0){lastPlayerHealthDisplay_=playerHealthDisplay_;healthFlashUntil_=tick+20;}
    else if(health>playerHealthDisplay_ && player.hurtTime()>0){lastPlayerHealthDisplay_=playerHealthDisplay_;healthFlashUntil_=tick+10;}
    if(tick-healthFlashUntil_>20) lastPlayerHealthDisplay_=health;
    playerHealthDisplay_=health;
    const bool flash=healthFlashUntil_>tick && ((healthFlashUntil_-tick)/3)%2==1;

    const int left=scaledWidth/2-91,right=scaledWidth/2+91,baseY=scaledHeight-39;
    const int armor=std::clamp(player.armorValue(),0,20);
    for(int i=0;i<10;++i){const float x=static_cast<float>(left+i*8); if(armor>0){icon(x,static_cast<float>(baseY-10),16,9);if(i*2+1<armor)icon(x,static_cast<float>(baseY-10),34,9);else if(i*2+1==armor)icon(x,static_cast<float>(baseY-10),25,9);}}

    // GuiIngame heart order/UVs, including the previous-health flash overlay and
    // the deterministic low-health jitter used by 1.12.2.
    for(int i=9;i>=0;--i){
        float y=static_cast<float>(baseY);
        if(health<=4){const unsigned seed=static_cast<unsigned>(tick*312871+i*1103515245u);y+=static_cast<float>((seed>>16)&1u);}
        const float x=static_cast<float>(left+i*8);
        icon(x,y,16+(flash?9:0),0);
        if(flash){if(i*2+1<lastPlayerHealthDisplay_)icon(x,y,70,0);else if(i*2+1==lastPlayerHealthDisplay_)icon(x,y,79,0);}
        if(i*2+1<health)icon(x,y,52,0);else if(i*2+1==health)icon(x,y,61,0);
    }

    const int food=std::clamp(player.foodStats().foodLevel(),0,20);
    for(int i=0;i<10;++i){float y=static_cast<float>(baseY);if(player.foodStats().saturationLevel()<=0.0F&&food>0&&tick%(food*3+1)==0){const unsigned seed=static_cast<unsigned>((tick+i)*312871);y+=static_cast<float>(static_cast<int>((seed>>16)%3)-1);}const float x=static_cast<float>(right-i*8-9);icon(x,y,16,27);if(i*2+1<food)icon(x,y,52,27);else if(i*2+1==food)icon(x,y,61,27);}

    if(player.air()<300){const int full=static_cast<int>(std::ceil((player.air()-2)*10.0/300.0));const int partial=static_cast<int>(std::ceil(player.air()*10.0/300.0))-full;for(int i=0;i<full+partial;++i)icon(static_cast<float>(right-i*8-9),static_cast<float>(baseY-10),i<full?16:25,18);}

    // Vanilla XP bar and level text.
    const float xpX=static_cast<float>(scaledWidth/2-91),xpY=static_cast<float>(scaledHeight-29);
    draw->AddImage(textureId(iconsTexture_),ImVec2(xpX*sx,xpY*sy),ImVec2((xpX+182)*sx,(xpY+5)*sy),ImVec2(0,64.0F/256.0F),ImVec2(182.0F/256.0F,69.0F/256.0F));
    const int xpWidth=std::clamp(static_cast<int>(player.experienceProgress()*183.0F),0,182);if(xpWidth>0)draw->AddImage(textureId(iconsTexture_),ImVec2(xpX*sx,xpY*sy),ImVec2((xpX+xpWidth)*sx,(xpY+5)*sy),ImVec2(0,69.0F/256.0F),ImVec2(xpWidth/256.0F,74.0F/256.0F));
    if(player.experienceLevel()>0){const std::string level=std::to_string(player.experienceLevel());const float x=static_cast<float>(scaledWidth)*0.5F-textWidth(level)*0.5F,y=static_cast<float>(scaledHeight-35);drawText(x+1,y,level,scaleFactor,false,0xFF000000U);drawText(x-1,y,level,scaleFactor,false,0xFF000000U);drawText(x,y+1,level,scaleFactor,false,0xFF000000U);drawText(x,y-1,level,scaleFactor,false,0xFF000000U);drawText(x,y,level,scaleFactor,false,0xFF80FF20U);}
}

void GameHud::renderDeathScreen(const Player& player, int scaledWidth, int scaledHeight, int scaleFactor) {
    if(!player.dead()) return;
    ImDrawList* draw=ImGui::GetBackgroundDrawList();const ImGuiIO& io=ImGui::GetIO();const float sx=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.x,1e-6F),sy=static_cast<float>(scaleFactor)/std::max(io.DisplayFramebufferScale.y,1e-6F);
    draw->AddRectFilled(ImVec2(0,0),ImVec2(scaledWidth*sx,scaledHeight*sy),IM_COL32(90,0,0,115));
    const float cx=scaledWidth*0.5F; drawText(cx-textWidth("You died!")*0.5F,scaledHeight*0.25F-10,"You died!",scaleFactor,false,0xFFFFFFFFU);
    drawText(cx-textWidth("Score: 0")*0.5F,scaledHeight*0.25F+14,"Score: 0",scaleFactor,false,0xFFFFFFFFU);
    if(menuButton(500,cx-100,scaledHeight*0.25F+72,200,"Respawn",scaleFactor))respawnRequested_=true;
    if(menuButton(501,cx-100,scaledHeight*0.25F+96,200,"Title screen",scaleFactor))returnToTitleRequested_=true;
}

void GameHud::render(const World& world, Player& player, const Camera& camera,
                     const WorldConfig& config, const ChunkStreamer& streamer,
                     const LightingEngine& lighting, const WorldRenderer& renderer,
                     const std::optional<RaycastHit>& hit,
                     int framebufferWidth, int framebufferHeight, double framesPerSecond, double worldTime,
                     bool showDebug, bool paused, bool inventoryOpen) {
    const double dayFraction = std::fmod(worldTime, 24000.0) / 24000.0;
    dynamicClockFrame_ = static_cast<int>(std::floor((dayFraction < 0.0 ? dayFraction + 1.0 : dayFraction) * 64.0)) & 63;
    const glm::dvec3 toSpawn = player.respawnPosition() - player.feetPosition();
    const glm::vec3 front = camera.front();
    if ((toSpawn.x * toSpawn.x + toSpawn.z * toSpawn.z) > 1.0e-6 &&
        (front.x * front.x + front.z * front.z) > 1.0e-6F) {
        const double target = std::atan2(toSpawn.z, toSpawn.x);
        const double facing = std::atan2(static_cast<double>(front.z), static_cast<double>(front.x));
        double relative = (target - facing) / (2.0 * 3.14159265358979323846);
        relative -= std::floor(relative);
        dynamicCompassFrame_ = (16 + static_cast<int>(std::floor(relative * 32.0 + 0.5))) & 31;
    }
    const ScaledResolution scaled = ScaledResolution::fromDisplay(
        framebufferWidth, framebufferHeight, config.guiScale, false);
    if (!inventoryOpen && !player.dead()) renderHotbar(player, scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
    if (!inventoryOpen) renderSurvivalStatus(player, scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
    if (showDebug && !inventoryOpen)
        renderDebug(world, player, camera, config, streamer, lighting, renderer, hit,
                    scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor,
                    framebufferWidth, framebufferHeight, framesPerSecond);
    if (inventoryOpen) {
        if (activeBlockEntityAction_)
            renderBlockEntityScreen(world, player, scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
        else
            renderInventory(player, player.gameMode() == GameMode::Creative,
                            scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
    }
    if (player.dead()) renderDeathScreen(player, scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
    else if (paused && !inventoryOpen) renderPauseMenu(scaled.scaledWidth, scaled.scaledHeight, scaled.scaleFactor);
}

void GameHud::endFrame() {
    if (frameHostOpen_) { ImGui::End(); frameHostOpen_ = false; }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
