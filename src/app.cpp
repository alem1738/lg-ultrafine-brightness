#include "app.h"
#include "resource.h"
#include "auto_brightness.h"
#include "schedule.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <dwmapi.h>
#include <algorithm>
#include <iostream>

#undef min
#undef max

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace app {

Application* Application::s_instance = nullptr;

// Borderless popup, no taskbar button (tool window), always on top like a flyout
static constexpr DWORD WINDOW_STYLE = WS_POPUP;
static constexpr DWORD WINDOW_EX_STYLE = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
static constexpr int WINDOW_WIDTH = 420;   // At 96 DPI
static constexpr int SCREEN_MARGIN = 12;   // Gap to the screen edge / taskbar, at 96 DPI

static std::wstring fromUtf8(const std::string& s) {
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size);
    return out;
}

static std::string toUtf8(const std::wstring& s) {
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size, nullptr, nullptr);
    return out;
}

Application::Application() {
    s_instance = this;
    m_lgDisplay = std::make_unique<brightness::BrightnessController>();
    m_als = std::make_unique<als::ALSSensor>();
    m_ui = std::make_unique<ui::UIRenderer>();
    m_tray = std::make_unique<tray::TrayIcon>();
    m_hotkey = std::make_unique<hotkey::HotkeyManager>();
}

Application::~Application() {
    shutdown();
    s_instance = nullptr;
}

bool Application::initialize(HINSTANCE hInstance, bool startHidden) {
    m_hInstance = hInstance;
    m_settings = settings::load();
    settings::refreshStartWithWindows();

    if (!createWindow(hInstance, startHidden)) {
        return false;
    }

    if (!m_ui->initialize(m_hwnd)) {
        return false;
    }
    m_ui->setGlass(m_glass);

    // Initialize tray icon
    m_tray->initialize(m_hwnd, hInstance, tray::TrayIcon::WM_TRAYICON);
    m_tray->show();

    // Ambient light sensor (built into LG Ultrafine displays, and some laptops)
#ifdef _DEBUG
    std::wcout << L"[App] Attempting to initialize ALS..." << std::endl;
#endif
    bool hasALS = m_als->initialize();
#ifdef _DEBUG
    std::wcout << L"[App] ALS initialization result: " << (hasALS ? L"SUCCESS" : L"FAILED") << std::endl;
#endif
    m_ui->setHasALS(hasALS);
    if (hasALS) {
        m_ui->setALSName(m_als->getSensorName());
    }

    rescanDisplays();

    // Initialize hotkeys
    m_hotkey->initialize(m_hwnd);
    applyHotkeys();

    setupCallbacks();

    return true;
}

std::vector<display::Display*> Application::displays() {
    std::vector<display::Display*> result;
    if (m_lgDisplay->isConnected()) {
        result.push_back(m_lgDisplay.get());
    }
    for (auto& d : m_ddcDisplays) {
        result.push_back(d.get());
    }
    return result;
}

void Application::rescanDisplays() {
    // LG Ultrafine over HID: verify the existing handle, or try to (re)connect
    if (m_lgDisplay->isConnected()) {
        m_lgDisplay->probe();
    } else {
        m_lgDisplay->initialize();
    }

    // DDC/CI monitors: handles go stale after display changes / sleep, so re-enumerate
    m_ddcDisplays.clear();
    m_ddcDisplays = ddc::enumerate();

    m_displayNames.clear();
    for (display::Display* d : displays()) {
        m_displayNames.push_back(toUtf8(d->name()));
    }

    syncUI();
    updateTooltip();
}

bool Application::isAutoBrightnessActive() const {
    return m_settings.autoBrightness && m_als->isAvailable();
}

