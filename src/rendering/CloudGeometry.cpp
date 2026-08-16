#include "rendering/CloudGeometry.hpp"

namespace {

void triangle(std::vector<EnvironmentVertex>& output, const EnvironmentVertex& a,
              const EnvironmentVertex& b, const EnvironmentVertex& c) {
    output.push_back(a);
    output.push_back(b);
    output.push_back(c);
}

void quad(std::vector<EnvironmentVertex>& output, const EnvironmentVertex& a,
          const EnvironmentVertex& b, const EnvironmentVertex& c,
          const EnvironmentVertex& d) {
    triangle(output, a, b, c);
    triangle(output, a, c, d);
}

EnvironmentVertex vertex(float x, float y, float z, float u, float v, float shade) {
    return {{x, y, z}, {u, v}, {shade, shade, shade, 0.8F}};
}

} // namespace

std::vector<EnvironmentVertex> buildFancyCloudGeometry() {
    constexpr float cellSize = 8.0F;
    constexpr float cloudThickness = 4.0F;
    constexpr float textureScale = 1.0F / 256.0F;
    constexpr float faceInset = 1.0F / 1024.0F;

    std::vector<EnvironmentVertex> output;
    output.reserve(8448);
    for (int cellX = -3; cellX <= 4; ++cellX) {
        for (int cellZ = -3; cellZ <= 4; ++cellZ) {
            const float textureX = cellX * cellSize;
            const float textureZ = cellZ * cellSize;
            const float x0 = textureX;
            const float x1 = textureX + cellSize;
            const float z0 = textureZ;
            const float z1 = textureZ + cellSize;
            const float u0 = textureX * textureScale;
            const float u1 = (textureX + cellSize) * textureScale;
            const float v0 = textureZ * textureScale;
            const float v1 = (textureZ + cellSize) * textureScale;

            // Bottom and top faces use the same 0.7/1.0 multipliers as vanilla.
            quad(output, vertex(x0, 0.0F, z1, u0, v1, 0.7F),
                 vertex(x1, 0.0F, z1, u1, v1, 0.7F),
                 vertex(x1, 0.0F, z0, u1, v0, 0.7F),
                 vertex(x0, 0.0F, z0, u0, v0, 0.7F));
            quad(output, vertex(x0, cloudThickness - faceInset, z0, u0, v0, 1.0F),
                 vertex(x1, cloudThickness - faceInset, z0, u1, v0, 1.0F),
                 vertex(x1, cloudThickness - faceInset, z1, u1, v1, 1.0F),
                 vertex(x0, cloudThickness - faceInset, z1, u0, v1, 1.0F));

            // Vanilla emits each side as eight one-pixel strips and samples at
            // the pixel centre. A single 8-pixel wall samples transparent texels
            // at its edge and is why the previous implementation lost the sides.
            if (cellX > -1) {
                for (int strip = 0; strip < 8; ++strip) {
                    const float x = x0 + strip;
                    const float u = (textureX + strip + 0.5F) * textureScale;
                    quad(output, vertex(x, 0.0F, z1, u, v1, 0.9F),
                         vertex(x, cloudThickness, z1, u, v1, 0.9F),
                         vertex(x, cloudThickness, z0, u, v0, 0.9F),
                         vertex(x, 0.0F, z0, u, v0, 0.9F));
                }
            }
            if (cellX <= 1) {
                for (int strip = 0; strip < 8; ++strip) {
                    const float x = x0 + strip + 1.0F - faceInset;
                    const float u = (textureX + strip + 0.5F) * textureScale;
                    quad(output, vertex(x, 0.0F, z0, u, v0, 0.9F),
                         vertex(x, cloudThickness, z0, u, v0, 0.9F),
                         vertex(x, cloudThickness, z1, u, v1, 0.9F),
                         vertex(x, 0.0F, z1, u, v1, 0.9F));
                }
            }
            if (cellZ > -1) {
                for (int strip = 0; strip < 8; ++strip) {
                    const float z = z0 + strip;
                    const float v = (textureZ + strip + 0.5F) * textureScale;
                    quad(output, vertex(x0, cloudThickness, z, u0, v, 0.8F),
                         vertex(x1, cloudThickness, z, u1, v, 0.8F),
                         vertex(x1, 0.0F, z, u1, v, 0.8F),
                         vertex(x0, 0.0F, z, u0, v, 0.8F));
                }
            }
            if (cellZ <= 1) {
                for (int strip = 0; strip < 8; ++strip) {
                    const float z = z0 + strip + 1.0F - faceInset;
                    const float v = (textureZ + strip + 0.5F) * textureScale;
                    quad(output, vertex(x0, 0.0F, z, u0, v, 0.8F),
                         vertex(x1, 0.0F, z, u1, v, 0.8F),
                         vertex(x1, cloudThickness, z, u1, v, 0.8F),
                         vertex(x0, cloudThickness, z, u0, v, 0.8F));
                }
            }
        }
    }
    return output;
}
