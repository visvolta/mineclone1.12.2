#include "client/FrontEnd.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <stb_image.h>

#include "client/ScaledResolution.hpp"

namespace {

ImTextureID textureId(GLuint value) {
    return static_cast<ImTextureID>(value);
}

ImU32 argb(std::uint32_t value) {
    return IM_COL32((value >> 16U) & 255U, (value >> 8U) & 255U,
                    value & 255U, (value >> 24U) & 255U);
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) return shader;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Title panorama shader failed: " + log);
}

GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) return program;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Title panorama program failed: " + log);
}

constexpr std::array<WorldType, 7> worldTypes = {
    WorldType::Default, WorldType::Flat, WorldType::LargeBiomes, WorldType::Amplified,
    WorldType::Customized, WorldType::DebugAllBlockStates, WorldType::Default11
};

std::string worldTypeUi(WorldType type) {
    switch (type) {
        case WorldType::Default: return "Default";
        case WorldType::Flat: return "Superflat";
        case WorldType::LargeBiomes: return "Large Biomes";
        case WorldType::Amplified: return "AMPLIFIED";
        case WorldType::Customized: return "Customized";
        case WorldType::DebugAllBlockStates: return "Debug Mode";
        case WorldType::Default11: return "Default 1.1";
    }
    return "Default";
}

std::string gameModeUi(GameMode mode) {
    return mode == GameMode::Creative ? "Creative" : "Survival";
}

std::string formatDate(std::int64_t millis) {
    if (millis <= 0) return "Unknown";
    const std::time_t time = static_cast<std::time_t>(millis / 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[64]{};
    std::strftime(buffer, sizeof(buffer), "%m/%d/%y %H:%M", &local);
    return buffer;
}

} // namespace

FrontEnd::FrontEnd(GLFWwindow* window, const std::filesystem::path& assetRoot,
                   std::filesystem::path savesRoot, const WorldConfig& defaults)
    : window_(window), assetRoot_(assetRoot), savesRoot_(std::move(savesRoot)),
      clientDefaults_(defaults) {
    if (window_ == nullptr) throw std::invalid_argument("FrontEnd requires a GLFW window");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true))
        throw std::runtime_error("Could not initialize title-screen ImGui GLFW backend");
    if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
        throw std::runtime_error("Could not initialize title-screen ImGui OpenGL backend");

    widgets_ = loadTexture(assetRoot_ / "assets/minecraft/textures/gui/widgets.png", 256, 256);
    std::vector<unsigned char> asciiPixels;
    ascii_ = loadTexture(assetRoot_ / "assets/minecraft/textures/font/ascii.png", 128, 128, &asciiPixels);
    buildAsciiWidths(asciiPixels);
    title_ = loadTexture(assetRoot_ / "assets/minecraft/textures/gui/title/minecraft.png", 256, 256);
    edition_ = loadTexture(assetRoot_ / "assets/minecraft/textures/gui/title/edition.png", 128, 16);
    optionsBackground_ = loadTexture(assetRoot_ / "assets/minecraft/textures/gui/options_background.png", 16, 16);
    glBindTexture(GL_TEXTURE_2D, optionsBackground_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    worldSelection_ = loadTexture(assetRoot_ / "assets/minecraft/textures/gui/world_selection.png", 256, 256);
    unknownWorldIcon_ = loadTexture(assetRoot_ / "assets/minecraft/textures/misc/unknown_server.png", 128, 128);

    std::ifstream language(assetRoot_ / "assets/minecraft/lang/en_us.lang");
    std::string line;
    while (std::getline(language, line)) {
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos)
            language_.insert_or_assign(line.substr(0, equals), line.substr(equals + 1));
    }
    std::ifstream splashFile(assetRoot_ / "assets/minecraft/texts/splashes.txt");
    while (std::getline(splashFile, line)) if (!line.empty()) splashes_.push_back(line);
    if (!splashes_.empty()) {
        std::mt19937 random(static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        do {
            splash_ = splashes_[random() % splashes_.size()];
        } while (std::hash<std::string>{}(splash_) == 125780783U && splashes_.size() > 1);
    } else {
        splash_ = "Minecraft!";
    }

    worldNameBuffer_.fill('\0');
    constexpr char defaultWorldName[] = "New World";
    std::copy_n(defaultWorldName, sizeof(defaultWorldName) - 1, worldNameBuffer_.begin());
    refreshWorlds();
    initPanorama();
}