void Application::syncUI() {
    std::vector<ui::UIRenderer::DisplayEntry> entries;
    auto list = displays();
    for (size_t i = 0; i < list.size(); ++i) {
        ui::UIRenderer::DisplayEntry entry;
        entry.name = i < m_displayNames.size() ? m_displayNames[i] : "Display";
        entry.brightness = list[i]->getBrightness();
        entries.push_back(std::move(entry));
    }
    m_ui->setDisplays(entries);
    m_ui->setAutoBrightnessEnabled(isAutoBrightnessActive());

    // Profiles tab
    std::vector<ui::UIRenderer::ProfileEntry> profileEntries;
    for (const profiles::Profile& p : m_settings.profiles) {
        ui::UIRenderer::ProfileEntry e;
        e.name = toUtf8(p.name);
        if (p.monitors.empty()) {
            e.summary = "All displays " + std::to_string(p.percent) + "%";
        } else {
            for (size_t i = 0; i < p.monitors.size(); ++i) {
                if (i > 0) e.summary += "  \xC2\xB7  ";  // middle dot
                e.summary += toUtf8(p.monitors[i].first) + " " + std::to_string(p.monitors[i].second) + "%";
            }
        }
        if (!p.hotkey.empty()) e.hotkey = hotkey::format(p.hotkey);
        profileEntries.push_back(std::move(e));
    }
    m_ui->setProfiles(profileEntries);

    // Hotkeys tab
    static const char* FIXED_NAMES[FIXED_HOTKEY_ENTRIES] = {
        "Brightness up", "Brightness down", "Toggle schedule", "Show window",
    };
    std::vector<ui::UIRenderer::HotkeyEntry> hotkeyEntries;
    for (int i = 0; i < hotkeyEntryCount(); ++i) {
        ui::UIRenderer::HotkeyEntry e;
        if (i < FIXED_HOTKEY_ENTRIES) {
            e.action = FIXED_NAMES[i];
        } else {
            e.action = toUtf8(m_settings.profiles[i - FIXED_HOTKEY_ENTRIES].name);
            e.isProfile = true;
        }
        e.binding = hotkey::format(*bindingForEntry(i));
        e.conflict = i < static_cast<int>(m_hotkeyConflicts.size()) && m_hotkeyConflicts[i];
        hotkeyEntries.push_back(std::move(e));
    }
    m_ui->setHotkeys(hotkeyEntries, m_captureEntry, m_captureMessage, m_settings.stepPercent);
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

int Application::hotkeyEntryCount() const {
    return FIXED_HOTKEY_ENTRIES + static_cast<int>(m_settings.profiles.size());
}

hotkey::Binding* Application::bindingForEntry(int entry) {
    switch (entry) {
    case 0: return &m_settings.brightnessUp;
    case 1: return &m_settings.brightnessDown;
    case 2: return &m_settings.toggleSchedule;
    case 3: return &m_settings.showWindow;
    }
    int profile = entry - FIXED_HOTKEY_ENTRIES;
    if (profile >= 0 && profile < static_cast<int>(m_settings.profiles.size())) {
        return &m_settings.profiles[profile].hotkey;
    }
    return nullptr;
}

void Application::applyHotkeys() {
    m_hotkey->unregisterAll();
    m_hotkeyConflicts.assign(hotkeyEntryCount(), false);

    // While capturing, nothing is registered so any combination reaches the window
    if (m_captureEntry >= 0) return;

    for (int i = 0; i < hotkeyEntryCount(); ++i) {
        bool allowRepeat = i < 2;  // Holding brightness up/down keeps stepping
        if (!m_hotkey->registerHotkey(i + 1, *bindingForEntry(i), allowRepeat)) {
            m_hotkeyConflicts[i] = true;
        }
    }
}

void Application::onHotkey(int id) {
    int entry = id - 1;
    switch (entry) {
    case 0: adjustAllBrightness(m_settings.stepPercent); break;
    case 1: adjustAllBrightness(-m_settings.stepPercent); break;
    case 2: setScheduleEnabled(!m_settings.schedule.enabled); break;
    case 3: showWindow(); break;
    default: applyProfile(entry - FIXED_HOTKEY_ENTRIES); break;
    }
}

void Application::startCapture(int entry) {
    if (!bindingForEntry(entry)) return;
    m_captureEntry = entry;
    m_captureMessage.clear();
    applyHotkeys();  // Unregisters everything while listening
}

void Application::cancelCapture() {
    if (m_captureEntry < 0) return;
    m_captureEntry = -1;
    m_captureMessage.clear();
    applyHotkeys();
}

void Application::onCaptureKey(UINT vk) {
    if (vk == VK_ESCAPE) {
        cancelCapture();
        return;
    }
    if (hotkey::isModifierKey(vk)) {
        return;  // Wait for the actual key
    }

    UINT mods = hotkey::currentModifiers();
    hotkey::Binding* target = bindingForEntry(m_captureEntry);
    if (!target) {
        cancelCapture();
        return;
    }

    if ((vk == VK_BACK || vk == VK_DELETE) && mods == 0) {
        *target = {};
    } else if (mods == 0 && !hotkey::allowedWithoutModifier(vk)) {
        // A bare letter/arrow as a global hotkey would break typing everywhere
        m_captureMessage = "Add Ctrl, Alt, Shift or Win (or use an F-key)";
        return;
    } else {
        hotkey::Binding binding = { mods, vk };
        // A combination can only do one thing: take it away from any other action
        for (int i = 0; i < hotkeyEntryCount(); ++i) {
            hotkey::Binding* other = bindingForEntry(i);
            if (i != m_captureEntry && other && *other == binding) {
                *other = {};
            }
        }
        *target = binding;
    }

    m_swallowChar = (mods & MOD_ALT) != 0;
    m_captureEntry = -1;
    m_captureMessage.clear();
    settings::save(m_settings);
    applyHotkeys();
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

void Application::applyProfile(int index) {
    if (index < 0 || index >= static_cast<int>(m_settings.profiles.size())) return;
    const profiles::Profile& profile = m_settings.profiles[index];

    // Same as any manual change: take control back from automation
    if (isAutoBrightnessActive()) {
        setAutoBrightness(false);
    }
    pauseSchedule();

    auto list = displays();
    for (display::Display* d : list) {
        d->setBrightness(profiles::percentFor(profile, d->name()));
    }
    updateTooltip();
}

profiles::Profile Application::snapshotProfile(const std::wstring& name) {
    profiles::Profile p;
    p.name = name;
    auto list = displays();
    int total = 0;
    for (display::Display* d : list) {
        p.monitors.emplace_back(d->name(), d->getBrightness());
        total += d->getBrightness();
    }
    // Monitors not in the snapshot (e.g. plugged in later) get the average
    p.percent = list.empty() ? 50 : total / static_cast<int>(list.size());
    return p;
}

void Application::updateTooltip() {
    std::wstring tooltip;
    auto list = displays();
    if (list.empty()) {
        tooltip = L"Brightness: no displays found";
    } else {
        tooltip = L"Brightness";
        if (isAutoBrightnessActive()) {
            tooltip += L" (auto)";
        } else if (m_settings.schedule.enabled) {
            tooltip += m_scheduleOverride ? L" (schedule paused)" : L" (schedule)";
        }
        for (display::Display* d : list) {
            tooltip += L"\n" + d->name() + L": " + std::to_wstring(d->getBrightness()) + L"%";
        }
    }
    m_tray->setTooltip(tooltip.c_str());
}

bool Application::createWindow(HINSTANCE hInstance, bool startHidden) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LGUltrafineBrightnessClass";

    RegisterClassExW(&wc);

    // Get DPI scale
    HDC hdc = GetDC(nullptr);
    m_dpiScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, hdc);

    // Height is refit to the content every frame (positionWindow)
    int width = static_cast<int>(WINDOW_WIDTH * m_dpiScale);
    int height = static_cast<int>(460 * m_dpiScale);

    m_hwnd = CreateWindowExW(
        WINDOW_EX_STYLE,
        wc.lpszClassName,
        L"Monitor Brightness",
        WINDOW_STYLE,
        0, 0, width, height,
        nullptr, nullptr, hInstance, nullptr);

    if (!m_hwnd) {
        return false;
    }

    m_glass = enableGlass();

    if (startHidden) {
        m_windowVisible = false;
    } else {
        m_windowVisible = false;
        showWindow();
    }

    return true;
}

