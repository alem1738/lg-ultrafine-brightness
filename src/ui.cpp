#include "ui.h"
#include "schedule_ui.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

namespace ui {

// Get DPI scale factor for a window
static float GetDpiScale(HWND hwnd) {
    // Try GetDpiForWindow (Windows 10 1607+)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
        auto getDpiForWindow = (GetDpiForWindowFunc)GetProcAddress(user32, "GetDpiForWindow");
        if (getDpiForWindow) {
            UINT dpi = getDpiForWindow(hwnd);
            return dpi / 96.0f;
        }
    }

    // Fallback: use DC
    HDC hdc = GetDC(hwnd);
    float scale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(hwnd, hdc);
    return scale;
}

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer() {
    shutdown();
}

bool UIRenderer::initialize(HWND hwnd) {
    m_hwnd = hwnd;
    m_dpiScale = GetDpiScale(hwnd);

    if (!createDeviceD3D(hwnd)) {
        return false;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Disable ini file

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    // Load font with DPI-scaled size
    float fontSize = 18.0f * m_dpiScale;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", fontSize);

    applyDarkTheme();

    return true;
}

void UIRenderer::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDeviceD3D();
}

bool UIRenderer::createDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &m_swapChain, &m_device, &featureLevel, &m_context);

    if (res != S_OK) {
        return false;
    }

    createRenderTarget();
    return true;
}

void UIRenderer::cleanupDeviceD3D() {
    cleanupRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void UIRenderer::createRenderTarget() {
    ID3D11Texture2D* backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
    backBuffer->Release();
}

void UIRenderer::cleanupRenderTarget() {
    if (m_renderTargetView) {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }
}

void UIRenderer::resize(UINT width, UINT height) {
    if (!m_swapChain || width == 0 || height == 0) return;

    cleanupRenderTarget();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createRenderTarget();
}

void UIRenderer::beginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void UIRenderer::endFrame() {
    ImGui::Render();

    // Clear to transparent so the DWM acrylic backdrop shows through ImGui's premultiplied-alpha output.
    const float opaque[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
    const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float* clear_color = m_glass ? transparent : opaque;
    m_context->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
    m_context->ClearRenderTargetView(m_renderTargetView, clear_color);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_swapChain->Present(1, 0); // VSync
}

void UIRenderer::applyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    float s = m_dpiScale; // Scale factor

    // Rounding (scaled), with no window rounding under glass because DWM rounds the real corners.
    style.WindowRounding = m_glass ? 0.0f : 10.0f * s;
    style.FrameRounding = 6.0f * s;
    style.GrabRounding = 6.0f * s;
    style.PopupRounding = 6.0f * s;
    style.ScrollbarRounding = 6.0f * s;

    // Spacing (scaled)
    style.WindowPadding = ImVec2(20 * s, 20 * s);
    style.FramePadding = ImVec2(10 * s, 8 * s);
    style.ItemSpacing = ImVec2(10 * s, 10 * s);
    style.ItemInnerSpacing = ImVec2(8 * s, 6 * s);

    // Sizes (scaled)
    style.ScrollbarSize = 8.0f * s;
    style.GrabMinSize = 12.0f * s;

    // Borders
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    // Colors - Modern dark theme with accent color
    ImVec4* colors = style.Colors;

    // Background colors
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.16f, 0.95f);

    // Border colors
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Frame colors
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);

    // Title bar
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);

    // Slider - Orange accent
    colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.65f, 0.25f, 1.0f);

    // Button
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.8f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);

    // Header
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.6f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.95f, 0.55f, 0.15f, 0.8f);

    // Text
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);

    // Separator
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.8f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.38f, 1.0f);

    // Check mark
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.19f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.6f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.24f, 0.24f, 0.28f, 1.0f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
    style.TabRounding = 6.0f * s;

    if (m_glass) {
        // Frosted glass uses a dark tint over the blur and white-alpha control layers so the blur shows through.
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.18f);
        colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.15f, 0.98f);  // Popups can't blur

        colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.11f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.15f);

        colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
        colors[ImGuiCol_Header] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);

        colors[ImGuiCol_Tab] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f);
        colors[ImGuiCol_TabSelected] = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);

        colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
        colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.30f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.40f);
    }
}

void UIRenderer::setALSName(const std::wstring& name) {
    int size = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    m_alsName.resize(size);
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, m_alsName.data(), size, nullptr, nullptr);
}

