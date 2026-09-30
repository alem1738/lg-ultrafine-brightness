#include "brightness.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <hidapi.h>
#include <iostream>
#include <iomanip>

#undef min
#undef max

namespace brightness {

// Step tables from reference implementation
const std::vector<uint16_t> SMALL_STEPS = {
    0x0190, 0x01af, 0x01d2, 0x01f7,
    0x021f, 0x024a, 0x0279, 0x02ac,
    0x02e2, 0x031d, 0x035c, 0x03a1,
    0x03eb, 0x043b, 0x0491, 0x04ee,
    0x0553, 0x05c0, 0x0635, 0x06b3,
    0x073c, 0x07d0, 0x086f, 0x091b,
    0x09d5, 0x0a9d, 0x0b76, 0x0c60,
    0x0d5c, 0x0e6c, 0x0f93, 0x10d0,
    0x1227, 0x1399, 0x1529, 0x16d9,
    0x18aa, 0x1aa2, 0x1cc1, 0x1f0b,
    0x2184, 0x2430, 0x2712, 0x2a2e,
    0x2d8b, 0x312b, 0x3516, 0x3951,
    0x3de2, 0x42cf, 0x4822, 0x4de1,
    0x5415, 0x5ac8, 0x6203, 0x69d2,
    0x7240, 0x7b5a, 0x852d, 0x8fc9,
    0x9b3d, 0xa79b, 0xb4f5, 0xc35f,
    0xd2f0,
};

const std::vector<uint16_t> BIG_STEPS = {
    0x0190, 0x021f, 0x02e2, 0x03eb,
    0x0553, 0x073c, 0x09d5, 0x0d5c,
    0x1227, 0x18aa, 0x2184, 0x2d8b,
    0x3de2, 0x5415, 0x7240, 0x9b3d,
    0xd2f0,
};

BrightnessController::BrightnessController() = default;

BrightnessController::~BrightnessController() {
    shutdown();
}

bool BrightnessController::initialize() {
    if (m_connected) {
        return true;
    }

    if (hid_init() != 0) {
        return false;
    }

    // Enumerate HID devices to find LG Ultrafine monitor
    hid_device_info* devs = hid_enumerate(0x0, 0x0);
    hid_device_info* cur_dev = devs;
    char* monitor_path = nullptr;

    while (cur_dev) {
        if (cur_dev->vendor_id == LG_VENDOR_ID) {
            if (cur_dev->product_string &&
                wcsstr(cur_dev->product_string, L"BRIGHTNESS")) {
                monitor_path = cur_dev->path;
                m_monitorName = cur_dev->product_string;
                break;
            }
        }
        cur_dev = cur_dev->next;
    }

    if (!monitor_path) {
        hid_free_enumeration(devs);
        return false;
    }

    // Open the device
    m_handle = hid_open_path(monitor_path);
    hid_free_enumeration(devs);

    if (!m_handle) {
        return false;
    }

    m_connected = true;
    return true;
}

void BrightnessController::shutdown() {
    disconnect();
    hid_exit();
}

void BrightnessController::disconnect() {
    if (m_handle) {
        hid_close(static_cast<hid_device*>(m_handle));
        m_handle = nullptr;
    }
    m_connected = false;
    m_hasCachedValue = false;
    autoTransitioning = false;
}

bool BrightnessController::probe() {
    if (!m_handle) return false;

    uint8_t data[7] = { 0 };
    int res = hid_get_feature_report(static_cast<hid_device*>(m_handle), data, sizeof(data));
    if (res < 0) {
        disconnect();
        return false;
    }

    m_cachedBrightness = data[1] + (data[2] << 8);
    m_hasCachedValue = true;
    return true;
}

uint16_t BrightnessController::getRawBrightness() {
    if (!m_handle) return MIN_BRIGHTNESS;

    // Return cached value if available to avoid slow HID read
    if (m_hasCachedValue) {
        return m_cachedBrightness;
    }

    uint8_t data[7] = { 0 };
    int res = hid_get_feature_report(static_cast<hid_device*>(m_handle), data, sizeof(data));
    if (res < 0) {
        return MIN_BRIGHTNESS;
    }

    m_cachedBrightness = data[1] + (data[2] << 8);
    m_hasCachedValue = true;
    return m_cachedBrightness;
}

void BrightnessController::setRawBrightness(uint16_t val) {
    if (!m_handle) return;

    // Clamp value
    val = std::clamp(val, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

    uint8_t data[7] = {
        0x00,
        static_cast<uint8_t>(val & 0x00ff),
        static_cast<uint8_t>((val >> 8) & 0x00ff),
        0x00, 0x00, 0x00, 0x00
    };

    if (hid_send_feature_report(static_cast<hid_device*>(m_handle), data, sizeof(data)) < 0) {
        // Monitor was unplugged or the handle went stale
        disconnect();
        return;
    }

    // Update cache immediately after setting
    m_cachedBrightness = val;
    m_hasCachedValue = true;

    if (m_callback) {
        m_callback(rawToPercent(val));
    }
}

int BrightnessController::getBrightness() {
    return rawToPercent(getRawBrightness());
}

void BrightnessController::setBrightness(int percent) {
    setRawBrightness(percentToRaw(percent));
}

void BrightnessController::stepUp(bool bigStep) {
    uint16_t current = getRawBrightness();
    const auto& steps = bigStep ? BIG_STEPS : SMALL_STEPS;
    uint16_t next = findNextStep(current, steps);
    setRawBrightness(next);
}

void BrightnessController::stepDown(bool bigStep) {
    uint16_t current = getRawBrightness();
    const auto& steps = bigStep ? BIG_STEPS : SMALL_STEPS;
    uint16_t prev = findPrevStep(current, steps);
    setRawBrightness(prev);
}

uint16_t BrightnessController::findNextStep(uint16_t val, const std::vector<uint16_t>& steps) {
    auto it = std::upper_bound(steps.begin(), steps.end(), val);
    if (it != steps.end()) {
        return *it;
    }
    return steps.back();
}

uint16_t BrightnessController::findPrevStep(uint16_t val, const std::vector<uint16_t>& steps) {
    auto it = std::lower_bound(steps.begin(), steps.end(), val);
    if (it != steps.begin()) {
        --it;
        return *it;
    }
    return steps.front();
}

int BrightnessController::rawToPercent(uint16_t raw) {
    float range = MAX_BRIGHTNESS - MIN_BRIGHTNESS;
    float normalized = static_cast<float>(raw - MIN_BRIGHTNESS) / range;
    // Round (not truncate) so percentToRaw -> rawToPercent round-trips exactly
    return std::clamp(static_cast<int>(std::lround(normalized * 100.0f)), 0, 100);
}

uint16_t BrightnessController::percentToRaw(int percent) {
    percent = std::clamp(percent, 0, 100);
    float range = MAX_BRIGHTNESS - MIN_BRIGHTNESS;
    return static_cast<uint16_t>(std::lround(MIN_BRIGHTNESS + (range * percent / 100.0f)));
}

} // namespace brightness