bool Application::enableGlass() {
    // Dark acrylic backdrop and rounded corners on Windows 11 22H2+, falling back to an opaque UI elsewhere.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;  // Acrylic, as used by flyouts
    if (FAILED(DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)))) {
        return false;
    }

    // Let the backdrop show through the whole client area (per-pixel alpha)
    MARGINS margins = { -1, -1, -1, -1 };
    return SUCCEEDED(DwmExtendFrameIntoClientArea(m_hwnd, &margins));
}

void Application::positionWindow() {
    // Anchor to the bottom-right of the taskbar monitor's work area, like the quick settings flyout.
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    HMONITOR monitor = taskbar ? MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY)
                               : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    RECT work = mi.rcWork;

    int margin = static_cast<int>(SCREEN_MARGIN * m_dpiScale);
    int width = static_cast<int>(WINDOW_WIDTH * m_dpiScale);
    int bottom = work.bottom - margin;
    int maxHeight = static_cast<int>(work.bottom - work.top) - margin * 2;

    RECT current;
    GetWindowRect(m_hwnd, &current);

    // Full height = everything visible without scrolling
    int fullHeight = m_ui->getContentHeight();
    if (fullHeight <= 0) {
        fullHeight = current.bottom - current.top;
    }
    fullHeight = std::min(fullHeight, maxHeight);
    int minHeight = std::min(fullHeight, m_ui->getHeaderHeight() + static_cast<int>(110 * m_dpiScale));

    // Top grab handle: the window's bottom stays put, the top follows the cursor
    if (m_ui->consumeResizeReset()) {
        m_settings.widgetHeight = 0;
        settings::save(m_settings);
    }
    if (m_ui->isResizeActive()) {
        POINT cursor;
        GetCursorPos(&cursor);
        if (!m_resizing) {
            m_resizing = true;
            m_resizeGrabOffset = cursor.y - current.top;
        }
        int h = bottom - (cursor.y - m_resizeGrabOffset);
        // Dragged all the way open = back to fit-to-content
        m_settings.widgetHeight = (h >= fullHeight) ? 0 : static_cast<int>(std::max(h, minHeight) / m_dpiScale);
    } else if (m_resizing) {
        m_resizing = false;
        settings::save(m_settings);
    }

    int height = fullHeight;
    if (m_settings.widgetHeight > 0) {
        height = std::clamp(static_cast<int>(m_settings.widgetHeight * m_dpiScale), minHeight, fullHeight);
    }

    // Slide up + fade in
    float t = std::min(1.0f, (GetTickCount() - m_showTick) / static_cast<float>(SHOW_ANIMATION_MS));
    float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // Ease-out cubic
    m_ui->setAlpha(eased);
    int slide = static_cast<int>((1.0f - eased) * 24.0f * m_dpiScale);

    int x = work.right - margin - width;
    int y = bottom - height + slide;

    if (current.left != x || current.top != y ||
        current.right - current.left != width || current.bottom - current.top != height) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }
}

