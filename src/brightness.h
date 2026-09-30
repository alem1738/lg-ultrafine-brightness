#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "display.h"

namespace brightness {

// LG Ultrafine brightness constants
constexpr uint16_t LG_VENDOR_ID = 0x043e;
constexpr uint16_t MIN_BRIGHTNESS = 0x0190;
constexpr uint16_t MAX_BRIGHTNESS = 0xd2f0;

// Brightness step tables for smooth adjustment
extern const std::vector<uint16_t> SMALL_STEPS;
extern const std::vector<uint16_t> BIG_STEPS;

// LG Ultrafine 4K/5K brightness over USB HID
class BrightnessController : public display::Display {
public:
    BrightnessController();
    ~BrightnessController() override;

    // Prevent copying
    BrightnessController(const BrightnessController&) = delete;
    BrightnessController& operator=(const BrightnessController&) = delete;

    // Initialize and find the monitor
    bool initialize();
    void shutdown();

    // Close the HID handle (e.g. monitor unplugged) without tearing down hidapi
    void disconnect();

    // Do a real HID read to verify the monitor is still there, disconnecting if it's gone.
    bool probe();

    // display::Display
    std::wstring name() const override { return m_monitorName; }
    bool isConnected() const override { return m_connected; }
    int getBrightness() override;
    void setBrightness(int percent) override;

    // Get raw brightness value
    uint16_t getRawBrightness();

    // Set raw brightness value
    void setRawBrightness(uint16_t value);

    // Step brightness up/down
    void stepUp(bool bigStep = false);
    void stepDown(bool bigStep = false);

    // Callback for brightness changes
    using BrightnessCallback = std::function<void(int)>;
    void setCallback(BrightnessCallback callback) { m_callback = callback; }

private:
    void* m_handle = nullptr;
    bool m_connected = false;
    std::wstring m_monitorName;
    BrightnessCallback m_callback;

    // Cached brightness value to avoid redundant HID reads
    uint16_t m_cachedBrightness = MIN_BRIGHTNESS;
    bool m_hasCachedValue = false;

    uint16_t findNextStep(uint16_t val, const std::vector<uint16_t>& steps);
    uint16_t findPrevStep(uint16_t val, const std::vector<uint16_t>& steps);
    int rawToPercent(uint16_t raw);
    uint16_t percentToRaw(int percent);
};

} // namespace brightness