FrontEnd::~FrontEnd() {
    destroyPanorama();
    if (unknownWorldIcon_) glDeleteTextures(1, &unknownWorldIcon_);
    if (worldSelection_) glDeleteTextures(1, &worldSelection_);
    if (optionsBackground_) glDeleteTextures(1, &optionsBackground_);
    if (edition_) glDeleteTextures(1, &edition_);
    if (title_) glDeleteTextures(1, &title_);
    if (ascii_) glDeleteTextures(1, &ascii_);
    if (widgets_) glDeleteTextures(1, &widgets_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

GLuint FrontEnd::loadTexture(const std::filesystem::path& path, int expectedWidth,
                             int expectedHeight, std::vector<unsigned char>* rgba) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width != expectedWidth || height != expectedHeight) {
        if (pixels != nullptr) stbi_image_free(pixels);
        throw std::runtime_error("Missing or invalid Minecraft 1.12.2 asset: " + path.string());
    }
    if (rgba != nullptr) rgba->assign(pixels, pixels + static_cast<std::size_t>(width * height * 4));
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    stbi_image_free(pixels);
    return texture;
}

void FrontEnd::buildAsciiWidths(const std::vector<unsigned char>& pixels) {
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
            bool occupied = false;
            for (int y = 0; y < 8; ++y) {
                const std::size_t alpha = static_cast<std::size_t>(((cellY + y) * 128 + cellX + right) * 4 + 3);
                if (alpha < pixels.size() && pixels[alpha] > 16) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) break;
        }
        charWidths_[static_cast<std::size_t>(character)] = std::max(1, right + 2);
    }
}

float FrontEnd::textWidth(std::string_view text) const {
    float width = 0.0F;
    for (unsigned char value : text) width += static_cast<float>(charWidths_[value]);
    return width;
}

void FrontEnd::drawText(float x, float y, std::string_view text,
                        unsigned int color, bool centered) const {
    if (centered) x -= textWidth(text) * 0.5F;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float cursor = x;
    for (unsigned char character : text) {
        const int characterWidth = charWidths_[character];
        if (character != ' ') {
            const int glyphX = character & 15;
            const int glyphY = character >> 4;
            draw->AddImage(textureId(ascii_),
                ImVec2(pixel(cursor), pixel(y)), ImVec2(pixel(cursor + 8.0F), pixel(y + 8.0F)),
                ImVec2(glyphX / 16.0F, glyphY / 16.0F),
                ImVec2((glyphX + 1) / 16.0F, (glyphY + 1) / 16.0F), argb(color));
        }
        cursor += static_cast<float>(characterWidth);
    }
}

void FrontEnd::drawRotatedText(float originX, float originY, std::string_view text,
                               float angleDegrees, float textScale,
                               unsigned int color) const {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float angle = angleDegrees * 0.01745329251994329577F;
    const float cs = std::cos(angle), sn = std::sin(angle);
    const float width = textWidth(text);
    float cursor = -width * 0.5F;
    const auto transform = [&](float x, float y) {
        const float sx = x * textScale;
        const float sy = y * textScale;
        return ImVec2(pixel(originX + sx * cs - sy * sn),
                      pixel(originY + sx * sn + sy * cs));
    };
    const ImU32 tint = argb(color);
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
            const ImVec2 p0 = transform(cursor, -8.0F);
            const ImVec2 p1 = transform(cursor + visible, -8.0F);
            const ImVec2 p2 = transform(cursor + visible, 0.0F);
            const ImVec2 p3 = transform(cursor, 0.0F);
            draw->AddImageQuad(textureId(ascii_), p0, p1, p2, p3,
                               uv0, ImVec2(uv1.x, uv0.y), uv1,
                               ImVec2(uv0.x, uv1.y), tint);
        }
        cursor += static_cast<float>(advance);
    }
}

std::string FrontEnd::tr(std::string_view key, std::string fallback) const {
    const auto found = language_.find(std::string(key));
    return found == language_.end() ? fallback : found->second;
}