void Application::setupCallbacks() {
    // UI slider for one display
    m_ui->setBrightnessCallback([this](int index, int percent) {
        auto list = displays();
        if (index >= 0 && index < static_cast<int>(list.size())) {
            pauseSchedule();
            list[index]->setBrightness(percent);
            updateTooltip();
        }
    });

    // UI close button callback
    m_ui->setCloseCallback([this]() {
        hideWindow();
    });

    // Auto-brightness callback
    m_ui->setAutoBrightnessCallback([this](bool enabled) {
        setAutoBrightness(enabled);
    });

    // Tray callbacks
    m_tray->setShowCallback([this]() {
        showWindow();
    });

    m_tray->setClickCallback([this]() {
        toggleFromTray();
    });

    m_tray->setExitCallback([this]() {
        m_running = false;
        PostQuitMessage(0);
    });

    m_tray->setPresetCallback([this](int percent) {
        setAllBrightness(percent);
    });

    m_tray->setAutoBrightnessToggleCallback([this]() {
        setAutoBrightness(!isAutoBrightnessActive());
    });

    m_tray->setScheduleToggleCallback([this]() {
        setScheduleEnabled(!m_settings.schedule.enabled);
    });

    m_tray->setStartupToggleCallback([]() {
        settings::setStartWithWindows(!settings::isStartWithWindows());
    });

    m_tray->setMenuStateProvider([this]() {
        tray::TrayIcon::MenuState state;
        state.connected = !displays().empty();
        state.hasALS = m_als->isAvailable();
        state.autoBrightness = isAutoBrightnessActive();
        state.startWithWindows = settings::isStartWithWindows();
        state.schedule = m_settings.schedule.enabled;
        for (const profiles::Profile& p : m_settings.profiles) {
            state.profiles.push_back(p.name);
        }
        return state;
    });

    // Schedule editor (UI edits m_settings.schedule in place)
    m_ui->setSchedule(&m_settings.schedule, [this](bool committed) {
        if (!m_settings.schedule.enabled) {
            m_scheduleOverride = false;
        } else if (m_settings.autoBrightness) {
            // Schedule and ambient-light auto-brightness are mutually exclusive
            m_settings.autoBrightness = false;
            m_ui->setAutoBrightnessEnabled(false);
        }
        // Save only when an edit finishes, not on every frame of a drag
        if (committed) {
            settings::save(m_settings);
        }
        updateTooltip();
    });

    m_ui->setResumeScheduleCallback([this]() {
        m_scheduleOverride = false;
        updateTooltip();
    });

    m_tray->setProfileCallback([this](int index) {
        applyProfile(index);
    });

    // Global hotkeys
    m_hotkey->setCallback([this](int id) {
        onHotkey(id);
    });

    // Profiles tab
    ui::UIRenderer::ProfileActions profileActions;
    profileActions.apply = [this](int i) { applyProfile(i); };
    profileActions.update = [this](int i) {
        if (i < 0 || i >= static_cast<int>(m_settings.profiles.size())) return;
        profiles::Profile& p = m_settings.profiles[i];
        profiles::Profile snap = snapshotProfile(p.name);
        p.percent = snap.percent;
        p.monitors = snap.monitors;
        settings::save(m_settings);
    };
    profileActions.remove = [this](int i) {
        if (i < 0 || i >= static_cast<int>(m_settings.profiles.size())) return;
        cancelCapture();
        m_settings.profiles.erase(m_settings.profiles.begin() + i);
        settings::save(m_settings);
        applyHotkeys();  // Profile hotkey ids shift
    };
    profileActions.rename = [this](int i, const std::string& name) {
        if (i < 0 || i >= static_cast<int>(m_settings.profiles.size())) return;
        std::wstring clean = profiles::sanitizeName(fromUtf8(name));
        if (clean.empty()) return;
        m_settings.profiles[i].name = clean;
        settings::save(m_settings);
    };
    profileActions.save = [this](const std::string& name) {
        std::wstring clean = profiles::sanitizeName(fromUtf8(name));
        if (clean.empty()) {
            clean = L"Profile " + std::to_wstring(m_settings.profiles.size() + 1);
        }
        m_settings.profiles.push_back(snapshotProfile(clean));
        settings::save(m_settings);
        applyHotkeys();
    };
    m_ui->setProfileActions(profileActions);

    // Hotkeys tab
    ui::UIRenderer::HotkeyActions hotkeyActions;
    hotkeyActions.capture = [this](int entry) { startCapture(entry); };
    hotkeyActions.cancelCapture = [this]() { cancelCapture(); };
    hotkeyActions.clear = [this](int entry) {
        if (hotkey::Binding* b = bindingForEntry(entry)) {
            *b = {};
            settings::save(m_settings);
            applyHotkeys();
        }
    };
    hotkeyActions.setStep = [this](int step) {
        m_settings.stepPercent = std::clamp(step, 1, 25);
        settings::save(m_settings);
    };
    m_ui->setHotkeyActions(hotkeyActions);
}

