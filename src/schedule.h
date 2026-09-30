#pragma once

#include <string>
#include <vector>

namespace schedule {

constexpr int MINUTES_PER_DAY = 24 * 60;

// One control point on the 24h brightness curve
struct Point {
    int minute = 0;    // Minute of day, 0-1439
    int percent = 50;  // Brightness, 0-100
};

// Brightness follows straight lines between points, with the last point of the day connecting to the first of the next.
struct Schedule {
    bool enabled = false;
    std::wstring timeZoneKey;  // Windows time zone key name, empty = system time zone
    std::vector<Point> points; // Sorted by minute, unique minutes, at least 2
};

std::vector<Point> defaultPoints();

// Interpolated brightness at a minute of day
int evaluate(const std::vector<Point>& points, int minute);

// Minute of the next point strictly after the given minute (wrapping)
int nextPointMinute(const std::vector<Point>& points, int minute);

// Current minute of day in a time zone (empty key = system time zone)
int currentMinute(const std::wstring& timeZoneKey);

// "07:30"
std::string formatMinute(int minute);

// Serialize as "390:20;480:80;..." for the registry
std::wstring serialize(const std::vector<Point>& points);
std::vector<Point> parse(const std::wstring& text);  // Falls back to defaults if invalid

// Available Windows time zones, ordered by UTC offset
struct TimeZone {
    std::wstring key;         // e.g. "Pacific Standard Time"
    std::string displayName;  // UTF-8, e.g. "(UTC-08:00) Pacific Time (US & Canada)"
};
const std::vector<TimeZone>& timeZones();

// Display name for a key ("" = system time zone)
std::string timeZoneDisplayName(const std::wstring& key);

} // namespace schedule