void FrontEnd::initPanorama() {
    const char* vertexSource =
        "#version 330 core\n"
        "layout(location=0) in vec2 p;out vec2 uv;"
        "void main(){uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}";
    const char* fragmentSource =
        "#version 330 core\n"
        "in vec2 uv;out vec4 color;uniform samplerCube sky;uniform float yaw;uniform float pitch;"
        "uniform vec2 viewportScale;"
        "vec3 directionFor(vec2 sampleUv){"
        "vec2 q=(sampleUv*2.0-1.0)*viewportScale;"
        "float f=tan(radians(60.0));"
        "vec3 d=normalize(vec3(q.x*f,-q.y*f,1.0));"
        "float cy=cos(yaw),sy=sin(yaw);"
        "d=vec3(cy*d.x+sy*d.z,d.y,-sy*d.x+cy*d.z);"
        "float cp=cos(pitch),sp=sin(pitch);"
        "return vec3(d.x,cp*d.y-sp*d.z,sp*d.y+cp*d.z);}"
        "void main(){vec3 sum=vec3(0.0);float weight=0.0;"
        "for(int i=-3;i<=3;i++){float w=1.0/(1.0+abs(float(i)));"
        "sum+=texture(sky,directionFor(uv+vec2(float(i)/768.0,0))).rgb*w;weight+=w;}"
        "color=vec4(sum/weight,1.0);}";
    panoramaProgram_ = linkProgram(compileShader(GL_VERTEX_SHADER, vertexSource),
                                   compileShader(GL_FRAGMENT_SHADER, fragmentSource));
    const float vertices[] = {-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
    glGenVertexArrays(1, &panoramaVao_);
    glGenBuffers(1, &panoramaVbo_);
    glBindVertexArray(panoramaVao_);
    glBindBuffer(GL_ARRAY_BUFFER, panoramaVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glGenTextures(1, &panoramaCubemap_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, panoramaCubemap_);
    constexpr std::array<GLenum, 6> targets = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Y
    };
    for (int index = 0; index < 6; ++index) {
        const auto path = assetRoot_ / "assets/minecraft/textures/gui/title/background" /
            ("panorama_" + std::to_string(index) + ".png");
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr || width != 256 || height != 256) {
            if (pixels != nullptr) stbi_image_free(pixels);
            throw std::runtime_error("Invalid Minecraft panorama asset: " + path.string());
        }
        for (int y = 0; y < height / 2; ++y) {
            unsigned char* top = pixels + static_cast<std::size_t>(y * width * 4);
            unsigned char* bottom = pixels + static_cast<std::size_t>((height - 1 - y) * width * 4);
            for (int x = 0; x < width * 4; ++x) std::swap(top[x], bottom[x]);
        }
        glTexImage2D(targets[static_cast<std::size_t>(index)], 0, GL_RGBA8,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void FrontEnd::destroyPanorama() {
    if (panoramaCubemap_) glDeleteTextures(1, &panoramaCubemap_);
    if (panoramaVbo_) glDeleteBuffers(1, &panoramaVbo_);
    if (panoramaVao_) glDeleteVertexArrays(1, &panoramaVao_);
    if (panoramaProgram_) glDeleteProgram(panoramaProgram_);
}

void FrontEnd::renderPanorama(float seconds, int width, int height) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(panoramaProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, panoramaCubemap_);
    glUniform1i(glGetUniformLocation(panoramaProgram_, "sky"), 0);
    const float timer = seconds * 20.0F;
    glUniform1f(glGetUniformLocation(panoramaProgram_, "yaw"),
                (-timer * 0.1F) * 0.01745329251994329577F);
    glUniform1f(glGetUniformLocation(panoramaProgram_, "pitch"),
                (20.0F + std::sin(timer / 400.0F) * 25.0F) * 0.01745329251994329577F);
    const float maximum = static_cast<float>(std::max(width, height));
    const float sx = maximum > 0.0F ? static_cast<float>(width) / maximum : 1.0F;
    const float sy = maximum > 0.0F ? static_cast<float>(height) / maximum : 1.0F;
    glUniform2f(glGetUniformLocation(panoramaProgram_, "viewportScale"), sx, sy);
    glBindVertexArray(panoramaVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void FrontEnd::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void FrontEnd::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool FrontEnd::button(int id, float x, float y, float width,
                      std::string_view label, bool enabled) {
    ImGui::SetCursorScreenPos(ImVec2(pixel(x), pixel(y)));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##button", ImVec2(pixel(width), pixel(20.0F)));
    const bool hovered = ImGui::IsItemHovered() && enabled;
    const bool clicked = enabled && ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float v = (enabled ? (hovered ? 86.0F : 66.0F) : 46.0F) / 256.0F;
    const float half = width * 0.5F;
    const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(160, 160, 160, 255);
    draw->AddImage(textureId(widgets_), ImVec2(pixel(x), pixel(y)),
                   ImVec2(pixel(x + half), pixel(y + 20.0F)),
                   ImVec2(0.0F, v), ImVec2(100.0F / 256.0F, v + 20.0F / 256.0F), tint);
    draw->AddImage(textureId(widgets_), ImVec2(pixel(x + half), pixel(y)),
                   ImVec2(pixel(x + width), pixel(y + 20.0F)),
                   ImVec2((200.0F - half) / 256.0F, v),
                   ImVec2(200.0F / 256.0F, v + 20.0F / 256.0F), tint);
    drawText(x + width * 0.5F, y + 6.0F, label,
             enabled ? (hovered ? 0xFFFFFFA0U : 0xFFFFFFFFU) : 0xFFA0A0A0U, true);
    return clicked;
}

bool FrontEnd::textField(int id, float x, float y, float width, char* buffer, std::size_t capacity) {
    ImGui::SetCursorScreenPos(ImVec2(pixel(x), pixel(y)));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##textfield", ImVec2(pixel(width), pixel(20.0F)));
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    if (clicked) activeTextField_ = id;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !clicked && activeTextField_ == id && !ImGui::IsAnyItemHovered())
        activeTextField_ = -1;

    const bool active = activeTextField_ == id;
    if (active) {
        std::size_t length = std::strlen(buffer);
        ImGuiIO& io = ImGui::GetIO();
        for (ImWchar character : io.InputQueueCharacters) {
            if (character >= 32 && character < 127 && length + 1 < capacity) {
                buffer[length++] = static_cast<char>(character);
                buffer[length] = '\0';
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && length > 0) buffer[--length] = '\0';
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) buffer[0] = '\0';
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(pixel(x), pixel(y)), ImVec2(pixel(x + width), pixel(y + 20.0F)),
                        IM_COL32(0, 0, 0, 255));
    draw->AddRect(ImVec2(pixel(x), pixel(y)), ImVec2(pixel(x + width), pixel(y + 20.0F)),
                  active ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255), pixel(1.0F));
    std::string visible(buffer);
    while (textWidth(visible) > width - 8.0F && !visible.empty()) visible.erase(visible.begin());
    drawText(x + 4.0F, y + 6.0F, visible);
    if (active && static_cast<int>(ImGui::GetTime() * 2.0) % 2 == 0)
        draw->AddRectFilled(ImVec2(pixel(x + 4.0F + textWidth(visible)), pixel(y + 5.0F)),
                            ImVec2(pixel(x + 5.0F + textWidth(visible)), pixel(y + 14.0F)),
                            IM_COL32_WHITE);
    return active;
}

void FrontEnd::drawTiledBackground(int width, int height) const {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    constexpr int tile = 32;
    for (int y = 0; y < height; y += tile) {
        for (int x = 0; x < width; x += tile) {
            const int right = std::min(width, x + tile);
            const int bottom = std::min(height, y + tile);
            const float u1 = static_cast<float>(right - x) / 32.0F;
            const float v1 = static_cast<float>(bottom - y) / 32.0F;
            draw->AddImage(textureId(optionsBackground_),
                ImVec2(pixel(static_cast<float>(x)), pixel(static_cast<float>(y))),
                ImVec2(pixel(static_cast<float>(right)), pixel(static_cast<float>(bottom))),
                ImVec2(0, 0), ImVec2(u1, v1), IM_COL32(64, 64, 64, 255));
        }
    }
}

void FrontEnd::renderMain(int width, int height) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // GuiMainMenu draws two full-screen gradients over the panorama.
    draw->AddRectFilledMultiColor(ImVec2(0, 0),
        ImVec2(pixel(static_cast<float>(width)), pixel(static_cast<float>(height))),
        IM_COL32(255,255,255,128), IM_COL32(255,255,255,128),
        IM_COL32(255,255,255,0), IM_COL32(255,255,255,0));
    draw->AddRectFilledMultiColor(ImVec2(0, 0),
        ImVec2(pixel(static_cast<float>(width)), pixel(static_cast<float>(height))),
        IM_COL32(0,0,0,0), IM_COL32(0,0,0,0),
        IM_COL32(0,0,0,128), IM_COL32(0,0,0,128));
    const float logoX = width * 0.5F - 137.0F;
    draw->AddImage(textureId(title_), ImVec2(pixel(logoX), pixel(30)), ImVec2(pixel(logoX + 155), pixel(74)),
                   ImVec2(0, 0), ImVec2(155.0F / 256.0F, 44.0F / 256.0F));
    draw->AddImage(textureId(title_), ImVec2(pixel(logoX + 155), pixel(30)), ImVec2(pixel(logoX + 310), pixel(74)),
                   ImVec2(0, 45.0F / 256.0F), ImVec2(155.0F / 256.0F, 89.0F / 256.0F));
    draw->AddImage(textureId(edition_), ImVec2(pixel(logoX + 88), pixel(67)), ImVec2(pixel(logoX + 186), pixel(81)),
                   ImVec2(0, 0), ImVec2(98.0F / 128.0F, 14.0F / 16.0F));
    const float pulse = 1.8F - std::abs(std::sin(std::fmod(menuSeconds_, 1.0F) *
        6.2831853071795864769F) * 0.1F);
    const float splashScale = pulse * 100.0F / (textWidth(splash_) + 32.0F);
    drawRotatedText(width * 0.5F + 90.0F, 70.0F, splash_, -20.0F,
                    splashScale, 0xFFFFFF00U);

    const float y = height / 4.0F + 48.0F;
    if (button(1, width * 0.5F - 100, y, 200, tr("menu.singleplayer", "Singleplayer"))) {
        refreshWorlds();
        screen_ = Screen::SelectWorld;
    }
    button(2, width * 0.5F - 100, y + 24, 200, tr("menu.multiplayer", "Multiplayer"), false);
    button(14, width * 0.5F - 100, y + 48, 200, tr("menu.online", "Minecraft Realms"), false);
    button(0, width * 0.5F - 100, y + 84, 98, tr("menu.options", "Options"), false);
    if (button(4, width * 0.5F + 2, y + 84, 98, tr("menu.quit", "Quit Game"))) quit_ = true;

    drawText(2, static_cast<float>(height - 10), "Minecraft 1.12.2");
    const std::string copyright = "Copyright Mojang AB. Do not distribute!";
    drawText(width - textWidth(copyright) - 2, static_cast<float>(height - 10), copyright);
}

void FrontEnd::refreshWorlds() {
    worlds_ = WorldSave::listWorlds(savesRoot_);
    if (worlds_.empty()) selectedWorld_ = -1;
    else if (selectedWorld_ < 0 || selectedWorld_ >= static_cast<int>(worlds_.size())) selectedWorld_ = 0;
    worldScroll_ = 0;
}

void FrontEnd::renderWorldSelect(int width, int height) {
    drawTiledBackground(width, height);
    drawText(width * 0.5F, 20, tr("selectWorld.title", "Select World"), 0xFFFFFFFFU, true);

    const int top = 32;
    const int bottom = height - 64;
    constexpr int slotHeight = 36;
    const int visibleRows = std::max(1, (bottom - top) / slotHeight);
    if (ImGui::GetIO().MouseWheel != 0.0F) {
        worldScroll_ -= static_cast<int>(ImGui::GetIO().MouseWheel);
        worldScroll_ = std::clamp(worldScroll_, 0,
            std::max(0, static_cast<int>(worlds_.size()) - visibleRows));
    }

    // GuiListWorldSelection expands GuiSlot's normal list width by 50.
    const float listWidth = 270.0F;
    const float listLeft = width * 0.5F - listWidth * 0.5F;
    int y = top;
    for (int row = 0; row < visibleRows; ++row, y += slotHeight) {
        const int index = worldScroll_ + row;
        if (index >= static_cast<int>(worlds_.size())) break;
        const WorldSummary& world = worlds_[static_cast<std::size_t>(index)];
        const bool selected = index == selectedWorld_;
        ImDrawList* draw = ImGui::GetForegroundDrawList();

        ImGui::SetCursorScreenPos(ImVec2(pixel(listLeft), pixel(static_cast<float>(y))));
        ImGui::PushID(index);
        ImGui::InvisibleButton("##world", ImVec2(pixel(listWidth), pixel(32.0F)));
        const bool doubleClick = ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ImGui::IsItemClicked()) selectedWorld_ = index;
        if (doubleClick) result_ = world.folder;
        ImGui::PopID();

        if (selected) {
            draw->AddRectFilled(ImVec2(pixel(listLeft-2), pixel(static_cast<float>(y-2))),
                                ImVec2(pixel(listLeft+listWidth+2), pixel(static_cast<float>(y+34))),
                                IM_COL32(128,128,128,128));
        }

        // Save icons are optional in vanilla. Worlds created by Blockcraft do
        // not yet capture a screenshot, so use Minecraft's exact missing-world
        // icon rather than a generated placeholder.
        draw->AddImage(textureId(unknownWorldIcon_),
            ImVec2(pixel(listLeft), pixel(static_cast<float>(y))),
            ImVec2(pixel(listLeft+32), pixel(static_cast<float>(y+32))),
            ImVec2(0,0), ImVec2(1,1));

        if (selected) {
            draw->AddRectFilled(ImVec2(pixel(listLeft), pixel(static_cast<float>(y))),
                                ImVec2(pixel(listLeft+32), pixel(static_cast<float>(y+32))),
                                IM_COL32(255,255,255,160));
            draw->AddImage(textureId(worldSelection_),
                ImVec2(pixel(listLeft), pixel(static_cast<float>(y))),
                ImVec2(pixel(listLeft+32), pixel(static_cast<float>(y+32))),
                ImVec2(0.0F,0.0F), ImVec2(32.0F/256.0F,32.0F/256.0F));
        }

        const float tx = listLeft + 35.0F;
        drawText(tx, y + 1.0F, world.levelName);
        drawText(tx, y + 12.0F, world.folderName + " (" + formatDate(world.lastPlayed) + ")", 0xFF808080U);
        std::string details = gameModeUi(world.gameMode);
        if (world.requiresConversion) details = "Must be converted!";
        else details += ", Version: 1.12.2";
        drawText(tx, y + 23.0F, details, world.requiresConversion ? 0xFFFF5555U : 0xFF808080U);
    }

    const bool valid = selectedWorld_ >= 0 && selectedWorld_ < static_cast<int>(worlds_.size());
    if (button(10, width * 0.5F - 154, height - 52.0F, 150,
               tr("selectWorld.select", "Play Selected World"), valid) && valid)
        result_ = worlds_[static_cast<std::size_t>(selectedWorld_)].folder;
    if (button(11, width * 0.5F + 4, height - 52.0F, 150,
               tr("selectWorld.create", "Create New World"))) {
        activeTextField_ = -1;
        moreWorldOptions_ = false;
        screen_ = Screen::CreateWorld;
    }
    button(12, width * 0.5F - 154, height - 28.0F, 72,
           tr("selectWorld.edit", "Edit"), false);
    if (button(13, width * 0.5F - 76, height - 28.0F, 72,
               tr("selectWorld.delete", "Delete"), valid) && valid) {
        pendingDelete_ = selectedWorld_;
        screen_ = Screen::DeleteConfirm;
    }
    button(14, width * 0.5F + 4, height - 28.0F, 72,
           tr("selectWorld.recreate", "Re-Create"), false);
    if (button(15, width * 0.5F + 82, height - 28.0F, 72,
               tr("gui.cancel", "Cancel")))
        screen_ = Screen::Main;
}