void UIRenderer::renderMainUI() {
    ImGuiIO& io = ImGui::GetIO();
    float s = m_dpiScale;

    ImGui::GetStyle().Alpha = m_alpha;

    // Full window ImGui frame
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 20.0f * s));
    ImGui::Begin("##Main", nullptr, window_flags);
    ImGui::PopStyleVar();

    // Center content
    float windowWidth = ImGui::GetWindowWidth();
    float contentWidth = windowWidth - 40.0f * s;

    // Grab handle along the top edge: drag to resize, double-click to reset
    {
        ImVec2 saved = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("##resize", ImVec2(windowWidth, 14.0f * s));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (hovered || active) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // Suppress the drag the second click of a double-click would start, so it can't overwrite the reset.
            m_resizeReset = true;
            m_resizeSuppressed = true;
        }
        if (!active) {
            m_resizeSuppressed = false;
        }
        m_resizeActive = active && !m_resizeSuppressed;

        ImVec2 winPos = ImGui::GetWindowPos();
        float cx = winPos.x + windowWidth * 0.5f;
        float alpha = m_resizeActive ? 0.6f : (hovered ? 0.45f : 0.22f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(cx - 20.0f * s, winPos.y + 6.0f * s), ImVec2(cx + 20.0f * s, winPos.y + 10.0f * s),
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), 2.0f * s);

        ImGui::SetCursorPos(saved);
    }

    ImGui::Indent(20.0f * s);

    // Header: title + display count, close button on the right
    bool connected = !m_displays.empty();
    {
        char status[32];
        if (!connected) {
            snprintf(status, sizeof(status), "No displays found");
        } else if (m_displays.size() == 1) {
            snprintf(status, sizeof(status), "1 display");
        } else {
            snprintf(status, sizeof(status), "%d displays", static_cast<int>(m_displays.size()));
        }
        ImVec4 statusColor = connected ?
            ImVec4(0.3f, 0.85f, 0.4f, 1.0f) :
            ImVec4(0.85f, 0.3f, 0.3f, 1.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "Monitor Brightness");
        ImGui::SameLine();
        ImGui::TextColored(statusColor, "%s", status);

        float closeSize = ImGui::GetFrameHeight();
        ImGui::SameLine(windowWidth - 20.0f * s - closeSize);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.2f, 0.85f));
        if (ImGui::Button("x##close", ImVec2(closeSize, closeSize)) && m_closeCallback) {
            m_closeCallback();
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();

    if (!ImGui::BeginTabBar("##tabs")) {
        ImGui::Unindent(20.0f * s);
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabItem("Brightness")) {
    beginScrollArea("##scrollBrightness");
    ImGui::Spacing();

    // Brightness control section
    if (connected) {
        // One block per display: name + percentage on one line, slider below
        for (size_t i = 0; i < m_displays.size(); ++i) {
            DisplayEntry& entry = m_displays[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", entry.name.c_str());
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d%%", entry.brightness);
                float textWidth = ImGui::CalcTextSize(buf).x;
                ImGui::SameLine(windowWidth - 20.0f * s - textWidth);
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.15f, 1.0f), "%s", buf);
            }

            ImGui::SetNextItemWidth(contentWidth);
            int brightness = entry.brightness;
            if (m_autoBrightnessEnabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SliderInt("##brightness", &brightness, 0, 100, "")) {
                if (!m_autoBrightnessEnabled) {
                    entry.brightness = brightness;
                    if (m_brightnessCallback) {
                        m_brightnessCallback(static_cast<int>(i), brightness);
                    }
                }
            }
            if (m_autoBrightnessEnabled) {
                ImGui::EndDisabled();
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        ImGui::Spacing();

        // Auto-brightness section (only when there's an ambient light sensor)
        if (m_hasALS) {
            ImGui::Separator();
            ImGui::Spacing();

            // Show ALS sensor info
            {
                const char* alsTitle = "Ambient Light Sensor";
                float textWidth = ImGui::CalcTextSize(alsTitle).x;
                ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", alsTitle);
            }

            ImGui::Spacing();

            // ALS device name
            if (!m_alsName.empty()) {
                float textWidth = ImGui::CalcTextSize(m_alsName.c_str()).x;
                ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
                ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "%s", m_alsName.c_str());
            }

            ImGui::Spacing();

            // Ambient light value (always show, even if 0)
            {
                char luxBuf[64];
                snprintf(luxBuf, sizeof(luxBuf), "%.1f lux", m_ambientLight);
                float textWidth = ImGui::CalcTextSize(luxBuf).x;
                ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.2f, 1.0f));
                ImGui::Text("%s", luxBuf);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            // Auto-brightness checkbox
            {
                bool autoEnabled = m_autoBrightnessEnabled;
                float checkboxWidth = ImGui::CalcTextSize("Auto Brightness").x + 30.0f * s;
                ImGui::SetCursorPosX((windowWidth - checkboxWidth) * 0.5f);
                if (ImGui::Checkbox("Auto Brightness", &autoEnabled)) {
                    m_autoBrightnessEnabled = autoEnabled;
                    if (m_autoBrightnessCallback) {
                        m_autoBrightnessCallback(autoEnabled);
                    }
                }
            }
        }
    } else {
        // Not connected message
        const char* msg = "No controllable displays";
        float textWidth = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", msg);

        ImGui::Spacing();

        const char* hint = "Enable DDC/CI in your monitor's menu";
        textWidth = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", hint);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    renderScheduleSection(contentWidth);

    endScrollArea();
    ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Profiles")) {
        beginScrollArea("##scrollProfiles");
        renderProfilesTab(contentWidth);
        endScrollArea();
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Hotkeys")) {
        beginScrollArea("##scrollHotkeys");
        renderHotkeysTab(contentWidth);
        endScrollArea();
        ImGui::EndTabItem();
    } else if (m_captureIndex >= 0 && m_hotkeyActions.cancelCapture) {
        // Left the tab while listening for a key
        m_hotkeyActions.cancelCapture();
    }

    ImGui::EndTabBar();
    ImGui::Unindent(20.0f * s);

    ImGui::End();
}

