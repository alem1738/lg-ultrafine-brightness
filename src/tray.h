#pragma once

#include <Windows.h>
#include <functional>
#include <string>
#include <vector>

namespace tray {

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    // Initialize tray icon
    bool initialize(HWND hwnd, HINSTANCE hInstance, UINT callbackMsg);
    void shutdown();

    // Show/hide tray icon
    void show();
    void hide();

    // Update tooltip
    void setTooltip(const wchar_t* tooltip);

    // Show notification
    void showNotification(const wchar_t* title, const wchar_t* message);

    // Menu callbacks
    using MenuCallback = std::function<void()>;
    void setShowCallback(MenuCallback callback) { m_showCallback = callback; }
    void setClickCallback(MenuCallback callback) { m_clickCallback = callback; }  // Left-click on the icon
    void setExitCallback(MenuCallback callback) { m_exitCallback = callback; }
    void setAutoBrightnessToggleCallback(MenuCallback callback) { m_autoBrightnessToggleCallback = callback; }
    void setStartupToggleCallback(MenuCallback callback) { m_startupToggleCallback = callback; }
    void setScheduleToggleCallback(MenuCallback callback) { m_scheduleToggleCallback = callback; }

    using PresetCallback = std::function<void(int)>;
    void setPresetCallback(PresetCallback callback) { m_presetCallback = callback; }

    using ProfileCallback = std::function<void(int index)>;
    void setProfileCallback(ProfileCallback callback) { m_profileCallback = callback; }

    // State used to build the context menu (queried each time it opens)
    struct MenuState {
        bool connected = false;
        bool hasALS = false;
        bool autoBrightness = false;
        bool startWithWindows = false;
        bool schedule = false;
        std::vector<std::wstring> profiles;
    };
    using MenuStateProvider = std::function<MenuState()>;
    void setMenuStateProvider(MenuStateProvider provider) { m_menuStateProvider = provider; }

    // Handle tray messages
    void handleMessage(WPARAM wParam, LPARAM lParam);

    // Re-add the icon after Explorer restarts (call on the TaskbarCreated message)
    void recreate();
    static UINT taskbarCreatedMessage();

    // Custom message ID
    static constexpr UINT WM_TRAYICON = WM_USER + 1;

private:
    void showContextMenu();

    HWND m_hwnd = nullptr;
    NOTIFYICONDATAW m_nid = {};
    bool m_visible = false;

    MenuCallback m_showCallback;
    MenuCallback m_clickCallback;
    MenuCallback m_exitCallback;
    MenuCallback m_autoBrightnessToggleCallback;
    MenuCallback m_startupToggleCallback;
    MenuCallback m_scheduleToggleCallback;
    PresetCallback m_presetCallback;
    ProfileCallback m_profileCallback;
    MenuStateProvider m_menuStateProvider;
};

} // namespace tray