void FrontEnd::renderCreateWorld(int width, int height) {
    drawTiledBackground(width, height);
    drawText(width * 0.5F, 20, tr("selectWorld.create", "Create New World"), 0xFFFFFFFFU, true);

    if (!moreWorldOptions_) {
        const float left = width * 0.5F - 100.0F;
        drawText(left, 47, tr("selectWorld.enterName", "World Name"));
        textField(100, left, 60, 200, worldNameBuffer_.data(), worldNameBuffer_.size());
        const std::string rawName = worldNameBuffer_.data();
        drawText(left, 84, "Will be saved in: " + (rawName.empty() ? std::string("World") : rawName),
                 0xFFAAAAAAU);

        if (button(20, width * 0.5F - 75, 115, 150,
                   tr("selectWorld.gameMode", "Game Mode") + ": " + gameModeUi(create_.gameMode))) {
            create_.gameMode = create_.gameMode == GameMode::Survival ? GameMode::Creative : GameMode::Survival;
        }
        if (create_.gameMode == GameMode::Survival) {
            drawText(width * 0.5F, 137, tr("selectWorld.gameMode.survival.line1",
                     "Search for resources, crafting, gain levels,"), 0xFFA0A0A0U, true);
            drawText(width * 0.5F, 149, tr("selectWorld.gameMode.survival.line2",
                     "health and hunger"), 0xFFA0A0A0U, true);
        } else {
            drawText(width * 0.5F, 137, tr("selectWorld.gameMode.creative.line1",
                     "Unlimited resources, free flying and"), 0xFFA0A0A0U, true);
            drawText(width * 0.5F, 149, tr("selectWorld.gameMode.creative.line2",
                     "destroy blocks instantly"), 0xFFA0A0A0U, true);
        }
        if (button(21, width * 0.5F - 75, 187, 150,
                   tr("selectWorld.moreWorldOptions", "More World Options..."))) {
            moreWorldOptions_ = true;
            activeTextField_ = -1;
        }
    } else {
        const float left = width * 0.5F - 100.0F;
        drawText(left, 47, tr("selectWorld.enterSeed", "Seed for the World Generator"));
        textField(101, left, 60, 200, seedBuffer_.data(), seedBuffer_.size());
        drawText(left, 84, tr("selectWorld.seedInfo", "Leave blank for a random seed"), 0xFFA0A0A0U);

        if (button(22, width * 0.5F - 155, 100, 150,
                   tr("selectWorld.mapFeatures", "Generate Structures") + std::string(" ") +
                   (create_.generateStructures ? tr("options.on", "ON") : tr("options.off", "OFF"))))
            create_.generateStructures = !create_.generateStructures;

        if (button(23, width * 0.5F + 5, 100, 150,
                   tr("selectWorld.mapType", "World Type") + std::string(" ") + worldTypeUi(create_.worldType))) {
            auto iterator = std::find(worldTypes.begin(), worldTypes.end(), create_.worldType);
            std::size_t index = iterator == worldTypes.end() ? 0 :
                static_cast<std::size_t>(iterator - worldTypes.begin());
            do {
                index = (index + 1) % worldTypes.size();
                create_.worldType = worldTypes[index];
            } while (create_.worldType == WorldType::DebugAllBlockStates && !ImGui::GetIO().KeyShift);
        }

        button(24, width * 0.5F - 155, 151, 150,
               tr("selectWorld.allowCommands", "Allow Cheats") + std::string(" ") +
               (create_.gameMode == GameMode::Creative ? tr("options.on", "ON") : tr("options.off", "OFF")),
               false);
        button(25, width * 0.5F + 5, 151, 150,
               tr("selectWorld.bonusItems", "Bonus Chest") + std::string(" ") + tr("options.off", "OFF"),
               false);

        if (button(26, width * 0.5F - 75, 187, 150, tr("gui.done", "Done"))) {
            moreWorldOptions_ = false;
            activeTextField_ = -1;
        }
    }

    if (button(27, width * 0.5F - 155, height - 28.0F, 150,
               tr("selectWorld.create", "Create New World"))) {
        create_.levelName = worldNameBuffer_.data();
        create_.seedText = seedBuffer_.data();
        create_.generatorOptions = create_.worldType == WorldType::Flat
            ? "3;minecraft:bedrock,2*minecraft:dirt,minecraft:grass;1;village" : "{}";
        result_ = WorldSave::createWorld(savesRoot_, create_, clientDefaults_);
    }
    if (button(28, width * 0.5F + 5, height - 28.0F, 150, tr("gui.cancel", "Cancel"))) {
        activeTextField_ = -1;
        moreWorldOptions_ = false;
        screen_ = Screen::SelectWorld;
    }
}

