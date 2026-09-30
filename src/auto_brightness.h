#pragma once

namespace display {
class Display;
}

namespace auto_brightness {

// Map ambient light (lux) to a target brightness percentage
int luxToPercent(float lux);

// Move a display one fade step toward a target, starting only once the difference exceeds the deadband.
void fadeToward(display::Display& d, int target, int deadband);

// Move a display one fade step toward the brightness for the given lux, with a 5% deadband against sensor noise.
void update(display::Display& d, float lux);

} // namespace auto_brightness
