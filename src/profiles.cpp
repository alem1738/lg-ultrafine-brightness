#include "profiles.h"

#include <algorithm>
#include <sstream>

namespace profiles {

std::vector<Profile> defaults() {
    auto make = [](const wchar_t* name, int percent, UINT key) {
        Profile p;
        p.name = name;
        p.percent = percent;
        p.hotkey = { MOD_CONTROL | MOD_ALT, key };
        return p;
    };
    return {
        make(L"Bright", 100, '1'),
        make(L"Day", 75, '2'),
        make(L"Evening", 45, '3'),
        make(L"Night", 15, '4'),
    };
}

int percentFor(const Profile& p, const std::wstring& monitorName) {
    for (const auto& [name, percent] : p.monitors) {
        if (name == monitorName) return percent;
    }
    return p.percent;
}

std::wstring sanitizeName(const std::wstring& name) {
    std::wstring out;
    for (wchar_t c : name) {
        if (c == L'\t' || c == L'\n' || c == L'\r' || c == L'|' || c == L'=') continue;
        out += c;
    }
    // Trim spaces
    size_t start = out.find_first_not_of(L' ');
    size_t end = out.find_last_not_of(L' ');
    return start == std::wstring::npos ? std::wstring() : out.substr(start, end - start + 1);
}

std::wstring serialize(const std::vector<Profile>& list) {
    std::wostringstream out;
    for (const Profile& p : list) {
        out << sanitizeName(p.name) << L'\t' << p.percent << L'\t' << p.hotkey.pack() << L'\t';
        for (size_t i = 0; i < p.monitors.size(); ++i) {
            if (i > 0) out << L'|';
            out << sanitizeName(p.monitors[i].first) << L'=' << p.monitors[i].second;
        }
        out << L'\n';
    }
    return out.str();
}

std::vector<Profile> parse(const std::wstring& text) {
    std::vector<Profile> list;
    std::wistringstream in(text);
    std::wstring line;
    while (std::getline(in, line)) {
        std::vector<std::wstring> fields;
        std::wistringstream fs(line);
        std::wstring field;
        while (std::getline(fs, field, L'\t')) fields.push_back(field);
        if (fields.size() < 3 || fields[0].empty()) continue;

        Profile p;
        p.name = fields[0];
        try {
            p.percent = std::clamp(std::stoi(fields[1]), 0, 100);
            p.hotkey = hotkey::Binding::unpack(static_cast<DWORD>(std::stoul(fields[2])));
        } catch (...) {
            continue;
        }

        if (fields.size() >= 4) {
            std::wistringstream ms(fields[3]);
            std::wstring entry;
            while (std::getline(ms, entry, L'|')) {
                size_t eq = entry.rfind(L'=');
                if (eq == std::wstring::npos || eq == 0) continue;
                try {
                    int percent = std::clamp(std::stoi(entry.substr(eq + 1)), 0, 100);
                    p.monitors.emplace_back(entry.substr(0, eq), percent);
                } catch (...) {
                }
            }
        }
        list.push_back(std::move(p));
    }
    return list;
}

} // namespace profiles
