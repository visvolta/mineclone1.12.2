#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

#include "save/WorldSave.hpp"

struct GLFWwindow;

class FrontEnd {
public:
    FrontEnd(GLFWwindow* window, const std::filesystem::path& assetRoot,
             std::filesystem::path savesRoot, const WorldConfig& clientDefaults);
    ~FrontEnd();
    FrontEnd(const FrontEnd&) = delete;
    FrontEnd& operator=(const FrontEnd&) = delete;

    [[nodiscard]] std::optional<std::filesystem::path> run();

private:
    enum class Screen { Main, SelectWorld, CreateWorld, DeleteConfirm };

    void beginFrame();
    void endFrame();
    void renderPanorama(float seconds, int width, int height);
    void renderMain(int width, int height);
    void renderWorldSelect(int width, int height);
    void renderCreateWorld(int width, int height);
    void renderDeleteConfirm(int width, int height);
    void refreshWorlds();

    bool button(int id, float x, float y, float width,
                              std::string_view label, bool enabled = true);
    bool textField(int id, float x, float y, float width,
                                 char* buffer, std::size_t capacity);
    void drawText(float x, float y, std::string_view text,
                  unsigned int argb = 0xFFFFFFFFU, bool centered = false) const;
    [[nodiscard]] float textWidth(std::string_view text) const;
    void drawTiledBackground(int width, int height) const;
    [[nodiscard]] std::string tr(std::string_view key, std::string fallback) const;
    [[nodiscard]] GLuint loadTexture(const std::filesystem::path& path, int expectedWidth,
                                     int expectedHeight, std::vector<unsigned char>* rgba = nullptr);
    void buildAsciiWidths(const std::vector<unsigned char>& rgba);
    void initPanorama();
    void destroyPanorama();
    [[nodiscard]] float pixel(float logical) const { return logical * uiScale_; }

    GLFWwindow* window_ = nullptr;
    std::filesystem::path assetRoot_;
    std::filesystem::path savesRoot_;
    WorldConfig clientDefaults_;
    Screen screen_ = Screen::Main;
    std::vector<WorldSummary> worlds_;
    int selectedWorld_ = -1;
    int pendingDelete_ = -1;
    int worldScroll_ = 0;
    int activeTextField_ = -1;
    std::optional<std::filesystem::path> result_;
    bool quit_ = false;

    float uiScale_ = 1.0F;
    CreateWorldRequest create_{};
    std::array<char, 65> worldNameBuffer_{};
    std::array<char, 129> seedBuffer_{};

    GLuint widgets_ = 0;
    GLuint ascii_ = 0;
    GLuint title_ = 0;
    GLuint edition_ = 0;
    GLuint optionsBackground_ = 0;
    GLuint worldSelection_ = 0;
    std::array<int, 256> charWidths_{};
    std::unordered_map<std::string, std::string> language_;
    std::vector<std::string> splashes_;
    std::string splash_;

    GLuint panoramaProgram_ = 0;
    GLuint panoramaVao_ = 0;
    GLuint panoramaVbo_ = 0;
    GLuint panoramaCubemap_ = 0;
};
