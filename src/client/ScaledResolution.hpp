#pragma once

#include <cmath>

struct ScaledResolution {
    double scaledWidthD = 0.0;
    double scaledHeightD = 0.0;
    int scaledWidth = 0;
    int scaledHeight = 0;
    int scaleFactor = 1;

    [[nodiscard]] static ScaledResolution fromDisplay(int displayWidth, int displayHeight,
                                                      int guiScale, bool unicode = false) {
        ScaledResolution result;
        result.scaledWidth = displayWidth;
        result.scaledHeight = displayHeight;
        result.scaleFactor = 1;

        int requested = guiScale;
        if (requested == 0) requested = 1000;
        while (result.scaleFactor < requested &&
               result.scaledWidth / (result.scaleFactor + 1) >= 320 &&
               result.scaledHeight / (result.scaleFactor + 1) >= 240) {
            ++result.scaleFactor;
        }

        if (unicode && (result.scaleFactor & 1) != 0 && result.scaleFactor != 1)
            --result.scaleFactor;

        result.scaledWidthD = static_cast<double>(result.scaledWidth) /
                              static_cast<double>(result.scaleFactor);
        result.scaledHeightD = static_cast<double>(result.scaledHeight) /
                               static_cast<double>(result.scaleFactor);
        result.scaledWidth = static_cast<int>(std::ceil(result.scaledWidthD));
        result.scaledHeight = static_cast<int>(std::ceil(result.scaledHeightD));
        return result;
    }
};
