#pragma once

#include <string>
#include <vector>

#include "schedule.h"

namespace ui {

struct ScheduleEditResult {
    bool changed = false;    // Points were modified this frame
    bool committed = false;  // An edit finished (drag released, point added/removed) - time to save
};

// Interactive 24h brightness curve (drag, double-click to add, right-click to remove) with a marker at nowMinute.
ScheduleEditResult scheduleGraph(const char* id, std::vector<schedule::Point>& points,
                                 int nowMinute, float width, float height, float dpiScale);

// Searchable time zone dropdown that returns true when the selection changed.
bool timeZoneCombo(const char* id, std::wstring& timeZoneKey, float width);

} // namespace ui