void Application::setAllBrightness(int percent) {
    // A manual change means the user wants control back
    if (isAutoBrightnessActive()) {
        setAutoBrightness(false);
    }
    pauseSchedule();

    for (display::Display* d : displays()) {
        d->setBrightness(percent);
    }
    updateTooltip();
}

void Application::adjustAllBrightness(int delta) {
    if (isAutoBrightnessActive()) {
        setAutoBrightness(false);
    }
    pauseSchedule();

    for (display::Display* d : displays()) {
        d->setBrightness(std::clamp(d->getBrightness() + delta, 0, 100));
    }
    updateTooltip();
}

void Application::setAutoBrightness(bool enabled) {
    m_settings.autoBrightness = enabled && m_als->isAvailable();
    if (m_settings.autoBrightness) {
        m_settings.schedule.enabled = false;  // Mutually exclusive
        m_scheduleOverride = false;
    }
    settings::save(m_settings);

    for (display::Display* d : displays()) {
        d->autoTransitioning = false;
    }
    m_ui->setAutoBrightnessEnabled(isAutoBrightnessActive());
    updateTooltip();
}

void Application::setScheduleEnabled(bool enabled) {
    m_settings.schedule.enabled = enabled;
    m_scheduleOverride = false;
    if (enabled && m_settings.autoBrightness) {
        m_settings.autoBrightness = false;  // Mutually exclusive
        m_ui->setAutoBrightnessEnabled(false);
    }
    settings::save(m_settings);
    updateTooltip();
}

