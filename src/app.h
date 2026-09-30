#pragma once

#include <Windows.h>
#include <memory>
#include <string>
#include <vector>

#include "brightness.h"
#include "ddc_display.h"
#include "als_sensor.h"
#include "ui.h"
#include "tray.h"
#include "hotkey.h"
#include "settings.h"

namespace app {

// Custom message for showing window from another instance
constexpr UINT WM_SHOWWINDOW_FROM_INSTANCE = WM_USER + 100;

class Application {
public:
    Application();
    ~Application();

    // Initialize the application (startHidden: go straight to the tray)
    bool initialize(HINSTANCE hInstance, bool startHidden = false);

    // Run the main loop
    int run();

    // Shutdown
    void shutdown();

    // Window procedure (static for Win32)
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool createWindow(HINSTANCE hInstance, bool startHidden);
    void setupCallbacks();

    // All controllable displays: LG Ultrafine (HID) first, then DDC/CI monitors
    std::vector<display::Display*> displays();

    // Re-detect monitors (startup, hot-plug, display changes, resume from sleep)
    void rescanDisplays();

    // Push display state to the UI (cheap, called every frame)
    void syncUI();
    void updateTooltip();

    // Manual brightness change (hotkey / tray preset): turns off auto-brightness
    void setAllBrightness(int percent);
    void adjustAllBrightness(int delta);
    void setAutoBrightness(bool enabled);
    bool isAutoBrightnessActive() const;

    // Time-of-day schedule, which a manual change pauses until the next schedule point.
    void setScheduleEnabled(bool enabled);
    void pauseSchedule();
    void updateSchedule();

    // Profiles
    void applyProfile(int index);
    profiles::Profile snapshotProfile(const std::wstring& name);

    // Hotkeys tab entries are 0 up, 1 down, 2 toggle schedule, 3 show window and 4+ profiles, with hotkey id = entry + 1.
    static constexpr int FIXED_HOTKEY_ENTRIES = 4;
    hotkey::Binding* bindingForEntry(int entry);
    int hotkeyEntryCount() const;
    void applyHotkeys();
    void onHotkey(int id);

    // Capturing a new key combination from the Hotkeys tab
    void startCapture(int entry);
    void cancelCapture();
    void onCaptureKey(UINT vk);

    // Widget-style popup anchored bottom-right that fits its content height and slides in when shown.
    void positionWindow();
    bool enableGlass();

    void showWindow();
    void hideWindow();
    void toggleFromTray();

    void updateAutoBrightness();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    bool m_running = true;
    bool m_windowVisible = true;
    bool m_shutdown = false;
    bool m_glass = false;
    float m_dpiScale = 1.0f;
    DWORD m_showTick = 0;       // When the show animation started
    DWORD m_hideTick = 0;       // When the popup last hid (tray click debounce)
    DWORD m_trayClickTick = 0;
    HWND m_foregroundAtShow = nullptr;  // Foreground window right after showing
    bool m_resizing = false;       // Dragging the top grab handle
    int m_resizeGrabOffset = 0;    // Cursor y minus window top when the drag started
    static constexpr DWORD SHOW_ANIMATION_MS = 180;

    settings::Settings m_settings;

    // Debounced rescan after device / display / power changes
    bool m_rescanPending = false;
    DWORD m_rescanAt = 0;
    static constexpr DWORD RESCAN_DELAY_MS = 1500;

    int m_captureEntry = -1;
    std::string m_captureMessage;
    bool m_swallowChar = false;          // Eat the WM_SYSCHAR that follows a captured Alt+key
    std::vector<bool> m_hotkeyConflicts;  // Per entry: registration failed

    bool m_scheduleOverride = false;
    int m_overrideStartMinute = 0;
    int m_overrideResumeMinute = 0;

    DWORD m_lastAutoBrightnessUpdate = 0;
    static constexpr DWORD AUTO_BRIGHTNESS_INTERVAL_MS = 200;  // Update every 200ms (also paces the fade)

    std::unique_ptr<brightness::BrightnessController> m_lgDisplay;
    std::vector<std::unique_ptr<ddc::DdcDisplay>> m_ddcDisplays;
    std::vector<std::string> m_displayNames;  // UTF-8, parallel to displays()
    std::unique_ptr<als::ALSSensor> m_als;

    std::unique_ptr<ui::UIRenderer> m_ui;
    std::unique_ptr<tray::TrayIcon> m_tray;
    std::unique_ptr<hotkey::HotkeyManager> m_hotkey;

    static Application* s_instance;
};

} // namespace app
