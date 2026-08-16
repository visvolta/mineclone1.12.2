#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct EnvironmentVertex {
    glm::vec3 position{};
    glm::vec2 uv{};
    glm::vec4 color{1.0F};
};

// Ports RenderGlobal#renderCloudsFancy's 8x8 cells, including the one-pixel
// side strips that give Fancy clouds their four-block thickness.
[[nodiscard]] std::vector<EnvironmentVertex> buildFancyCloudGeometry();
