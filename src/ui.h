#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <functional>
#include <string>
#include <vector>

#include "schedule.h"

namespace ui {

class UIRenderer {
public:
    UIRenderer();
    ~UIRenderer();

    // Initialize DirectX and ImGui
    bool initialize(HWND hwnd);
    void shutdown();

    // Render frame
    void beginFrame();
    void endFrame();

    // Apply custom theme
    void applyDarkTheme();

    // Frosted glass mode renders translucent backgrounds and clears to transparent over the DWM acrylic backdrop.
    void setGlass(bool glass) { m_glass = glass; applyDarkTheme(); }

    // Global UI opacity (fade-in animation)
    void setAlpha(float alpha) { m_alpha = alpha; }

    // Set brightness callback (display index, percent)
    using BrightnessChangedCallback = std::function<void(int, int)>;
    void setBrightnessCallback(BrightnessChangedCallback callback) {
        m_brightnessCallback = callback;
    }

    // Set close callback
    using CloseCallback = std::function<void()>;
    void setCloseCallback(CloseCallback callback) {
        m_closeCallback = callback;
    }

    // Set auto-brightness callback
    using AutoBrightnessCallback = std::function<void(bool)>;
    void setAutoBrightnessCallback(AutoBrightnessCallback callback) {
        m_autoBrightnessCallback = callback;
    }

    // Brightness schedule edited in place, where the callback's committed flag means the edit is finished and should be saved.
    using ScheduleCallback = std::function<void(bool committed)>;
    void setSchedule(schedule::Schedule* sched, ScheduleCallback callback) {
        m_schedule = sched;
        m_scheduleCallback = callback;
    }

    struct ScheduleStatus {
        int nowMinute = 0;      // Current minute in the schedule's time zone
        int targetPercent = 0;  // Scheduled brightness right now
        bool paused = false;    // Manual override active
        int resumeMinute = 0;   // When the override ends
    };
    void setScheduleStatus(const ScheduleStatus& status) { m_scheduleStatus = status; }

    using ResumeCallback = std::function<void()>;
    void setResumeScheduleCallback(ResumeCallback callback) { m_resumeScheduleCallback = callback; }

    // ---- Profiles tab ----
    struct ProfileEntry {
        std::string name;     // UTF-8
        std::string summary;  // e.g. "All displays 75%"
        std::string hotkey;   // e.g. "Ctrl+Alt+1", empty if none
    };
    void setProfiles(const std::vector<ProfileEntry>& profiles) { m_profiles = profiles; }

    struct ProfileActions {
        std::function<void(int)> apply;
        std::function<void(int)> update;  // Overwrite with current brightness
        std::function<void(int)> remove;
        std::function<void(int, const std::string&)> rename;
        std::function<void(const std::string&)> save;  // New profile from current brightness
    };
    void setProfileActions(const ProfileActions& actions) { m_profileActions = actions; }

    // ---- Hotkeys tab ----
    struct HotkeyEntry {
        std::string action;   // "Brightness up", "Profile: Night", ...
        std::string binding;  // "Ctrl+Alt+Up" or "None"
        bool conflict = false;  // Registration failed (taken by another app)
        bool isProfile = false;
    };
    // captureIndex: entry waiting for a key press (-1 = none)
    void setHotkeys(const std::vector<HotkeyEntry>& hotkeys, int captureIndex,
                    const std::string& captureMessage, int stepPercent) {
        m_hotkeys = hotkeys;
        m_captureIndex = captureIndex;
        m_captureMessage = captureMessage;
        m_stepPercent = stepPercent;
    }

    struct HotkeyActions {
        std::function<void(int)> capture;  // Start listening for a combination
        std::function<void()> cancelCapture;
        std::function<void(int)> clear;
        std::function<void(int)> setStep;
    };
    void setHotkeyActions(const HotkeyActions& actions) { m_hotkeyActions = actions; }

    // Displays shown in the UI, one slider each
    struct DisplayEntry {
        std::string name;  // UTF-8
        int brightness = 0;
    };
    void setDisplays(const std::vector<DisplayEntry>& displays) { m_displays = displays; }

    // Set auto-brightness state
    void setAutoBrightnessEnabled(bool enabled) { m_autoBrightnessEnabled = enabled; }
    void setAmbientLight(float lux) { m_ambientLight = lux; }
    void setHasALS(bool hasALS) { m_hasALS = hasALS; }
    void setALSName(const std::wstring& name);

    // Render main UI
    void renderMainUI();

    // Client-area height the last frame's content needed without scrolling (pixels)
    int getContentHeight() const { return m_contentHeight; }

    // Height of the fixed header (title + tabs); the tab content below scrolls
    int getHeaderHeight() const { return m_headerHeight; }

    // Top-edge grab handle: true while the user is dragging it
    bool isResizeActive() const { return m_resizeActive; }
    // True once after the handle was double-clicked (reset to full height)
    bool consumeResizeReset() { bool r = m_resizeReset; m_resizeReset = false; return r; }

    // Resize the swap chain after the window size changes
    void resize(UINT width, UINT height);

    // Get DirectX device for window transparency
    ID3D11Device* getDevice() const { return m_device; }
    ID3D11DeviceContext* getContext() const { return m_context; }

private:
    bool createDeviceD3D(HWND hwnd);
    void cleanupDeviceD3D();
    void createRenderTarget();
    void cleanupRenderTarget();

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;

    HWND m_hwnd = nullptr;
    std::vector<DisplayEntry> m_displays;
    float m_dpiScale = 1.0f;
    bool m_glass = false;
    float m_alpha = 1.0f;
    int m_contentHeight = 0;
    int m_headerHeight = 0;
    bool m_resizeActive = false;
    bool m_resizeReset = false;
    bool m_resizeSuppressed = false;  // Ignore the rest of a double-click press

    // Scrollable area for a tab's content, spanning the full window width
    void beginScrollArea(const char* id);
    void endScrollArea();
    float m_scrollAreaTop = 0.0f;

    bool m_autoBrightnessEnabled = false;
    float m_ambientLight = 0.0f;
    bool m_hasALS = false;
    std::string m_alsName;

    schedule::Schedule* m_schedule = nullptr;
    ScheduleStatus m_scheduleStatus;
    ScheduleCallback m_scheduleCallback;
    ResumeCallback m_resumeScheduleCallback;

    void renderScheduleSection(float contentWidth);
    void renderProfilesTab(float contentWidth);
    void renderHotkeysTab(float contentWidth);

    std::vector<ProfileEntry> m_profiles;
    ProfileActions m_profileActions;
    int m_renameIndex = -1;
    bool m_renameFocus = false;
    char m_renameBuf[64] = "";
    char m_newProfileBuf[64] = "";

    std::vector<HotkeyEntry> m_hotkeys;
    HotkeyActions m_hotkeyActions;
    int m_captureIndex = -1;
    std::string m_captureMessage;
    int m_stepPercent = 5;

    BrightnessChangedCallback m_brightnessCallback;
    CloseCallback m_closeCallback;
    AutoBrightnessCallback m_autoBrightnessCallback;
};

} // namespace ui