void UIRenderer::beginScrollArea(const char* id) {
    float s = m_dpiScale;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Everything above this point (title + tabs) stays fixed
    m_scrollAreaTop = ImGui::GetCursorPosY();
    m_headerHeight = static_cast<int>(m_scrollAreaTop);

    // Fill the rest of the window at full width with its own padding so child-local x coordinates match the main window's.
    float width = ImGui::GetWindowWidth();
    float height = (std::max)(1.0f, ImGui::GetWindowHeight() - m_scrollAreaTop - style.WindowPadding.y);
    ImGui::SetCursorPosX(0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * s, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void UIRenderer::endScrollArea() {
    // Content height of the scroll area (GetCursorPosY is scroll-independent)
    float content = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y;
    ImGui::EndChild();

    // Height the window needs to show everything without scrolling
    m_contentHeight = static_cast<int>(std::ceil(m_scrollAreaTop + content + ImGui::GetStyle().WindowPadding.y));
}

void UIRenderer::renderScheduleSection(float contentWidth) {
    if (!m_schedule) return;
    float s = m_dpiScale;
    schedule::Schedule& sched = *m_schedule;

    ImGui::SetNextItemOpen(sched.enabled, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Brightness Schedule")) {
        return;
    }

    ImGui::Spacing();

    // Enable toggle + status on the same line
    bool enabled = sched.enabled;
    if (ImGui::Checkbox("Follow schedule", &enabled)) {
        sched.enabled = enabled;
        if (m_scheduleCallback) m_scheduleCallback(true);
    }

    if (sched.enabled) {
        const ScheduleStatus& st = m_scheduleStatus;
        if (st.paused) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Paused until %s",
                               schedule::formatMinute(st.resumeMinute).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Resume") && m_resumeScheduleCallback) {
                m_resumeScheduleCallback();
            }
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s  %d%%", schedule::formatMinute(st.nowMinute).c_str(), st.targetPercent);
            float textWidth = ImGui::CalcTextSize(buf).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - 20.0f * s - textWidth);
            ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "%s", buf);
        }
    }

    ImGui::Spacing();

    // The graph
    ScheduleEditResult edit = scheduleGraph("##schedule", sched.points, m_scheduleStatus.nowMinute,
                                            contentWidth, 170.0f * s, s);
    if ((edit.changed || edit.committed) && m_scheduleCallback) {
        m_scheduleCallback(edit.committed);
    }

    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "Drag points, double-click to add, right-click to remove");

    ImGui::Spacing();

    // Time zone
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Time zone");
    if (timeZoneCombo("##timezone", sched.timeZoneKey, contentWidth) && m_scheduleCallback) {
        m_scheduleCallback(true);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to default")) {
        sched.points = schedule::defaultPoints();
        if (m_scheduleCallback) m_scheduleCallback(true);
    }
}

