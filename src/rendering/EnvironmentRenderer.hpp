#pragma once

#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Environment;
struct EnvironmentFrame;
class World;

class EnvironmentRenderer {
public:
    explicit EnvironmentRenderer(const Environment& environment);
    ~EnvironmentRenderer();
    EnvironmentRenderer(const EnvironmentRenderer&) = delete;
    EnvironmentRenderer& operator=(const EnvironmentRenderer&) = delete;

    void renderSky(const EnvironmentFrame& frame, const glm::vec3& cameraPosition,
                   const glm::mat4& view, const glm::mat4& projection);
    void renderClouds(const EnvironmentFrame& frame, const glm::vec3& cameraPosition,
                      const glm::mat4& view, const glm::mat4& projection);
    void renderWeather(const EnvironmentFrame& frame, const World& world,
                       const glm::vec3& cameraPosition, const glm::mat4& view,
                       const glm::mat4& projection);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