void Application::pauseSchedule() {
    const schedule::Schedule& sched = m_settings.schedule;
    if (!sched.enabled) return;

    int now = schedule::currentMinute(sched.timeZoneKey);
    bool wasPaused = m_scheduleOverride;
    m_scheduleOverride = true;
    m_overrideStartMinute = now;
    m_overrideResumeMinute = schedule::nextPointMinute(sched.points, now);
    if (!wasPaused) {
        updateTooltip();
    }
}

void Application::updateSchedule() {
    const schedule::Schedule& sched = m_settings.schedule;
    int now = schedule::currentMinute(sched.timeZoneKey);
    int target = schedule::evaluate(sched.points, now);

    if (sched.enabled && m_scheduleOverride) {
        // Resume once the next schedule point is reached (minutes are mod 24h)
        int span = (m_overrideResumeMinute - m_overrideStartMinute + schedule::MINUTES_PER_DAY) % schedule::MINUTES_PER_DAY;
        if (span == 0) span = schedule::MINUTES_PER_DAY;
        int elapsed = (now - m_overrideStartMinute + schedule::MINUTES_PER_DAY) % schedule::MINUTES_PER_DAY;
        if (elapsed >= span) {
            m_scheduleOverride = false;
            updateTooltip();
        }
    }

    ui::UIRenderer::ScheduleStatus status;
    status.nowMinute = now;
    status.targetPercent = target;
    status.paused = m_scheduleOverride;
    status.resumeMinute = m_overrideResumeMinute;
    m_ui->setScheduleStatus(status);

    if (sched.enabled && !m_scheduleOverride) {
        bool changed = false;
        for (display::Display* d : displays()) {
            int before = d->getBrightness();
            auto_brightness::fadeToward(*d, target, 0);
            changed |= d->getBrightness() != before;
        }
        if (changed) {
            updateTooltip();
        }
    }
}

void Application::showWindow() {
    if (!m_windowVisible) {
        m_showTick = GetTickCount();
        m_ui->setAlpha(0.0f);
    }
    m_windowVisible = true;
    positionWindow();
    ShowWindow(m_hwnd, SW_SHOW);

    // Borrow the foreground thread's input state so the popup can take focus, since only the foreground app may.
    if (!SetForegroundWindow(m_hwnd)) {
        HWND fg = GetForegroundWindow();
        DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        DWORD ourThread = GetCurrentThreadId();
        if (fgThread && fgThread != ourThread && AttachThreadInput(ourThread, fgThread, TRUE)) {
            SetForegroundWindow(m_hwnd);
            AttachThreadInput(ourThread, fgThread, FALSE);
        }
    }
    m_foregroundAtShow = GetForegroundWindow();
}

void Application::hideWindow() {
    if (!m_windowVisible) return;
    cancelCapture();
    ShowWindow(m_hwnd, SW_HIDE);
    m_windowVisible = false;
    m_hideTick = GetTickCount();
}

void Application::toggleFromTray() {
    DWORD now = GetTickCount();

    // A double-click sends two clicks; treat it as one
    if (now - m_trayClickTick < GetDoubleClickTime()) return;
    m_trayClickTick = now;

    if (m_windowVisible) {
        hideWindow();
    } else if (now - m_hideTick > 300) {
        // If the popup just hid, this click caused it by taking focus, so stay closed.
        showWindow();
    }
}

int Application::run() {
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (m_running) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                m_running = false;
            }
        }

        if (!m_running) break;

        // Re-detect monitors shortly after a device / display change
        if (m_rescanPending && GetTickCount() - m_rescanAt >= RESCAN_DELAY_MS) {
            m_rescanPending = false;
            rescanDisplays();
        }

        // Update auto-brightness if enabled
        updateAutoBrightness();

        // Close the popup once another window takes the foreground, in case activation was refused and WM_ACTIVATE never fired.
        if (m_windowVisible && GetTickCount() - m_showTick > 300) {
            HWND fg = GetForegroundWindow();
            if (fg && fg != m_hwnd && fg != m_foregroundAtShow) {
                hideWindow();
            }
        }

        // Only render when window is visible
        if (m_windowVisible) {
            syncUI();
            m_ui->beginFrame();
            m_ui->renderMainUI();
            m_ui->endFrame();
            positionWindow();
            // Limit to ~60 FPS to reduce CPU usage
            Sleep(16);
        } else {
            // Sleep a bit to reduce CPU usage when hidden
            Sleep(10);  // 10ms for responsive hotkeys
        }
    }

    return static_cast<int>(msg.wParam);
}

