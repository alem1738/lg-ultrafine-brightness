#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "hotkey.h"
#include "profiles.h"
#include "schedule.h"

namespace settings {

// Persistent user settings, stored under HKCU\Software\LGUltrafineBrightness
struct Settings {
    bool autoBrightness = false;
    schedule::Schedule schedule;

    std::vector<profiles::Profile> profiles;

    // Global hotkeys (profile hotkeys live in each Profile)
    hotkey::Binding brightnessUp = { MOD_CONTROL | MOD_ALT, VK_UP };
    hotkey::Binding brightnessDown = { MOD_CONTROL | MOD_ALT, VK_DOWN };
    hotkey::Binding toggleSchedule;
    hotkey::Binding showWindow;

    // Slider order on the Brightness tab, by monitor name; unlisted monitors go last
    std::vector<std::wstring> displayOrder;

    int widgetHeight = 0;  // User-chosen widget height at 96 DPI, 0 = fit to content
};

Settings load();
void save(const Settings& s);

// Start with Windows (HKCU\...\Run entry, launches minimized to tray)
bool isStartWithWindows();
void setStartWithWindows(bool enabled);

// Rewrite the Run entry with the current exe path (in case the exe was moved)
void refreshStartWithWindows();

} // namespace settings