static float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void UIRenderer::renderProfilesTab(float contentWidth) {
    float s = m_dpiScale;
    float right = ImGui::GetWindowWidth() - 20.0f * s;
    const ImVec4 gray(0.55f, 0.55f, 0.55f, 1.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::Spacing();

    if (m_profiles.empty()) {
        ImGui::TextColored(gray, "No profiles yet.");
    }

    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const ProfileEntry& p = m_profiles[i];
        int index = static_cast<int>(i);
        ImGui::PushID(index);

        // Line 1: name (or rename field) + Apply / ... buttons
        float buttonsW = buttonWidth("Apply") + spacing + buttonWidth("...");
        if (m_renameIndex == index) {
            ImGui::SetNextItemWidth(contentWidth - buttonsW - spacing);
            if (m_renameFocus) {
                ImGui::SetKeyboardFocusHere();
                m_renameFocus = false;
            }
            bool enter = ImGui::InputText("##rename", m_renameBuf, sizeof(m_renameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (enter || ImGui::IsItemDeactivated()) {
                if (m_profileActions.rename) m_profileActions.rename(index, m_renameBuf);
                m_renameIndex = -1;
            }
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", p.name.c_str());
        }

        ImGui::SameLine(right - buttonsW);
        if (ImGui::Button("Apply") && m_profileActions.apply) {
            m_profileActions.apply(index);
        }
        ImGui::SameLine();
        if (ImGui::Button("...")) {
            ImGui::OpenPopup("profileMenu");
        }
        if (ImGui::BeginPopup("profileMenu")) {
            if (ImGui::MenuItem("Update to current brightness") && m_profileActions.update) {
                m_profileActions.update(index);
            }
            if (ImGui::MenuItem("Rename")) {
                m_renameIndex = index;
                m_renameFocus = true;
                snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", p.name.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete") && m_profileActions.remove) {
                m_profileActions.remove(index);
                if (m_renameIndex == index) m_renameIndex = -1;
            }
            ImGui::EndPopup();
        }

        // Line 2: levels, and the hotkey on the right when there's room
        ImGui::TextColored(gray, "%s", p.summary.c_str());
        if (!p.hotkey.empty()) {
            float summaryW = ImGui::CalcTextSize(p.summary.c_str()).x;
            float hotkeyW = ImGui::CalcTextSize(p.hotkey.c_str()).x;
            if (20.0f * s + summaryW + spacing * 2 + hotkeyW < right) {
                ImGui::SameLine(right - hotkeyW);
            }
            ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "%s", p.hotkey.c_str());
        }

        ImGui::PopID();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Save a new profile from the current brightness
    ImGui::TextColored(gray, "Save current brightness as a profile");
    float saveW = buttonWidth("Save");
    ImGui::SetNextItemWidth(contentWidth - saveW - spacing);
    bool enter = ImGui::InputTextWithHint("##newprofile", "Profile name", m_newProfileBuf, sizeof(m_newProfileBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Save") || enter) && m_profileActions.save) {
        m_profileActions.save(m_newProfileBuf);
        m_newProfileBuf[0] = '\0';
    }
}

void UIRenderer::renderHotkeysTab(float contentWidth) {
    float s = m_dpiScale;
    float right = ImGui::GetWindowWidth() - 20.0f * s;
    const ImVec4 gray(0.55f, 0.55f, 0.55f, 1.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    ImGui::Spacing();

    // Step size for the up/down hotkeys
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Brightness step");
    ImGui::SameLine(right - 170.0f * s);
    ImGui::SetNextItemWidth(170.0f * s);
    ImGui::SliderInt("##step", &m_stepPercent, 1, 25, "%d%%");
    if (ImGui::IsItemDeactivatedAfterEdit() && m_hotkeyActions.setStep) {
        m_hotkeyActions.setStep(m_stepPercent);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_captureIndex >= 0) {
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "Press a key combination...");
        ImGui::TextColored(gray, "Esc cancels, Backspace clears");
        if (!m_captureMessage.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.35f, 1.0f), "%s", m_captureMessage.c_str());
        }
    } else {
        ImGui::TextColored(gray, "Click a shortcut to change it");
    }
    ImGui::Spacing();

    const float bindingW = 180.0f * s;
    const float clearW = ImGui::GetFrameHeight();
    bool profilesHeader = false;

    for (size_t i = 0; i < m_hotkeys.size(); ++i) {
        const HotkeyEntry& h = m_hotkeys[i];
        int index = static_cast<int>(i);

        if (h.isProfile && !profilesHeader) {
            profilesHeader = true;
            ImGui::Spacing();
            ImGui::TextColored(gray, "Profiles");
        }

        ImGui::PushID(index);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", h.action.c_str());
        ImGui::SameLine(right - bindingW - spacing - clearW);

        bool capturing = index == m_captureIndex;
        int colors = 0;
        if (capturing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.55f, 0.15f, 0.8f));
            ++colors;
        }
        if (h.conflict) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
            ++colors;
        } else if (h.binding == "None") {
            ImGui::PushStyleColor(ImGuiCol_Text, gray);
            ++colors;
        }

        if (ImGui::Button(capturing ? "Press keys..." : h.binding.c_str(), ImVec2(bindingW, 0))) {
            if (capturing) {
                if (m_hotkeyActions.cancelCapture) m_hotkeyActions.cancelCapture();
            } else if (m_hotkeyActions.capture) {
                m_hotkeyActions.capture(index);
            }
        }
        ImGui::PopStyleColor(colors);
        if (h.conflict && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Another app already uses this shortcut");
        }

        ImGui::SameLine();
        if (h.binding != "None") {
            if (ImGui::Button("x", ImVec2(clearW, 0)) && m_hotkeyActions.clear) {
                m_hotkeyActions.clear(index);
            }
        } else {
            ImGui::Dummy(ImVec2(clearW, clearW));
        }
        ImGui::PopID();
    }
}

} // namespace ui
