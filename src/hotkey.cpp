#include "hotkey.h"

#include <cstdio>

namespace hotkey {

static std::string keyName(UINT vk) {
    switch (vk) {
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_TAB: return "Tab";
    case VK_PAUSE: return "Pause";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_VOLUME_UP: return "Volume Up";
    case VK_VOLUME_DOWN: return "Volume Down";
    case VK_VOLUME_MUTE: return "Mute";
    case VK_MEDIA_PLAY_PAUSE: return "Play/Pause";
    case VK_MEDIA_NEXT_TRACK: return "Next Track";
    case VK_MEDIA_PREV_TRACK: return "Prev Track";
    }

    char buf[32];
    if (vk >= VK_F1 && vk <= VK_F24) {
        snprintf(buf, sizeof(buf), "F%u", vk - VK_F1 + 1);
        return buf;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        snprintf(buf, sizeof(buf), "Num %u", vk - VK_NUMPAD0);
        return buf;
    }

    // Let the keyboard layout name it (e.g. punctuation keys)
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[32] = {};
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 32) > 0) {
        char utf8[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
        return utf8;
    }

    snprintf(buf, sizeof(buf), "Key 0x%02X", vk);
    return buf;
}

std::string format(const Binding& b) {
    if (b.empty()) return "None";

    std::string s;
    if (b.modifiers & MOD_CONTROL) s += "Ctrl+";
    if (b.modifiers & MOD_ALT) s += "Alt+";
    if (b.modifiers & MOD_SHIFT) s += "Shift+";
    if (b.modifiers & MOD_WIN) s += "Win+";
    return s + keyName(b.vk);
}

bool isModifierKey(UINT vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
        return true;
    }
    return false;
}

bool allowedWithoutModifier(UINT vk) {
    return (vk >= VK_F1 && vk <= VK_F24) ||
           (vk >= VK_VOLUME_MUTE && vk <= VK_MEDIA_PLAY_PAUSE) ||
           vk == VK_PAUSE || vk == VK_SCROLL;
}

UINT currentModifiers() {
    UINT mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
    return mods;
}

HotkeyManager::HotkeyManager() = default;

HotkeyManager::~HotkeyManager() {
    shutdown();
}

bool HotkeyManager::initialize(HWND hwnd) {
    m_hwnd = hwnd;
    return true;
}

void HotkeyManager::shutdown() {
    unregisterAll();
}

bool HotkeyManager::registerHotkey(int id, const Binding& binding, bool allowRepeat) {
    if (binding.empty()) return true;

    UINT mods = binding.modifiers | (allowRepeat ? 0 : MOD_NOREPEAT);
    if (!RegisterHotKey(m_hwnd, id, mods, binding.vk)) {
        return false;
    }
    m_registeredIds.push_back(id);
    return true;
}

void HotkeyManager::unregisterAll() {
    for (int id : m_registeredIds) {
        UnregisterHotKey(m_hwnd, id);
    }
    m_registeredIds.clear();
}

void HotkeyManager::handleHotkey(WPARAM hotkeyId) {
    if (m_callback) {
        m_callback(static_cast<int>(hotkeyId));
    }
}

} // namespace hotkey
