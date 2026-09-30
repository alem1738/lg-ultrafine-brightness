#include "tray.h"
#include "resource.h"
#include <shellapi.h>

namespace tray {

// Context menu command IDs
enum MenuId : UINT {
    ID_SHOW = 1,
    ID_EXIT = 2,
    ID_AUTO_BRIGHTNESS = 3,
    ID_START_WITH_WINDOWS = 4,
    ID_SCHEDULE = 5,
    ID_PRESET_BASE = 100,   // ID_PRESET_BASE + percent
    ID_PROFILE_BASE = 300,  // ID_PROFILE_BASE + profile index
};

static constexpr int PRESETS[] = { 100, 75, 50, 25, 0 };

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    shutdown();
}

bool TrayIcon::initialize(HWND hwnd, HINSTANCE hInstance, UINT callbackMsg) {
    m_hwnd = hwnd;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = callbackMsg;

    // Load application icon from resources
    m_nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    if (!m_nid.hIcon) {
        // Fallback to default icon if loading fails
        m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }

    wcscpy_s(m_nid.szTip, L"Monitor Brightness");

    return true;
}

void TrayIcon::shutdown() {
    if (m_visible) {
        hide();
    }
}

void TrayIcon::show() {
    if (!m_visible) {
        Shell_NotifyIconW(NIM_ADD, &m_nid);
        m_visible = true;
    }
}

void TrayIcon::hide() {
    if (m_visible) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_visible = false;
    }
}

void TrayIcon::recreate() {
    if (m_visible) {
        m_visible = false;
        show();
    }
}

UINT TrayIcon::taskbarCreatedMessage() {
    static UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

void TrayIcon::setTooltip(const wchar_t* tooltip) {
    // Truncate rather than crash: tooltips are limited to 128 chars
    wcsncpy_s(m_nid.szTip, tooltip, _TRUNCATE);
    if (m_visible) {
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void TrayIcon::showNotification(const wchar_t* title, const wchar_t* message) {
    m_nid.uFlags |= NIF_INFO;
    wcscpy_s(m_nid.szInfoTitle, title);
    wcscpy_s(m_nid.szInfo, message);
    m_nid.dwInfoFlags = NIIF_INFO;

    Shell_NotifyIconW(NIM_MODIFY, &m_nid);

    // Reset info flags
    m_nid.uFlags &= ~NIF_INFO;
}

void TrayIcon::handleMessage(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_LBUTTONUP:
        if (m_clickCallback) {
            m_clickCallback();
        }
        break;

    case WM_RBUTTONUP:
        showContextMenu();
        break;
    }
}

void TrayIcon::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    MenuState state;
    if (m_menuStateProvider) {
        state = m_menuStateProvider();
    }

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_SHOW, L"Show");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Brightness presets
    HMENU hPresets = CreatePopupMenu();
    for (int preset : PRESETS) {
        wchar_t label[16];
        swprintf_s(label, L"%d%%", preset);
        AppendMenuW(hPresets, MF_STRING, ID_PRESET_BASE + preset, label);
    }
    UINT presetFlags = MF_POPUP | (state.connected ? 0 : MF_GRAYED);
    AppendMenuW(hMenu, presetFlags, reinterpret_cast<UINT_PTR>(hPresets), L"Brightness");

    // Saved profiles
    if (!state.profiles.empty()) {
        HMENU hProfiles = CreatePopupMenu();
        for (size_t i = 0; i < state.profiles.size(); ++i) {
            AppendMenuW(hProfiles, MF_STRING, ID_PROFILE_BASE + i, state.profiles[i].c_str());
        }
        AppendMenuW(hMenu, presetFlags, reinterpret_cast<UINT_PTR>(hProfiles), L"Profiles");
    }

    UINT autoFlags = MF_STRING |
        (state.autoBrightness ? MF_CHECKED : 0) |
        (state.connected && state.hasALS ? 0 : MF_GRAYED);
    AppendMenuW(hMenu, autoFlags, ID_AUTO_BRIGHTNESS, L"Auto Brightness");
    AppendMenuW(hMenu, MF_STRING | (state.schedule ? MF_CHECKED : 0), ID_SCHEDULE, L"Brightness Schedule");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (state.startWithWindows ? MF_CHECKED : 0),
                ID_START_WITH_WINDOWS, L"Start with Windows");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_EXIT, L"Exit");

    // Required for menu to work properly
    SetForegroundWindow(m_hwnd);

    UINT cmd = TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, m_hwnd, nullptr);

    DestroyMenu(hMenu);  // Also destroys the attached submenus

    switch (cmd) {
    case ID_SHOW:
        if (m_showCallback) m_showCallback();
        break;
    case ID_EXIT:
        if (m_exitCallback) m_exitCallback();
        break;
    case ID_AUTO_BRIGHTNESS:
        if (m_autoBrightnessToggleCallback) m_autoBrightnessToggleCallback();
        break;
    case ID_SCHEDULE:
        if (m_scheduleToggleCallback) m_scheduleToggleCallback();
        break;
    case ID_START_WITH_WINDOWS:
        if (m_startupToggleCallback) m_startupToggleCallback();
        break;
    default:
        if (cmd >= ID_PRESET_BASE && cmd <= ID_PRESET_BASE + 100 && m_presetCallback) {
            m_presetCallback(static_cast<int>(cmd - ID_PRESET_BASE));
        } else if (cmd >= ID_PROFILE_BASE && cmd < ID_PROFILE_BASE + state.profiles.size() && m_profileCallback) {
            m_profileCallback(static_cast<int>(cmd - ID_PROFILE_BASE));
        }
        break;
    }
}

} // namespace tray
