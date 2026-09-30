#pragma once

#include <string>

namespace display {

// A monitor whose brightness we can control (LG Ultrafine HID, DDC/CI, ...)
class Display {
public:
    virtual ~Display() = default;

    // Human-readable monitor name
    virtual std::wstring name() const = 0;

    // Current brightness (0-100%), cached so it's cheap to call every frame.
    virtual int getBrightness() = 0;

    // Set brightness (0-100%)
    virtual void setBrightness(int percent) = 0;

    virtual bool isConnected() const = 0;

    // Auto-brightness fade state (owned by the auto-brightness logic)
    bool autoTransitioning = false;
};

} // namespace display
