#include "auto_brightness.h"
#include "display.h"

#include <algorithm>
#include <cstdlib>

namespace auto_brightness {

int luxToPercent(float lux) {
    // Auto-brightness algorithm: map lux to brightness percentage
    // Based on typical indoor/outdoor lighting levels:
    // - 0-50 lux: very dark (10-20% brightness)
    // - 50-200 lux: dim indoor (20-40% brightness)
    // - 200-500 lux: normal indoor (40-70% brightness)
    // - 500-1000 lux: bright indoor (70-90% brightness)
    // - 1000+ lux: very bright/outdoor (90-100% brightness)

    int target = 10;  // Default minimum

    if (lux < 50.0f) {
        // Very dark: 10-20%
        target = 10 + static_cast<int>(lux * 0.2f);
    } else if (lux < 200.0f) {
        // Dim indoor: 20-40%
        target = 20 + static_cast<int>((lux - 50.0f) * 0.133f);
    } else if (lux < 500.0f) {
        // Normal indoor: 40-70%
        target = 40 + static_cast<int>((lux - 200.0f) * 0.1f);
    } else if (lux < 1000.0f) {
        // Bright indoor: 70-90%
        target = 70 + static_cast<int>((lux - 500.0f) * 0.04f);
    } else {
        // Very bright/outdoor: 90-100%
        float excess = std::min(lux - 1000.0f, 1000.0f);
        target = 90 + static_cast<int>(excess * 0.01f);
    }

    return std::clamp(target, 10, 100);
}

void update(display::Display& d, float lux) {
    fadeToward(d, luxToPercent(lux), 5);
}

void fadeToward(display::Display& d, int target, int deadband) {
    int current = d.getBrightness();
    int diff = target - current;

    if (!d.autoTransitioning && std::abs(diff) > deadband) {
        d.autoTransitioning = true;
    }
    if (!d.autoTransitioning) {
        return;
    }
    if (diff == 0) {
        d.autoTransitioning = false;
        return;
    }

    // Move a quarter of the remaining distance per update (at least 1%)
    int step = diff / 4;
    if (step == 0) {
        step = (diff > 0) ? 1 : -1;
    }
    d.setBrightness(current + step);
}

} // namespace auto_brightness
