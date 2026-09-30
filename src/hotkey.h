#pragma once

#include <Windows.h>
#include <functional>
#include <string>
#include <vector>

namespace hotkey {

// A key combination of MOD_* flags and a virtual key code, where vk == 0 means unassigned.
struct Binding {
    UINT modifiers = 0;
    UINT vk = 0;

    bool empty() const { return vk == 0; }
    bool operator==(const Binding& o) const { return modifiers == o.modifiers && vk == o.vk; }

    // Packed form for the registry: modifiers in the high word, vk in the low word
    DWORD pack() const { return (modifiers << 16) | (vk & 0xffff); }
    static Binding unpack(DWORD v) { return { v >> 16, v & 0xffff }; }
};

// "Ctrl+Alt+Up", or "None" when unassigned (UTF-8)
std::string format(const Binding& b);

// True for modifier keys, which can't be a hotkey on their own
bool isModifierKey(UINT vk);

// Keys that are OK as a global hotkey without a modifier (F-keys, media keys)
bool allowedWithoutModifier(UINT vk);

// Current modifier state as MOD_* flags
UINT currentModifiers();

// Registers global hotkeys by id and reports presses through one callback
class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    // Initialize with window handle
    bool initialize(HWND hwnd);
    void shutdown();

    // Returns false if the combination is already taken by another app
    bool registerHotkey(int id, const Binding& binding, bool allowRepeat);
    void unregisterAll();

    // Handle WM_HOTKEY
    void handleHotkey(WPARAM hotkeyId);

    using HotkeyCallback = std::function<void(int id)>;
    void setCallback(HotkeyCallback callback) { m_callback = callback; }

private:
    HWND m_hwnd = nullptr;
    std::vector<int> m_registeredIds;
    HotkeyCallback m_callback;
};

} // namespace hotkey
