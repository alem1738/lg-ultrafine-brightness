#include "schedule.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace schedule {

std::vector<Point> defaultPoints() {
    // Dim overnight, ramp up in the morning, bright through the day, wind down in the evening
    return {
        { 6 * 60 + 30, 20 },
        { 8 * 60, 80 },
        { 17 * 60, 80 },
        { 21 * 60, 40 },
        { 23 * 60, 20 },
    };
}

int evaluate(const std::vector<Point>& points, int minute) {
    if (points.empty()) return 50;
    if (points.size() == 1) return points[0].percent;

    minute = ((minute % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;

    // Find the segment [a, b] containing the minute
    auto it = std::upper_bound(points.begin(), points.end(), minute,
                               [](int m, const Point& p) { return m < p.minute; });
    const Point& b = (it == points.end()) ? points.front() : *it;
    const Point& a = (it == points.begin()) ? points.back() : *(it - 1);

    int span = (b.minute - a.minute + MINUTES_PER_DAY) % MINUTES_PER_DAY;
    if (span == 0) return a.percent;
    int offset = (minute - a.minute + MINUTES_PER_DAY) % MINUTES_PER_DAY;

    float t = static_cast<float>(offset) / span;
    return static_cast<int>(std::lround(a.percent + (b.percent - a.percent) * t));
}

int nextPointMinute(const std::vector<Point>& points, int minute) {
    if (points.empty()) return minute;
    for (const Point& p : points) {
        if (p.minute > minute) return p.minute;
    }
    return points.front().minute;
}

// Look up a time zone by key (cached, the list doesn't change while running)
static bool findTimeZone(const std::wstring& key, DYNAMIC_TIME_ZONE_INFORMATION& out) {
    static std::wstring cachedKey;
    static DYNAMIC_TIME_ZONE_INFORMATION cached = {};
    static bool cachedFound = false;

    if (key != cachedKey || !cachedFound) {
        cachedKey = key;
        cachedFound = false;
        DYNAMIC_TIME_ZONE_INFORMATION dtzi;
        for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &dtzi) == ERROR_SUCCESS; ++i) {
            if (key == dtzi.TimeZoneKeyName) {
                cached = dtzi;
                cachedFound = true;
                break;
            }
        }
    }

    if (cachedFound) out = cached;
    return cachedFound;
}

int currentMinute(const std::wstring& timeZoneKey) {
    SYSTEMTIME local;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    SYSTEMTIME utc;
    GetSystemTime(&utc);

    if (timeZoneKey.empty() || !findTimeZone(timeZoneKey, dtzi) ||
        !SystemTimeToTzSpecificLocalTimeEx(&dtzi, &utc, &local)) {
        GetLocalTime(&local);
    }
    return local.wHour * 60 + local.wMinute;
}

std::string formatMinute(int minute) {
    minute = ((minute % MINUTES_PER_DAY) + MINUTES_PER_DAY) % MINUTES_PER_DAY;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minute / 60, minute % 60);
    return buf;
}

std::wstring serialize(const std::vector<Point>& points) {
    std::wostringstream out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0) out << L';';
        out << points[i].minute << L':' << points[i].percent;
    }
    return out.str();
}

std::vector<Point> parse(const std::wstring& text) {
    std::vector<Point> points;
    std::wistringstream in(text);
    std::wstring item;
    while (std::getline(in, item, L';')) {
        Point p;
        wchar_t colon = 0;
        std::wistringstream pair(item);
        if (pair >> p.minute >> colon >> p.percent && colon == L':' &&
            p.minute >= 0 && p.minute < MINUTES_PER_DAY && p.percent >= 0 && p.percent <= 100) {
            points.push_back(p);
        }
    }

    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.minute < b.minute; });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const Point& a, const Point& b) { return a.minute == b.minute; }),
                 points.end());

    if (points.size() < 2) {
        return defaultPoints();
    }
    return points;
}

static std::string toUtf8(const wchar_t* s) {
    int size = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), size, nullptr, nullptr);
    return out;
}

const std::vector<TimeZone>& timeZones() {
    static std::vector<TimeZone> zones = [] {
        struct Entry { LONG bias; TimeZone tz; };
        std::vector<Entry> entries;

        DYNAMIC_TIME_ZONE_INFORMATION dtzi;
        for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &dtzi) == ERROR_SUCCESS; ++i) {
            // Friendly name like "(UTC-08:00) Pacific Time (US & Canada)" lives in the registry
            std::wstring regPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\";
            regPath += dtzi.TimeZoneKeyName;
            wchar_t display[256] = {};
            DWORD size = sizeof(display);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, regPath.c_str(), L"Display", RRF_RT_REG_SZ,
                             nullptr, display, &size) != ERROR_SUCCESS) {
                wcsncpy_s(display, dtzi.TimeZoneKeyName, _TRUNCATE);
            }
            entries.push_back({ dtzi.Bias, { dtzi.TimeZoneKeyName, toUtf8(display) } });
        }

        // Bias is minutes *behind* UTC, so larger bias = further west
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.bias != b.bias) return a.bias > b.bias;
            return a.tz.displayName < b.tz.displayName;
        });

        std::vector<TimeZone> result;
        for (auto& e : entries) result.push_back(std::move(e.tz));
        return result;
    }();
    return zones;
}

std::string timeZoneDisplayName(const std::wstring& key) {
    if (key.empty()) {
        DYNAMIC_TIME_ZONE_INFORMATION dtzi;
        if (GetDynamicTimeZoneInformation(&dtzi) != TIME_ZONE_ID_INVALID) {
            for (const TimeZone& tz : timeZones()) {
                if (tz.key == dtzi.TimeZoneKeyName) return "System: " + tz.displayName;
            }
        }
        return "System time zone";
    }
    for (const TimeZone& tz : timeZones()) {
        if (tz.key == key) return tz.displayName;
    }
    return toUtf8(key.c_str());
}

} // namespace schedule
