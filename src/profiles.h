#pragma once

#include <string>
#include <utility>
#include <vector>

#include "hotkey.h"

namespace profiles {

// A saved brightness setup where listed monitors get their own level and any other monitor gets `percent`.
struct Profile {
    std::wstring name;
    int percent = 50;
    std::vector<std::pair<std::wstring, int>> monitors;  // (monitor name, percent)
    hotkey::Binding hotkey;
};

// Pre-made profiles for a fresh install
std::vector<Profile> defaults();

// Brightness this profile gives a monitor
int percentFor(const Profile& p, const std::wstring& monitorName);

// One profile per line: name \t percent \t hotkey \t monitor=percent|monitor=percent
std::wstring serialize(const std::vector<Profile>& list);
std::vector<Profile> parse(const std::wstring& text);

// Strip characters the storage format uses as separators
std::wstring sanitizeName(const std::wstring& name);

} // namespace profiles
