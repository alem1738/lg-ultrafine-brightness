#include "settings.h"

#include <string>

namespace settings {

static constexpr const wchar_t* SETTINGS_KEY = L"Software\\LGUltrafineBrightness";
static constexpr const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr const wchar_t* RUN_VALUE = L"LGUltrafineBrightness";

static DWORD readDword(HKEY key, const wchar_t* name, DWORD defaultValue) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return defaultValue;
    }
    return value;
}

static void writeDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

static std::wstring readString(HKEY key, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcslen(value.c_str()));
    return value;
}

static void writeString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

Settings load() {
    Settings s;
    s.schedule.points = schedule::defaultPoints();

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        s.autoBrightness = readDword(key, L"AutoBrightness", 0) != 0;
        s.schedule.enabled = readDword(key, L"ScheduleEnabled", 0) != 0;
        s.schedule.timeZoneKey = readString(key, L"ScheduleTimeZone");
        std::wstring points = readString(key, L"SchedulePoints");
        if (!points.empty()) {
            s.schedule.points = schedule::parse(points);
        }

        // ProfilesSaved distinguishes "user deleted every profile" from "never saved"
        if (readDword(key, L"ProfilesSaved", 0)) {
            s.profiles = profiles::parse(readString(key, L"Profiles"));
        } else {
            s.profiles = profiles::defaults();
        }

        s.brightnessUp = hotkey::Binding::unpack(readDword(key, L"HotkeyBrightnessUp", s.brightnessUp.pack()));
        s.brightnessDown = hotkey::Binding::unpack(readDword(key, L"HotkeyBrightnessDown", s.brightnessDown.pack()));
        s.toggleSchedule = hotkey::Binding::unpack(readDword(key, L"HotkeyToggleSchedule", 0));
        s.showWindow = hotkey::Binding::unpack(readDword(key, L"HotkeyShowWindow", 0));
        s.stepPercent = static_cast<int>(readDword(key, L"StepPercent", 5));
        if (s.stepPercent < 1 || s.stepPercent > 25) s.stepPercent = 5;
        s.widgetHeight = static_cast<int>(readDword(key, L"WidgetHeight", 0));

        RegCloseKey(key);
    } else {
        s.profiles = profiles::defaults();
    }
    return s;
}

void save(const Settings& s) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        writeDword(key, L"AutoBrightness", s.autoBrightness ? 1 : 0);
        writeDword(key, L"ScheduleEnabled", s.schedule.enabled ? 1 : 0);
        writeString(key, L"ScheduleTimeZone", s.schedule.timeZoneKey);
        writeString(key, L"SchedulePoints", schedule::serialize(s.schedule.points));
        writeDword(key, L"ProfilesSaved", 1);
        writeString(key, L"Profiles", profiles::serialize(s.profiles));
        writeDword(key, L"HotkeyBrightnessUp", s.brightnessUp.pack());
        writeDword(key, L"HotkeyBrightnessDown", s.brightnessDown.pack());
        writeDword(key, L"HotkeyToggleSchedule", s.toggleSchedule.pack());
        writeDword(key, L"HotkeyShowWindow", s.showWindow.pack());
        writeDword(key, L"StepPercent", static_cast<DWORD>(s.stepPercent));
        writeDword(key, L"WidgetHeight", static_cast<DWORD>(s.widgetHeight));
        RegCloseKey(key);
    }
}

bool isStartWithWindows() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    bool exists = RegQueryValueExW(key, RUN_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

void setStartWithWindows(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS) {
        return;
    }

    if (enabled) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring command = L"\"" + std::wstring(exePath) + L"\" --minimized";
        RegSetValueExW(key, RUN_VALUE, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, RUN_VALUE);
    }

    RegCloseKey(key);
}

void refreshStartWithWindows() {
    if (isStartWithWindows()) {
        setStartWithWindows(true);
    }
}

} // namespace settings