void Application::shutdown() {
    // Called explicitly from WinMain and again from the destructor
    if (m_shutdown) return;
    m_shutdown = true;

    m_hotkey->shutdown();
    m_tray->shutdown();
    m_ui->shutdown();
    m_ddcDisplays.clear();  // Flushes pending DDC/CI writes
    m_als->shutdown();
    m_lgDisplay->shutdown();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

LRESULT WINAPI Application::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    Application* app = s_instance;

    // Explorer restarted: the tray icon is gone and must be re-added
    if (msg == tray::TrayIcon::taskbarCreatedMessage() && app && app->m_tray) {
        app->m_tray->recreate();
        return 0;
    }

    // Listening for a new hotkey combination (Hotkeys tab)
    if (app && app->m_captureEntry >= 0) {
        switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            app->onCaptureKey(static_cast<UINT>(wParam));
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            return 0;
        case WM_KILLFOCUS:
            app->cancelCapture();
            break;
        }
    } else if (app && app->m_swallowChar && msg == WM_SYSCHAR) {
        // Alt+key that just finished a capture: don't let Windows beep
        app->m_swallowChar = false;
        return 0;
    }

    switch (msg) {
    case WM_ACTIVATE:
        // Flyout behavior: clicking anywhere else closes it
        if (app && LOWORD(wParam) == WA_INACTIVE) {
            app->hideWindow();
        }
        return 0;

    case WM_SHOWWINDOW_FROM_INSTANCE:
        // Another instance wants to show this window
        if (app) {
            app->showWindow();
        }
        return 0;

    case WM_SIZE:
        if (app && app->m_ui && app->m_ui->getDevice() != nullptr && wParam != SIZE_MINIMIZED) {
            app->m_ui->resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_SYSCOMMAND:
        // Allow normal minimize behavior
        // Only intercept close, not minimize
        break;

    case WM_CLOSE:
        // Hide to tray instead of closing
        if (app) {
            app->hideWindow();
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_DEVICECHANGE:
    case WM_DISPLAYCHANGE:
        // Monitors or display layout changed, so rescan after a short debounce while enumeration settles.
        if (app) {
            app->m_rescanPending = true;
            app->m_rescanAt = GetTickCount();
        }
        return (msg == WM_DEVICECHANGE) ? TRUE : 0;

    case WM_POWERBROADCAST:
        // DDC/CI handles are often stale after sleep
        if (app && wParam == PBT_APMRESUMEAUTOMATIC) {
            app->m_rescanPending = true;
            app->m_rescanAt = GetTickCount();
        }
        return TRUE;

    case WM_HOTKEY:
        if (app && app->m_hotkey) {
            app->m_hotkey->handleHotkey(wParam);
        }
        return 0;

    case tray::TrayIcon::WM_TRAYICON:
        if (app && app->m_tray) {
            app->m_tray->handleMessage(wParam, lParam);
        }
        return 0;

    case WM_KEYDOWN:
        // Esc hides the window, unless it's cancelling text input (e.g. renaming a profile)
        if (wParam == VK_ESCAPE && !ImGui::GetIO().WantTextInput) {
            if (app) app->hideWindow();
            return 0;
        }
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void Application::updateAutoBrightness() {
    DWORD currentTime = GetTickCount();
    if (currentTime - m_lastAutoBrightnessUpdate < AUTO_BRIGHTNESS_INTERVAL_MS) {
        return;
    }
    m_lastAutoBrightnessUpdate = currentTime;

    updateSchedule();

    if (m_als->isAvailable()) {
        // Always update ambient light reading in UI (even if auto-brightness is off)
        float lux = m_als->getLux();
        m_ui->setAmbientLight(lux);

        // Only adjust brightness if auto-brightness is enabled
        if (isAutoBrightnessActive()) {
            for (display::Display* d : displays()) {
                auto_brightness::update(*d, lux);
            }
            updateTooltip();
        }
    }
}

} // namespace app