void FrontEnd::renderDeleteConfirm(int width, int height) {
    drawTiledBackground(width, height);
    if (pendingDelete_ < 0 || pendingDelete_ >= static_cast<int>(worlds_.size())) {
        screen_ = Screen::SelectWorld;
        return;
    }
    const WorldSummary& world = worlds_[static_cast<std::size_t>(pendingDelete_)];
    drawText(width * 0.5F, height * 0.5F - 40,
             tr("selectWorld.deleteQuestion", "Are you sure you want to delete this world?"),
             0xFFFFFFFFU, true);
    drawText(width * 0.5F, height * 0.5F - 24,
             "'" + world.levelName + "' will be lost forever! (A long time!)", 0xFFAAAAAAU, true);
    if (button(30, width * 0.5F - 102, height * 0.5F + 8, 100,
               tr("selectWorld.deleteButton", "Delete"))) {
        WorldSave::deleteWorld(world.folder);
        pendingDelete_ = -1;
        refreshWorlds();
        screen_ = Screen::SelectWorld;
    }
    if (button(31, width * 0.5F + 2, height * 0.5F + 8, 100, tr("gui.cancel", "Cancel"))) {
        pendingDelete_ = -1;
        screen_ = Screen::SelectWorld;
    }
}

std::optional<std::filesystem::path> FrontEnd::run() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    const auto start = std::chrono::steady_clock::now();
    while (!quit_ && !result_ && glfwWindowShouldClose(window_) == GLFW_FALSE) {
        glfwPollEvents();
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        const ScaledResolution resolution = ScaledResolution::fromDisplay(
            framebufferWidth, framebufferHeight, clientDefaults_.guiScale, false);
        uiScale_ = static_cast<float>(resolution.scaleFactor);
        const int width = resolution.scaledWidth;
        const int height = resolution.scaledHeight;
        glViewport(0, 0, framebufferWidth, framebufferHeight);

        const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        menuSeconds_ = seconds;
        if (screen_ == Screen::Main) renderPanorama(seconds, framebufferWidth, framebufferHeight);
        else {
            glDisable(GL_DEPTH_TEST);
            glClearColor(0.2F, 0.2F, 0.2F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
        }

        beginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0F);
        ImGui::Begin("##blockcraft-front-end", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        switch (screen_) {
            case Screen::Main: renderMain(width, height); break;
            case Screen::SelectWorld: renderWorldSelect(width, height); break;
            case Screen::CreateWorld: renderCreateWorld(width, height); break;
            case Screen::DeleteConfirm: renderDeleteConfirm(width, height); break;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && activeTextField_ < 0) {
            if (screen_ == Screen::Main) quit_ = true;
            else if (screen_ == Screen::CreateWorld || screen_ == Screen::DeleteConfirm) screen_ = Screen::SelectWorld;
            else screen_ = Screen::Main;
        }
        ImGui::End();
        endFrame();
        glfwSwapBuffers(window_);
    }
    return result_;
}


