#include "schedule_ui.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {

using schedule::MINUTES_PER_DAY;
using schedule::Point;

constexpr int SNAP_MINUTES = 15;

static ImU32 color(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

static int snapMinute(float minute) {
    int m = static_cast<int>(std::lround(minute / SNAP_MINUTES)) * SNAP_MINUTES;
    return std::clamp(m, 0, MINUTES_PER_DAY - SNAP_MINUTES);
}

ScheduleEditResult scheduleGraph(const char* id, std::vector<Point>& points,
                                 int nowMinute, float width, float height, float s) {
    ScheduleEditResult result;

    const ImU32 accent = color(0.95f, 0.55f, 0.15f);
    const ImU32 accentFill = color(0.95f, 0.55f, 0.15f, 0.16f);
    const ImU32 gridColor = color(1.0f, 1.0f, 1.0f, 0.06f);
    const ImU32 labelColor = color(0.5f, 0.5f, 0.5f);
    const ImU32 nowColor = color(0.45f, 0.72f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImGuiID itemId = ImGui::GetItemID();

    // Plot area (leave room for axis labels)
    const float labelSize = ImGui::GetFontSize() * 0.75f;
    ImVec2 p0(origin.x + labelSize * 2.6f, origin.y + 6.0f * s);
    ImVec2 p1(origin.x + width - 6.0f * s, origin.y + height - labelSize - 8.0f * s);
    float plotW = p1.x - p0.x;
    float plotH = p1.y - p0.y;

    auto toX = [&](float minute) { return p0.x + plotW * minute / MINUTES_PER_DAY; };
    auto toY = [&](float percent) { return p1.y - plotH * percent / 100.0f; };
    auto fromX = [&](float x) { return (x - p0.x) / plotW * MINUTES_PER_DAY; };
    auto fromY = [&](float y) { return (p1.y - y) / plotH * 100.0f; };

    // --- Input -------------------------------------------------------------

    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mouseInPlot = mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y - 8.0f * s && mouse.y <= p1.y + 8.0f * s;

    int hoverIndex = -1;
    {
        float bestDist = (10.0f * s) * (10.0f * s);
        for (size_t i = 0; i < points.size(); ++i) {
            float dx = toX(static_cast<float>(points[i].minute)) - mouse.x;
            float dy = toY(static_cast<float>(points[i].percent)) - mouse.y;
            float d = dx * dx + dy * dy;
            if (d < bestDist) {
                bestDist = d;
                hoverIndex = static_cast<int>(i);
            }
        }
        if (!hovered) hoverIndex = -1;
    }

    // Index of the point being dragged, kept in ImGui's per-widget storage
    ImGuiStorage* storage = ImGui::GetStateStorage();
    int dragIndex = storage->GetInt(itemId, -1);

    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverIndex >= 0) {
        dragIndex = hoverIndex;
    }

    if (dragIndex >= 0 && dragIndex < static_cast<int>(points.size())) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Keep points ordered: a point can't pass its neighbours
            int lo = dragIndex > 0 ? points[dragIndex - 1].minute + SNAP_MINUTES : 0;
            int hi = dragIndex + 1 < static_cast<int>(points.size())
                         ? points[dragIndex + 1].minute - SNAP_MINUTES
                         : MINUTES_PER_DAY - SNAP_MINUTES;
            int minute = std::clamp(snapMinute(fromX(mouse.x)), lo, std::max(lo, hi));
            int percent = std::clamp(static_cast<int>(std::lround(fromY(mouse.y))), 0, 100);

            Point& p = points[dragIndex];
            if (p.minute != minute || p.percent != percent) {
                p.minute = minute;
                p.percent = percent;
                result.changed = true;
            }
            hoverIndex = dragIndex;
        } else {
            dragIndex = -1;
            result.committed = true;
        }
    } else {
        dragIndex = -1;
    }
    storage->SetInt(itemId, dragIndex);

    // Double-click empty space: add a point there
    if (hovered && hoverIndex < 0 && mouseInPlot && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Point p;
        p.minute = snapMinute(fromX(mouse.x));
        p.percent = std::clamp(static_cast<int>(std::lround(fromY(mouse.y))), 0, 100);
        bool tooClose = std::any_of(points.begin(), points.end(), [&](const Point& q) {
            return std::abs(q.minute - p.minute) < SNAP_MINUTES;
        });
        if (!tooClose) {
            auto it = std::upper_bound(points.begin(), points.end(), p.minute,
                                       [](int m, const Point& q) { return m < q.minute; });
            points.insert(it, p);
            result.changed = result.committed = true;
        }
    }

    // Right-click a point: remove it (keep at least two)
    if (hovered && hoverIndex >= 0 && dragIndex < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        points.size() > 2) {
        points.erase(points.begin() + hoverIndex);
        hoverIndex = -1;
        result.changed = result.committed = true;
    }

    // --- Drawing -----------------------------------------------------------

    dl->AddRectFilled(p0, p1, color(0.0f, 0.0f, 0.0f, 0.28f), 6.0f * s);

    // Horizontal grid + percent labels
    ImFont* font = ImGui::GetFont();
    for (int pct = 0; pct <= 100; pct += 25) {
        float y = toY(static_cast<float>(pct));
        if (pct > 0 && pct < 100) {
            dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), gridColor, 1.0f);
        }
        if (pct % 50 == 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            ImVec2 ts = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, buf);
            dl->AddText(font, labelSize, ImVec2(p0.x - ts.x - 4.0f * s, y - ts.y * 0.5f), labelColor, buf);
        }
    }

    // Vertical grid every 3h, labels every 6h
    for (int hour = 0; hour <= 24; hour += 3) {
        float x = toX(hour * 60.0f);
        if (hour > 0 && hour < 24) {
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), gridColor, 1.0f);
        }
        if (hour % 6 == 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", hour);
            ImVec2 ts = font->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, buf);
            float lx = std::clamp(x - ts.x * 0.5f, p0.x, p1.x - ts.x);
            dl->AddText(font, labelSize, ImVec2(lx, p1.y + 4.0f * s), labelColor, buf);
        }
    }

    // Curve: straight segments through the points, wrapping at midnight
    if (!points.empty()) {
        std::vector<ImVec2> line;
        float midnight = static_cast<float>(schedule::evaluate(points, 0));
        if (points.front().minute != 0) {
            line.emplace_back(p0.x, toY(midnight));
        }
        for (const Point& p : points) {
            line.emplace_back(toX(static_cast<float>(p.minute)), toY(static_cast<float>(p.percent)));
        }
        line.emplace_back(p1.x, toY(midnight));

        for (size_t i = 0; i + 1 < line.size(); ++i) {
            dl->AddQuadFilled(line[i], line[i + 1], ImVec2(line[i + 1].x, p1.y), ImVec2(line[i].x, p1.y), accentFill);
        }
        dl->AddPolyline(line.data(), static_cast<int>(line.size()), accent, ImDrawFlags_None, 2.5f * s);
    }

    // Current time marker
    {
        float x = toX(static_cast<float>(nowMinute));
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), nowColor, 1.5f * s);
        float y = toY(static_cast<float>(schedule::evaluate(points, nowMinute)));
        dl->AddCircleFilled(ImVec2(x, y), 4.0f * s, nowColor);
    }

    // Control points
    for (size_t i = 0; i < points.size(); ++i) {
        ImVec2 c(toX(static_cast<float>(points[i].minute)), toY(static_cast<float>(points[i].percent)));
        bool hot = static_cast<int>(i) == hoverIndex;
        float r = (hot ? 7.0f : 5.0f) * s;
        dl->AddCircleFilled(c, r, hot ? color(1.0f, 0.7f, 0.3f) : accent);
        dl->AddCircle(c, r, color(1.0f, 1.0f, 1.0f, 0.85f), 0, 1.5f * s);
    }

    // Tooltip: the point under the mouse, or the curve value at the mouse position
    if (hoverIndex >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const Point& p = points[hoverIndex];
        ImGui::SetTooltip("%s   %d%%", schedule::formatMinute(p.minute).c_str(), p.percent);
    } else if (hovered && mouseInPlot) {
        int minute = std::clamp(static_cast<int>(fromX(mouse.x)), 0, MINUTES_PER_DAY - 1);
        ImGui::SetTooltip("%s   %d%%", schedule::formatMinute(minute).c_str(), schedule::evaluate(points, minute));
    }

    return result;
}

static bool containsIgnoreCase(const std::string& haystack, const char* needle) {
    if (!needle || !*needle) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle, needle + strlen(needle),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

bool timeZoneCombo(const char* id, std::wstring& key, float width) {
    static char filter[64] = "";
    bool changed = false;

    std::string preview = schedule::timeZoneDisplayName(key);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##tzfilter", "Search time zones...", filter, sizeof(filter));

        std::string systemName = schedule::timeZoneDisplayName(L"");
        if (containsIgnoreCase(systemName, filter)) {
            bool selected = key.empty();
            if (ImGui::Selectable(systemName.c_str(), selected)) {
                key.clear();
                changed = true;
            }
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }

        for (const auto& tz : schedule::timeZones()) {
            std::string keyUtf8;  // Key names are ASCII
            for (wchar_t c : tz.key) keyUtf8 += static_cast<char>(c);
            if (!containsIgnoreCase(tz.displayName, filter) && !containsIgnoreCase(keyUtf8, filter)) {
                continue;
            }
            bool selected = key == tz.key;
            ImGui::PushID(keyUtf8.c_str());
            if (ImGui::Selectable(tz.displayName.c_str(), selected)) {
                key = tz.key;
                changed = true;
            }
            ImGui::PopID();
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace ui
