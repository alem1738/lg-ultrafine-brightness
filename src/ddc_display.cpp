#include "ddc_display.h"

#include <PhysicalMonitorEnumerationAPI.h>
#include <LowLevelMonitorConfigurationAPI.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#pragma comment(lib, "dxva2.lib")

#undef min
#undef max

namespace ddc {

constexpr BYTE VCP_BRIGHTNESS = 0x10;

// How often to re-read brightness while idle
constexpr auto REFRESH_INTERVAL = std::chrono::seconds(3);

DdcDisplay::DdcDisplay(HANDLE physicalMonitor, std::wstring name, DWORD maxRaw, DWORD currentRaw)
    : m_handle(physicalMonitor),
      m_name(std::move(name)),
      m_maxRaw(maxRaw),
      m_percent(static_cast<int>(std::lround(currentRaw * 100.0 / maxRaw))) {
    m_worker = std::thread(&DdcDisplay::workerLoop, this);
}

DdcDisplay::~DdcDisplay() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    m_worker.join();
    DestroyPhysicalMonitor(m_handle);
}

void DdcDisplay::setBrightness(int percent) {
    percent = std::clamp(percent, 0, 100);
    m_percent = percent;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = percent;
        ++m_generation;
    }
    m_cv.notify_one();
}

void DdcDisplay::workerLoop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true) {
        bool woke = m_cv.wait_for(lock, REFRESH_INTERVAL, [this] { return m_stop || m_pending >= 0; });

        if (!woke) {
            // Idle: pick up changes made outside this app
            unsigned generation = m_generation;
            lock.unlock();

            MC_VCP_CODE_TYPE type;
            DWORD current = 0, max = 0;
            bool ok = GetVCPFeatureAndVCPFeatureReply(m_handle, VCP_BRIGHTNESS, &type, &current, &max) && max > 0;

            // Replies are occasionally garbage; only accept a change if a second read agrees
            if (ok && static_cast<int>(std::lround(current * 100.0 / max)) != m_percent) {
                Sleep(50);
                DWORD current2 = 0, max2 = 0;
                ok = GetVCPFeatureAndVCPFeatureReply(m_handle, VCP_BRIGHTNESS, &type, &current2, &max2) &&
                     current2 == current && max2 == max;
            }

            lock.lock();
            // Ignore the reading if the user changed brightness while we were reading
            if (ok && generation == m_generation && m_pending < 0) {
                m_maxRaw = max;
                m_percent = static_cast<int>(std::lround(current * 100.0 / max));
            }
            continue;
        }

        // Flush a pending value even when stopping, so the last change isn't lost
        if (m_pending < 0) {
            break;
        }

        int percent = m_pending;
        m_pending = -1;
        lock.unlock();

        writeVerified(static_cast<DWORD>(std::lround(percent * m_maxRaw / 100.0)));

        lock.lock();
    }
}

void DdcDisplay::writeVerified(DWORD raw) {
    // Read back after each write and retry, since DDC/CI commands can fail or be silently dropped.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            Sleep(50);
        }
        if (!SetVCPFeature(m_handle, VCP_BRIGHTNESS, raw)) {
            continue;
        }

        // Monitors need ~50ms between DDC/CI commands
        Sleep(50);

        {
            // A newer value is waiting; it will be written next, so don't bother verifying
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending >= 0) return;
        }

        MC_VCP_CODE_TYPE type;
        DWORD current = 0, max = 0;
        if (GetVCPFeatureAndVCPFeatureReply(m_handle, VCP_BRIGHTNESS, &type, &current, &max) &&
            (current > raw ? current - raw : raw - current) <= 1) {
            return;
        }
    }
}

// Map GDI device names (\\.\DISPLAY1) to EDID monitor names, since dxva2 usually just reports "Generic PnP Monitor".
static std::map<std::wstring, std::wstring> friendlyNamesByGdiDevice() {
    std::map<std::wstring, std::wstring> names;

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return names;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return names;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        const auto& path = paths[i];

        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;

        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
            target.monitorFriendlyDeviceName[0] != L'\0') {
            names.emplace(source.viewGdiDeviceName, target.monitorFriendlyDeviceName);
        }
    }

    return names;
}

struct EnumContext {
    std::map<std::wstring, std::wstring> friendlyNames;
    std::vector<std::pair<LONG, std::unique_ptr<DdcDisplay>>> found;  // (left edge, display)
};

static BOOL CALLBACK enumMonitorProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMonitor, &info)) {
        return TRUE;
    }

    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &count) || count == 0) {
        return TRUE;
    }

    std::vector<PHYSICAL_MONITOR> monitors(count);
    if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, count, monitors.data())) {
        return TRUE;
    }

    auto it = ctx->friendlyNames.find(info.szDevice);

    for (auto& pm : monitors) {
        // Retry before skipping the monitor, since DDC/CI reads fail intermittently.
        MC_VCP_CODE_TYPE type;
        DWORD current = 0, max = 0;
        bool ok = false;
        for (int attempt = 0; attempt < 4 && !ok; ++attempt) {
            if (attempt > 0) Sleep(60);
            ok = GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, VCP_BRIGHTNESS, &type, &current, &max) && max > 0;
        }
        if (!ok) {
            // No DDC/CI support (or disabled in the monitor's OSD)
            DestroyPhysicalMonitor(pm.hPhysicalMonitor);
            continue;
        }

        std::wstring name = (it != ctx->friendlyNames.end()) ? it->second : pm.szPhysicalMonitorDescription;
        ctx->found.emplace_back(info.rcMonitor.left,
                                std::make_unique<DdcDisplay>(pm.hPhysicalMonitor, name, max, current));
    }

    return TRUE;
}

std::vector<std::unique_ptr<DdcDisplay>> enumerate() {
    EnumContext ctx;
    ctx.friendlyNames = friendlyNamesByGdiDevice();
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorProc, reinterpret_cast<LPARAM>(&ctx));

    std::stable_sort(ctx.found.begin(), ctx.found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::unique_ptr<DdcDisplay>> result;
    for (auto& entry : ctx.found) {
        result.push_back(std::move(entry.second));
    }
    return result;
}

} // namespace ddc