void FrontEnd::showLoading(std::string_view title, std::string_view message, int progress) {
    if (window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE) return;
    glfwPollEvents();
    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
    const ScaledResolution resolution = ScaledResolution::fromDisplay(
        framebufferWidth, framebufferHeight, clientDefaults_.guiScale, false);
    uiScale_ = static_cast<float>(resolution.scaleFactor);
    const int width = resolution.scaledWidth;
    const int height = resolution.scaledHeight;
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.25F, 0.25F, 0.25F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    beginFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0F);
    ImGui::Begin("##blockcraft-loading", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    drawTiledBackground(width, height);
    drawText(width * 0.5F, height * 0.5F - 20.0F, title, 0xFFFFFFFFU, true);
    drawText(width * 0.5F, height * 0.5F + 4.0F, message, 0xFFFFFFFFU, true);
    if (progress >= 0) {
        const int clamped = std::clamp(progress, 0, 100);
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const float x = width * 0.5F - 50.0F;
        const float y = height * 0.5F + 20.0F;
        draw->AddRectFilled(ImVec2(pixel(x), pixel(y)), ImVec2(pixel(x + 100), pixel(y + 2)),
                            IM_COL32(128, 128, 128, 255));
        draw->AddRectFilled(ImVec2(pixel(x), pixel(y)), ImVec2(pixel(x + clamped), pixel(y + 2)),
                            IM_COL32(128, 255, 128, 255));
    }
    ImGui::End();
    endFrame();
    glfwSwapBuffers(window_);
    glEnable(GL_DEPTH_TEST);
}
